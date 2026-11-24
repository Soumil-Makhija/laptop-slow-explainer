#include "slow.h"

/* Mach absolute time is not necessarily nanoseconds (notably Apple Silicon). */
int slow_ticks_to_ns(uint64_t ticks, uint32_t numer, uint32_t denom, uint64_t *out) {
    if (!numer || !denom || !out) return -1;
    uint64_t quotient = ticks / denom, remainder = ticks % denom;
    if (quotient > UINT64_MAX / numer) return -1;
    uint64_t high = quotient * numer;
    /* Each factor fits uint32_t, so this multiplication cannot overflow. */
    uint64_t low = remainder * numer / denom;
    if (high > UINT64_MAX - low) return -1;
    *out = high + low;
    return 0;
}
