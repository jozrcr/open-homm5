"""Turn the raw Ghidra export into the committed inventory files.

Usage:
    python tools/inventory/build_inventory.py analysis/export ref/H5_Game_31_vanilla.exe

Reads (from analysis/export/, see README.md):
    functions.tsv, func_strings.tsv   from ghidra/ExportInventory.java
    rtti_scan.tsv                     from rtti_scan.py
Writes:
    progress/inventory.csv   one row per function: address, size, name, origin, class, subsystem
    analysis/classes.txt     one row per RTTI class: name, type descriptor, vftables
    progress/progress.json   dashboard totals (see CLAUDE.md)
    analysis/export/vtable_targets_missing.txt   vftable entries Ghidra has no function for

"origin" is a guess at where a function comes from:
    game      Nival's engine and game code: what we decompile
  library (counted separately, not decompiled):
    crt       Microsoft C/C++ runtime pieces linked into the exe
    lua       Lua 5.0 scripting language (compiled as part of the game project; approximate)
    xiph      libogg / libvorbis / libtheora (audio, video; edges approximate)
    directx   static DirectX SDK code (DxErr error strings, D3DX helpers)
    ubi       Ubisoft online-services SDK and related libraries (ubi.com, now offline)
    thunk     6-byte jump stubs into DLL imports
  compiler-generated (counted separately, produced automatically with their parent code):
    adjustor  "this"-pointer adjustor thunks for multiple / virtual inheritance
    eh        C++ exception unwind funclets (.text$x)
    init      constructors/destructors of global objects (.text$yc / .text$yd)
"""
import bisect
import collections
import csv
import datetime
import hashlib
import json
import pathlib
import re
import struct
import sys

import pefile

REPO = pathlib.Path(__file__).resolve().parents[2]

LIBRARY_ORIGINS = {"crt", "lua", "xiph", "directx", "ubi", "thunk"}
GENERATED_ORIGINS = {"adjustor", "eh", "init"}

# Layout of the tail of .text in the reference exe (H5_Game_31_vanilla.exe).
# The linker places the game's own object files first, then static libraries,
# then the special .text$x / .text$yc / .text$yd groups. Boundaries were read
# off the Ghidra listing (named CRT helpers, import stubs, Unwind@ funclets);
# see docs/phase0-report.md. Everything before the first region is game code.
REGIONS = [
    (0x00D119B0, 0x00D12160, "crt"),      # static CRT objects (entry, __SEH_prolog, __alldiv...) + msvcr71 stubs
    (0x00D12160, 0x00D3E37E, "xiph"),     # libogg / libvorbis / libtheora (no classes, "vorbis"/"theora" strings)
    (0x00D3E37E, 0x00D3E3F6, "thunk"),    # import stubs (um, wsock32, d3d9)
    (0x00D3E3F6, 0x00D59E90, "directx"),  # DxErr9 (DXGetErrorString/Description), D3DX static helpers
    (0x00D59E90, 0x00D92D30, "ubi"),      # ubi.com SDK ("ladderquery", "clanservice"...), ATL, /GS cookie
    (0x00D92D30, 0x00DA1950, "eh"),       # exception unwind funclets (Ghidra names them Unwind@...)
    (0x00DA1950, 0x00E0A000, "init"),     # global object initializers / terminators
]

# Substrings that only Lua 5.0's own source uses.
LUA_STRINGS = [
    "attempt to %", "attempt to compare", "value for `lua_getinfo' is not a function", "tag-method",
    "C stack overflow", "error in error handling", "cannot resume dead coroutine", "attempt to yield across",
    "malformed number", "unfinished long string", "unfinished long comment", "unfinished string",
    "lexical element too long", "near `", "<eof>", "too many local variables", "too many upvalues",
    "chunk has too many syntax levels", "' expected", "unexpected symbol", "ambiguous syntax",
    "cannot use `...'", "no loop to break", "function or expression too complex", "constant table overflow",
    "code size overflow", "string slice too long", "invalid capture index", "invalid pattern capture",
    "malformed pattern", "too many captures", "unfinished capture", "invalid format string",
    "invalid option to `format'", "table index is nil", "table index is NaN", "table overflow",
    "invalid key for `next'", "invalid order function for sorting", "wrong number of arguments to `insert'",
    "in table for `concat'", "bad argument #", "calling `", "expected, got", "invalid option `",
    "_LOADED", "_ALERT", "`tostring' must return", "nil or table expected", "cannot change a protected metatable",
    "base out of range", "too many results to unpack", "assertion failed!", "__metatable", "__tostring",
    "__newindex", "bad code in precompiled chunk", "memory allocation error", "[string \"", "LUA_PATH",
    "?;?.lua", "loop or previous error loading", "unable to dump given function", "invalid value (at index",
]

# this-adjustor thunks: one or two of sub ecx,[ecx+d8] / sub ecx,d8 / sub ecx,d32 /
# add ecx,d8 / add ecx,d32, then jmp rel32
ADJUSTOR = re.compile(rb"^((?:\x2b\x49.|\x83\xe9.|\x81\xe9....|\x83\xc1.|\x81\xc1....){1,2})\xe9(....)", re.S)

# Top-level namespace of a class -> subsystem (first guess, refined later).
NAMESPACE_SUBSYSTEM = {
    "NAI": "ai",
    "NWorld": "adventure",
    "NVisit": "adventure",
    "NRMG": "adventure",
    "NAdventureMapScript": "adventure",
    "NCombatArenaScript": "combat",
    "NUI": "ui",
    "NGfx": "ui",
    "NRender": "render",
    "NGScene": "render",
    "NAnimation": "render",
    "NSound": "io",
    "NFMSound": "io",
    "NDb": "io",
    "NVFS": "io",
    "NZip": "io",
    "NLXML": "io",
    "NXml": "io",
    "NNet": "net",
    "NMP": "net",
    "NMPChat": "net",
    "NUbi": "net",
}
SUBSYSTEMS = ["ai", "net", "adventure", "combat", "ui", "render", "io", "lib"]


def read_tsv(path):
    with open(path, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


def class_subsystem(cls):
    if not cls:
        return ""
    if re.search(r"(^|::)(C|I|S)?(Combat)", cls):
        return "combat"
    if re.search(r"AI|Strateg", cls):
        return "ai"
    return NAMESPACE_SUBSYSTEM.get(cls.split("::")[0], "")


def is_lua_string(s):
    return any(k in s for k in LUA_STRINGS)


def looks_lua_neutral(s):
    """Strings a Lua C function could plausibly reference (short lowercase words, formats)."""
    return is_lua_string(s) or re.fullmatch(r"[a-z_ .%0-9]{1,24}", s) is not None


def main():
    export, exe = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    data = exe.read_bytes()
    pe = pefile.PE(data=data, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    text_lo, text_hi = base + text.VirtualAddress, base + text.VirtualAddress + text.Misc_VirtualSize

    def file_off(va):
        for s in pe.sections:
            if s.VirtualAddress <= va - base < s.VirtualAddress + s.SizeOfRawData:
                return s.PointerToRawData + va - base - s.VirtualAddress
        return None

    def read_u32(va):
        off = file_off(va)
        return None if off is None else struct.unpack_from("<I", data, off)[0]

    funcs = read_tsv(export / "functions.tsv")
    for f in funcs:
        f["va"] = int(f["address"], 16)
        f["size"] = int(f["size"])

    # Adjustor thunks often jump straight to the function placed right after
    # them, and Ghidra then merges both into one function. Split them again:
    # the thunk keeps the start address, the real function gets its own row.
    split = []
    for f in funcs:
        off = file_off(f["va"])
        m = ADJUSTOR.match(data[off:off + 16]) if off is not None else None
        f["adjustor"] = bool(m)
        if not m:
            continue
        thunk_len = len(m.group(1)) + 5
        target = f["va"] + thunk_len + struct.unpack("<i", m.group(2))[0]
        f["adj_target"] = target
        if f["va"] + thunk_len <= target < f["va"] + f["size"]:
            rest = dict(f, va=target, size=f["size"] - (target - f["va"]), adjustor=False,
                        name="FUN_%08x" % target, address="%08x" % target, source="SPLIT")
            f["size"] = thunk_len
            split.append(rest)
    funcs += [s for s in split if s["va"] not in {f["va"] for f in funcs}]
    funcs.sort(key=lambda f: f["va"])
    by_va = {f["va"]: f for f in funcs}

    strings = collections.defaultdict(list)
    for row in read_tsv(export / "func_strings.tsv"):
        strings[int(row["function"], 16)].append(row["string"])

    # --- classes from RTTI (rtti_scan.py) --------------------------------------
    classes = collections.defaultdict(lambda: {"td": "", "vftables": []})
    for r in read_tsv(export / "rtti_scan.tsv"):
        if r["kind"] == "td":
            classes[r["class"]]["td"] = r["address"]
        elif r["kind"] == "vftable":
            classes[r["class"]]["vftables"].append((r["address"], int(r["object_offset"])))

    # Virtual functions: walk each vftable while entries point into .text.
    # A vftable ends where the next one's locator pointer begins (that pointer
    # is in .rdata, so the walk stops on it naturally).
    vt_starts = {int(v, 16) for c in classes.values() for v, _ in c["vftables"]}
    owner = {}
    vtable_targets = set()
    for name in sorted(classes):
        for v, offset in classes[name]["vftables"]:
            va = int(v, 16)
            n = 0
            while True:
                if n and (va + 4 * n) in vt_starts:
                    break
                ptr = read_u32(va + 4 * n)
                if ptr is None or not (text_lo <= ptr < text_hi):
                    break
                vtable_targets.add(ptr)
                # main vftable (offset 0) of the class wins over secondary ones
                if offset == 0 or ptr not in owner:
                    owner.setdefault(ptr, name)
                n += 1
            classes[name].setdefault("slots", []).append(n)
    missing = sorted(t for t in vtable_targets if t not in by_va)
    with open(export / "vtable_targets_missing.txt", "w", encoding="utf-8") as fo:
        fo.write("".join("%08x\n" % t for t in missing))

    # vftables point at adjustor thunks; the class also owns the thunk's target
    for f in funcs:
        if f.get("adj_target") and f["va"] in owner:
            owner.setdefault(f["adj_target"], owner[f["va"]])

    for f in funcs:
        f["class"] = f["namespace"] if f["namespace"] in classes else owner.get(f["va"], "")

    # --- origin ------------------------------------------------------------------
    region_starts = [r[0] for r in REGIONS]
    for f in funcs:
        f["origin"] = "game"
        i = bisect.bisect_right(region_starts, f["va"]) - 1
        if i >= 0 and f["va"] < REGIONS[i][1]:
            f["origin"] = REGIONS[i][2]
        if f["thunk"] == "1" or ".DLL::" in f["name"].upper():
            f["origin"] = "thunk"
        elif f["fid"] == "1" or (f["origin"] != "game" and re.match(r"_{1,2}[a-z]", f["name"])):
            f["origin"] = "crt"
        elif f["adjustor"] and f["size"] <= 16:
            f["origin"] = "adjustor"

    # Lua: Nival compiled the Lua .c files inside the game project, so they sit
    # between game files. Seed with functions using Lua-only strings, keep
    # clusters of 2+ seeds, then grow each cluster over neighbouring
    # functions that have no class and no game-looking strings.
    seeds = sorted(f["va"] for f in funcs if f["origin"] == "game"
                   and any(is_lua_string(s) for s in strings.get(f["va"], [])))
    clusters, cur = [], seeds[:1]
    for va in seeds[1:]:
        if va - cur[-1] < 0x4000:
            cur.append(va)
        else:
            clusters.append(cur)
            cur = [va]
    if cur:
        clusters.append(cur)
    clusters = [c for c in clusters if len(c) >= 2]
    index = {f["va"]: i for i, f in enumerate(funcs)}

    def lua_candidate(f):
        return (f["origin"] == "game" and not f["class"]
                and all(looks_lua_neutral(s) for s in strings.get(f["va"], [])))

    lua_ranges = []
    for c in clusters:
        lo, hi = index[c[0]], index[c[-1]]
        while lo > 0 and lua_candidate(funcs[lo - 1]):
            lo -= 1
        while hi + 1 < len(funcs) and lua_candidate(funcs[hi + 1]):
            hi += 1
        for f in funcs[lo:hi + 1]:
            if lua_candidate(f) or f["va"] in c:
                f["origin"] = "lua"
        lua_ranges.append((funcs[lo]["va"], funcs[hi]["va"], len(c)))

    for f in funcs:
        if f["origin"] == "game":
            f["subsystem"] = class_subsystem(f["class"])
        elif f["origin"] in LIBRARY_ORIGINS:
            f["subsystem"] = "lib"
        else:
            f["subsystem"] = ""

    # --- write inventory.csv -----------------------------------------------------
    with open(REPO / "progress" / "inventory.csv", "w", encoding="utf-8", newline="") as fo:
        w = csv.writer(fo, lineterminator="\n")
        w.writerow(["address", "size", "name", "origin", "class", "subsystem"])
        for f in funcs:
            name = f["name"] if not f["namespace"] else f["namespace"] + "::" + f["name"]
            w.writerow(["%08x" % f["va"], f["size"], name, f["origin"], f["class"], f["subsystem"]])

    # --- write classes.txt -------------------------------------------------------
    with open(REPO / "analysis" / "classes.txt", "w", encoding="utf-8", newline="\n") as fo:
        fo.write("# RTTI classes recovered from the reference exe (tools/inventory/rtti_scan.py).\n")
        fo.write("# class <TAB> type_descriptor <TAB> vftables as address+offset_in_object:slot_count, ...\n")
        for name in sorted(classes):
            c = classes[name]
            vts = ", ".join("%s+%d:%d" % (v, off, s)
                            for (v, off), s in zip(c["vftables"], c.get("slots", [])))
            fo.write("%s\t%s\t%s\n" % (name, c["td"], vts))

    # --- write progress.json -----------------------------------------------------
    game = [f for f in funcs if f["origin"] == "game"]
    sub_counts = collections.Counter(f["subsystem"] or "unassigned" for f in game)
    progress = {
        "generated": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "reference": "H5_Game.exe 3.1 (sha256 %s)" % hashlib.sha256(data).hexdigest(),
        "totals": {
            "functions": len(game),
            "library": sum(1 for f in funcs if f["origin"] in LIBRARY_ORIGINS),
            "generated": sum(1 for f in funcs if f["origin"] in GENERATED_ORIGINS),
            "matched": 0,
            "equivalent": 0,
        },
        "subsystems": [{"name": s, "functions": sub_counts.get(s, 0), "matched": 0}
                       for s in SUBSYSTEMS if s != "lib"] +
                      [{"name": "unassigned", "functions": sub_counts.get("unassigned", 0), "matched": 0}],
        "units": [],
    }
    with open(REPO / "progress" / "progress.json", "w", encoding="utf-8", newline="\n") as fo:
        json.dump(progress, fo, indent=2)
        fo.write("\n")

    # --- summary to stdout ---------------------------------------------------------
    origins = collections.Counter(f["origin"] for f in funcs)
    print("functions:", len(funcs), dict(origins.most_common()))
    print("totals:", progress["totals"])
    print("game bytes: %d of %d" % (sum(f["size"] for f in game), sum(f["size"] for f in funcs)))
    for lo, hi, n in lua_ranges:
        print("lua cluster %08x-%08x (%d seeds)" % (lo, hi, n))
    print("classes:", len(classes), "with vftable:", sum(1 for c in classes.values() if c["vftables"]))
    print("vftable targets:", len(vtable_targets), "without a Ghidra function:", len(missing))
    print("game functions with a class:", sum(1 for f in game if f["class"]))
    print("subsystems (game):", dict(sub_counts.most_common()))


if __name__ == "__main__":
    main()
