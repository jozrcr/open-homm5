"""What the decompilation worker should do next (docs/decomp/worker.md).

    python tools/decomp/next.py            # the next 20 functions to try
    python tools/decomp/next.py -n 50
    python tools/decomp/next.py --status   # how far each priority area is

Order: the areas in tools/decomp/priorities.txt, top to bottom, then the rest of
the game's code by address. Within an area: by address, the order the linker
kept, so a unit's functions come together. Skipped:
  - functions already in progress/matched.txt or progress/equivalent.txt;
  - library and compiler-generated functions (origin != game in
    progress/inventory.csv);
  - functions that failed MAX_ATTEMPTS times (progress/attempts.txt).
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


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-n", type=int, default=20, help="how many functions to list")
    p.add_argument("--status", action="store_true", help="progress per priority area")
    args = p.parse_args()

    with open(PROGRESS / "inventory.csv", encoding="utf-8") as f:
        game = [r for r in csv.DictReader(f) if r["origin"] == "game"]
    done = addresses(PROGRESS / "matched.txt") | addresses(PROGRESS / "equivalent.txt")
    failed = attempts()
    areas = priorities()

    def area_of(addr):
        for i, (_name, low, high) in enumerate(areas):
            if low <= addr < high:
                return i
        return len(areas)

    if args.status:
        for i, (name, low, high) in enumerate(areas + [("rest of the game", 0, 0)]):
            rs = [r for r in game if area_of(int(r["address"], 16)) == i]
            ok = sum(1 for r in rs if int(r["address"], 16) in done)
            print("%-28s %5d of %5d functions done" % (name, ok, len(rs)))
        return 0

    todo = [r for r in game if int(r["address"], 16) not in done and failed[int(r["address"], 16)] < MAX_ATTEMPTS]
    todo.sort(key=lambda r: (area_of(int(r["address"], 16)), int(r["address"], 16)))
    for r in todo[:args.n]:
        addr = int(r["address"], 16)
        name = areas[area_of(addr)][0] if area_of(addr) < len(areas) else "rest"
        print("%08x  %5s bytes  %-24s %s%s" % (addr, r["size"], name, r["class"] or "-",
                                             "  (%d failed attempts)" % failed[addr] if failed[addr] else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
