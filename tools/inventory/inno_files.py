"""What is inside an MMH5.5 installer (Inno Setup 6.4, files stored without
compression), without running it.

Usage:
    python -I tools/inventory/inno_files.py <setup.exe> <out.csv> [<extract dir> <pattern> ...]

Why: the installer wants administrator rights (a Windows UAC prompt) and writes
an "uninstall" entry and shortcuts. To know which files a new MMH5.5 version
changes, it is enough to read the installer's own file table: it holds every
file's destination, size, SHA-256, date and version.

What it does:
  - prints the setup header's main facts (app id and version, the default
    folder, whether it needs admin rights, how many registry / run / delete
    entries it has);
  - writes <out.csv>: one row per installed file (dest, size, sha256,
    filetime_utc, version, flags);
  - with <extract dir> and patterns (e.g. "bin\\55u.dll" "data\\*.pak"), copies
    those files out of the installer and checks each against the SHA-256 in the
    installer's table ("OK" / "BAD").
Read only: it only reads the installer. Run it with python -I and keep the
extract folder separate from the game (downloaded files are untrusted data).

Format notes (Inno Setup 6.4.3, checked on MMH55_RC19k.exe):
  - the loader table "rDlPtS..." gives the offsets of setup-0 (header) and of
    the file data;
  - setup-0 is a 64-byte id, then two blocks (CRC-checked 4 KB chunks, LZMA):
    the setup header and the file location table (85 bytes per file);
  - each file entry in the header is 10 strings (source, dest, ...), the min /
    only-below versions (20 bytes), then the index of its location entry;
  - with Compression=none, each data chunk is "zlb\\x1a" followed by the raw
    file bytes.
"""
import datetime
import fnmatch
import hashlib
import lzma
import os
import re
import struct
import sys
import zlib

LOADER_ID = b"rDlPtS\xcd\xe6\xd7\x7b\x0b\x2a"
LOCATION_SIZE = 85
# MinVersion 6.1 (Inno 6's default), as the file entries of MMH5.5's installer store it.
DEFAULT_MIN_VERSION = bytes.fromhex("00000000000001060001")
PRIVILEGES = {0: "none", 1: "poweruser", 2: "admin (UAC prompt)", 3: "lowest"}


def read_block(d, p):
    """One setup-0 block: header CRC, stored size, compressed flag, CRC'd 4 KB chunks."""
    hcrc, stored, compressed = struct.unpack_from("<IIB", d, p)
    if zlib.crc32(d[p + 4:p + 9]) != hcrc:
        sys.exit("bad block header CRC at 0x%x" % p)
    p += 9
    data = bytearray()
    left = stored
    while left > 0:
        crc = struct.unpack_from("<I", d, p)[0]
        n = min(4096, left - 4)
        chunk = d[p + 4:p + 4 + n]
        if zlib.crc32(chunk) != crc:
            sys.exit("bad chunk CRC at 0x%x" % p)
        data += chunk
        p += 4 + n
        left -= 4 + n
    if compressed:
        props, dict_size = data[0], struct.unpack_from("<I", data, 1)[0]
        lc, rest = props % 9, props // 9
        dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=[
            {"id": lzma.FILTER_LZMA1, "lc": lc, "lp": rest % 5, "pb": rest // 5, "dict_size": dict_size}])
        data = dec.decompress(bytes(data[5:]))
    return bytes(data), p


def read_str(h, p, limit=4096):
    n = struct.unpack_from("<I", h, p)[0]
    if n > limit:
        return None, p
    return h[p + 4:p + 4 + n], p + 4 + n


def header_facts(h):
    """The 33 Unicode strings, the 3 ANSI texts and the compiled code, then the counts."""
    strings, p = [], 0
    for _ in range(33):
        s, p = read_str(h, p, 1 << 20)
        strings.append(s.decode("utf-16le", "replace"))
    for _ in range(4):  # license, info before, info after, compiled [Code]
        _, p = read_str(h, p, 1 << 24)
    counts = struct.unpack_from("<16i", h, p)
    names = ("languages custom_messages permissions types components tasks dirs files file_locations "
             "icons ini registry install_delete uninstall_delete run uninstall_run").split()
    p += 64 + 20 + 10  # counts, min/only-below versions, wizard style and size
    p += 4 + 16 + 4 + 24 + 8 + 4  # password test, KDF salt, KDF iterations, nonce, extra space, slices
    privileges = h[p + 2]
    return strings, dict(zip(names, counts)), privileges


def file_table(d):
    i = d.find(LOADER_ID)
    if i < 0:
        sys.exit("not an Inno Setup installer (no loader table)")
    _, total, _, _, _, off0, off1, _ = struct.unpack_from("<8I", d, i + 12)
    hdr, p = read_block(d, off0 + 64)
    locs_raw, _ = read_block(d, p)
    if len(locs_raw) % LOCATION_SIZE:
        sys.exit("unexpected location entry size (not Inno Setup 6.4?)")
    locs = []
    for k in range(len(locs_raw) // LOCATION_SIZE):
        e = locs_raw[k * LOCATION_SIZE:(k + 1) * LOCATION_SIZE]
        start = struct.unpack_from("<I", e, 8)[0]
        sub, size = struct.unpack_from("<qq", e, 12)
        ft = struct.unpack_from("<Q", e, 68)[0]
        vms, vls = struct.unpack_from("<II", e, 76)
        locs.append(dict(start=start, sub=sub, size=size, sha=e[36:68].hex(), ft=ft, vms=vms, vls=vls, flags=e[84]))
    files = []
    for m in re.finditer(rb"\x00\x00\x00\x00(....)\{\x00a\x00p\x00p\x00\}\x00\\\x00", hdr, re.S):
        p = m.start() + 4
        dest, q = read_str(hdr, p, 1024)
        if dest is None or len(dest) % 2:
            continue
        ok = True
        for _ in range(8):
            s, q = read_str(hdr, q)
            if s is None:
                ok = False
                break
        if not ok or hdr[q:q + 10] != DEFAULT_MIN_VERSION:
            continue
        loc = struct.unpack_from("<i", hdr, q + 20)[0]
        if 0 <= loc < len(locs):
            files.append((dest.decode("utf-16le"), loc))
    return hdr, locs, sorted(files), off1


def filetime(ft):
    if not ft:
        return ""
    return (datetime.datetime(1601, 1, 1) + datetime.timedelta(microseconds=ft // 10)).strftime("%Y-%m-%d %H:%M:%S")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    setup, out_csv = sys.argv[1], sys.argv[2]
    d = open(setup, "rb").read()
    hdr, locs, files, data_start = file_table(d)
    strings, counts, privileges = header_facts(hdr)
    print("%s %s, AppId %s, default folder %s" % (strings[0], strings[9], strings[2], strings[10]))
    print("privileges required: %s" % PRIVILEGES.get(privileges, privileges))
    print("entries: " + ", ".join("%s %d" % kv for kv in counts.items() if kv[1]))
    print("file entries read: %d (location entries %d)" % (len(files), len(locs)))
    with open(out_csv, "w", encoding="utf-8") as f:
        f.write("dest,size,sha256,filetime_utc,version,flags\n")
        for dest, loc in files:
            L = locs[loc]
            v = ("%d.%d.%d.%d" % (L["vms"] >> 16, L["vms"] & 0xffff, L["vls"] >> 16, L["vls"] & 0xffff)
                 if L["vms"] or L["vls"] else "")
            f.write("%s,%d,%s,%s,%s,0x%02x\n" % (dest, L["size"], L["sha"], filetime(L["ft"]), v, L["flags"]))
    if len(sys.argv) > 4:
        out_dir, patterns = sys.argv[3], sys.argv[4:]
        for dest, loc in files:
            rel = dest[len("{app}\\"):]
            if not any(fnmatch.fnmatch(rel.lower(), pat.lower()) for pat in patterns):
                continue
            L = locs[loc]
            q = data_start + L["start"]
            if d[q:q + 4] != b"zlb\x1a":
                sys.exit("unexpected chunk id for %s (compressed installer?)" % rel)
            data = d[q + 4 + L["sub"]:q + 4 + L["sub"] + L["size"]]
            ok = hashlib.sha256(data).hexdigest() == L["sha"]
            if ok:
                path = os.path.join(out_dir, rel)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "wb") as f:
                    f.write(data)
            print("%s %s (%d bytes)" % ("OK " if ok else "BAD", rel, L["size"]))


if __name__ == "__main__":
    main()
