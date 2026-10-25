#!/usr/bin/env python3
"""Names the addresses in the hang watchdog's report (src/hook/watchdog.cpp).

    python tools/hang/symbolize.py <open-homm5.log> [--map build/hook/dinput8.map]
                                   [--functions analysis/export/functions.tsv] [--all]

Reads the "watchdog:" lines of the log (only the last report, or all with --all)
and adds a name after each address:
  - our DLL ("dinput8.dll (open-homm5)+0x1234"): the function from the map file of
    THE SAME BUILD (the report gives the DLL's link time stamp; the map's header
    has it too: "Timestamp is ...");
  - the exe ("MMH55_Utility_64.exe+0x46e9d0 (0x0086e9d0..."): the Ghidra function
    that contains it (functions.tsv, exported from the reference exe; gitignored).
System DLLs are already named by the report (nearest export).
"""
import argparse
import bisect
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def load_map(path):
    """[(rva, name)] of the code symbols of an MSVC map file, sorted, and its time stamp."""
    preferred = 0x10000000
    stamp = None
    rows = []
    for line in Path(path).read_text(errors="replace").splitlines():
        m = re.match(r"\s*Timestamp is ([0-9a-f]+)", line)
        if m:
            stamp = int(m.group(1), 16)
        m = re.match(r"\s*Preferred load address is ([0-9a-f]+)", line)
        if m:
            preferred = int(m.group(1), 16)
        m = re.match(r"\s*0001:[0-9a-f]+\s+(\S+)\s+([0-9a-f]{8})\s", line)
        if m:
            rows.append((int(m.group(2), 16) - preferred, demangle(m.group(1))))
    rows.sort()
    return rows, stamp


def demangle(name):
    """'?PumpHook@?A0x85d5cfd5@mods@@YIXPAX0@Z' -> 'mods::PumpHook' (enough to read)."""
    if not name.startswith("?") or name.startswith("??"):
        return name
    parts = name[1:].split("@@")[0].split("@")
    parts = [p for p in parts if p and not p.startswith("?A0x")]
    return "::".join(reversed(parts))


def load_functions(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        next(f)
        for line in f:
            p = line.rstrip("\n").split("\t")
            rows.append((int(p[0], 16), int(p[1]), (p[3] + "::" if p[3] else "") + p[2]))
    rows.sort()
    return rows


def nearest(rows, key):
    i = bisect.bisect_right([r[0] for r in rows], key) - 1
    return rows[i] if i >= 0 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--map", default=str(REPO / "build" / "hook" / "dinput8.map"))
    ap.add_argument("--functions", default=str(REPO / "analysis" / "export" / "functions.tsv"))
    ap.add_argument("--all", action="store_true", help="every report in the log, not only the last one")
    args = ap.parse_args()

    dll = exe = None
    stamp = None
    if Path(args.map).exists():
        dll, stamp = load_map(args.map)
    else:
        print(f"(no map file {args.map}: our DLL's offsets stay as they are)")
    if Path(args.functions).exists():
        exe = load_functions(args.functions)
        exe_starts = [r[0] for r in exe]
    else:
        print(f"(no {args.functions}: the exe's addresses stay as they are)")

    lines = [l.rstrip("\n") for l in open(args.log, encoding="utf-8", errors="replace") if "watchdog:" in l]
    if not args.all:
        starts = [i for i, l in enumerate(lines) if "looking where it is" in l]
        lines = lines[starts[-1]:] if starts else lines
    for line in lines:
        notes = []
        m = re.search(r"link time stamp ([0-9a-f]{8})", line)
        if m and stamp is not None and int(m.group(1), 16) != stamp:
            notes.append(f"WARNING: the map is from another build (its time stamp is {stamp:08x})")
        for m in re.finditer(r"dinput8\.dll \(open-homm5\)\+0x([0-9a-f]+)", line):
            rva = int(m.group(1), 16)
            r = nearest(dll, rva) if dll else None
            if r:
                notes.append(f"{r[1]}+0x{rva - r[0]:x}")
        for m in re.finditer(r"\(0x([0-9a-f]{8})", line):
            va = int(m.group(1), 16)
            if exe:
                i = bisect.bisect_right(exe_starts, va) - 1
                if i >= 0 and exe[i][0] <= va < exe[i][0] + exe[i][1]:
                    notes.append(f"{exe[i][2]}+0x{va - exe[i][0]:x}")
        print(line + (("   => " + ", ".join(notes)) if notes else ""))


if __name__ == "__main__":
    sys.exit(main())
