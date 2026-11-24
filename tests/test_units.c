#include "slow.h"
#include <assert.h>

int main(void) {
    uint64_t result;
    assert(slow_ticks_to_ns(24000000, 125, 3, &result) == 0 && result == 1000000000);
    assert(slow_ticks_to_ns(1, 125, 3, &result) == 0 && result == 41);
    assert(slow_ticks_to_ns(UINT64_MAX, 1, 1, &result) == 0 && result == UINT64_MAX);
    assert(slow_ticks_to_ns(UINT64_MAX, 1, 3, &result) == 0 && result == UINT64_MAX / 3);
    assert(slow_ticks_to_ns(UINT64_MAX, 3, 1, &result) == -1);
    /* Overflow in the remainder contribution, after quotient product fits. */
    assert(slow_ticks_to_ns(UINT64_MAX, 3, 2, &result) == -1);
    assert(slow_ticks_to_ns(0, UINT32_MAX, 1, &result) == 0 && result == 0);
    assert(slow_ticks_to_ns(1, 0, 1, &result) == -1);
    assert(slow_ticks_to_ns(1, 1, 0, &result) == -1);
    assert(slow_ticks_to_ns(1, 1, 1, NULL) == -1);
    puts("Mach timebase conversion checks passed");
    return 0;
}
