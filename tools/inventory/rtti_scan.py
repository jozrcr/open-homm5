"""Recover MSVC RTTI classes and vftables straight from the exe bytes.

Usage:
    python tools/inventory/rtti_scan.py ref/H5_Game_31_vanilla.exe analysis/export/rtti_scan.tsv

We do this ourselves instead of relying on Ghidra's RTTI analyzer, which
refuses to run on our reference exe (the Steam DRM wrapper erased the Rich
header Ghidra uses to recognize MSVC binaries; see tools/inventory/README.md).

How MSVC RTTI is laid out (32-bit):
  TypeDescriptor        { void* type_info_vftable; void* spare; char name[] }   name = ".?AVCFoo@NBar@@"
  CompleteObjectLocator { 0, offset_in_object, ctor_disp_offset, TypeDescriptor*, ClassHierarchyDescriptor* }
  vftable               the dword just before it points to the CompleteObjectLocator

Class names are demangled with Ghidra's standalone Microsoft demangler
(ghidra/DemangleNames.java), so Java and Ghidra must be installed. Set
GHIDRA_HOME and JAVA_HOME if they are not at the default paths below.

Output TSV columns: kind (td | vftable), address, class, object_offset, mangled
"""
import os
import pathlib
import re
import struct
import subprocess
import sys
import tempfile

import pefile

HERE = pathlib.Path(__file__).resolve().parent
GHIDRA_HOME = pathlib.Path(os.environ.get("GHIDRA_HOME", r"C:\tools\ghidra_12.1.4_PUBLIC"))
JAVA_HOME = pathlib.Path(os.environ.get("JAVA_HOME", r"C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot"))


def demangle(names):
    dmang = GHIDRA_HOME / "Ghidra" / "Features" / "MicrosoftDmang" / "lib" / "MicrosoftDmang.jar"
    lang3 = next(GHIDRA_HOME.rglob("commons-lang3*.jar"))
    with tempfile.TemporaryDirectory() as tmp:
        src, dst = pathlib.Path(tmp, "in.txt"), pathlib.Path(tmp, "out.tsv")
        src.write_text("\n".join(names) + "\n", encoding="utf-8")
        subprocess.run([str(JAVA_HOME / "bin" / "java"), "-cp", os.pathsep.join([str(dmang), str(lang3)]),
                        str(HERE / "ghidra" / "DemangleNames.java"), str(src), str(dst)], check=True)
        out = {}
        for line in dst.read_text(encoding="utf-8").splitlines():
            mangled, _, demangled = line.partition("\t")
            out[mangled] = re.sub(r"^(class|struct|union) ", "", demangled) or mangled
        return out


def main():
    exe, out_path = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    data = exe.read_bytes()
    pe = pefile.PE(data=data, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase

    def off_to_va(off):
        for s in pe.sections:
            if s.PointerToRawData <= off < s.PointerToRawData + s.SizeOfRawData:
                return base + s.VirtualAddress + off - s.PointerToRawData
        return None

    # 1. TypeDescriptors: the name string sits 8 bytes into the structure.
    tds = {}
    for m in re.finditer(rb"\.\?A[VU][\x21-\x7e]+?@@\x00", data):
        va = off_to_va(m.start() - 8)
        if va:
            tds[va] = m.group()[:-1].decode("ascii")

    # 2. CompleteObjectLocators and 3. vftables, both in .rdata.
    rdata = next(s for s in pe.sections if s.Name.startswith(b".rdata"))
    raw, rbase = rdata.get_data(), base + rdata.VirtualAddress
    cols = {}
    for i in range(0, len(raw) - 20, 4):
        sig, offset, _cd, ptd, pchd = struct.unpack_from("<5I", raw, i)
        if sig == 0 and ptd in tds and pchd > base:
            cols[rbase + i] = (ptd, offset)
    vftables = []
    for i in range(0, len(raw) - 4, 4):
        v = struct.unpack_from("<I", raw, i)[0]
        if v in cols:
            vftables.append((rbase + i + 4, cols[v]))

    names = demangle(sorted(set(tds.values())))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("kind\taddress\tclass\tobject_offset\tmangled\n")
        for va, mangled in sorted(tds.items()):
            f.write("td\t%08x\t%s\t\t%s\n" % (va, names[mangled], mangled))
        for va, (ptd, offset) in sorted(vftables):
            f.write("vftable\t%08x\t%s\t%d\t%s\n" % (va, names[tds[ptd]], offset, tds[ptd]))
    print("%d type descriptors, %d object locators, %d vftables" % (len(tds), len(cols), len(vftables)))


if __name__ == "__main__":
    main()
