"""The oracle: compile decompiled units with the original compiler and compare
each function's machine code with the reference exe.

Usage:
    python tools/verify/verify.py                 # build + verify every unit in src/decomp/
    python tools/verify/verify.py GameTimer       # only these units
    python tools/verify/verify.py --list Unit     # show the functions an obj defines (to fill unit.json)
    python tools/verify/verify.py --record        # also write progress/matched.txt from the results

A unit is a folder src/decomp/<Unit>/ holding the reconstructed source of one
original translation unit and a unit.json:
    {
      "sources": ["GameTimer.cpp"],
      "flags": ["/O2"],                       optional, replaces DEFAULT_FLAGS
      "functions": {"00401000": "?Mangled@Name@@..."}
    }

A function MATCHES when:
  1. its bytes in the .obj equal the exe bytes at its address, ignoring the
     4-byte fields that the linker fills in (relocations);
  2. its size equals the size of the function in the exe (progress/inventory.csv);
  3. every relocation points to the same thing in the exe: a function or global
     with a known address (unit.json files, progress/symbols.txt, DLL imports),
     or data whose bytes match (string literals, constant tables).

Rules (CLAUDE.md): never edit this file to make a function pass.
"""
import csv
import os
import pathlib
import struct
import subprocess
import sys

import pefile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import coff  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[2]
EXE = REPO / "ref" / "H5_Game_31_vanilla.exe"
EXE_SHA256 = "94309157dc65498203c1ee0c357a30ae8788184b91d73bcd5eb5c3159aa81375"
TOOLKIT = pathlib.Path(os.environ.get("VCTOOLKIT2003", r"C:\tools\vctoolkit2003"))
DECOMP = REPO / "src" / "decomp"
BUILD = REPO / "build" / "decomp"

# Best known flags for the game's code. Refined as functions match; see docs/toolchain.md.
# /G7 (tune for Pentium 4) is required: without it 3 of the first 10 functions differ.
DEFAULT_FLAGS = ["/nologo", "/c", "/O2", "/G7", "/GX", "/GR", "/W3"]


# --- the reference exe ---------------------------------------------------------
class Exe:
    def __init__(self, path):
        self.data = path.read_bytes()
        self.pe = pefile.PE(data=self.data, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
        self.imports = {}  # undecorated name -> IAT address
        for entry in getattr(self.pe, "DIRECTORY_ENTRY_IMPORT", []):
            for imp in entry.imports:
                if imp.name:
                    self.imports[imp.name.decode("latin-1")] = imp.address

    def read(self, va, n):
        return self.pe.get_data(va - self.base, n)

    def u32(self, va):
        return struct.unpack("<I", self.read(va, 4))[0]


def load_sizes():
    sizes = {}
    with open(REPO / "progress" / "inventory.csv", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            sizes[int(r["address"], 16)] = int(r["size"])
    return sizes


def load_units(names):
    import json
    units = []
    for d in sorted(DECOMP.iterdir()):
        if (d / "unit.json").is_file() and (not names or d.name in names):
            cfg = json.loads((d / "unit.json").read_text(encoding="utf-8"))
            cfg["name"], cfg["dir"] = d.name, d
            units.append(cfg)
    return units


def load_symbol_map(units):
    """Mangled name -> exe address, from all unit.json files and progress/symbols.txt."""
    symbols = {}
    for u in units:
        for addr, name in u.get("functions", {}).items():
            symbols[name] = int(addr, 16)
    path = REPO / "progress" / "symbols.txt"
    if path.is_file():
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                addr, name = line.split(None, 1)
                symbols[name.strip()] = int(addr, 16)
    return symbols


# --- building ----------------------------------------------------------------------
def compile_unit(unit):
    out = BUILD / unit["name"]
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["PATH"] = str(TOOLKIT / "bin") + os.pathsep + env.get("PATH", "")
    env["INCLUDE"] = os.pathsep.join([str(TOOLKIT / "include"), str(DECOMP / "include")])
    objs = []
    for src in unit["sources"]:
        obj = out / (pathlib.Path(src).stem + ".obj")
        cmd = [str(TOOLKIT / "bin" / "cl.exe")] + unit.get("flags", DEFAULT_FLAGS) + \
              ["/Fo" + str(obj), str(unit["dir"] / src)]
        r = subprocess.run(cmd, env=env, capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stdout + r.stderr)
            raise SystemExit("compile failed: %s" % src)
        objs.append(obj)
    return objs


# --- comparing -----------------------------------------------------------------------
def resolve(obj, sym, symbols, exe):
    """Exe address the relocation's symbol should have, or a string saying how to check it."""
    if sym.section == 0:  # external
        if sym.name in symbols:
            return symbols[sym.name]
        if sym.name.startswith("__imp_"):
            bare = sym.name[len("__imp_"):]
            for candidate in (bare, bare.lstrip("_"), bare.lstrip("_").split("@")[0]):
                if candidate in exe.imports:
                    return exe.imports[candidate]
        return None
    if sym.name in symbols:
        return symbols[sym.name]
    return ("content", obj.section(sym.section), sym.value)


def check_function(obj, sym, body, relocs, addr, exe, sizes, symbols):
    problems = []
    exe_size = sizes.get(addr)
    if exe_size is not None and exe_size != len(body):
        problems.append("size: obj %d bytes, exe %d bytes" % (len(body), exe_size))
    target = exe.read(addr, len(body))
    masked = set()
    for r in relocs:
        masked.update(range(r.offset, r.offset + 4))
    for i, (a, b) in enumerate(zip(body, target)):
        if i not in masked and a != b:
            problems.append("bytes differ from +0x%x: obj %s / exe %s" % (
                i, body[i:i + 12].hex(" "), target[i:i + 12].hex(" ")))
            break

    for r in relocs:
        rsym = obj.symbols[r.symbol]
        addend = struct.unpack_from("<i", body, r.offset)[0]
        field = addr + r.offset
        if r.type == coff.REL_REL32:
            actual = field + 4 + struct.unpack("<i", exe.read(field, 4))[0]
        elif r.type == coff.REL_DIR32:
            actual = exe.u32(field)
        elif r.type == coff.REL_DIR32NB:
            actual = exe.u32(field) + exe.base
        else:
            problems.append("relocation type %#x at +0x%x not supported" % (r.type, r.offset))
            continue
        want = resolve(obj, rsym, symbols, exe)
        if want is None:
            problems.append("unknown address for %s (reloc at +0x%x, exe points to %08x): "
                            "add it to progress/symbols.txt" % (rsym.name, r.offset, actual))
        elif isinstance(want, tuple):
            _, sec, base_off = want
            start = base_off + addend
            expect = sec.data[start:]
            if not sec.data.strip(b"\0") and not expect:
                continue
            got = exe.read(actual, len(expect)) if expect else b""
            if expect and got != expect:
                problems.append("data at %08x for %s does not match the obj's %s section" % (
                    actual, rsym.name, sec.name))
        elif want + addend != actual:
            problems.append("reloc at +0x%x to %s: exe has %08x, expected %08x" % (
                r.offset, rsym.name, actual, want + addend))
    return problems


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    exe = Exe(EXE)
    import hashlib
    if hashlib.sha256(exe.data).hexdigest() != EXE_SHA256:
        raise SystemExit("ref/H5_Game_31_vanilla.exe is not the reference exe (see docs/reference-binaries.md)")
    sizes = load_sizes()
    all_units = load_units([])
    symbols = load_symbol_map(all_units)
    units = [u for u in all_units if not args or u["name"] in args]

    results = []
    for unit in units:
        objs = compile_unit(unit)
        defined = {}
        for path in objs:
            obj = coff.CoffObject(path.read_bytes())
            for sym, body, relocs in obj.functions():
                defined[sym.name] = (obj, sym, body, relocs)
        if "--list" in flags:
            print("== %s" % unit["name"])
            for name, (_o, _s, body, relocs) in sorted(defined.items(), key=lambda kv: kv[0]):
                print("  %5d bytes  %2d relocs  %s" % (len(body), len(relocs), name))
            continue
        print("== %s" % unit["name"])
        for addr_s, name in sorted(unit.get("functions", {}).items()):
            addr = int(addr_s, 16)
            if name not in defined:
                print("  MISSING  %08x %s (not defined in the obj)" % (addr, name))
                results.append((addr, name, unit["name"], False))
                continue
            obj, sym, body, relocs = defined[name]
            problems = check_function(obj, sym, body, relocs, addr, exe, sizes, symbols)
            print("  %s  %08x %s" % ("MATCH   " if not problems else "DIFFERS ", addr, name))
            for p in problems:
                print("           - " + p)
            results.append((addr, name, unit["name"], not problems))

    if results:
        ok = sum(1 for r in results if r[3])
        print("\n%d of %d functions match" % (ok, len(results)))
    if "--record" in flags and not args:
        path = REPO / "progress" / "matched.txt"
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("# Functions that byte-match the reference exe (tools/verify/verify.py --record).\n")
            f.write("# address  unit  mangled name\n")
            for addr, name, unit, good in sorted(results):
                if good:
                    f.write("%08x  %s  %s\n" % (addr, unit, name))
    return 0 if all(r[3] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
