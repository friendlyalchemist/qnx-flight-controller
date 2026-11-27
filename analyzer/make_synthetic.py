"""Write a SYNTHETIC log (ideal model output, not measured) in the same CSV format
as the C program. Use it to test analyze.py without a QNX VM.

    python make_synthetic.py ../examples/sample_synthetic.csv
"""
import sys

from rtmodel import simulate

CHUNK_MS = 10
TASKS = [  # keep in sync with g_cfg[] in src/priority_demo.c
    dict(name="HIGH",   prio=30, offset_ms=500,  period_ms=1000, jobs=3, work_ms=200),
    dict(name="MEDIUM", prio=20, offset_ms=1000, period_ms=0,    jobs=1, work_ms=1000),
    dict(name="LOW",    prio=10, offset_ms=0,    period_ms=0,    jobs=1, work_ms=3000),
]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "sample_synthetic.csv"
    events = sorted(simulate(TASKS, CHUNK_MS), key=lambda e: e["t_us"])
    with open(out, "w") as f:
        f.write("# platform=simulation\n# source=synthetic\n# program=rtmodel\n")
        f.write(f"# chunk_ms={CHUNK_MS}\n")
        for t in TASKS:
            f.write("# task={name},{prio},{offset_ms},{period_ms},{jobs},{work_ms}\n".format(**t))
        f.write("t_us,task,prio,job,event,value\n")
        for e in events:
            t = TASKS[e["task"]]
            f.write(f"{e['t_us']},{t['name']},{t['prio']},{e['job']},{e['event']},{e['value']}\n")
    print(f"wrote {out} ({len(events)} events, SYNTHETIC)")


if __name__ == "__main__":
    main()
