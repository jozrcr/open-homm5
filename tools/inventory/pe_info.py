"""Collect identifying facts about every PE file (exe/dll) in a folder.

Usage:
    python tools/inventory/pe_info.py ref/            # markdown table to stdout
    python tools/inventory/pe_info.py ref/ --json     # full details as JSON

Reads files only; never writes next to them.
"""
import datetime
import hashlib
import json
import pathlib
import sys

import pefile

MACHINES = {0x14C: "x86", 0x8664: "x64", 0x1C0: "ARM", 0xAA64: "ARM64"}


def version_strings(pe):
    out = {}
    for fi in getattr(pe, "FileInfo", None) or []:
        for entry in fi:
            if entry.Key == b"StringFileInfo":
                for table in entry.StringTable:
                    for k, v in table.entries.items():
                        out[k.decode(errors="replace")] = v.decode(errors="replace")
    if hasattr(pe, "VS_FIXEDFILEINFO") and pe.VS_FIXEDFILEINFO:
        f = pe.VS_FIXEDFILEINFO[0]
        out["_FixedFileVersion"] = "%d.%d.%d.%d" % (
            f.FileVersionMS >> 16, f.FileVersionMS & 0xFFFF,
            f.FileVersionLS >> 16, f.FileVersionLS & 0xFFFF)
    return out


def describe(path):
    data = path.read_bytes()
    info = {
        "name": path.name,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }
    try:
        pe = pefile.PE(data=data, fast_load=False)
    except pefile.PEFormatError as e:
        info["error"] = str(e)
        return info
    fh, oh = pe.FILE_HEADER, pe.OPTIONAL_HEADER
    info["machine"] = MACHINES.get(fh.Machine, hex(fh.Machine))
    info["timestamp_utc"] = datetime.datetime.fromtimestamp(
        fh.TimeDateStamp, datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    info["is_dll"] = bool(fh.Characteristics & 0x2000)
    info["large_address_aware"] = bool(fh.Characteristics & 0x20)
    info["linker"] = "%d.%02d" % (oh.MajorLinkerVersion, oh.MinorLinkerVersion)
    info["image_base"] = hex(oh.ImageBase)
    info["entry_rva"] = hex(oh.AddressOfEntryPoint)
    info["checksum_header"] = hex(oh.CheckSum)
    dirs = oh.DATA_DIRECTORY
    info["signed"] = dirs[4].Size > 0          # IMAGE_DIRECTORY_ENTRY_SECURITY
    info["dotnet"] = dirs[14].Size > 0         # IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR
    info["has_rich"] = pe.RICH_HEADER is not None
    info["sections"] = [
        {"name": s.Name.rstrip(b"\0").decode(errors="replace"),
         "vaddr": hex(s.VirtualAddress), "vsize": s.Misc_VirtualSize,
         "rawsize": s.SizeOfRawData, "entropy": round(s.get_entropy(), 2)}
        for s in pe.sections]
    last = max(s.PointerToRawData + s.SizeOfRawData for s in pe.sections)
    info["overlay_bytes"] = max(0, len(data) - last - (dirs[4].Size if dirs[4].Size else 0))
    info["imports"] = sorted({e.dll.decode(errors="replace").lower()
                              for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])})
    info["version"] = version_strings(pe)
    return info


def main():
    folder = pathlib.Path(sys.argv[1])
    files = sorted(p for p in folder.iterdir()
                   if p.suffix.lower() in (".exe", ".dll"))
    rows = [describe(p) for p in files]
    if "--json" in sys.argv:
        json.dump(rows, sys.stdout, indent=2)
        return
    print("| File | Size | SHA-256 | Machine | File version | PE timestamp (UTC) |")
    print("|---|---:|---|---|---|---|")
    for r in rows:
        v = r.get("version", {})
        ver = v.get("FileVersion") or v.get("_FixedFileVersion") or "-"
        print("| %s | %s | `%s` | %s | %s | %s |" % (
            r["name"], f'{r["size"]:,}', r["sha256"], r.get("machine", "?"),
            ver.strip(), r.get("timestamp_utc", "?")))


if __name__ == "__main__":
    main()
