#ifndef SLOW_H
#define SLOW_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SLOW_MAX_PROCESSES 4096
#define SLOW_NAME_SIZE 64
/* CPU tick indices are user, system, idle, nice. */
typedef struct {
    int pid;
    uint64_t start_sec, start_usec, sampled_ns;
    char name[SLOW_NAME_SIZE];
    uint64_t user_ns, system_ns, footprint_bytes;
    uint64_t read_bytes, write_bytes;
} slow_process;
typedef struct {
    uint64_t sampled_ns, cpu_ticks[4];
    uint64_t memory_bytes, compressed_bytes, swapout_pages, page_size;
    unsigned cpu_count;
    bool cpu_valid, vm_valid, truncated;
    size_t count, unavailable;
    slow_process processes[SLOW_MAX_PROCESSES];
} slow_snapshot;
typedef struct {
    int pid;
    char name[SLOW_NAME_SIZE];
    uint64_t footprint_bytes;
    bool cpu_valid, io_valid;
    double cpu_pct, read_mib_s, write_mib_s;
} slow_row;
typedef struct {
    double seconds, busy_pct, swapout_mib;
    bool cpu_valid, swap_valid, truncated, synthetic;
    uint64_t memory_bytes, compressed_bytes;
    unsigned cpu_count;
    size_t count, unavailable, unmatched;
    slow_row rows[SLOW_MAX_PROCESSES];
} slow_result;

/* Invalid timing/counts return -1. Counter resets become unknown, never spikes. */
int slow_analyze(const slow_snapshot *before, const slow_snapshot *after, slow_result *out);
void slow_print(FILE *stream, const slow_result *result, bool json);
int slow_collect(slow_snapshot *out, char *error, size_t error_size);
void slow_demo(slow_snapshot *before, slow_snapshot *after);
int slow_ticks_to_ns(uint64_t ticks, uint32_t numer, uint32_t denom, uint64_t *out);
#endif
