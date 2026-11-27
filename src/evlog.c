#include "evlog.h"

#include <stdlib.h>

#include "timeutil.h"

static event_t g_ev[EVLOG_MAX];
static int     g_n = 0;           /* slots CLAIMED (may exceed EVLOG_MAX) */

void evlog_add(int task, int job, ev_type_t type, int64_t val)
{
    int64_t t = now_ns();                                   /* stamp first */
    int i = __atomic_fetch_add(&g_n, 1, __ATOMIC_SEQ_CST);  /* claim a slot */
    if (i < EVLOG_MAX) {
        g_ev[i].t_ns = t;
        g_ev[i].task = task;
        g_ev[i].job  = job;
        g_ev[i].type = type;
        g_ev[i].val  = val;
    }
}

int evlog_count(void)
{
    return g_n < EVLOG_MAX ? g_n : EVLOG_MAX;
}

int evlog_dropped(void)
{
    return g_n > EVLOG_MAX ? g_n - EVLOG_MAX : 0;
}

static int cmp_events(const void *a, const void *b)
{
    const event_t *x = a, *y = b;
    return (x->t_ns > y->t_ns) - (x->t_ns < y->t_ns);
}

void evlog_sort(void)
{
    qsort(g_ev, (size_t)evlog_count(), sizeof g_ev[0], cmp_events);
}

const event_t *evlog_get(int i)
{
    return &g_ev[i];
}

const char *evlog_type_name(ev_type_t t)
{
    switch (t) {
    case EV_WAKE: return "WAKE";
    case EV_TICK: return "TICK";
    case EV_DONE: return "DONE";
    }
    return "?";
}

int evlog_write_csv(FILE *f, int64_t t0_ns, const char *const names[], const int prios[])
{
    fprintf(f, "t_us,task,prio,job,event,value\n");
    for (int i = 0; i < evlog_count(); i++) {
        const event_t *e = &g_ev[i];
        int64_t v = e->val;
        if (e->type != EV_TICK) {
            v /= 1000;                       /* ns -> us for WAKE and DONE */
        }
        fprintf(f, "%lld,%s,%d,%d,%s,%lld\n",
                (long long)((e->t_ns - t0_ns) / 1000),
                names[e->task], prios[e->task], e->job,
                evlog_type_name(e->type), (long long)v);
    }
    return ferror(f) ? -1 : 0;
}
