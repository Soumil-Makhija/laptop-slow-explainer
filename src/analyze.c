#include "slow.h"

#include <inttypes.h>
#include <string.h>

#define MIB 1048576.0

static const slow_process *previous(const slow_snapshot *before,
                                    const slow_process *process)
{
    size_t i;
    for (i = 0; i < before->count; ++i) {
        const slow_process *candidate = &before->processes[i];
        if (candidate->pid == process->pid &&
            candidate->start_sec == process->start_sec &&
            candidate->start_usec == process->start_usec)
            return candidate;
    }
    return NULL;
}

int slow_analyze(const slow_snapshot *before, const slow_snapshot *after,
                 slow_result *out)
{
    size_t i;
    double total_ticks = 0.0, idle_ticks = 0.0;
    if (before == NULL || after == NULL || out == NULL ||
        before->count > SLOW_MAX_PROCESSES ||
        after->count > SLOW_MAX_PROCESSES ||
        after->sampled_ns <= before->sampled_ns)
        return -1;
    memset(out, 0, sizeof(*out));
    out->seconds = (double)(after->sampled_ns - before->sampled_ns) / 1e9;
    out->cpu_count = after->cpu_count;
    out->memory_bytes = after->memory_bytes;
    out->compressed_bytes = after->compressed_bytes;
    out->truncated = before->truncated || after->truncated;
    out->unavailable = SIZE_MAX - before->unavailable < after->unavailable
        ? SIZE_MAX : before->unavailable + after->unavailable;
    out->cpu_valid = before->cpu_valid && after->cpu_valid;
    for (i = 0; i < 4; ++i) {
        if (after->cpu_ticks[i] < before->cpu_ticks[i]) {
            out->cpu_valid = false;
            break;
        }
        {
            double delta = (double)(after->cpu_ticks[i] - before->cpu_ticks[i]);
            total_ticks += delta;
            if (i == 2)
                idle_ticks = delta;
        }
    }
    if (total_ticks <= 0.0)
        out->cpu_valid = false;
    if (out->cpu_valid)
        out->busy_pct = 100.0 * (total_ticks - idle_ticks) / total_ticks;
    out->swap_valid = before->vm_valid && after->vm_valid &&
        before->page_size > 0 && before->page_size == after->page_size &&
        after->swapout_pages >= before->swapout_pages;
    if (out->swap_valid)
        out->swapout_mib = (double)(after->swapout_pages - before->swapout_pages)
            * (double)after->page_size / MIB;
    out->count = after->count;
    for (i = 0; i < after->count; ++i) {
        const slow_process *current = &after->processes[i];
        const slow_process *old = previous(before, current);
        slow_row *row = &out->rows[i];
        double seconds;
        row->pid = current->pid;
        memcpy(row->name, current->name, sizeof(row->name));
        row->name[sizeof(row->name) - 1] = '\0';
        row->footprint_bytes = current->footprint_bytes;
        if (old == NULL) {
            ++out->unmatched;
            continue;
        }
        if (current->sampled_ns <= old->sampled_ns)
            continue;
        seconds = (double)(current->sampled_ns - old->sampled_ns) / 1e9;
        row->cpu_valid = current->user_ns >= old->user_ns &&
            current->system_ns >= old->system_ns;
        if (row->cpu_valid) {
            double cpu_ns = (double)(current->user_ns - old->user_ns) +
                (double)(current->system_ns - old->system_ns);
            row->cpu_pct = 100.0 * cpu_ns / (seconds * 1e9);
        }
        row->io_valid = current->read_bytes >= old->read_bytes &&
            current->write_bytes >= old->write_bytes;
        if (row->io_valid) {
            row->read_mib_s = (double)(current->read_bytes - old->read_bytes)
                / MIB / seconds;
            row->write_mib_s = (double)(current->write_bytes - old->write_bytes)
                / MIB / seconds;
        }
    }
    return 0;
}

static void print_name(FILE *stream, const char *name, bool json)
{
    size_t i;
    if (json)
        fputc('"', stream);
    for (i = 0; i < SLOW_NAME_SIZE && name[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (json) {
            if (c == '"' || c == '\\') {
                fputc('\\', stream);
                fputc(c, stream);
            } else if (c < 32 || c > 126) {
                fprintf(stream, "\\u%04x", (unsigned)c);
            } else {
                fputc(c, stream);
            }
        } else {
            fputc(c >= 32 && c <= 126 ? c : '?', stream);
        }
    }
    if (json)
        fputc('"', stream);
}

enum ranking { CPU, FOOTPRINT, DISK };

static bool eligible(const slow_row *row, enum ranking kind)
{
    return kind == FOOTPRINT || (kind == CPU ? row->cpu_valid : row->io_valid);
}

static bool ahead(const slow_row *left, const slow_row *right, enum ranking kind)
{
    if (kind == FOOTPRINT) {
        if (left->footprint_bytes != right->footprint_bytes)
            return left->footprint_bytes > right->footprint_bytes;
    } else {
        double a = kind == CPU ? left->cpu_pct : left->read_mib_s + left->write_mib_s;
        double b = kind == CPU ? right->cpu_pct : right->read_mib_s + right->write_mib_s;
        if (a != b)
            return a > b;
    }
    return left->pid < right->pid;
}

static size_t leaders(const slow_result *result, enum ranking kind,
                       const slow_row *top[5])
{
    size_t count = 0, i;
    for (i = 0; i < result->count; ++i) {
        const slow_row *row = &result->rows[i];
        size_t at = 0, j;
        if (!eligible(row, kind))
            continue;
        while (at < count && !ahead(row, top[at], kind))
            ++at;
        if (at >= 5)
            continue;
        if (count < 5)
            ++count;
        for (j = count - 1; j > at; --j)
            top[j] = top[j - 1];
        top[at] = row;
    }
    return count;
}

static void nullable(FILE *stream, bool known, double value)
{
    if (known)
        fprintf(stream, "%.6f", value);
    else
        fputs("null", stream);
}

static size_t observations(const slow_result *result, const char *notes[4])
{
    size_t n = 0;
    if (result->cpu_valid && result->busy_pct >= 85.0)
        notes[n++] = "High aggregate CPU activity: CPU contention may contribute.";
    else if (result->cpu_valid)
        notes[n++] = "No high aggregate CPU activity in this interval; this does not exclude a performance issue.";
    else
        notes[n++] = "Aggregate CPU activity is unknown for this interval.";
    if (result->swap_valid && result->swapout_mib > 0.0)
        notes[n++] = "Swap-outs occurred: memory pressure may contribute; this does not identify a responsible process.";
    else if (result->swap_valid)
        notes[n++] = "No swap-outs in this interval; this does not exclude memory-related delays.";
    else
        notes[n++] = "Swap-out activity is unknown for this interval.";
    notes[n++] = "Disk bytes measure activity, not latency or disk pressure.";
    return n;
}

static size_t suggestions(const slow_result *result, const char *notes[3])
{
    size_t n = 0;
    if (result->cpu_valid && result->busy_pct >= 85.0)
        notes[n++] = "Inspect the top CPU list; consider pausing a recognized background job or lowering its concurrency.";
    else
        notes[n++] = "Repeat a longer sample while the slowdown occurs; low aggregate CPU can hide a single-thread bottleneck or blocked work.";
    if (result->swap_valid && result->swapout_mib > 0.0)
        notes[n++] = "Save your work and consider closing unused applications; inspect memory footprints without treating them as proof of responsibility.";
    notes[n++] = "Compare disk activity with the workload you expect; collect latency evidence before concluding storage is the bottleneck.";
    return n;
}

static void print_json(FILE *stream, const slow_result *result)
{
    static const char *keys[] = {"top_cpu_pids", "top_footprint_pids", "top_disk_activity_pids"};
    const char *notes[4];
    size_t i, n;
    fprintf(stream, "{\"source\":\"%s\",\"seconds\":%.6f,\"cpu_count\":%u,\"busy_pct\":",
            result->synthetic ? "synthetic" : "macos-native", result->seconds,
            result->cpu_count);
    nullable(stream, result->cpu_valid, result->busy_pct);
    fputs(",\"swapout_mib\":", stream);
    nullable(stream, result->swap_valid, result->swapout_mib);
    fprintf(stream, ",\"memory_bytes\":%" PRIu64 ",\"compressed_bytes\":", result->memory_bytes);
    if (result->swap_valid)
        fprintf(stream, "%" PRIu64, result->compressed_bytes);
    else
        fputs("null", stream);
    fprintf(stream, ",\"coverage\":{\"processes\":%zu,\"unmatched\":%zu,\"unavailable\":%zu,\"truncated\":%s},\"processes\":[",
            result->count,
            result->unmatched, result->unavailable, result->truncated ? "true" : "false");
    for (i = 0; i < result->count; ++i) {
        const slow_row *row = &result->rows[i];
        fprintf(stream, "%s{\"pid\":%d,\"name\":", i ? "," : "", row->pid);
        print_name(stream, row->name, true);
        fprintf(stream, ",\"footprint_bytes\":%" PRIu64 ",\"cpu_pct\":", row->footprint_bytes);
        nullable(stream, row->cpu_valid, row->cpu_pct);
        fputs(",\"read_mib_s\":", stream);
        nullable(stream, row->io_valid, row->read_mib_s);
        fputs(",\"write_mib_s\":", stream);
        nullable(stream, row->io_valid, row->write_mib_s);
        fputc('}', stream);
    }
    fputc(']', stream);
    for (i = 0; i < 3; ++i) {
        const slow_row *top[5];
        size_t j;
        n = leaders(result, (enum ranking)i, top);
        fprintf(stream, ",\"%s\":[", keys[i]);
        for (j = 0; j < n; ++j)
            fprintf(stream, "%s%d", j ? "," : "", top[j]->pid);
        fputc(']', stream);
    }
    fputs(",\"observations\":[", stream);
    n = observations(result, notes);
    for (i = 0; i < n; ++i)
        fprintf(stream, "%s\"%s\"", i ? "," : "", notes[i]);
    fputs("],\"disk_pressure\":\"unknown\",\"suggestions\":[", stream);
    n = suggestions(result, notes);
    for (i = 0; i < n; ++i)
        fprintf(stream, "%s\"%s\"", i ? "," : "", notes[i]);
    fputs("]}\n", stream);
}

void slow_print(FILE *stream, const slow_result *result, bool json)
{
    static const char *titles[] = {
        "Top CPU (100% = one core)", "Top current memory footprint",
        "Top disk byte activity (not latency or pressure)"
    };
    const char *notes[4];
    size_t i, n;
    if (json) {
        print_json(stream, result);
        return;
    }
    fprintf(stream, "Laptop Slow Explainer — %.3f second sample, %u logical CPUs\n",
            result->seconds, result->cpu_count);
    fputs("Aggregate CPU busy: ", stream);
    if (result->cpu_valid)
        fprintf(stream, "%.1f%%\n", result->busy_pct);
    else
        fputs("unknown\n", stream);
    fputs("Swap-outs during sample: ", stream);
    if (result->swap_valid)
        fprintf(stream, "%.3f MiB\n", result->swapout_mib);
    else
        fputs("unknown\n", stream);
    fprintf(stream, "Physical memory: %.1f MiB; compressed memory: ",
            (double)result->memory_bytes / MIB);
    if (result->swap_valid)
        fprintf(stream, "%.1f MiB\n", (double)result->compressed_bytes / MIB);
    else
        fputs("unknown\n", stream);
    fprintf(stream, "Coverage: %zu processes, %zu unmatched, %zu unavailable observations; truncated: %s\n",
            result->count, result->unmatched, result->unavailable, result->truncated ? "yes" : "no");
    for (i = 0; i < 3; ++i) {
        const slow_row *top[5];
        size_t j;
        fprintf(stream, "\n%s\n", titles[i]);
        n = leaders(result, (enum ranking)i, top);
        if (n == 0)
            fputs("  No comparable process samples.\n", stream);
        for (j = 0; j < n; ++j) {
            const slow_row *row = top[j];
            fprintf(stream, "  PID %-6d ", row->pid);
            print_name(stream, row->name, false);
            if (i == CPU)
                fprintf(stream, ": %.1f%%\n", row->cpu_pct);
            else if (i == FOOTPRINT)
                fprintf(stream, ": %.2f MiB\n", (double)row->footprint_bytes / MIB);
            else
                fprintf(stream, ": %.3f MiB/s read, %.3f MiB/s write\n",
                        row->read_mib_s, row->write_mib_s);
        }
    }
    fputs("\nObservations\n", stream);
    n = observations(result, notes);
    for (i = 0; i < n; ++i)
        fprintf(stream, "  %s\n", notes[i]);
    fputs("\nSuggested next steps (nothing is changed automatically)\n", stream);
    n = suggestions(result, notes);
    for (i = 0; i < n; ++i)
        fprintf(stream, "  %s\n", notes[i]);
}
