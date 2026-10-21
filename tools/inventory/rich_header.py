"""Decode the MSVC "Rich header" of an exe: which compiler/linker builds made it.

Usage:
    python tools/inventory/rich_header.py ref/MMH55_Utility.exe [comp_id.txt]

comp_id.txt is the ID table from https://github.com/dishather/richprint
(optional; without it, only raw IDs are printed).
"""
import re
import sys

import pefile


def load_names(path):
    names = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"^([0-9a-fA-F]{8})\s+(.*)$", line.strip())
            if m:
                names[int(m.group(1), 16)] = m.group(2)
    return names


def main():
    pe = pefile.PE(sys.argv[1], fast_load=True)
    names = load_names(sys.argv[2]) if len(sys.argv) > 2 else {}
    rich = pe.parse_rich_header()
    if not rich:
        sys.exit("no Rich header (stripped, or not built with MSVC)")
    vals = rich["values"]
    print("key %s" % rich["key"].hex())
    print("comp.id   prodid build  count  tool")
    for i in range(0, len(vals), 2):
        cid, count = vals[i], vals[i + 1]
        print("%08x  0x%04x %5d %6d  %s" % (
            cid, cid >> 16, cid & 0xFFFF, count, names.get(cid, "?")))


if __name__ == "__main__":
    main()
