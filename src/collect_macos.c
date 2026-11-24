#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#endif
#include "slow.h"
#include <errno.h>
#include <string.h>
#include <time.h>

#ifdef __APPLE__
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/sysctl.h>

static uint64_t now_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

int slow_collect(slow_snapshot *out, char *error, size_t error_size) {
    memset(out, 0, sizeof(*out));
    mach_timebase_info_data_t timebase;
    if (mach_timebase_info(&timebase) != KERN_SUCCESS || !timebase.numer || !timebase.denom) {
        snprintf(error, error_size, "Cannot read Mach clock timebase");
        return -1;
    }
    mach_port_t host = mach_host_self();
    host_cpu_load_info_data_t cpu;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    out->sampled_ns = now_ns();
    kern_return_t status = host_statistics(host, HOST_CPU_LOAD_INFO,
                                           (host_info_t)&cpu, &count);
    if (status != KERN_SUCCESS || count < HOST_CPU_LOAD_INFO_COUNT || !out->sampled_ns) {
        snprintf(error, error_size, "Cannot read system CPU counters (Mach status %d)", status);
        mach_port_deallocate(mach_task_self(), host);
        return -1;
    }
    for (size_t i = 0; i < 4; ++i) out->cpu_ticks[i] = cpu.cpu_ticks[i];
    out->cpu_valid = true;
    vm_statistics64_data_t vm;
    count = HOST_VM_INFO64_COUNT;
    vm_size_t page_size = 0;
    if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm, &count) == KERN_SUCCESS &&
        count >= HOST_VM_INFO64_REV1_COUNT && host_page_size(host, &page_size) == KERN_SUCCESS) {
        out->page_size = page_size;
        out->swapout_pages = vm.swapouts;
        out->compressed_bytes = (uint64_t)vm.compressor_page_count * page_size;
        out->vm_valid = true;
    }
    mach_port_deallocate(mach_task_self(), host);
    size_t size = sizeof(out->memory_bytes);
    if (sysctlbyname("hw.memsize", &out->memory_bytes, &size, NULL, 0) != 0) {
        snprintf(error, error_size, "Cannot read physical memory size: %s", strerror(errno));
        return -1;
    }
    int ncpu = 0;
    size = sizeof(ncpu);
    if (sysctlbyname("hw.logicalcpu", &ncpu, &size, NULL, 0) != 0 || ncpu < 1) {
        snprintf(error, error_size, "Cannot read logical CPU count");
        return -1;
    }
    out->cpu_count = (unsigned)ncpu;

    /* Fixed bound: coverage is reported when a busy machine fills the list. */
    int pids[SLOW_MAX_PROCESSES];
    int bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
    if (bytes <= 0) {
        snprintf(error, error_size, "Cannot enumerate processes: %s. Run in a normal Terminal; no sudo is required.", strerror(errno));
        return -1;
    }
    out->truncated = (size_t)bytes >= sizeof(pids);
    size_t n = (size_t)bytes / sizeof(pids[0]);
    if (n > SLOW_MAX_PROCESSES) n = SLOW_MAX_PROCESSES;
    for (size_t i = 0; i < n; ++i) {
        if (pids[i] <= 0) continue;
        struct proc_bsdinfo first, last;
        struct rusage_info_v2 usage;
        if (proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &first, sizeof(first)) != sizeof(first) ||
            proc_pid_rusage(pids[i], RUSAGE_INFO_V2, (rusage_info_t *)&usage) != 0) {
            ++out->unavailable;
            continue;
        }
        uint64_t stamp = now_ns();
        /* Detect exit/reuse while the separate name and accounting APIs run. */
        if (!stamp ||
            proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &last, sizeof(last)) != sizeof(last) ||
            first.pbi_start_tvsec != last.pbi_start_tvsec ||
            first.pbi_start_tvusec != last.pbi_start_tvusec) {
            ++out->unavailable;
            continue;
        }
        uint64_t user_ns, system_ns;
        if (slow_ticks_to_ns(usage.ri_user_time, timebase.numer, timebase.denom, &user_ns) != 0 ||
            slow_ticks_to_ns(usage.ri_system_time, timebase.numer, timebase.denom, &system_ns) != 0) {
            ++out->unavailable;
            continue;
        }
        slow_process *p = &out->processes[out->count++];
        p->pid = pids[i];
        /* Kernel lifetime identity, deliberately never converted to wall time. */
        p->start_sec = usage.ri_proc_start_abstime;
        p->sampled_ns = stamp;
        const char *name = first.pbi_name[0] ? first.pbi_name : first.pbi_comm;
        size_t limit = first.pbi_name[0] ? sizeof(first.pbi_name) : sizeof(first.pbi_comm);
        size_t length = strnlen(name, limit);
        if (length >= sizeof(p->name)) length = sizeof(p->name) - 1;
        memcpy(p->name, name, length);
        p->user_ns = user_ns;
        p->system_ns = system_ns;
        p->footprint_bytes = usage.ri_phys_footprint;
        p->read_bytes = usage.ri_diskio_bytesread;
        p->write_bytes = usage.ri_diskio_byteswritten;
    }
    return 0;
}
#else
int slow_collect(slow_snapshot *out, char *error, size_t error_size) {
    (void)out;
    snprintf(error, error_size, "Live collection requires macOS. Use --demo for the portable synthetic example.");
    return -1;
}
#endif
