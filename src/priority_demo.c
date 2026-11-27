/*
 * priority_demo.c - Phase 1: priority-based preemptive scheduling on QNX Neutrino
 *
 * Three POSIX threads with different SCHED_FIFO priorities share ONE CPU.
 * Each thread is "released" at a planned time, then does CPU-bound work in
 * small chunks. Every event is timestamped, kept in memory, then printed and
 * written to a CSV file after all threads finish.
 *
 * Planned timeline (ms after start) if the scheduler behaves as expected:
 *
 *     0   LOW    released, starts its 3000 ms of work
 *   500   HIGH   job 0 released -> preempts LOW   (200 ms of work)
 *  1000   MEDIUM released       -> preempts LOW   (1000 ms of work)
 *  1500   HIGH   job 1 released -> preempts MEDIUM
 *  2500   HIGH   job 2 released -> preempts LOW (MEDIUM is done by then)
 *
 * Preemption is not printed directly (user code cannot see it). It shows up
 * as GAPS in a lower-priority thread's progress ticks while a higher-priority
 * thread is running.
 *
 * Usage:  ./priority_demo [log.csv]        (run as root so priorities can be set)
 *
 * STATUS: reviewed, NOT yet run on a QNX VM. Treat all output as unverified
 *         until you have run it there.
 */

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __QNX__
#include <sys/neutrino.h>   /* ThreadCtl, _NTO_TCTL_RUNMASK (QNX specific) */
#endif

#include "evlog.h"
#include "timeutil.h"

#define NUM_TASKS  3
#define CHUNK_MS   10          /* progress tick every 10 ms of CPU work */

/* ---------- Task configuration (edit these to run experiments) ---------- */

typedef struct {
    const char *name;
    int         priority;    /* SCHED_FIFO priority; verify range on your QNX */
    long        offset_ms;   /* first release, relative to common start time  */
    long        period_ms;   /* time between releases (unused if jobs == 1)   */
    int         jobs;        /* how many times the task runs                  */
    long        work_ms;     /* CPU work per job (multiple of CHUNK_MS)       */
} task_cfg_t;

static const task_cfg_t g_cfg[NUM_TASKS] = {
    /* name      prio  offset  period  jobs  work */
    { "HIGH",     30,    500,   1000,    3,   200 },
    { "MEDIUM",   20,   1000,      0,    1,  1000 },
    { "LOW",      10,      0,      0,    1,  3000 },
};

static const char *g_names[NUM_TASKS];
static int         g_prios[NUM_TASKS];

static int64_t           g_t0;            /* common start time              */
static uint64_t          g_iters_per_ms;  /* from calibrate_iters_per_ms()  */
static pthread_barrier_t g_start;
static int               g_pin_failures;  /* threads that could not be pinned */

/* ---------- Pin to one CPU ---------- */

/* All three threads must compete for ONE CPU. On a multi-core VM they would
 * otherwise run in parallel and no preemption would be visible. */
static int pin_to_cpu0(void)
{
#ifdef __QNX__
    return ThreadCtl(_NTO_TCTL_RUNMASK, (void *)1);   /* -1 on error */
#else
    return 0;   /* host test build: run with  taskset -c 0 ./priority_demo */
#endif
}

/* ---------- Workload ---------- */

static void burn(int task, int job, long work_ms)
{
    long done = 0;
    while (done < work_ms) {
        long step = (work_ms - done < CHUNK_MS) ? (work_ms - done) : CHUNK_MS;
        spin(g_iters_per_ms * (uint64_t)step);
        done += step;
        evlog_add(task, job, EV_TICK, done);
    }
}

static void *task_main(void *arg)
{
    int id = (int)(intptr_t)arg;
    const task_cfg_t *c = &g_cfg[id];

    if (pin_to_cpu0() == -1) {
        __atomic_fetch_add(&g_pin_failures, 1, __ATOMIC_SEQ_CST);
    }

    pthread_barrier_wait(&g_start);   /* everyone starts together */

    for (int job = 0; job < c->jobs; job++) {
        int64_t release = g_t0 + (c->offset_ms + (int64_t)job * c->period_ms) * NS_PER_MS;
        sleep_until_ns(release);                         /* blocked until released */
        int64_t woke = now_ns();
        evlog_add(id, job, EV_WAKE, woke - release);     /* wake-up latency */
        burn(id, job, c->work_ms);
        evlog_add(id, job, EV_DONE, now_ns() - release); /* response time   */
    }
    return NULL;
}

/* ---------- Output ---------- */

static void print_table(void)
{
    int64_t last[NUM_TASKS];
    for (int i = 0; i < NUM_TASKS; i++) last[i] = -1;

    printf("\n%10s  %-6s %4s %3s  %-5s  %s\n",
           "time(ms)", "task", "prio", "job", "event", "detail");
    printf("---------------------------------------------------------------\n");

    for (int i = 0; i < evlog_count(); i++) {
        const event_t *e = evlog_get(i);
        const task_cfg_t *c = &g_cfg[e->task];
        double t_ms = (double)(e->t_ns - g_t0) / 1e6;

        printf("%10.1f  %-6s %4d %3d  %-5s  ", t_ms, c->name, c->priority, e->job,
               evlog_type_name(e->type));

        switch (e->type) {
        case EV_WAKE:
            printf("wake-up latency = %.3f ms", (double)e->val / 1e6);
            break;
        case EV_TICK:
            printf("work done = %ld/%ld ms", (long)e->val, c->work_ms);
            if (last[e->task] >= 0) {
                double gap = (double)(e->t_ns - last[e->task]) / 1e6;
                if (gap > 2.5 * CHUNK_MS) {
                    printf("   <-- gap %.0f ms (likely preempted)", gap);
                }
            }
            break;
        case EV_DONE:
            printf("response time = %.1f ms (CPU work = %ld ms)",
                   (double)e->val / 1e6, c->work_ms);
            break;
        }
        printf("\n");
        last[e->task] = e->t_ns;
    }
}

static int write_csv(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "error: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
#ifdef __QNX__
    fprintf(f, "# platform=qnx\n");
#else
    fprintf(f, "# platform=host\n");
#endif
    fprintf(f, "# source=measured\n");
    fprintf(f, "# program=priority_demo\n");
    fprintf(f, "# chunk_ms=%d\n", CHUNK_MS);
    fprintf(f, "# iters_per_ms=%" PRIu64 "\n", g_iters_per_ms);
    fprintf(f, "# pin_failures=%d\n", g_pin_failures);
    fprintf(f, "# events_dropped=%d\n", evlog_dropped());
    for (int i = 0; i < NUM_TASKS; i++) {
        /* task=name,priority,offset_ms,period_ms,jobs,work_ms */
        fprintf(f, "# task=%s,%d,%ld,%ld,%d,%ld\n", g_cfg[i].name, g_cfg[i].priority,
                g_cfg[i].offset_ms, g_cfg[i].period_ms, g_cfg[i].jobs, g_cfg[i].work_ms);
    }
    int rc = evlog_write_csv(f, g_t0, g_names, g_prios);
    fclose(f);
    return rc;
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    const char *csv_path = (argc > 1) ? argv[1] : "priority_demo_log.csv";

    for (int i = 0; i < NUM_TASKS; i++) {
        g_names[i] = g_cfg[i].name;
        g_prios[i] = g_cfg[i].priority;
    }

    int pmin = sched_get_priority_min(SCHED_FIFO);
    int pmax = sched_get_priority_max(SCHED_FIFO);
    printf("SCHED_FIFO priority range on this system: %d..%d\n", pmin, pmax);

    for (int i = 0; i < NUM_TASKS; i++) {
        if (g_cfg[i].priority < pmin || g_cfg[i].priority > pmax) {
            fprintf(stderr, "error: priority %d for %s is outside %d..%d\n",
                    g_cfg[i].priority, g_cfg[i].name, pmin, pmax);
            return 1;
        }
    }

    g_iters_per_ms = calibrate_iters_per_ms();
    printf("Calibration: %" PRIu64 " loop iterations per ms\n", g_iters_per_ms);

    pthread_barrier_init(&g_start, NULL, NUM_TASKS + 1);   /* 3 tasks + main */

    pthread_t th[NUM_TASKS];
    for (int i = 0; i < NUM_TASKS; i++) {
        pthread_attr_t attr;
        struct sched_param sp;

        pthread_attr_init(&attr);
        /* Without EXPLICIT_SCHED the new thread would inherit main's policy. */
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        memset(&sp, 0, sizeof sp);
        sp.sched_priority = g_cfg[i].priority;
        pthread_attr_setschedparam(&attr, &sp);

        int rc = pthread_create(&th[i], &attr, task_main, (void *)(intptr_t)i);
        pthread_attr_destroy(&attr);
        if (rc != 0) {
            fprintf(stderr, "error: pthread_create(%s) failed: %s\n",
                    g_cfg[i].name, strerror(rc));
            fprintf(stderr, "hint: run as root (priorities need permission).\n");
            return 1;
        }
    }

    g_t0 = now_ns();                 /* set BEFORE releasing the threads */
    pthread_barrier_wait(&g_start);

    for (int i = 0; i < NUM_TASKS; i++) {
        pthread_join(th[i], NULL);
    }
    pthread_barrier_destroy(&g_start);

    if (g_pin_failures > 0) {
        printf("\nWARNING: %d thread(s) could not be pinned to CPU 0. On a multi-core\n"
               "system the threads ran in parallel and results are NOT valid.\n",
               g_pin_failures);
    }
    if (evlog_dropped() > 0) {
        printf("\nWARNING: %d events dropped (increase EVLOG_MAX).\n", evlog_dropped());
    }

    evlog_sort();
    print_table();

    if (write_csv(csv_path) == 0) {
        printf("\nCSV log written to %s\n", csv_path);
    }
    return 0;
}
