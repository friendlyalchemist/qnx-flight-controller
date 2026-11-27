/* evlog.h - tiny in-memory event log, written out as CSV after the run.
 *
 * Why in memory? printf() inside a running thread takes time and can block,
 * which would distort the very timing we want to observe.
 * Why no mutex? A mutex shared by threads of different priorities adds
 * priority inversion to a demo about priorities. Each thread instead claims a
 * unique slot with one atomic add.
 */
#ifndef EVLOG_H
#define EVLOG_H

#include <stdint.h>
#include <stdio.h>

#define EVLOG_MAX 4096

typedef enum { EV_WAKE, EV_TICK, EV_DONE } ev_type_t;

typedef struct {
    int64_t   t_ns;   /* CLOCK_MONOTONIC timestamp                               */
    int       task;   /* task index                                              */
    int       job;    /* job (release) number of that task                       */
    ev_type_t type;
    int64_t   val;    /* WAKE: wake-up latency ns | TICK: CPU work done, ms
                         DONE: response time ns (release -> completion)          */
} event_t;

/* Record an event stamped with the current time. Safe from any thread. */
void evlog_add(int task, int job, ev_type_t type, int64_t val);

/* Call only after all threads have finished. */
int  evlog_count(void);              /* events stored                  */
int  evlog_dropped(void);            /* events lost (log was full)     */
void evlog_sort(void);               /* sort by time                   */
const event_t *evlog_get(int i);     /* valid after evlog_sort()       */
const char *evlog_type_name(ev_type_t t);

/* Write "t_us,task,prio,job,event,value" rows. Times are relative to t0_ns.
 * value units: WAKE = latency in us, TICK = work done in ms, DONE = response in us. */
int evlog_write_csv(FILE *f, int64_t t0_ns, const char *const names[], const int prios[]);

#endif
