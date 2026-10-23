"""Where does the AI spend its thinking time? Reads the robot's AI profiler
samples (run_ai_test.py --profile-ai -> build/robot/<run>/ai-profile.txt) and
lists the game functions most often found running ("self": the function the
main thread was in) and on the call stack ("inclusive": the function or
something it called).

Usage:
    python tools/robot/profile_report.py build/robot/<run>/ai-profile.txt [--top 30]

Function boundaries and names come from analysis/export/functions.tsv and
progress/inventory.csv (class, subsystem). Samples are taken every ~2 ms while
an AI turn-driver call runs, so N samples are roughly N x 2 ms of thinking.

Samples in our own hook DLL (the mods) are named from its linker map,
build/hook/dinput8.map, using the load address the profiler writes on its
first line ("# dll <address>"; --dll-base for older profiles). The map must
belong to the DLL that ran.
"""
import argparse
import bisect
import collections
import csv
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]


def load_functions():
    starts, sizes = [], []
    with open(REPO / "analysis" / "export" / "functions.tsv", encoding="utf-8") as f:
        next(f)
        for line in f:
            a, size = line.split("\t")[:2]
            starts.append(int(a, 16))
            sizes.append(int(size))
    order = sorted(range(len(starts)), key=starts.__getitem__)
    starts = [starts[i] for i in order]
    sizes = [sizes[i] for i in order]
    classes = {}
    path = REPO / "progress" / "inventory.csv"
    if path.exists():
        with open(path, encoding="utf-8") as f:
            for r in csv.DictReader(f):
                classes[int(r["address"], 16)] = (r["class"], r["subsystem"])
    return starts, sizes, classes


def function_of(address, starts, sizes):
    i = bisect.bisect_right(starts, address) - 1
    if i >= 0 and address < starts[i] + max(sizes[i], 1) + 16:
        return starts[i]
    return None


def load_dll_map():
    syms = []
    path = REPO / "build" / "hook" / "dinput8.map"
    if path.exists():
        for line in open(path, encoding="latin-1"):
            m = re.match(r"\s*0001:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})", line)
            if m:
                syms.append((int(m.group(2), 16), m.group(1)))
    syms.sort()
    return [a for a, _ in syms], [n for _, n in syms]


def short_name(mangled):
    """?Name@?A0x1234@fastpaths@mods@@... -> mods::fastpaths::Name (good enough to read)."""
    if not mangled.startswith("?"):
        return mangled
    parts = [p for p in mangled[1:].split("@@")[0].split("@") if p and not p.startswith("?A0x")]
    return "::".join(list(reversed(parts[1:])) + parts[:1])


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("samples")
    p.add_argument("--top", type=int, default=25)
    p.add_argument("--dll-base", help="load address of our DLL (hex), for profiles without a '# dll' line")
    args = p.parse_args()
    starts, sizes, classes = load_functions()
    dll_base = int(args.dll_base, 16) if args.dll_base else None
    dll_starts, dll_names = load_dll_map()
    self_count, incl_count, dll_count = collections.Counter(), collections.Counter(), collections.Counter()
    total = 0
    for line in open(args.samples, encoding="ascii", errors="replace"):
        if line.startswith("# dll "):
            dll_base = int(line.split()[2], 16)
            continue
        if line.startswith("#"):
            continue
        addrs = [int(x, 16) for x in line.split() if x]
        if not addrs:
            continue
        total += 1
        if dll_base and dll_starts and 0 <= addrs[0] - dll_base < 0x100000:
            i = bisect.bisect_right(dll_starts, addrs[0] - dll_base + 0x10000000) - 1
            if i >= 0:
                dll_count[short_name(dll_names[i])] += 1
        funcs = [function_of(a, starts, sizes) for a in addrs]
        if funcs[0] is not None:
            self_count[funcs[0]] += 1
        for fn in set(f for f in funcs if f is not None):
            incl_count[fn] += 1
    print("%d samples (about %.1f s of AI thinking)" % (total, total * 0.002))

    def name(fn):
        cls, sub = classes.get(fn, ("", ""))
        return "%08x  %s%s" % (fn, cls or "?", " [%s]" % sub if sub else "")

    print("\nself (where the AI was):")
    for fn, n in self_count.most_common(args.top):
        print("  %5.1f%%  %s" % (100.0 * n / total, name(fn)))
    if dll_count:
        print("\nself, in our DLL (%.1f%% of all samples):" % (100.0 * sum(dll_count.values()) / total))
        for fn, n in dll_count.most_common(args.top):
            print("  %5.1f%%  %s" % (100.0 * n / total, fn))
    print("\ninclusive (on the call stack):")
    for fn, n in incl_count.most_common(args.top):
        print("  %5.1f%%  %s" % (100.0 * n / total, name(fn)))


if __name__ == "__main__":
    main()
