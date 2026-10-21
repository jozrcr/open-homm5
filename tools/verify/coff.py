"""Minimal reader for 32-bit x86 COFF object files (.obj) produced by MSVC.

Only what the verifier needs: sections with their raw bytes and relocations,
and the symbol table with long names resolved.
"""
import struct
from dataclasses import dataclass, field

IMAGE_FILE_MACHINE_I386 = 0x14C

# i386 relocation types
REL_DIR32 = 0x06
REL_DIR32NB = 0x07
REL_SECTION = 0x0A
REL_SECREL = 0x0B
REL_REL32 = 0x14

IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_LNK_COMDAT = 0x00001000

SYM_CLASS_EXTERNAL = 2
SYM_CLASS_STATIC = 3
SYM_TYPE_FUNCTION = 0x20


@dataclass
class Relocation:
    offset: int        # offset inside the section
    symbol: int        # index into the symbol table
    type: int


@dataclass
class Section:
    index: int         # 1-based, as used by symbols
    name: str
    characteristics: int
    data: bytes
    relocations: list = field(default_factory=list)

    @property
    def is_code(self):
        return bool(self.characteristics & IMAGE_SCN_CNT_CODE)


@dataclass
class Symbol:
    index: int
    name: str
    value: int
    section: int       # 1-based section number, 0 = undefined (external), <0 special
    type: int
    storage_class: int

    @property
    def is_function(self):
        return (self.type & 0xF0) == SYM_TYPE_FUNCTION


class CoffObject:
    def __init__(self, data: bytes):
        machine, nsec, _ts, symptr, nsym, opt_size, _chars = struct.unpack_from("<HHIIIHH", data, 0)
        if machine != IMAGE_FILE_MACHINE_I386:
            raise ValueError("not an i386 COFF object (machine %#x)" % machine)
        strtab_off = symptr + 18 * nsym
        self._strtab = data[strtab_off:]

        self.sections = []
        for i in range(nsec):
            off = 20 + opt_size + 40 * i
            raw_name = data[off:off + 8]
            (_vsize, _vaddr, size, ptr, relptr, _lnptr, nrel, _nln,
             chars) = struct.unpack_from("<IIIIIIHHI", data, off + 8)
            sec = Section(i + 1, self._name(raw_name), chars, data[ptr:ptr + size] if ptr else b"\0" * size)
            for r in range(nrel):
                roff, rsym, rtype = struct.unpack_from("<IIH", data, relptr + 10 * r)
                sec.relocations.append(Relocation(roff, rsym, rtype))
            self.sections.append(sec)

        self.symbols = {}
        i = 0
        while i < nsym:
            off = symptr + 18 * i
            raw_name = data[off:off + 8]
            value, secnum, stype, sclass, naux = struct.unpack_from("<IhHBB", data, off + 8)
            self.symbols[i] = Symbol(i, self._name(raw_name), value, secnum, stype, sclass)
            i += 1 + naux

    def _name(self, raw):
        if raw[:4] == b"\0\0\0\0":
            (str_off,) = struct.unpack_from("<I", raw, 4)
            end = self._strtab.index(b"\0", str_off)
            return self._strtab[str_off:end].decode("latin-1")
        return raw.rstrip(b"\0").decode("latin-1")

    def section(self, number):
        return self.sections[number - 1]

    def functions(self):
        """Yield (symbol, bytes, relocations-relative-to-function-start) for each defined function."""
        by_section = {}
        for s in self.symbols.values():
            if s.is_function and s.section > 0 and s.storage_class in (SYM_CLASS_EXTERNAL, SYM_CLASS_STATIC):
                by_section.setdefault(s.section, []).append(s)
        for secnum, syms in by_section.items():
            sec = self.section(secnum)
            syms.sort(key=lambda s: s.value)
            for k, s in enumerate(syms):
                end = syms[k + 1].value if k + 1 < len(syms) else len(sec.data)
                relocs = [Relocation(r.offset - s.value, r.symbol, r.type)
                          for r in sec.relocations if s.value <= r.offset < end]
                yield s, sec.data[s.value:end], relocs
