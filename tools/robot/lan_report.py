"""Compare AI timings of robot LAN runs (tools/robot/run_lan_test.py).

Usage:
    python tools/robot/lan_report.py build/robot/<run>-lan [more runs...]
    python tools/robot/lan_report.py --last 3

For each run (the host's log): settings, AI steps, the wait before each AI
step (gap), AI turn lengths, how far the client lags behind the host and the
game speed factor (with --trace-mp), and the median wait per kind of command.
"""
import argparse
import json
import pathlib
import re
import statistics

REPO = pathlib.Path(__file__).resolve().parents[2]


def settings(run):
    """The network settings the host's DLL reported (mod or older experiments)."""
    found = []
    for line in (run / "host" / "open-homm5.log").read_text(encoding="utf-8", errors="replace").splitlines():
        if "EXPERIMENT" in line:
            found.append(line.split("EXPERIMENT", 1)[1].strip())
        elif "net tuning:" in line:
            found.append(line.split("net tuning:", 1)[1].strip())
    return found or ["game's own network timings"]


def crc_check(run):
    """Compare the world checksums logged by both copies (trace_crc): every PC
    computes one per check number; they must all be equal (no desync)."""
    by_check = {}
    for copy in ("host", "client"):
        path = run / copy / "open-homm5.log"
        if not path.exists():
            continue
        for m in re.finditer(r"crc player=(\d+) check=(-?\d+) crc=([0-9a-f]+)", path.read_text(encoding="utf-8", errors="replace")):
            by_check.setdefault(int(m.group(2)), set()).add(m.group(3))
    if not by_check:
        return None
    bad = sorted(c for c, values in by_check.items() if len(values) > 1)
    if bad:
        return "%d checks, %d DIFFERENT (first at check %d) -> DESYNC" % (len(by_check), len(bad), bad[0])
    return "%d checks, all equal on both copies (no desync)" % len(by_check)


def report(run):
    log = (run / "host" / "open-homm5.log").read_text(encoding="utf-8", errors="replace").splitlines()
    steps = [l for l in log if "ai-step" in l]
    gaps = [float(m.group(1)) for m in (re.search(r"gap=(-?[\d.]+)ms", l) for l in steps) if m and float(m.group(1)) >= 0]
    turns = [i for i, l in enumerate(log) if "AI turn" in l and "started" in l]
    lengths = []
    for k, i in enumerate(turns):
        j = turns[k + 1] if k + 1 < len(turns) else len(log)
        t = [int(l.split()[0]) for l in log[i:j] if "ai-step" in l]
        if len(t) > 1:
            lengths.append((len(t), (t[-1] - t[0]) / 1000))
    lag = [int(a) - int(b) for a, b in re.findall(r"mp-step me=0 segments=\[(\d+),(\d+)\]", "\n".join(log))]
    speed = [float(x) for x in re.findall(r"mp-step .*speed=([\d.]+)", "\n".join(log))]
    print("==", run.name)
    for e in settings(run):
        print("   network:", e)
    if not gaps:
        print("   no AI steps")
        return
    q = sorted(gaps)
    print("   AI steps %d, wait before a step: median %.0f ms (p10 %.0f, p90 %.0f)" %
          (len(steps), statistics.median(q), q[len(q) // 10], q[len(q) * 9 // 10]))
    if lengths:
        print("   AI turns: " + ", ".join("%d steps %.1f s" % x for x in lengths) +
              "  -> %.2f s per step" % (sum(s for _, s in lengths) / max(1, sum(n - 1 for n, _ in lengths))))
    if lag:
        print("   client behind host (as the host sees it): median %d segments; speed factor median %.2f" %
              (statistics.median(lag), statistics.median(speed)))
    crc = crc_check(run)
    if crc:
        print("   world checksums: %s" % crc)
    summary = run / "host" / "summary.json"
    if summary.exists():
        kinds = json.loads(summary.read_text(encoding="utf-8")).get("wait_after_step_kind", {})
        for k, v in list(kinds.items())[:3]:
            print("   %-58s %4d x  median %6.0f ms" % (k[:58], v["count"], v["median_wait_ms"]))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("runs", nargs="*")
    p.add_argument("--last", type=int, default=0, help="the last N LAN runs in build/robot")
    args = p.parse_args()
    runs = [pathlib.Path(r) for r in args.runs]
    if args.last:
        runs += sorted((REPO / "build" / "robot").glob("*-lan"))[-args.last:]
    for r in runs:
        report(r)


if __name__ == "__main__":
    main()
