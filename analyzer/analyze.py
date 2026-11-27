"""Analyse a priority_demo CSV log: per-task statistics, expected-vs-measured
response times, and a timeline (Gantt-style) plot.

    python analyze.py ../logs/priority_demo_log.csv            # summary + timeline.png
    python analyze.py log.csv --out my_timeline.png
    python analyze.py log.csv --no-plot

How the timeline is built: each TICK event means "this thread has now done
another chunk of CPU work". A bar runs from the previous event of that job
(WAKE or earlier TICK) to the TICK. A chunk that took much longer than its
nominal length contains a preemption, so it is drawn hatched. Resolution is
therefore one chunk; this is an inference from timestamps, not a kernel trace.
"""
import argparse
import csv
import os
import statistics as st

from rtmodel import simulate


def load(path):
    meta, tasks, data = {}, [], []
    with open(path, newline="") as f:
        for ln in f.read().splitlines():
            if ln.startswith("#"):
                if "=" in ln:
                    k, v = ln[1:].split("=", 1)
                    k, v = k.strip(), v.strip()
                    if k == "task":
                        n, p, o, per, jobs, w = v.split(",")
                        tasks.append(dict(name=n, prio=int(p), offset_ms=int(o),
                                          period_ms=int(per), jobs=int(jobs), work_ms=int(w)))
                    else:
                        meta[k] = v
            elif ln.strip():
                data.append(ln)
    rows = [dict(t_us=int(r["t_us"]), task=r["task"], prio=int(r["prio"]),
                 job=int(r["job"]), event=r["event"], value=int(r["value"]))
            for r in csv.DictReader(data)]
    meta["tasks"] = tasks
    return meta, rows


def job_table(rows):
    """(task, job) -> dict(release_us, done_us, wake_us, resp_us)"""
    jobs = {}
    for r in rows:
        j = jobs.setdefault((r["task"], r["job"]), {})
        if r["event"] == "WAKE":
            j["release_us"] = r["t_us"] - r["value"]
            j["wake_us"] = r["value"]
        elif r["event"] == "DONE":
            j["done_us"] = r["t_us"]
            j["resp_us"] = r["value"]
    return jobs


def expected_response_us(meta):
    """Ideal-model response time per (task, job); {} if the log has no task config."""
    if not meta["tasks"]:
        return {}
    chunk = int(meta.get("chunk_ms", 10))
    names = [t["name"] for t in meta["tasks"]]
    out = {}
    for e in simulate(meta["tasks"], chunk):
        if e["event"] == "DONE":
            out[(names[e["task"]], e["job"])] = e["value"]
    return out


def print_summary(meta, rows):
    jobs = job_table(rows)
    exp = expected_response_us(meta)
    print(f"source={meta.get('source', '?')}  platform={meta.get('platform', '?')}  "
          f"pin_failures={meta.get('pin_failures', '?')}  dropped={meta.get('events_dropped', '?')}")
    if meta.get("source") == "synthetic":
        print("*** SYNTHETIC DATA: ideal-model output, not a measurement ***")
    if meta.get("pin_failures", "0") not in ("0", "?"):
        print("*** WARNING: threads were not pinned to one CPU; results may be invalid ***")

    hdr = f"{'task':<8}{'prio':>5}{'job':>4}{'wake lat (us)':>15}{'resp (ms)':>11}{'expected (ms)':>15}{'delta (ms)':>12}"
    print("\n" + hdr + "\n" + "-" * len(hdr))
    prio = {r["task"]: r["prio"] for r in rows}
    for (task, job), j in sorted(jobs.items(), key=lambda kv: (-prio[kv[0][0]], kv[0][1])):
        if "resp_us" not in j:
            continue
        e = exp.get((task, job))
        e_s = f"{e / 1000:.1f}" if e is not None else "-"
        d_s = f"{(j['resp_us'] - e) / 1000:+.1f}" if e is not None else "-"
        print(f"{task:<8}{prio[task]:>5}{job:>4}{j['wake_us']:>15}{j['resp_us'] / 1000:>11.1f}{e_s:>15}{d_s:>12}")

    lat = [j["wake_us"] for j in jobs.values() if "wake_us" in j]
    if lat:
        print(f"\nwake-up latency (us): min {min(lat)}  mean {st.mean(lat):.0f}  max {max(lat)}  (n={len(lat)})")


def plot_timeline(meta, rows, out_path):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from matplotlib.patches import Patch
    except ImportError:
        print("matplotlib not installed: skipping plot (pip install matplotlib)")
        return

    chunk_us = int(meta.get("chunk_ms", 10)) * 1000
    prio = {}
    for r in rows:
        prio[r["task"]] = r["prio"]
    order = sorted(prio, key=lambda n: -prio[n])             # highest priority on top
    ypos = {n: len(order) - 1 - i for i, n in enumerate(order)}
    colors = dict(zip(order, ["#c0392b", "#d68910", "#2e86c1", "#7d3c98"]))

    last = {}                                                # (task, job) -> last event time
    fig, ax = plt.subplots(figsize=(11, 3.6))
    for r in sorted(rows, key=lambda r: r["t_us"]):
        key = (r["task"], r["job"])
        if r["event"] == "WAKE":
            last[key] = r["t_us"]
            ax.plot(r["t_us"] / 1000, ypos[r["task"]] + 0.42, marker="v", color=colors[r["task"]], ms=6)
        elif r["event"] == "TICK" and key in last:
            start, end = last[key], r["t_us"]
            gap = end - start > 2.5 * chunk_us
            ax.barh(ypos[r["task"]], (end - start) / 1000, left=start / 1000, height=0.6,
                    color=colors[r["task"]], alpha=0.35 if gap else 0.9,
                    hatch="///" if gap else None, edgecolor=colors[r["task"]],
                    linewidth=0 if not gap else 0.3)
            last[key] = end

    ax.set_yticks(list(ypos.values()))
    ax.set_yticklabels([f"{n} (prio {prio[n]})" for n in ypos])
    ax.set_xlabel("time since start (ms)")
    ax.legend(handles=[Patch(facecolor="grey", label="running"),
                       Patch(facecolor="grey", alpha=0.35, hatch="///", label="chunk interrupted (preempted)")],
              loc="upper right", fontsize=8)
    tag = "SYNTHETIC (ideal model, not measured)" if meta.get("source") == "synthetic" \
        else f"measured on {meta.get('platform', '?')}"
    ax.set_title(f"priority_demo timeline: {tag}   (v = job released)", fontsize=10)
    ax.grid(axis="x", alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path, dpi=140)
    print(f"timeline written to {out_path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("--out", help="PNG path (default: next to the CSV)")
    ap.add_argument("--no-plot", action="store_true")
    a = ap.parse_args()

    meta, rows = load(a.csv)
    print_summary(meta, rows)
    if not a.no_plot:
        plot_timeline(meta, rows, a.out or os.path.splitext(a.csv)[0] + "_timeline.png")


if __name__ == "__main__":
    main()
