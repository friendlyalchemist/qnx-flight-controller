/* timeutil.h - monotonic time, absolute sleeps and a calibrated CPU-burn loop */
#ifndef TIMEUTIL_H
#define TIMEUTIL_H

#include <stdint.h>

#define NS_PER_MS 1000000LL

/* Current CLOCK_MONOTONIC time in nanoseconds. */
int64_t now_ns(void);

/* Sleep until an ABSOLUTE CLOCK_MONOTONIC time. Absolute sleeps do not drift
 * the way "sleep for 1000 ms" in a loop does. */
void sleep_until_ns(int64_t t_ns);

/* CPU-bound loop of 'iters' iterations (cannot be optimised away). */
void spin(uint64_t iters);

/* How many spin() iterations fit in 1 ms. Call once, before the demo threads
 * exist. Work is then counted in iterations, so a preempted thread needs more
 * WALL-CLOCK time to finish the same amount of CPU work. */
uint64_t calibrate_iters_per_ms(void);

#endif
