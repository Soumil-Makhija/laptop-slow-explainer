#include "slow.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void help(void) {
    puts("Usage: slow-explain [--seconds 1..30] [--json] [--demo]\n"
         "Sample macOS CPU, process memory and disk activity (default 3 seconds).\n"
         "--demo uses synthetic data on any platform. No sudo or remediation.\n"
         "Live output contains process names. Nothing is uploaded.");
}

void slow_demo(slow_snapshot *a, slow_snapshot *b) {
    memset(a, 0, sizeof(*a));
    memset(b, 0, sizeof(*b));
    a->sampled_ns = UINT64_C(1000000000);
    b->sampled_ns = UINT64_C(3000000000);
    a->cpu_ticks[0] = 100;
    a->cpu_ticks[1] = 50;
    a->cpu_ticks[2] = 500;
    b->cpu_ticks[0] = 790;
    b->cpu_ticks[1] = 80;
    b->cpu_ticks[2] = 580;
    a->cpu_count = b->cpu_count = 4;
    a->cpu_valid = b->cpu_valid = true;
    a->vm_valid = b->vm_valid = true;
    a->page_size = b->page_size = 16384;
    b->memory_bytes = UINT64_C(17179869184);
    b->compressed_bytes = UINT64_C(1073741824);
    a->swapout_pages = 10;
    b->swapout_pages = 74;
    a->count = b->count = 3;
    const char *names[] = {"video-export", "photo-library", "backup-worker"};
    for (size_t i = 0; i < 3; ++i) {
        a->processes[i].pid = b->processes[i].pid = 100 + (int)i;
        a->processes[i].start_sec = b->processes[i].start_sec = 42;
        a->processes[i].sampled_ns = a->sampled_ns;
        b->processes[i].sampled_ns = b->sampled_ns;
        snprintf(b->processes[i].name, SLOW_NAME_SIZE, "%s", names[i]);
        b->processes[i].footprint_bytes = UINT64_C(1073741824) * (i + 1);
    }
    b->processes[0].user_ns = UINT64_C(6000000000); /* 300%% of one core */
    b->processes[1].user_ns = UINT64_C(1000000000);
    b->processes[2].write_bytes = UINT64_C(67108864); /* 32 MiB/s */
}

int main(int argc, char **argv) {
    bool json = false, demo = false;
    unsigned seconds = 3;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) { help(); return 0; }
        if (strcmp(argv[i], "--json") == 0) json = true;
        else if (strcmp(argv[i], "--demo") == 0) demo = true;
        else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            const char *arg = argv[++i];
            /* Only canonical decimal integers; reject signs and whitespace. */
            for (const char *p = arg; *p; ++p) {
                if (*p < '0' || *p > '9') { fputs("Seconds must be 1..30\n", stderr); return 2; }
            }
            errno = 0;
            char *end;
            unsigned long value = strtoul(arg, &end, 10);
            if (!*arg || *end || errno || value < 1 || value > 30) {
                fputs("Seconds must be 1..30\n", stderr); return 2;
            }
            seconds = (unsigned)value;
        } else { fputs("Unknown or incomplete option; use --help\n", stderr); return 2; }
    }
    slow_snapshot *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    slow_result *result = calloc(1, sizeof(*result));
    if (!a || !b || !result) { fputs("Out of memory\n", stderr); free(a); free(b); free(result); return 1; }
    int status = 0;
    char error[256];
    if (demo) {
        slow_demo(a, b);
        if (!json) puts("SYNTHETIC DEMO (not measurements from this machine)");
    } else {
        if (slow_collect(a, error, sizeof(error)) != 0) { fprintf(stderr, "%s\n", error); status = 1; goto done; }
        struct timespec delay = {(time_t)seconds, 0};
        while (nanosleep(&delay, &delay) != 0) {
            if (errno != EINTR) { fputs("Cannot wait for next sample\n", stderr); status = 1; goto done; }
        }
        if (slow_collect(b, error, sizeof(error)) != 0) { fprintf(stderr, "%s\n", error); status = 1; goto done; }
    }
    if (slow_analyze(a, b, result) != 0) {
        fputs("Invalid sample interval\n", stderr); status = 1; goto done;
    }
    result->synthetic = demo;
    slow_print(stdout, result, json);
    if (ferror(stdout)) status = 1;
done:
    free(a); free(b); free(result);
    return status;
}
