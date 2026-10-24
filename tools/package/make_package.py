#!/usr/bin/env python3
"""Build the easy-install package: build/package/open-homm5-<version>.zip

The zip holds the hook DLL (build/hook/dinput8.dll), the settings template, the
install and uninstall scripts for Windows and Linux, and a short README.txt.
The scripts and templates live in tools/package/files/.

    python tools/package/make_package.py                    # version 0.1.0-dev
    python tools/package/make_package.py --version 0.1.0
    python tools/package/make_package.py --rebuild          # build the DLL first

If build/hook/dinput8.dll is missing it is built with src/hook/build.ps1 (Windows
with Visual Studio only). Nothing is uploaded or published; the zip stays local.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import time
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FILES = REPO / "tools" / "package" / "files"
DEFAULT_DLL = REPO / "build" / "hook" / "dinput8.dll"
OUT_DIR = REPO / "build" / "package"

# The install scripts tell our dinput8.dll from somebody else's by this text,
# which the DLL carries in its log message (src/hook/dllmain.cpp).
MARKER = b"open-homm5 hook DLL"

# Order in the zip. (name, source file in tools/package/files/ or None for the DLL)
CONTENTS = [
    ("README.txt", "README.txt"),
    ("dinput8.dll", None),
    ("open-homm5.ini", "open-homm5.ini"),
    ("install.bat", "install.bat"),
    ("install.ps1", "install.ps1"),
    ("uninstall.bat", "uninstall.bat"),
    ("uninstall.ps1", "uninstall.ps1"),
    ("install.sh", "install.sh"),
    ("uninstall.sh", "uninstall.sh"),
]

# Line endings inside the zip: Windows files get CRLF (Notepad, cmd.exe), shell
# scripts must stay LF. The repo keeps LF (and CRLF for .bat, see .gitattributes).
CRLF_SUFFIXES = {".bat", ".ps1", ".txt", ".ini"}
EXECUTABLE_SUFFIXES = {".sh"}

# The code that finds the game is written twice (install and uninstall) so that
# each script stands alone. The two copies sit between these lines and must match.
SHARED_PAIRS = [("install.ps1", "uninstall.ps1"), ("install.sh", "uninstall.sh")]
SHARED_START = "# ---- shared with uninstall."
SHARED_END = "# ---- end of the shared block ----"


def fail(message):
    print("error: " + message, file=sys.stderr)
    sys.exit(1)


def read_template(name):
    """Template text with LF line endings; must be plain ASCII (PowerShell 5.1
    reads a script without a byte-order mark as ANSI, so accents would break)."""
    raw = (FILES / name).read_bytes()
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as e:
        fail(f"{name} must be plain ASCII ({e})")
    return text.replace("\r\n", "\n")


def shared_block(name, text):
    lines = text.split("\n")
    start = [i for i, l in enumerate(lines) if l.startswith(SHARED_START)]
    end = [i for i, l in enumerate(lines) if l == SHARED_END]
    if len(start) != 1 or len(end) != 1 or start[0] >= end[0]:
        fail(f"{name}: need exactly one shared block ('{SHARED_START}...' to '{SHARED_END}')")
    return "\n".join(lines[start[0]:end[0] + 1])


def check_shared_blocks():
    for a, b in SHARED_PAIRS:
        if shared_block(a, read_template(a)) != shared_block(b, read_template(b)):
            fail(f"the shared 'find the game' blocks of {a} and {b} differ; keep them identical")


def build_dll():
    if os.name != "nt":
        fail("the DLL must be built on Windows: powershell -File src\\hook\\build.ps1")
    print("building the hook DLL (src/hook/build.ps1) ...")
    r = subprocess.run(["powershell", "-NoProfile", "-File", str(REPO / "src" / "hook" / "build.ps1")])
    if r.returncode != 0:
        fail("the DLL build failed")


def file_bytes(name, source, dll_path, version):
    if source is None:
        data = dll_path.read_bytes()
        if MARKER not in data:
            fail(f"{dll_path} does not contain the text '{MARKER.decode()}', "
                 "which the install scripts use to recognise our DLL")
        return data
    text = read_template(source)
    if name == "README.txt":
        text = text.replace("@VERSION@", version)
    if "@VERSION@" in text:
        fail(f"{name}: unreplaced @VERSION@")
    if Path(name).suffix in CRLF_SUFFIXES:
        text = text.replace("\n", "\r\n")
    return text.encode("ascii")


def zip_time():
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", time.time()))
    t = time.localtime(max(epoch, 315532800))  # a zip cannot hold dates before 1980
    return (t.tm_year, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec - t.tm_sec % 2)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default="0.1.0-dev", help="version in the file name and README.txt (default 0.1.0-dev)")
    ap.add_argument("--dll", type=Path, default=DEFAULT_DLL, help="the DLL to pack (default build/hook/dinput8.dll)")
    ap.add_argument("--rebuild", action="store_true", help="build the DLL first, even if there is one")
    ap.add_argument("--out-dir", type=Path, default=OUT_DIR, help="where the zip goes (default build/package)")
    args = ap.parse_args()

    if not re.fullmatch(r"[0-9A-Za-z][0-9A-Za-z._+-]*", args.version):
        fail("the version may only hold letters, digits and . _ + -")
    check_shared_blocks()

    if args.rebuild or (args.dll == DEFAULT_DLL and not args.dll.exists()):
        build_dll()
    if not args.dll.exists():
        fail(f"{args.dll} does not exist")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = args.out_dir / f"open-homm5-{args.version}.zip"
    tmp_path = zip_path.with_suffix(".zip.tmp")
    stamp = zip_time()
    with zipfile.ZipFile(tmp_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, source in CONTENTS:
            data = file_bytes(name, source, args.dll, args.version)
            info = zipfile.ZipInfo(f"open-homm5-{args.version}/{name}", stamp)  # one top folder
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3  # Unix, so that unzip on Linux honours the mode below
            mode = 0o755 if Path(name).suffix in EXECUTABLE_SUFFIXES else 0o644
            info.external_attr = (0o100000 | mode) << 16
            z.writestr(info, data)
    os.replace(tmp_path, zip_path)

    print(f"wrote {zip_path.relative_to(REPO) if zip_path.is_relative_to(REPO) else zip_path}")
    with zipfile.ZipFile(zip_path) as z:
        for i in z.infolist():
            print(f"  {i.file_size:>9}  {i.filename}")
    print(f"  sha256 {hashlib.sha256(zip_path.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
