#include "timeutil.h"

#include <errno.h>
#include <time.h>

int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void sleep_until_ns(int64_t t_ns)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(t_ns / 1000000000LL);
    ts.tv_nsec = (long)(t_ns % 1000000000LL);
    /* clock_nanosleep returns an error NUMBER (not -1); retry if interrupted */
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {
    }
}

void spin(uint64_t iters)
{
    volatile uint64_t c = 0;           /* volatile: loop is not deleted */
    for (uint64_t i = 0; i < iters; i++) {
        c += i;
    }
}

uint64_t calibrate_iters_per_ms(void)
{
    const uint64_t n = 20000000ULL;
    uint64_t best = 0;

    spin(n / 10);                      /* warm-up */
    for (int r = 0; r < 3; r++) {      /* keep the fastest of 3 runs */
        int64_t a = now_ns();
        spin(n);
        int64_t b = now_ns();
        double ms = (double)(b - a) / 1e6;
        if (ms < 0.001) ms = 0.001;
        uint64_t rate = (uint64_t)((double)n / ms);
        if (rate > best) best = rate;
    }
    return best;
}
