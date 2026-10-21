"""Byte-compare two same-size builds of an exe and list the patched spots.

Usage:
    python tools/inventory/pe_diff.py ref/H5_Game_31_vanilla.exe ref/MMH55_Utility.exe

Prints one line per changed region: section, virtual address, length, and the
first bytes on each side. Differences closer than 16 bytes are merged.
"""
import sys

import pefile

GAP = 16


def main():
    a_path, b_path = sys.argv[1], sys.argv[2]
    a, b = open(a_path, "rb").read(), open(b_path, "rb").read()
    if len(a) != len(b):
        sys.exit("sizes differ (%d vs %d): not a simple patch" % (len(a), len(b)))
    pe = pefile.PE(data=a, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase

    def where(off):
        for s in pe.sections:
            if s.PointerToRawData <= off < s.PointerToRawData + s.SizeOfRawData:
                return (s.Name.rstrip(b"\0").decode(),
                        base + s.VirtualAddress + off - s.PointerToRawData)
        return "header", off

    diffs = [i for i in range(len(a)) if a[i] != b[i]]
    if not diffs:
        print("identical")
        return
    runs, start, prev = [], diffs[0], diffs[0]
    for i in diffs[1:]:
        if i > prev + GAP:
            runs.append((start, prev))
            start = i
        prev = i
    runs.append((start, prev))

    print("%d differing bytes in %d regions" % (len(diffs), len(runs)))
    for s, e in runs:
        sec, va = where(s)
        print("%-7s %#010x len=%-4d %s -> %s" % (
            sec, va, e - s + 1, a[s:e + 1][:16].hex(), b[s:e + 1][:16].hex()))


if __name__ == "__main__":
    main()
