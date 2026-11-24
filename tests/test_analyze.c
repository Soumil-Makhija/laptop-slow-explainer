#include "slow.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static slow_snapshot before, after;
static slow_result result;

static void close_to(double actual, double expected)
{
    assert(fabs(actual - expected) < 0.000001);
}

static void fixture(void)
{
    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));
    before.sampled_ns = 1000000000;
    after.sampled_ns = 3000000000;
    before.cpu_valid = after.cpu_valid = true;
    before.vm_valid = after.vm_valid = true;
    before.page_size = after.page_size = 16384;
    after.cpu_count = 8;
    after.cpu_ticks[0] = 60;
    after.cpu_ticks[1] = 20;
    after.cpu_ticks[2] = 10;
    after.cpu_ticks[3] = 10;
    before.swapout_pages = 2;
    after.swapout_pages = 66;
    before.count = after.count = 1;
    before.processes[0].pid = after.processes[0].pid = 20;
    before.processes[0].start_sec = after.processes[0].start_sec = 123;
    before.processes[0].start_usec = after.processes[0].start_usec = 456;
    before.processes[0].sampled_ns = 1500000000;
    after.processes[0].sampled_ns = 2500000000;
    before.processes[0].user_ns = 100;
    after.processes[0].user_ns = 500000100;
    after.processes[0].system_ns = 250000000;
    after.processes[0].footprint_bytes = 12345;
    after.processes[0].read_bytes = 2097152;
    after.processes[0].write_bytes = 1048576;
    strcpy(after.processes[0].name, "compiler");
}

static char *render(bool json)
{
    FILE *stream = tmpfile();
    long size;
    char *text;
    assert(stream != NULL);
    slow_print(stream, &result, json);
    assert(fflush(stream) == 0);
    size = ftell(stream);
    assert(size >= 0);
    text = malloc((size_t)size + 1);
    assert(text != NULL);
    rewind(stream);
    assert(fread(text, 1, (size_t)size, stream) == (size_t)size);
    text[size] = '\0';
    assert(fclose(stream) == 0);
    return text;
}

static void test_known_math(void)
{
    fixture();
    before.unavailable = 2;
    after.unavailable = 3;
    before.truncated = true;
    after.memory_bytes = 987654321;
    after.compressed_bytes = 123456789;
    assert(slow_analyze(&before, &after, &result) == 0);
    close_to(result.seconds, 2.0);
    assert(result.cpu_valid && result.swap_valid);
    close_to(result.busy_pct, 90.0);
    close_to(result.swapout_mib, 1.0);
    assert(result.rows[0].cpu_valid && result.rows[0].io_valid);
    close_to(result.rows[0].cpu_pct, 75.0);
    close_to(result.rows[0].read_mib_s, 2.0);
    close_to(result.rows[0].write_mib_s, 1.0);
    assert(result.rows[0].footprint_bytes == 12345);
    assert(result.memory_bytes == 987654321 && result.compressed_bytes == 123456789);
    assert(result.unavailable == 5 && result.truncated);
}

static void test_identity_and_process_churn(void)
{
    fixture();
    ++after.processes[0].start_usec;
    assert(slow_analyze(&before, &after, &result) == 0);
    assert(result.unmatched == 1 && result.count == 1);
    assert(!result.rows[0].cpu_valid && !result.rows[0].io_valid);
    assert(result.rows[0].footprint_bytes == 12345);
    fixture();
    after.processes[1] = after.processes[0];
    after.processes[1].pid = 99;
    after.count = 2;
    assert(slow_analyze(&before, &after, &result) == 0);
    assert(result.unmatched == 1 && result.rows[0].cpu_valid);
    after.count = 0;
    assert(slow_analyze(&before, &after, &result) == 0);
    assert(result.count == 0 && result.unmatched == 0);
}

static void test_unknown_and_reset(void)
{
    fixture();
    before.cpu_ticks[0] = 61;
    before.swapout_pages = 67;
    before.processes[0].user_ns = after.processes[0].user_ns + 1;
    before.processes[0].write_bytes = after.processes[0].write_bytes + 1;
    assert(slow_analyze(&before, &after, &result) == 0);
    assert(!result.cpu_valid && !result.swap_valid);
    assert(!result.rows[0].cpu_valid && !result.rows[0].io_valid);
    {
        char *json = render(true);
        assert(strstr(json, "\"busy_pct\":null") != NULL);
        assert(strstr(json, "\"read_mib_s\":null") != NULL);
        assert(strstr(json, "\"write_mib_s\":null") != NULL);
        assert(strstr(json, "nan") == NULL && strstr(json, ":inf") == NULL);
        free(json);
    }
    fixture();
    after.page_size = 4096;
    assert(slow_analyze(&before, &after, &result) == 0 && !result.swap_valid);
    fixture();
    after.vm_valid = false;
    assert(slow_analyze(&before, &after, &result) == 0 && !result.swap_valid);
    fixture();
    after.processes[0].sampled_ns = before.processes[0].sampled_ns;
    assert(slow_analyze(&before, &after, &result) == 0);
    assert(!result.rows[0].cpu_valid && !result.rows[0].io_valid);
}

static void test_idle_and_bounds(void)
{
    fixture();
    memset(after.cpu_ticks, 0, sizeof(after.cpu_ticks));
    after.cpu_ticks[2] = 100;
    assert(slow_analyze(&before, &after, &result) == 0 && result.cpu_valid);
    close_to(result.busy_pct, 0.0);
    memset(after.cpu_ticks, 0, sizeof(after.cpu_ticks));
    assert(slow_analyze(&before, &after, &result) == 0 && !result.cpu_valid);
    after.sampled_ns = before.sampled_ns;
    assert(slow_analyze(&before, &after, &result) == -1);
    after.sampled_ns = before.sampled_ns - 1;
    assert(slow_analyze(&before, &after, &result) == -1);
    fixture();
    before.count = SLOW_MAX_PROCESSES + 1;
    assert(slow_analyze(&before, &after, &result) == -1);
    fixture();
    after.count = SLOW_MAX_PROCESSES + 1;
    assert(slow_analyze(&before, &after, &result) == -1);
    fixture();
    before.processes[0].user_ns = before.processes[0].system_ns = 0;
    after.processes[0].user_ns = after.processes[0].system_ns = UINT64_MAX;
    after.swapout_pages = UINT64_MAX;
    assert(slow_analyze(&before, &after, &result) == 0);
    assert(isfinite(result.rows[0].cpu_pct) && result.rows[0].cpu_pct > 1e12);
    assert(isfinite(result.swapout_mib) && result.swapout_mib > 1e12);
}

static void test_output_and_independent_ranking(void)
{
    char *json, *text;
    fixture();
    memcpy(after.processes[0].name, "a\"\\\n\033\377", 7);
    before.count = after.count = 3;
    before.processes[1] = before.processes[2] = before.processes[0];
    after.processes[1] = after.processes[2] = after.processes[0];
    before.processes[1].pid = after.processes[1].pid = 10;
    before.processes[2].pid = after.processes[2].pid = 30;
    after.processes[1].footprint_bytes = 50000;
    after.processes[2].write_bytes = 10485760;
    assert(slow_analyze(&before, &after, &result) == 0);
    json = render(true);
    assert(strstr(json, "a\\\"\\\\\\u000a\\u001b\\u00ff") != NULL);
    assert(strstr(json, "\"top_cpu_pids\":[10,20,30]") != NULL);
    assert(strstr(json, "\"top_footprint_pids\":[10,20,30]") != NULL);
    assert(strstr(json, "\"top_disk_activity_pids\":[30,10,20]") != NULL);
    text = render(false);
    assert(strchr(text, '\033') == NULL);
    assert(strstr(text, "a\"\\???") != NULL);
    assert(strstr(text, "not latency or disk pressure") != NULL);
    free(json);
    free(text);
}

int main(void)
{
    test_known_math();
    test_identity_and_process_churn();
    test_unknown_and_reset();
    test_idle_and_bounds();
    test_output_and_independent_ranking();
    puts("analysis tests passed");
    return 0;
}
