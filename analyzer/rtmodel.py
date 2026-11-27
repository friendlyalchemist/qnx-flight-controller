"""Ideal fixed-priority preemptive schedule on ONE CPU (the "expected" behaviour).

1 ms time steps, zero context-switch cost, equal priorities served first-come
first-served. This is a MODEL, not a measurement: use it to predict what the
QNX scheduler should do, then compare with the measured CSV.
"""


def simulate(tasks, chunk_ms):
    """tasks: list of dicts with name, prio, offset_ms, period_ms, jobs, work_ms.

    Returns events as dicts: t_us, task (index), job, event, value, using the
    same units as the C program's CSV (WAKE/DONE value in us, TICK in ms).
    """
    pending = sorted(
        (t["offset_ms"] + j * t["period_ms"], i, j)
        for i, t in enumerate(tasks)
        for j in range(t["jobs"])
    )
    ready, remaining, done, rel = [], {}, {}, {}
    events = []
    t = 0
    while pending or ready:
        while pending and pending[0][0] <= t:
            r, i, j = pending.pop(0)
            ready.append((i, j))
            remaining[(i, j)] = tasks[i]["work_ms"]
            done[(i, j)] = 0
            rel[(i, j)] = r
            events.append(dict(t_us=t * 1000, task=i, job=j, event="WAKE", value=0))
        if ready:
            # highest priority wins; ties go to whoever has waited longest
            k = max(range(len(ready)), key=lambda x: (tasks[ready[x][0]]["prio"], -x))
            i, j = ready[k]
            remaining[(i, j)] -= 1
            done[(i, j)] += 1
            now = t + 1
            if done[(i, j)] % chunk_ms == 0 or remaining[(i, j)] == 0:
                events.append(dict(t_us=now * 1000, task=i, job=j, event="TICK",
                                   value=done[(i, j)]))
            if remaining[(i, j)] == 0:
                events.append(dict(t_us=now * 1000, task=i, job=j, event="DONE",
                                   value=(now - rel[(i, j)]) * 1000))
                ready.pop(k)
        t += 1
    return events
