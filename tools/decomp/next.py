"""What the decompilation should do next (docs/decomp/coordinator.md).

    python tools/decomp/next.py            # the next 20 functions to try
    python tools/decomp/next.py -n 50
    python tools/decomp/next.py --status   # how far each priority area is
    python tools/decomp/next.py --assign 3 # one group of functions per worker
    python tools/decomp/next.py --big      # functions too big for a worker (for Opus)

Order: the areas in tools/decomp/priorities.txt, top to bottom, then the rest of
the game's code by address. Within an area: by address, the order the linker
kept, so a unit's functions come together. Skipped:
  - functions already in progress/matched.txt or progress/equivalent.txt;
  - functions held for a verifier fix (progress/held.txt);
  - library and compiler-generated functions (origin != game in
    progress/inventory.csv);
  - functions that failed MAX_ATTEMPTS times (progress/attempts.txt);
  - with --assign and by default: functions bigger than --max-size bytes, which
    go to Opus instead (--big lists them).

--assign N splits the next functions into N groups of --group consecutive
functions, one per worker, and names a unit folder for each:
<Area>_<first address>, unique, so parallel workers never write the same files.
"""
import argparse
import collections
import csv
import pathlib

REPO = pathlib.Path(__file__).resolve().parents[2]
PROGRESS = REPO / "progress"
MAX_ATTEMPTS = 3


def addresses(path):
    """Exe addresses listed at the start of each line of a progress file."""
    out = set()
    if path.is_file():
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                out.add(int(line.split()[0], 16))
    return out


def attempts():
    """Exe address -> number of failed attempts (progress/attempts.txt)."""
    count = collections.Counter()
    path = PROGRESS / "attempts.txt"
    if path.is_file():
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                count[int(line.split()[0], 16)] += 1
    return count


def priorities():
    """[(name, low, high)] from tools/decomp/priorities.txt."""
    out = []
    for line in (REPO / "tools" / "decomp" / "priorities.txt").read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            low, high, name = line.split(None, 2)
            out.append((name, int(low, 16), int(high, 16)))
    return out


def unit_name(area, cls, addr):
    """A unique folder name for a worker's group: <Area or class>_<address>."""
    if cls:
        base = cls.split("::")[-1].split("<")[0]
        base = base[1:] if len(base) > 1 and base[0] == "C" and base[1].isupper() else base
    else:
        base = "".join(w.capitalize() for w in area.split()) if area != "rest" else "Game"
    base = "".join(ch for ch in base if ch.isalnum()) or "Game"
    return "%s_%08x" % (base, addr)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-n", type=int, default=20, help="how many functions to list")
    p.add_argument("--status", action="store_true", help="progress per priority area")
    p.add_argument("--assign", type=int, metavar="N", help="N groups of functions, one per worker")
    p.add_argument("--group", type=int, default=20, help="functions per group (default 20)")
    p.add_argument("--max-size", type=int, default=1024, help="bigger functions are left to Opus")
    p.add_argument("--big", action="store_true", help="list the functions bigger than --max-size")
    args = p.parse_args()

    with open(PROGRESS / "inventory.csv", encoding="utf-8") as f:
        game = [r for r in csv.DictReader(f) if r["origin"] == "game"]
    done = addresses(PROGRESS / "matched.txt") | addresses(PROGRESS / "equivalent.txt")
    held = addresses(PROGRESS / "held.txt")
    failed = attempts()
    areas = priorities()

    def area_of(addr):
        for i, (_name, low, high) in enumerate(areas):
            if low <= addr < high:
                return i
        return len(areas)

    def area_name(addr):
        i = area_of(addr)
        return areas[i][0] if i < len(areas) else "rest"

    if args.status:
        for i, (name, low, high) in enumerate(areas + [("rest of the game", 0, 0)]):
            rs = [r for r in game if area_of(int(r["address"], 16)) == i]
            ok = sum(1 for r in rs if int(r["address"], 16) in done)
            hd = sum(1 for r in rs if int(r["address"], 16) in held)
            print("%-28s %5d of %5d functions done%s" % (name, ok, len(rs), "  (%d held)" % hd if hd else ""))
        return 0

    todo = [r for r in game if int(r["address"], 16) not in done | held and failed[int(r["address"], 16)] < MAX_ATTEMPTS]
    todo.sort(key=lambda r: (area_of(int(r["address"], 16)), int(r["address"], 16)))
    # Workers get untried functions up to --max-size; the big ones and the ones a
    # worker already failed are for Opus.
    small = [r for r in todo if int(r["size"]) <= args.max_size and not failed[int(r["address"], 16)]]
    big = [r for r in todo if int(r["size"]) > args.max_size or failed[int(r["address"], 16)]]

    if args.big:
        for r in big[:args.n]:
            addr = int(r["address"], 16)
            print("%08x  %5s bytes  %-24s %s%s" % (addr, r["size"], area_name(addr), r["class"] or "-",
                                                 "  (%d failed attempts)" % failed[addr] if failed[addr] else ""))
        print("%d functions for Opus (over %d bytes, or failed before)" % (len(big), args.max_size))
        return 0

    if args.assign:
        # Consecutive functions of one area per group; a group also ends at a
        # wide address gap (likely another source file).
        groups, cur = [], []
        for r in small:
            addr = int(r["address"], 16)
            if cur and (len(cur) == args.group or area_of(addr) != area_of(int(cur[-1]["address"], 16))
                        or addr - int(cur[-1]["address"], 16) > 0x2000):
                groups.append(cur)
                cur = []
                if len(groups) == args.assign:
                    break
            cur.append(r)
        if cur and len(groups) < args.assign:
            groups.append(cur)
        for g, rs in enumerate(groups):
            first = int(rs[0]["address"], 16)
            classes = collections.Counter(r["class"] for r in rs if r["class"])
            cls = classes.most_common(1)[0][0] if classes else ""
            print("group %d  unit %s  area %s  %d functions, %d bytes" % (
                g + 1, unit_name(area_name(first), cls, first), area_name(first), len(rs),
                sum(int(r["size"]) for r in rs)))
            print("  " + " ".join(r["address"] for r in rs))
        return 0

    for r in small[:args.n]:
        addr = int(r["address"], 16)
        print("%08x  %5s bytes  %-24s %s%s" % (addr, r["size"], area_name(addr), r["class"] or "-",
                                             "  (%d failed attempts)" % failed[addr] if failed[addr] else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
