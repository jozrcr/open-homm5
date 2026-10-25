"""Where does MMH5.5's 55u.dll patch the game at run time, and do any of those
spots sit in a game function our hook DLL uses?

Usage:
    python tools/inventory/mmh55_patch_sites.py <path to 55u.dll> [--list]

Why: MMH5.5's co-op exe (MMH55_Utility(_64).exe) loads 55u.dll in place of
Ubisoft's um.dll. On start-up 55u.dll reads MMH55Stats.xml (in
data/MMH55-Stats.pak) and changes code bytes of the exe (movement costs, PEST
"endless simultaneous turns", the battle camera, ...). It keeps those spots in
tables of 16-byte entries: an exe address, the size in bytes, a zero and a
small number (its meaning is not known yet). A new
MMH5.5 version can add spots. If one lands inside a game function that our
mods replace with their own code (src/mods/fast_paths_*.cpp ports the
pathfinder), the game's patched behaviour would be skipped; if one lands on a
call or table slot we redirect, our own byte check makes that mod stay off.

What it prints:
  - the tables found and the number of patched spots;
  - every game function (from progress/inventory.csv) that holds both a
    55u.dll spot and an address named in src/hook or src/mods.
Run it on the old and the new 55u.dll and compare (docs/plans/mmh55-rc19k.md).
Read-only: it only reads the DLL and the repo.
"""
import bisect
import csv
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE_LO, EXE_HI = 0x00401000, 0x00dd9000  # the vanilla 3.1 image (code and data)


def is_entry(d, o):
    if o < 0 or o + 16 > len(d):
        return False
    a, size, zero, small = struct.unpack_from("<IIII", d, o)
    return EXE_LO <= a < EXE_HI and 1 <= size <= 16 and zero == 0 and small < 0x100


def patch_sites(d):
    """Entries that have a neighbour of the same shape (a table, not a coincidence)."""
    sites, tables, o = [], [], 0
    while o + 16 <= len(d):
        if is_entry(d, o) and (is_entry(d, o - 16) or is_entry(d, o + 16)):
            a, size = struct.unpack_from("<II", d, o)
            if tables and tables[-1][1] == o:
                tables[-1][1] = o + 16
            else:
                tables.append([o, o + 16])
            sites.append((a, size))
            o += 16
        else:
            o += 4
    return sites, tables


def load_functions():
    funcs = []
    with open(os.path.join(REPO, "progress", "inventory.csv"), encoding="utf-8") as f:
        for row in csv.DictReader(f):
            funcs.append((int(row["address"], 16), max(int(row["size"]), 1), row["name"]))
    funcs.sort()
    return funcs


def our_addresses():
    found = {}
    pattern = re.compile(r"0x00([4-9a-dA-D][0-9a-fA-F]{5})\b")
    for sub in ("src/hook", "src/mods"):
        for root, _, files in os.walk(os.path.join(REPO, sub)):
            if "third_party" in root:
                continue
            for name in files:
                if not name.endswith((".cpp", ".h")):
                    continue
                with open(os.path.join(root, name), encoding="utf-8", errors="replace") as f:
                    for line in f:
                        for m in pattern.finditer(line):
                            found.setdefault(int("00" + m.group(1), 16), set()).add(name)
    return found


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    d = open(sys.argv[1], "rb").read()
    sites, tables = patch_sites(d)
    print("%s: %d patched spots in %d tables" % (sys.argv[1], len(sites), len(tables)))
    for lo, hi in tables:
        print("  table at file offset 0x%x, %d entries" % (lo, (hi - lo) // 16))
    if "--list" in sys.argv:
        for a, size in sorted(sites):
            print("  0x%08x  %2d bytes" % (a, size))

    funcs = load_functions()
    starts = [f[0] for f in funcs]

    def func_of(a):
        i = bisect.bisect_right(starts, a) - 1
        return funcs[i] if i >= 0 and funcs[i][0] <= a < funcs[i][0] + funcs[i][1] else None

    by_func = {}
    for a, _ in sites:
        f = func_of(a)
        if f:
            by_func.setdefault(f[0], []).append(a)
    ours = {}
    for a, names in our_addresses().items():
        f = func_of(a)
        if f:
            ours.setdefault(f[0], set()).update(names)
    both = sorted(set(by_func) & set(ours))
    print("game functions with both a 55u.dll spot and one of our addresses: %d" % len(both))
    for start in both:
        f = func_of(start)
        print("  %s (0x%08x, %d bytes): 55u.dll spots %s; our files: %s" % (
            f[2], f[0], f[1], ", ".join("0x%08x" % a for a in sorted(by_func[start])), ", ".join(sorted(ours[start]))))


if __name__ == "__main__":
    main()
