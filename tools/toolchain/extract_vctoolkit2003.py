"""Unpack the Microsoft Visual C++ Toolkit 2003 (v1.01) without running its installer.

The game was built with cl.exe 13.10.3077 (see docs/toolchain.md). The free
Toolkit 2003 v1.01 contains exactly that compiler, but its 2004 InstallShield
installer does not run on modern Windows. This script extracts the files directly:

  VCToolkitSetup.exe   InstallShield "Setup Launcher": 6 embedded files, each
                       XOR-scrambled with a key derived from its file name
  -> *.msi             Windows Installer database (an OLE compound file)
  -> Data1.cab         cabinet stream inside the MSI, files named by MSI keys
  -> folder tree       rebuilt from the MSI File / Component / Directory tables

Usage:
    python tools/toolchain/extract_vctoolkit2003.py VCToolkitSetup.exe C:\\tools\\vctoolkit2003

Needs Python 3.12 on Windows (msilib) and expand.exe (built into Windows).
Expected installer: 32,948,392 bytes, SHA-256
c040f6ffbc2259426dd523b7561575fe1704b58bc37092fbfe27b64aefc449f6, Authenticode-signed
by Microsoft Corporation. v1.00 (32,952,488 bytes) contains build 13.10.3052: wrong one.
"""
import hashlib
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import warnings

import pefile

EXPECTED_SHA256 = "c040f6ffbc2259426dd523b7561575fe1704b58bc37092fbfe27b64aefc449f6"
MAGIC = bytes([0x13, 0x35, 0x86, 0x07])


# --- 1. InstallShield Setup Launcher -----------------------------------------
def unpack_launcher(data):
    """Yield (name, bytes) for each file embedded after the launcher's PE image."""
    # the payload starts right after the last PE section
    pe = pefile.PE(data=data, fast_load=True)
    start = max(s.PointerToRawData + s.SizeOfRawData for s in pe.sections)
    assert data[start:start + 14] == b"InstallShield\x00", "no InstallShield payload after the PE image"
    count = struct.unpack_from("<H", data, start + 14)[0]
    table = [bytes((~(k ^ (((b << 4) | (b >> 4)) & 0xFF))) & 0xFF for b in range(256)) for k in range(256)]
    pos = start + 0x2E
    for _ in range(count):
        name = data[pos:pos + 260].split(b"\0")[0]
        _flags, _unk, size = struct.unpack_from("<III", data, pos + 260)
        body = data[pos + 0x138:pos + 0x138 + size]
        pos += 0x138 + size
        key = bytes(c ^ MAGIC[i % 4] for i, c in enumerate(name))
        # per byte: swap nibbles, xor with the key byte, invert
        out = bytearray(len(body))
        kl = len(key)
        for i, b in enumerate(body):
            out[i] = table[key[i % kl]][b]
        yield name.decode("ascii"), bytes(out)


# --- 2. OLE compound file: read the biggest stream (the cabinet) ---------------
def largest_cfb_stream(data):
    assert data[:8] == b"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1", "not an MSI/CFB file"
    ssz = 1 << struct.unpack_from("<H", data, 0x1E)[0]
    n_fat, first_dir = struct.unpack_from("<II", data, 0x2C)
    first_difat, n_difat = struct.unpack_from("<II", data, 0x44)

    def sector(i):
        return data[(i + 1) * ssz:(i + 2) * ssz]

    difat = list(struct.unpack_from("<109I", data, 0x4C))
    nxt = first_difat
    for _ in range(n_difat):
        vals = struct.unpack("<%dI" % (ssz // 4), sector(nxt))
        difat += vals[:-1]
        nxt = vals[-1]
    fat = []
    for i in difat[:n_fat]:
        fat += struct.unpack("<%dI" % (ssz // 4), sector(i))

    def read(start, size):
        parts, i = [], start
        while i < 0xFFFFFFFA:
            parts.append(sector(i))
            i = fat[i]
        return b"".join(parts)[:size]

    dirdata = read(first_dir, 1 << 30)
    best = None
    for off in range(0, len(dirdata) - 127, 128):
        e = dirdata[off:off + 128]
        if e[66] == 2:  # stream
            start, size = struct.unpack_from("<IQ", e, 116)
            if best is None or size > best[1]:
                best = (start, size)
    return read(*best)


# --- 3. MSI tables -> folder tree ------------------------------------------------
def msi_layout(msi_path):
    import msilib  # Windows-only, Python <= 3.12
    db = msilib.OpenDatabase(str(msi_path), msilib.MSIDBOPEN_READONLY)

    def rows(sql, n):
        view = db.OpenView(sql)
        view.Execute(None)
        out = []
        while True:
            try:
                r = view.Fetch()
            except Exception:
                break
            if r is None:
                break
            out.append([r.GetString(i) for i in range(1, n + 1)])
        view.Close()
        return out

    dirs = {d: (p, dd) for d, p, dd in rows("SELECT Directory, Directory_Parent, DefaultDir FROM Directory", 3)}
    comps = {c: d for c, d in rows("SELECT Component, Directory_ FROM Component", 2)}

    def long_name(s):
        return s.split(":")[0].split("|")[-1]

    def dir_path(d):
        parts = []
        while d in dirs and d != "TARGETDIR":
            parent, default = dirs[d]
            name = long_name(default)
            if name not in (".", "SourceDir"):
                parts.append(name)
            d = parent
        return list(reversed(parts))

    for key, comp, fname in rows("SELECT File, Component_, FileName FROM File", 3):
        rel = dir_path(comps[comp])
        # drop the product folder level ("Microsoft Visual C++ Toolkit 2003")
        yield key, pathlib.Path(*rel[1:], long_name(fname))


def main():
    setup, dest = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    data = setup.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        sys.exit("unexpected installer (sha256 %s); expected Toolkit 2003 v1.01" % digest)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        msi = None
        for name, body in unpack_launcher(data):
            if name.lower().endswith("toolkit 2003.msi"):
                msi = tmp / "vctoolkit.msi"
                msi.write_bytes(body)
        cab = tmp / "Data1.cab"
        cab.write_bytes(largest_cfb_stream(msi.read_bytes()))
        (tmp / "cab").mkdir()
        # full path: Git Bash / MSYS have an unrelated "expand" on PATH
        expand = pathlib.Path(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "expand.exe")
        subprocess.run([str(expand), "-F:*", str(cab), str(tmp / "cab")], check=True,
                       stdout=subprocess.DEVNULL)
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", DeprecationWarning)
            layout = list(msi_layout(msi))
        for key, rel in layout:
            target = dest / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(tmp / "cab" / key, target)
    print("extracted %d files to %s" % (len(layout), dest))


if __name__ == "__main__":
    main()
