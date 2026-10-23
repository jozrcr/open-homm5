"""Compare the AI's path searches of robot runs (run_ai_test.py --trace-paths).

Usage:
    python tools/robot/path_report.py build/robot/<run A> build/robot/<run B> [...]

For each run: AI thinking time, time in path searches, and the call sites that
cost the most. Then each run is compared with the first one, search by search:
the big searches (2,000+ nodes) must come in the same order with the same
start, node count and result fingerprint (a hash of every node the search
reached, src/hook/robot_paths.cpp). Two runs of the unchanged game on the same
save show whether the game is deterministic; a mod that claims "same results"
(src/mods/fast_paths.cpp) must then match too.

By default the comparison leaves out bytes the game never reads, which differ
from run to run even without a mod (docs/subsystems/ai-pathfinder.md):
node fields 7, 9 and 13 (uninitialized start-node bytes and cost padding) and
flag bits 10-14. --strict compares everything.
"""
import argparse
import collections
import pathlib
import re

DETAIL = re.compile(r"path site=(\w+) pf=\w+ start=([\d,]+) target=(\w+) limit=(-?\d+) nodes=(\d+) tiles=\d+ "
                    r"same=\d+ us=(\d+) reached=(\d+) result=(\w+)[^\n]*?(?: fields=((?:[0-9a-f]{4} ){16})(?:bits=([0-9a-f]{64}))?"
                    r"(?: list=(\d+)/(\w+) open=(\d+)/(\w+) ends=(\w+))?)?$", re.M)
STEP = re.compile(r"paths step searches=(\d+) .* ms=([\d.]+)(?: remembered=(\d+)/(\d+))? sites:(.*)")
THINK = re.compile(r"ai-step .* think=([\d.]+)ms")


def read(run):
    text = (pathlib.Path(run) / "open-homm5.log").read_text(encoding="utf-8", errors="replace")
    searches = [m.groups() for m in DETAIL.finditer(text)]
    steps = [m.groups() for m in STEP.finditer(text)]
    think = sum(float(m.group(1)) for m in THINK.finditer(text))
    sites = collections.Counter()
    for _n, _ms, _h, _t, detail in steps:
        for part in detail.split(" | ")[0].split():  # " | misses ..." follows in newer logs
            site, rest = part.split(":", 1)
            sites[site] += float(rest.split("/")[2].rstrip("ms"))
    return searches, steps, think, sites


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("runs", nargs="+")
    p.add_argument("--ignore-fields", default="7,9,13",
                   help="node dwords (0-15) to leave out of the comparison (default: never read by the game); "
                        "needs runs that logged fields=")
    p.add_argument("--ignore-flag-bits", default="10,11,12,13,14",
                   help="bits (0-31) of the node flags (field 1) to leave out (default: never initialized); "
                        "needs runs that logged bits=; field 1 is then compared bit by bit")
    p.add_argument("--strict", action="store_true", help="compare every byte (no fields or bits left out)")
    args = p.parse_args()
    if args.strict:
        args.ignore_fields = args.ignore_flag_bits = ""
    ignore = {int(x) for x in args.ignore_fields.split(",") if x}
    ignore_bits = {int(x) for x in args.ignore_flag_bits.split(",") if x}

    def bits(s):
        return [s[9][i * 2:i * 2 + 2] for i in range(32)] if s[9] else None

    def key(s):
        if (ignore or ignore_bits) and s[8]:
            fields = s[8].split()
            k = s[:5] + (s[6],) + tuple(f for i, f in enumerate(fields) if i not in ignore and
                                        not (i == 1 and ignore_bits and s[9]))
            if ignore_bits and s[9] and 1 not in ignore:
                k += tuple(b for i, b in enumerate(bits(s)) if i not in ignore_bits)
            return k
        return s[:5] + s[6:8]
    data = [read(r) for r in args.runs]
    for run, (searches, steps, think, sites) in zip(args.runs, data):
        ms = sum(float(s[1]) for s in steps)
        hits = sum(int(s[2] or 0) for s in steps[-1:])
        total = sum(int(s[3] or 0) for s in steps[-1:])
        print("== %s" % pathlib.Path(run).name)
        print("   AI steps %d, thinking %.1f s, in path searches %.1f s; big searches logged %d" %
              (len(steps), think / 1000, ms / 1000, len(searches)))
        if total:
            print("   remembered checks: %d of %d answered from memory (%.0f%%)" % (hits, total, 100.0 * hits / total))
        print("   " + ", ".join("%s %.1f s" % (site, t / 1000) for site, t in sites.most_common(4)))
    base = data[0][0]
    for run, (searches, _steps, _think, _sites) in zip(args.runs[1:], data[1:]):
        n = min(len(base), len(searches))
        same = 0
        first = None
        for i in range(n):
            a, b = base[i], searches[i]
            # the reached list, the rest of the open list and the end flags, when both runs logged them
            outputs_same = not (a[10] and b[10]) or a[10:15] == b[10:15]
            if key(a) == key(b) and outputs_same:
                same += 1
            elif first is None:
                first = i
        fields_diff, bits_diff = collections.Counter(), collections.Counter()
        for a, b in zip(base[:n], searches[:n]):
            if a[8] and b[8]:
                fa, fb = a[8].split(), b[8].split()
                fields_diff.update(i for i in range(16) if fa[i] != fb[i])
            if a[9] and b[9]:
                ba, bb = bits(a), bits(b)
                bits_diff.update(i for i in range(32) if ba[i] != bb[i])
        if fields_diff:
            print("   node fields that differ (field: searches): %s" % dict(sorted(fields_diff.items())))
        if bits_diff:
            print("   flag bits that differ (bit: searches): %s" % dict(sorted(bits_diff.items())))
        print("== %s vs %s: %d of %d big searches identical%s" %
              (pathlib.Path(run).name, pathlib.Path(args.runs[0]).name, same, n,
               "" if first is None else "; first difference at #%d:\n   %s\n   %s" % (first, base[first], searches[first])))
        if len(base) != len(searches):
            print("   (runs logged %d and %d big searches; the shorter run may have been cut by its time limit)" %
                  (len(base), len(searches)))
        t_a = sum(int(s[5]) for s in base[:n])
        t_b = sum(int(s[5]) for s in searches[:n])
        if t_b:
            print("   time of the compared searches: %.1f s -> %.1f s (x%.2f)" % (t_a / 1e6, t_b / 1e6, t_a / t_b))


if __name__ == "__main__":
    main()
