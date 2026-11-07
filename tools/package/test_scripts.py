#!/usr/bin/env python3
"""Tests the install and uninstall scripts of the package against FAKE folders.

    python tools/package/test_scripts.py [--bash PATH] [--work-dir DIR] [--keep] [--only windows|linux]

It builds the zip with make_package.py (version "0.0.0-test"), unzips it, makes a fake
Steam folder and fake game folders (empty files named like the game's exes), and
runs the scripts from the unzipped package. It never touches a real game folder.

- Windows scripts (install.bat / install.ps1, uninstall.bat / uninstall.ps1):
  run only on Windows.
- Linux scripts (install.sh / uninstall.sh): need a bash. On Windows pass Git
  Bash with --bash "C:\\Program Files\\Git\\bin\\bash.exe" (the bash that comes
  with WSL does not work with Windows paths). The scripts find Steam below
  $HOME, so the tests point HOME at a fake home folder.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import uuid
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
GAME = "Heroes of Might and Magic 5 Tribes of the East"
OURS_MARK = b"H5 Together hook DLL"

failures = []
checks = 0
# the installers never offer to fetch the original game's exe from GitHub during the tests
os.environ["H5_TOGETHER_NO_DOWNLOAD"] = "1"


sys.stdout.reconfigure(errors="replace")


def check(ok, label, detail=""):
    global checks
    checks += 1
    print(("  ok    " if ok else "  FAIL  ") + label)
    if not ok:
        failures.append(label)
        if detail:
            print("        " + detail.replace("\n", "\n        "))


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def snapshot(folder):
    return {p.name: sha(p) for p in Path(folder).iterdir() if p.is_file()}


def tree(folder):
    """Everything below folder, files and folders: {relative path: sha256, or None for a folder}."""
    folder = Path(folder)
    return {p.relative_to(folder).as_posix(): (sha(p) if p.is_file() else None) for p in sorted(folder.rglob("*"))}


def make_game(library, exes=("H5_Game.exe", "MMH55_Utility_64.exe"), name=GAME):
    """A fake game folder with empty-ish exes. Returns its bin folder."""
    b = Path(library) / "steamapps" / "common" / name / "bin"
    b.mkdir(parents=True, exist_ok=True)
    for e in exes:
        (b / e).write_bytes(b"fake " + e.encode())
    (b / "data.txt").write_bytes(b"pretend game data")
    return b


def make_steam(steam, libraries, escape_backslashes=True):
    """A fake Steam folder whose libraryfolders.vdf lists the given library folders."""
    steam = Path(steam)
    (steam / "steamapps").mkdir(parents=True, exist_ok=True)
    lines = ['"libraryfolders"', "{"]
    for i, lib in enumerate(libraries):
        text = str(lib)
        if escape_backslashes:
            text = text.replace("\\", "\\\\")
        lines += [f'\t"{i}"', "\t{", f'\t\t"path"\t\t"{text}"', '\t\t"label"\t\t""',
                  '\t\t"apps"', "\t\t{", '\t\t\t"12345"\t\t"678"', "\t\t}", "\t}"]
    lines.append("}")
    # Steam writes UTF-8 without a byte-order mark
    (steam / "steamapps" / "libraryfolders.vdf").write_bytes(("\n".join(lines) + "\n").encode("utf-8"))


def bat(path, *args):
    """A command line that runs a .bat file as a double-click would, whatever is in its path."""
    return 'cmd /s /c "' + subprocess.list2cmdline([str(path), *args]) + '"'


def run(cmd, stdin="", env=None, cwd=None):
    p = subprocess.run(cmd, input=stdin.encode("utf-8"), capture_output=True, env=env, cwd=cwd, timeout=180)
    out = (p.stdout + p.stderr).decode("utf-8", errors="replace")
    return p.returncode, out


# ---------------------------------------------------------------- templates

TEMPLATES_SRC = REPO / "src" / "mods" / "rmg" / "templates"


def template_dir(pkg):
    return Path(pkg) / "h5-together" / "templates"


def same_bytes(folder, src_folder, names):
    """True if every name exists in both folders with the same bytes."""
    return all((Path(folder) / n).is_file() and (Path(folder) / n).read_bytes() == (Path(src_folder) / n).read_bytes()
               for n in names)


def package_checks(pkg, zip_path):
    """The ready templates are in the package, byte for byte (UTF-8 with BOM and CRLF)."""
    print("\nTemplates in the package")
    wanted = sorted(p.name for p in TEMPLATES_SRC.glob("*.xdb"))
    packed = sorted(p.name for p in template_dir(pkg).iterdir()) if template_dir(pkg).is_dir() else []
    check(len(wanted) >= 16, f"src/mods/rmg/templates holds {len(wanted)} templates (16 or more)")
    check(packed == wanted, "package: h5-together/templates holds every src/mods/rmg/templates/*.xdb and nothing else", f"{packed}\n{wanted}")
    check(same_bytes(template_dir(pkg), TEMPLATES_SRC, wanted), "package: the templates are byte for byte the source files")
    data = [(template_dir(pkg) / n).read_bytes() for n in packed]
    check(all(d.startswith(b"\xef\xbb\xbf") and b"\r\n" in d and b"\n" not in d.replace(b"\r\n", b"") for d in data),
          "package: the templates keep their UTF-8 BOM and CRLF line endings")
    with zipfile.ZipFile(zip_path) as z:
        entries = [i for i in z.infolist() if "/h5-together/templates/" in i.filename]
        check(len(entries) == len(wanted) and all(i.filename.count("/") == 3 for i in entries),
              "package: the templates sit in the zip under <top folder>/h5-together/templates/")
        check(all(((i.external_attr >> 16) & 0o777) == 0o644 for i in entries), "package: the templates are plain data files (mode 644)")


def licence_checks(pkg, zip_path):
    """Our licence and Dear ImGui's (the only third-party code in the DLL) travel with the DLL."""
    print("\nLicence notices in the package")
    imgui_dir = REPO / "src" / "hook" / "third_party" / "imgui"
    lf = lambda raw: raw.replace(b"\r\n", b"\n")
    check(lf((pkg / "LICENSE.txt").read_bytes()) == lf((REPO / "LICENSE").read_bytes()),
          "licence: LICENSE.txt is the LICENSE of the repo, word for word")
    raw = (pkg / "THIRD-PARTY-NOTICES.txt").read_bytes()
    text = lf(raw).decode("ascii", errors="replace")
    imgui_licence = lf((imgui_dir / "LICENSE.txt").read_bytes()).decode("ascii").strip("\n")
    check(imgui_licence in text, "licence: THIRD-PARTY-NOTICES.txt carries Dear ImGui's LICENSE.txt word for word")
    version = re.search(r'#define IMGUI_VERSION\s+"([^"]+)"', (imgui_dir / "imgui.h").read_text(encoding="utf-8")).group(1)
    check(f"Dear ImGui {version}" in text, f"licence: the notices name the Dear ImGui version we ship ({version})")
    stb = sorted(p.name for p in imgui_dir.glob("imstb_*.h"))
    check(stb == ["imstb_rectpack.h", "imstb_textedit.h", "imstb_truetype.h"],
          "licence: Dear ImGui still bundles exactly the 3 stb files the notices list", str(stb))
    check("stb_truetype" in text and "stb_rect_pack" in text and "stb_textedit" in text and "Sean Barrett" in text,
          "licence: the notices cover the stb files, by Sean Barrett")
    check("Tristan Grimmer" in text and "ProggyClean" in text, "licence: the notices cover the ProggyClean font built into Dear ImGui")
    check("steam_api.dll" in text and "Valve Corporation" in text and "Steamworks SDK Access Agreement" in text
          and "not covered by our MIT licence" in text,
          "licence: the notices have a section for Valve's steam_api.dll (Steamworks SDK Access Agreement, not under our MIT licence)")
    check(b"@" not in raw and all(b < 0x80 for b in raw) and raw.count(b"\r\n") == raw.count(b"\n"),
          "licence: the notices are plain ASCII with CRLF line endings and no unreplaced placeholder")
    readme = (pkg / "README.txt").read_text(encoding="ascii")
    check("LICENSE.txt" in readme and "THIRD-PARTY-NOTICES.txt" in readme, "licence: README.txt tells where the licences are")
    check("steam_api.dll" in readme and "Valve" in readme, "licence: README.txt says whose steam_api.dll is (when a package has it)")
    with zipfile.ZipFile(zip_path) as z:
        names = {i.filename.split("/", 1)[1] for i in z.infolist() if "/" in i.filename}
        check({"LICENSE.txt", "THIRD-PARTY-NOTICES.txt"} <= names, "licence: both are in the zip, next to README.txt")


# ---------------------------------------------------------------- language files

TEXTS_SRC = REPO / "src" / "mods" / "texts"


def texts_dir(pkg):
    return Path(pkg) / "h5-together" / "texts"


def texts_package_checks(pkg, zip_path):
    """Our language files are in the package: UTF-8 with a BOM and CRLF (by design: the
    other files we ship must stay plain ASCII)."""
    print("\nLanguage files in the package")
    wanted = sorted(p.name for p in TEXTS_SRC.glob("*.txt"))
    packed = sorted(p.name for p in texts_dir(pkg).iterdir()) if texts_dir(pkg).is_dir() else []
    check({"en.txt", "fr.txt"} <= set(wanted), "src/mods/texts holds en.txt and fr.txt", str(wanted))
    check(packed == wanted, "package: h5-together/texts holds every src/mods/texts/*.txt and nothing else (no TRANSLATING.md)",
          f"{packed}\n{wanted}")
    ok = True
    for n in packed:
        data = (texts_dir(pkg) / n).read_bytes()
        source = (TEXTS_SRC / n).read_bytes().decode("utf-8-sig").replace("\r\n", "\n")
        ok = ok and data.startswith(b"\xef\xbb\xbf") and b"\n" not in data.replace(b"\r\n", b"") and \
            data[3:].decode("utf-8").replace("\r\n", "\n") == source
    check(ok, "package: each language file is its source, as UTF-8 with a byte-order mark and CRLF line ends")
    fr = (texts_dir(pkg) / "fr.txt").read_bytes().decode("utf-8-sig") if "fr.txt" in packed else ""
    check("R\u00e8gles" in fr and "\u00e9" in fr, "package: fr.txt keeps its accents (UTF-8, not ASCII)")
    with zipfile.ZipFile(zip_path) as z:
        entries = [i for i in z.infolist() if "/h5-together/texts/" in i.filename]
        check(len(entries) == len(wanted) and all(i.filename.count("/") == 3 for i in entries),
              "package: the language files sit in the zip under <top folder>/h5-together/texts/")
        check(all(((i.external_attr >> 16) & 0o777) == 0o644 for i in entries), "package: the language files are plain data files (mode 644)")
        others = [i.filename for i in z.infolist()
                  if not i.filename.endswith("/") and "/h5-together/" not in i.filename and not i.filename.endswith("/dinput8.dll")]
        not_ascii = [n for n in others if any(b >= 0x80 for b in z.read(n))]
        check(others and not not_ascii, "package: every other file we ship (scripts, settings, read-me, notices) is plain ASCII",
              str(not_ascii))


def texts_tests(who, pkg, game_bin, install, uninstall):
    """The language files in a fake game folder: install, a translation of the player's own,
    reinstall, uninstall."""
    print(f"\nLanguage files ({who})")
    src = texts_dir(pkg)
    ours = sorted(p.name for p in src.iterdir())
    n = len(ours)
    tdir = game_bin / "h5-together" / "texts"
    before = tree(game_bin)

    def names():
        return sorted(p.name for p in tdir.iterdir()) if tdir.is_dir() else None

    code, out = install()
    check(code == 0, "texts: fresh install: exit 0", out)
    check(names() == ours and same_bytes(tdir, src, ours), "texts: fresh install: exactly our language files, byte for byte, in bin/h5-together/texts")
    check(f"installed {n} language files" in out and "en, fr" in out, "texts: fresh install: the summary says how many, and which", out)

    mine = {"de.txt": "[players]\nday = Tag {n}\n".encode("utf-8"), "notes.md": b"my notes"}
    for name, data in mine.items():
        (tdir / name).write_bytes(data)
    (tdir / "fr.txt").write_bytes(b"[players]\nday = changed by hand {n}\n")
    code, out = install()
    check(code == 0 and same_bytes(tdir, src, ours), "texts: reinstall overwrites ours (fr.txt changed by hand)", out)
    check(all((tdir / k).read_bytes() == v for k, v in mine.items()) and names() == sorted(ours + list(mine)),
          "texts: reinstall keeps the player's own translation and files", str(names()))
    check(f"{n} of them replaced" in out and "left the 2 other" in out, "texts: reinstall: the summary counts replaced and left alone", out)

    code, out = uninstall()
    check(code == 0 and names() == sorted(mine), "texts: uninstall removes only our language files; the player's own stay", str(names()))
    check(f"removed {n} language files" in out and "kept" in out, "texts: uninstall says what it removed and kept", out)
    for name in mine:
        (tdir / name).unlink()
    install()
    code, out = uninstall()
    check(code == 0 and not (game_bin / "h5-together").exists() and tree(game_bin) == before,
          "texts: install then uninstall leaves the game folder exactly as it was (empty folders removed)", out)


def template_tests(who, pkg, game_bin, install, uninstall):
    """The templates in a fake game folder: install, a player's own files, reinstall, uninstall.

    install() and uninstall() run the scripts for game_bin and return (exit code, output)."""
    print(f"\nRandom map templates ({who})")
    src = template_dir(pkg)
    ours = sorted(p.name for p in src.iterdir())
    n = len(ours)
    tdir = game_bin / "h5-together" / "templates"
    before = tree(game_bin)

    def names():
        return sorted(p.name for p in tdir.iterdir()) if tdir.is_dir() else None

    code, out = install()
    check(code == 0, "templates: fresh install: exit 0", out)
    check(names() == ours and same_bytes(tdir, src, ours), "templates: fresh install: exactly our templates, byte for byte, in bin/h5-together/templates")
    check(f"installed {n} random map templates" in out, "templates: fresh install: the summary says how many were installed", out)

    # a player's own files, and ours changed or missing
    mine = {"my-own.xdb": b"my own template", "OH-mine.xdb": b"mine too, but named like ours", "notes.txt": b"notes"}
    for name, data in mine.items():
        (tdir / name).write_bytes(data)
    (tdir / ours[0]).write_bytes(b"changed by hand")
    (tdir / ours[1]).unlink()
    code, out = install()
    check(code == 0, "templates: reinstall: exit 0", out)
    check(same_bytes(tdir, src, ours), "templates: reinstall overwrites ours (the changed one) and brings back the missing one")
    check(all((tdir / k).read_bytes() == v for k, v in mine.items()), "templates: reinstall keeps the player's own files untouched")
    check(names() == sorted(ours + list(mine)), "templates: reinstall adds nothing else", str(names()))
    check(f"installed {n} random map templates" in out and f"{n - 1} of them replaced" in out and "left the 3 other" in out,
          "templates: reinstall: the summary counts installed, replaced and left alone", out)

    # uninstall removes only ours
    code, out = uninstall()
    check(code == 0 and not (game_bin / "dinput8.dll").exists(), "templates: uninstall: exit 0", out)
    check(names() == sorted(mine) and all((tdir / k).read_bytes() == v for k, v in mine.items()),
          "templates: uninstall removes only our templates; the player's own files stay", str(names()))
    check(f"removed {n} random map templates" in out, "templates: uninstall says how many it removed", out)

    # with nothing of the player's left, uninstall also removes the empty folders
    for name in mine:
        (tdir / name).unlink()
    code, out = install()
    code2, out2 = uninstall()
    check(code == 0 and code2 == 0 and not (game_bin / "h5-together").exists(), "templates: uninstall removes the empty h5-together folders", out2)
    check(tree(game_bin) == before, "templates: after install and uninstall the game folder is exactly as it was")

    # a folder h5-together that holds something else stays
    other = game_bin / "h5-together" / "other.txt"
    other.parent.mkdir()
    other.write_bytes(b"not ours")
    install()
    code, out = uninstall()
    check(code == 0 and other.read_bytes() == b"not ours" and not tdir.exists(),
          "templates: a folder h5-together with other things in it stays; the empty templates folder goes", out)
    other.unlink()
    other.parent.rmdir()

    # nothing installed: nothing to do, no error
    code, out = uninstall()
    check(code == 0 and "no random map templates" in out and tree(game_bin) == before, "templates: uninstall with nothing installed: no error, nothing changed", out)


# ---------------------------------------------------------------- Valve's steam_api.dll

# The real file never comes near the tests (it is not in the repo, and GitHub's build has
# none): they use a tiny fake with the headers of a 32-bit Windows DLL.
MAKE_PACKAGE = REPO / "tools" / "package" / "make_package.py"
STEAM_API_LINE = "installed Valve's steam_api.dll in h5-together (online play over Steam, off until you turn it on in Options)"


def fake_pe(machine=0x14C, characteristics=0x2102, note=b"a fake steam_api.dll for the tests", date=0):
    """A tiny file with the headers of a Windows DLL (machine 0x14c: 32-bit), and nothing
    else: it cannot be loaded. characteristics 0x2102 says DLL, 0x0102 a program. date: the
    build date in the header (seconds since 1970; PE_DATE_2008 = the 2008 game's)."""
    mz = bytearray(0x40)
    mz[0:2] = b"MZ"
    mz[0x3C:0x40] = (0x40).to_bytes(4, "little")  # where the PE header starts
    coff = (machine.to_bytes(2, "little") + bytes(2) + date.to_bytes(4, "little") + bytes(10)
            + characteristics.to_bytes(2, "little"))
    return bytes(mz) + b"PE\0\0" + coff + note


PE_DATE_2008 = 1212587935  # 2008-06-04 13:58:55: the 2008 game's exe, and MMH5.5's exes made from it


def fake_exe_2008(imports=b"um.dll", size=0x2000):
    """A tiny stand-in for the 2008 game's exe without Steam's protection: its header (the 2008
    date, 32-bit, the image size 0x00DD9000) and one section, `size` bytes in all, naming
    `imports` (um.dll: the game's own; 55u.dll: one of MMH5.5's exes)."""
    b = bytearray(size)
    b[0:4] = b"MZ\x90\x00"
    pe = 0x80
    b[0x3C:0x40] = pe.to_bytes(4, "little")
    b[pe:pe + 4] = b"PE\0\0"
    b[pe + 4:pe + 6] = (0x14C).to_bytes(2, "little")
    b[pe + 6:pe + 8] = (1).to_bytes(2, "little")
    b[pe + 8:pe + 12] = PE_DATE_2008.to_bytes(4, "little")
    b[pe + 20:pe + 22] = (0xE0).to_bytes(2, "little")
    b[pe + 22:pe + 24] = (0x10E).to_bytes(2, "little")
    b[pe + 24:pe + 26] = (0x10B).to_bytes(2, "little")
    b[pe + 24 + 56:pe + 24 + 60] = (0x00DD9000).to_bytes(4, "little")
    sec = pe + 24 + 0xE0
    b[sec:sec + 8] = b".text\0\0\0"
    b[sec + 16:sec + 20] = (size - 0x400).to_bytes(4, "little")
    b[sec + 20:sec + 24] = (0x400).to_bytes(4, "little")
    b[0x500:0x500 + len(imports)] = imports
    return bytes(b)


def fake_mmh55_installer(path):
    """A stand-in for MMH5.5's installer: other bytes, one of MMH5.5's own exes (55u.dll, same
    date), more bytes, then the 2008 game's exe, then more."""
    path.write_bytes(b"Inno Setup stand-in " * 3000 + fake_exe_2008(b"55u.dll") + b"MZ junk " * 5000
                     + fake_exe_2008(b"um.dll", 0x3000) + b" the end" * 1000)


def steam_api_package_checks(work, zip_path):
    """make_package.py and Valve's steam_api.dll: --add-steam-api adds it to a zip built
    already and changes nothing else, --steam-api builds the same zip, and both refuse a
    file that is not a 32-bit DLL called steam_api.dll. Returns the unzipped package with
    the fake file in it."""
    print("\nValve's steam_api.dll in the package (a fake one)")
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as z:
        old_infos = z.infolist()
    top = old_infos[0].filename.split("/", 1)[0]
    check(not any(i.filename.lower().endswith("steam_api.dll") for i in old_infos),
          "steam_api: a package built without the option has none (GitHub's build)")
    fake = work / "fake" / "steam_api.dll"
    fake.parent.mkdir()
    fake.write_bytes(fake_pe())
    target = work / Path(zip_path).name
    shutil.copyfile(zip_path, target)
    original = target.read_bytes()

    def add(zip_file, dll):
        return run([sys.executable, str(MAKE_PACKAGE), "--add-steam-api", str(zip_file), str(dll)])

    wrong = {"a file that is not a DLL": ("steam_api.dll", b"just some text", "not a Windows DLL"),
             "a 64-bit DLL": ("steam_api.dll", fake_pe(machine=0x8664), "not a 32-bit DLL"),
             "steam_api64.dll": ("steam_api64.dll", fake_pe(), "must be called steam_api.dll"),
             "a program": ("steam_api.dll", fake_pe(characteristics=0x0102), "a program, not a DLL")}
    for i, (label, (name, data, says)) in enumerate(wrong.items()):
        f = work / "wrong" / str(i) / name
        f.parent.mkdir(parents=True)
        f.write_bytes(data)
        code, out = add(target, f)
        check(code != 0 and says in out and target.read_bytes() == original,
              f"steam_api: --add-steam-api refuses {label} and leaves the zip as it was", out)

    code, out = add(target, fake)
    new = target.read_bytes()
    check(code == 0 and new != original, "steam_api: --add-steam-api adds the file: exit 0", out)
    check(f"sha256 {hashlib.sha256(new).hexdigest()}" in out, "steam_api: it prints the new zip's SHA-256", out)
    name = f"{top}/h5-together/steam_api.dll"
    with zipfile.ZipFile(target) as z:
        infos = z.infolist()
        added = z.read(name) if name in z.namelist() else b""
        old_data_ok = z.testzip() is None
    fields = lambda i: (i.filename, i.CRC, i.compress_size, i.file_size, i.header_offset, i.date_time, i.external_attr)
    check([i.filename for i in infos] == [i.filename for i in old_infos] + [name],
          "steam_api: the zip holds the same files in the same order, then <top folder>/h5-together/steam_api.dll",
          str([i.filename for i in infos][-3:]))
    check(old_data_ok and [fields(i) for i in infos[:-1]] == [fields(i) for i in old_infos]
          and new[:infos[-1].header_offset] == original[:infos[-1].header_offset],
          "steam_api: every other file of the zip is byte for byte as it was (headers and compressed data)")
    check(added == fake.read_bytes() and ((infos[-1].external_attr >> 16) & 0o777) == 0o644
          and infos[-1].date_time == old_infos[0].date_time,
          "steam_api: the added file is the DLL given, byte for byte, a plain file (644) with the package's date")

    code, out = add(target, fake)
    check(code == 0 and "nothing to do" in out and target.read_bytes() == new, "steam_api: adding the same file again changes nothing", out)
    other = work / "other" / "steam_api.dll"
    other.parent.mkdir()
    other.write_bytes(fake_pe(note=b"another version"))
    code, out = add(target, other)
    check(code != 0 and "already holds another" in out and target.read_bytes() == new,
          "steam_api: adding a different steam_api.dll to a zip that has one is refused", out)
    stray = work / "stray.zip"
    with zipfile.ZipFile(stray, "w") as z:
        z.writestr("readme.txt", "not our package")
    code, out = add(stray, fake)
    check(code != 0 and "not an H5 Together package" in out, "steam_api: a zip that is not our package is refused", out)

    # --steam-api while building gives the same zip as a build and then --add-steam-api (same date)
    env = dict(os.environ, SOURCE_DATE_EPOCH="1762200000")
    build = [sys.executable, str(MAKE_PACKAGE), "--version", "0.0.0-steamtest"]
    code1, out1 = run(build + ["--out-dir", str(work / "built with"), "--steam-api", str(fake)], env=env)
    code2, out2 = run(build + ["--out-dir", str(work / "built then added")], env=env)
    z1 = work / "built with" / "h5-together-0.0.0-steamtest.zip"
    z2 = work / "built then added" / "h5-together-0.0.0-steamtest.zip"
    code3, out3 = add(z2, fake)
    check(code1 == 0 and code2 == 0 and code3 == 0 and z1.read_bytes() == z2.read_bytes(),
          "steam_api: make_package.py --steam-api builds byte for byte the zip that a build and then --add-steam-api make",
          out1 + out2 + out3)
    code, out = run(build + ["--out-dir", str(work / "not built"), "--steam-api", str(work / "wrong" / "1" / "steam_api.dll")], env=env)
    check(code != 0 and "not a 32-bit DLL" in out and not (work / "not built").exists(),
          "steam_api: --steam-api refuses a 64-bit DLL and builds nothing", out)

    with zipfile.ZipFile(target) as z:
        z.extractall(work / "package")
    return work / "package" / top


def steam_api_tests(who, pkg, pkg_steam, game_bin, install, uninstall):
    """Valve's steam_api.dll in a fake game folder. install(p) and uninstall(p) run the
    scripts of the unzipped package p for game_bin (the uninstaller deletes the settings).
    pkg has no steam_api.dll (GitHub's build), pkg_steam has the fake one."""
    print(f"\nValve's steam_api.dll ({who})")
    fake = (pkg_steam / "h5-together" / "steam_api.dll").read_bytes()
    ours = game_bin / "h5-together"
    dst = ours / "steam_api.dll"
    before = tree(game_bin)

    code, out = install(pkg)
    check(code == 0 and not dst.exists() and "steam_api" not in out,
          "steam_api: a package without it installs none and says nothing about it", out)
    code, out = uninstall(pkg)
    check(code == 0 and "steam_api" not in out and tree(game_bin) == before,
          "steam_api: its uninstall says nothing about it either, and leaves the game folder as it was", out)

    code, out = install(pkg_steam)
    check(code == 0 and dst.is_file() and dst.read_bytes() == fake,
          "steam_api: install copies it to bin/h5-together/steam_api.dll, byte for byte", out)
    check(out.count("steam_api") == 1 and STEAM_API_LINE in out, "steam_api: install says so in one line", out)
    dst.write_bytes(b"an older steam_api.dll")
    code, out = install(pkg_steam)
    check(code == 0 and dst.read_bytes() == fake, "steam_api: a reinstall overwrites another copy in bin/h5-together", out)

    code, out = uninstall(pkg)
    check(code == 0 and dst.read_bytes() == fake and "steam_api" not in out
          and sorted(p.name for p in ours.iterdir()) == ["steam_api.dll"],
          "steam_api: an uninstall from a package without it leaves it alone (and the folder h5-together with it)", out)

    install(pkg_steam)
    code, out = uninstall(pkg_steam)
    check(code == 0 and not dst.exists() and "removed steam_api.dll" in out,
          "steam_api: an uninstall from the package with it removes the same file", out)
    check(not ours.exists() and tree(game_bin) == before,
          "steam_api: then the empty h5-together folder goes too: the game folder is as it was")

    install(pkg_steam)
    dst.write_bytes(b"another steam_api.dll, put there by hand")
    code, out = uninstall(pkg_steam)
    check(code == 0 and dst.read_bytes() == b"another steam_api.dll, put there by hand" and "kept h5-together" in out
          and "not the copy this package installs" in out and sorted(p.name for p in ours.iterdir()) == ["steam_api.dll"],
          "steam_api: an uninstall keeps a steam_api.dll that differs from the package's, and says so", out)
    dst.unlink()
    ours.rmdir()


# ---------------------------------------------------------------- the settings file

# A player's settings file from 0.2.3, then called open-homm5.ini (that version's file,
# with a few values set by hand, builder_last as the DLL writes it, a setting this
# version does not know, and two sections added by hand).
OLD_INI = REPO / "tools" / "package" / "test_data" / "open-homm5-0.2.3.ini"
KEPT = "; kept from your previous file:"


def crlf(text):
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode("ascii")


def mentioned(text):
    """{(section, key)} of the lines ";key=..." and "key=..." (names in lower case)."""
    sec, out = "", set()
    for line in text.replace("\r\n", "\n").split("\n"):
        t = line.strip(" \t")
        if t.startswith("[") and "]" in t:
            sec = t[1:t.index("]")].strip(" \t").lower()
            continue
        m = re.match(r";?([A-Za-z0-9_]+)[ \t]*=", t)
        if m:
            out.add((sec, m.group(1).lower()))
    return out


def replaced(text, pairs):
    for a, b in pairs:
        if text.count(a) != 1:
            check(False, f"settings: test setup: the package's ini should have {a.strip()!r} once")
        text = text.replace(a, b)
    return text


def settings_tests(who, pkg, game_bin, install, uninstall):
    """The settings file: refreshed from the package's, the player's values kept, backups.

    install() and uninstall(answer) run the scripts for game_bin and return (exit code,
    output). Returns {case: the refreshed file's bytes}, to compare Windows with Linux."""
    print(f"\nThe settings file ({who})")
    ini = game_bin / "h5-together.ini"
    bak = [game_bin / f"h5-together.ini.{e}" for e in ("bak", "bak2", "bak3")]
    tpl_bytes = (pkg / "h5-together.ini").read_bytes()
    tpl = tpl_bytes.decode("ascii").replace("\r\n", "\n")
    results = {}

    def backups():
        return [p.read_bytes() if p.exists() else None for p in bak]

    def plain(data):
        return all(b < 0x80 for b in data) and b"\n" not in data.replace(b"\r\n", b"")

    # fresh install: the package's file, no backup
    code, out = install()
    check(code == 0 and ini.read_bytes() == tpl_bytes and backups() == [None] * 3,
          "settings: fresh install: the package's h5-together.ini, no backup", out)
    code, out = install()
    check(code == 0 and ini.read_bytes() == tpl_bytes and backups() == [None] * 3 and "already up to date (every setting at its default)" in out,
          "settings: install again over the untouched file: nothing changes, no backup", out)

    # a 0.2.3 file with the player's values: refreshed into this version's file
    old = crlf(OLD_INI.read_text(encoding="ascii"))
    ini.write_bytes(old)
    code, out = install()
    want = replaced(tpl, [
        ("\n;view_delay_ms=50\n", "\nview_delay_ms=50\n"),
        ("\n;net_sleep_ms=1\n", f"\nnet_sleep_ms=1\n\n{KEPT}\nfast_turns=1\n"),
        ("\n;language=auto\n", "\nlanguage=fr\n"),
        ("\n;builder_last=\n", "\nbuilder_last=OH-X1-321110nnxxxxxxxa\n"),
        ("\n;original_music=0\n", "\noriginal_music=1\n"),
        ("\n;texts=game\n", "\ntexts=fr\n"),
    ]) + f"\n[mine]\n{KEPT}\nnote=hello\n"
    new = len(mentioned(tpl) - mentioned(old.decode("ascii")))
    got = ini.read_bytes()
    results["0.2.3 file"] = got
    check(code == 0 and got == crlf(want), "settings: a 0.2.3 file: every value kept in its line, the rest is this version's file",
          f"{out}\n--- got:\n{got.decode('ascii', 'replace')}")
    check(plain(got), "settings: the refreshed file is plain ASCII with CRLF line endings, like the package's")
    check(f"settings file refreshed: 8 of your settings kept, {new} new settings available (see the comments)" in out and new > 30,
          "settings: the summary counts the settings kept and the new ones", out)
    check("2 settings not listed in this version" in out and "h5-together.ini.bak" in out,
          "settings: the summary names the unknown settings and the backup", out)
    check(backups() == [old, None, None], "settings: the previous file is saved as h5-together.ini.bak, byte for byte")

    # again: nothing to do, no second backup
    code, out = install()
    check(code == 0 and ini.read_bytes() == got and backups() == [old, None, None],
          "settings: a second install changes nothing and makes no new backup", out)
    check("settings file already up to date: 8 of your settings kept" in out, "settings: a second install says the file is up to date", out)

    # odd files: a byte-order mark, LF, a setting before any section, other upper/lower
    # case and spaces, a second copy, comments that are not settings, a line without "="
    odd = (b"\xef\xbb\xbftop=1\n[NET]\n  Tuning = 0  \ntuning=1\n#keepalive_ms=5\n; keepalive_ms=7\n"
           b"lonely line\n[ai]\nfast_paths=0\n")
    ini.write_bytes(odd)
    code, out = install()
    want = replaced(tpl, [
        ("same settings.\n\n[net]\n", f"same settings.\n\n{KEPT}\ntop=1\n\n[net]\n"),
        ("\n;tuning=1\n", "\ntuning=0\n"),
        ("\n;net_sleep_ms=1\n", f"\n;net_sleep_ms=1\n\n{KEPT}\ntuning=1\nlonely line\n"),
        ("\n;fast_paths=1\n", "\nfast_paths=0\n"),
    ])
    got = ini.read_bytes()
    results["odd file"] = got
    check(code == 0 and got == crlf(want), "settings: odd file (BOM, LF, a setting before [net], case, spaces, a second copy): as expected",
          f"{out}\n--- got:\n{got.decode('ascii', 'replace')}")
    check(f"4 of your settings kept, {len(mentioned(tpl)) - 2} new settings available" in out and "1 setting not listed" in out,
          "settings: odd file: the counts", out)
    check(backups() == [odd, old, None], "settings: a second refresh keeps the older backup as h5-together.ini.bak2")

    # more refreshes: only the last three backups stay
    ini.write_bytes(b"")
    code, out = install()
    results["empty file"] = ini.read_bytes()
    check(code == 0 and ini.read_bytes() == tpl_bytes and backups() == [b"", odd, old],
          "settings: an empty file becomes the package's file; three backups", out)
    one = b"[net]\r\ntuning=0\r\n"
    ini.write_bytes(one)
    code, out = install()
    results["one setting"] = ini.read_bytes()
    check(code == 0 and ini.read_bytes() == crlf(replaced(tpl, [("\n;tuning=1\n", "\ntuning=0\n")])),
          "settings: a file with one setting: refreshed", out)
    check(backups() == [one, b"", odd] and not (game_bin / "h5-together.ini.bak4").exists(),
          "settings: a fourth refresh drops the oldest backup (three are kept)")

    # a "Unicode" (UTF-16) file is left alone
    current, saved = ini.read_bytes(), backups()
    utf16 = "[net]\r\ntuning=0\r\n".encode("utf-16")
    ini.write_bytes(utf16)
    code, out = install()
    check(code == 0 and ini.read_bytes() == utf16 and backups() == saved and "not a plain text file" in out,
          "settings: a UTF-16 file is kept as it is, no backup", out)
    ini.write_bytes(current)

    # uninstall: one question for the file and its backups
    code, out = uninstall("n\n")
    check(code == 0 and ini.exists() and backups() == saved and "its old copies" in out,
          "settings: uninstall (n): the file and its backups stay", out)
    code, out = uninstall("y\n")
    check(code == 0 and not ini.exists() and backups() == [None] * 3, "settings: uninstall (y): the file and its backups are removed", out)
    bak[1].write_bytes(b"old copy")
    code, out = uninstall("y\n")
    check(code == 0 and not bak[1].exists() and "removed h5-together.ini.bak2" in out, "settings: uninstall (y) with only a backup left: removed", out)
    return results


# ---------------------------------------------------------------- the old name (open-homm5, until 0.2.7)

# A dinput8.dll of 0.2.7, when the add-on was called open-homm5 (only its marker text matters).
V027_DLL = b"MZ pretend dll ... open-homm5 hook DLL 0.2.7 loaded (built Nov  1 2026) ..."
V027_INI = b"[net]\r\ntuning=0\r\n"  # a setting the player changed
V027_SKINS = b"[skins]\r\nGodric=Freyda\r\n"


def make_old_install(pkg, game_bin, extra_template=True):
    """A 0.2.7 install in game_bin, with the old names: the DLL, the settings and an older copy of
    them, the hero looks, two logs, and the folder open-homm5 with a French pack, an old copy of one
    of our templates and a template of the player's own. Returns {path below bin: bytes}."""
    ours = sorted(p.name for p in template_dir(pkg).iterdir())
    files = {
        "dinput8.dll": V027_DLL,
        "open-homm5.ini": V027_INI,
        "open-homm5.ini.bak": b"an older copy of the settings",
        "open-homm5-skins.ini": V027_SKINS,
        "open-homm5.log": b"the last log of 0.2.7\n",
        "open-homm5.old.log": b"an even older log\n",
        "open-homm5/lang/fr/x.pak": b"PK a French pack",
        f"open-homm5/templates/{ours[0]}": b"an old copy of one of ours",
    }
    if extra_template:
        files["open-homm5/templates/OH-mine.xdb"] = b"the player's own template"
    for rel, data in files.items():
        (game_bin / rel).parent.mkdir(parents=True, exist_ok=True)
        (game_bin / rel).write_bytes(data)
    return files


def migration_tests(who, pkg, game_bin, install, uninstall):
    """An install over a 0.2.7 one (the old names) moves the player's files to the new names.

    install() and uninstall(answer) run the scripts for game_bin and return (exit code, output).
    Returns {case: the settings file's bytes}, to compare Windows with Linux."""
    print(f"\nThe old name: install over open-homm5 0.2.7 ({who})")
    results = {}
    src = template_dir(pkg)
    ours = sorted(p.name for p in src.iterdir())
    tpl = (pkg / "h5-together.ini").read_bytes()
    want_ini = tpl.replace(b"\r\n;tuning=1\r\n", b"\r\ntuning=0\r\n")
    before = tree(game_bin)
    make_old_install(pkg, game_bin)

    code, out = install()
    check(code == 0 and "open-homm5 is now called H5 Together: your files got the new name, h5-together "
          "(the settings, the hero looks, the log, the folder)" in out,
          "old name: install over 0.2.7: exit 0, and one line says the files got the new name", out)
    check((game_bin / "dinput8.dll").read_bytes() == (pkg / "dinput8.dll").read_bytes() and "replaced the older" in out,
          "old name: the 0.2.7 dinput8.dll is recognised as ours and replaced", out)
    got = (game_bin / "h5-together.ini").read_bytes()
    results["0.2.7 file, moved"] = got
    check(got == want_ini and "settings file refreshed: 1 of your settings kept" in out,
          "old name: open-homm5.ini became h5-together.ini, refreshed, the player's value kept", out)
    check((game_bin / "h5-together.ini.bak").read_bytes() == V027_INI
          and (game_bin / "h5-together.ini.bak2").read_bytes() == b"an older copy of the settings"
          and not (game_bin / "h5-together.ini.bak3").exists(),
          "old name: the backups follow: the moved file is h5-together.ini.bak, its older copy .bak2")
    check((game_bin / "h5-together-skins.ini").read_bytes() == V027_SKINS, "old name: the hero looks are h5-together-skins.ini, byte for byte")
    check((game_bin / "h5-together.old.log").read_bytes() == b"the last log of 0.2.7\n" and not (game_bin / "h5-together.log").exists(),
          "old name: the last log is h5-together.old.log (the older log); a new log starts with the game")
    left = sorted(p.name for p in game_bin.iterdir() if p.name.lower().startswith("open-homm5"))
    check(left == [], "old name: no file or folder of the old name is left in bin", str(left))
    new_dir = game_bin / "h5-together"
    check((new_dir / "lang" / "fr" / "x.pak").read_bytes() == b"PK a French pack", "old name: the French pack moved with the folder, byte for byte")
    names = sorted(p.name for p in (new_dir / "templates").iterdir())
    check(names == sorted(ours + ["OH-mine.xdb"]) and same_bytes(new_dir / "templates", src, ours)
          and (new_dir / "templates" / "OH-mine.xdb").read_bytes() == b"the player's own template",
          "old name: the templates: ours are this version's, the player's own one stays", str(names))

    # again: nothing to move, nothing changes
    after = tree(game_bin)
    code, out = install()
    check(code == 0 and "is now called H5 Together" not in out and "already up to date" in out and tree(game_bin) == after,
          "old name: a second install moves nothing and changes nothing", out)

    # uninstall: the new names go, the player's own files stay
    code, out = uninstall("y\n")
    gone = [n for n in ("dinput8.dll", "h5-together.ini", "h5-together.ini.bak", "h5-together.ini.bak2", "h5-together.old.log")
            if (game_bin / n).exists()]
    check(code == 0 and not gone and (game_bin / "h5-together-skins.ini").exists()
          and (new_dir / "lang" / "fr" / "x.pak").exists() and sorted(p.name for p in (new_dir / "templates").iterdir()) == ["OH-mine.xdb"],
          "old name: uninstall after the move: ours removed; the hero looks, the French pack and the player's template stay", out)
    for p in sorted(game_bin.rglob("*"), reverse=True):
        if p.relative_to(game_bin).as_posix() not in before:
            p.unlink() if p.is_file() else p.rmdir()

    # both names there: the new ones are kept, what the new folder lacks moves into it
    print(f"\nThe old name: both names there ({who})")
    install()
    (new_dir / "templates" / "OH-new-own.xdb").write_bytes(b"a template made with the new version")
    (new_dir / "notes.txt").write_bytes(b"new notes")
    make_old_install(pkg, game_bin)
    (game_bin / "open-homm5" / "notes.txt").write_bytes(b"old notes")
    (game_bin / "open-homm5.log").unlink()
    (game_bin / "h5-together.log").write_bytes(b"a log of the new version\n")
    ini_before = (game_bin / "h5-together.ini").read_bytes()
    code, out = install()
    check(code == 0 and "kept open-homm5.ini as it is: there is already a h5-together.ini, which is the one used" in out
          and (game_bin / "open-homm5.ini").read_bytes() == V027_INI and (game_bin / "h5-together.ini").read_bytes() == ini_before
          and (game_bin / "open-homm5.ini.bak").exists() and not (game_bin / "h5-together.ini.bak").exists(),
          "both: open-homm5.ini and its copy stay as they are; h5-together.ini is the one used, unchanged", out)
    check((game_bin / "h5-together-skins.ini").read_bytes() == V027_SKINS, "both: the hero looks (only under the old name) moved")
    check(not (game_bin / "open-homm5.old.log").exists() and not (game_bin / "h5-together.old.log").exists()
          and (game_bin / "h5-together.log").read_bytes() == b"a log of the new version\n",
          "both: the old older log is removed; the new log stays as it is")
    names = sorted(p.name for p in (new_dir / "templates").iterdir())
    check(names == sorted(ours + ["OH-mine.xdb", "OH-new-own.xdb"]) and same_bytes(new_dir / "templates", src, ours),
          "both: the templates of both folders are in h5-together\\templates, ours this version's", str(names))
    check((new_dir / "lang" / "fr" / "x.pak").read_bytes() == b"PK a French pack" and (new_dir / "notes.txt").read_bytes() == b"new notes",
          "both: the French pack moved into h5-together; a file both folders have keeps the new folder's copy")
    old_left = tree(game_bin / "open-homm5") if (game_bin / "open-homm5").is_dir() else None
    check(old_left == {"notes.txt": sha(game_bin / "open-homm5" / "notes.txt")} if old_left else False,
          "both: only that file stays in the old folder, the rest of it is gone", str(old_left))
    check("left 1 file(s) in the folder open-homm5: the folder h5-together has them too" in out, "both: the summary says a file stayed in open-homm5", out)
    code, out = uninstall("y\n")
    check(code == 0 and not (game_bin / "open-homm5.ini").exists() and not (game_bin / "h5-together.ini").exists()
          and not (game_bin / "open-homm5.ini.bak").exists() and "removed h5-together.ini" in out and "removed open-homm5.ini.bak" in out,
          "both: uninstall asks once for both settings files and removes them and their copies", out)
    for p in sorted(game_bin.rglob("*"), reverse=True):
        if p.relative_to(game_bin).as_posix() not in before:
            p.unlink() if p.is_file() else p.rmdir()

    # an install of 0.2.7 never updated, removed with this package's uninstaller
    print(f"\nThe old name: uninstall of a 0.2.7 install ({who})")
    make_old_install(pkg, game_bin)
    code, out = uninstall("n\n")
    check(code == 0 and not (game_bin / "dinput8.dll").exists() and not (game_bin / "open-homm5.log").exists()
          and not (game_bin / "open-homm5.old.log").exists() and (game_bin / "open-homm5.ini").exists()
          and (game_bin / "open-homm5.ini.bak").exists() and "kept open-homm5.ini and its old copies" in out,
          "old install: uninstall (n): the 0.2.7 DLL and the logs go, the settings stay", out)
    old_tpl = game_bin / "open-homm5" / "templates"
    check(sorted(p.name for p in old_tpl.iterdir()) == ["OH-mine.xdb"] and f"removed 1 random map templates from open-homm5" in out
          and (game_bin / "open-homm5" / "lang" / "fr" / "x.pak").exists(),
          "old install: our templates in open-homm5\\templates go; the player's own and the French pack stay", out)
    code, out = uninstall("y\n")
    check(code == 0 and not (game_bin / "open-homm5.ini").exists() and not (game_bin / "open-homm5.ini.bak").exists()
          and "removed open-homm5.ini" in out and (game_bin / "open-homm5-skins.ini").exists(),
          "old install: uninstall (y): open-homm5.ini and its copy go; the hero looks stay (as with the new name)", out)
    for p in sorted(game_bin.rglob("*"), reverse=True):
        if p.relative_to(game_bin).as_posix() not in before:
            p.unlink() if p.is_file() else p.rmdir()
    check(tree(game_bin) == before, "old name: the test game folder is back as it was")
    return results


# ---------------------------------------------------------------- finding the game, and what the scripts say about it

def make_manifest(library, language="french", installdir=GAME):
    """Steam's file for the game, in the library that holds it (the scripts read the folder and
    the language from it). language None: the file has no language line."""
    lines = ['"AppState"', "{", '\t"appid"\t\t"15370"', f'\t"installdir"\t\t"{installdir}"']
    if language:
        lines += ['\t"UserConfig"', "\t{", f'\t\t"language"\t\t"{language}"', "\t}"]
    lines.append("}")
    f = Path(library) / "steamapps" / "appmanifest_15370.acf"
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))


def add_mmh55(b, version="19e", u55=b"pretend 55u.dll"):
    """MMH5.5's version pak (data/MMH55-Index.pak) and a 55u.dll for a fake game. Returns the
    sha256 of the 55u.dll: with it, the scripts' test option counts the file as a known version."""
    root = Path(b).parent
    (root / "data").mkdir(exist_ok=True)
    with zipfile.ZipFile(root / "data" / "MMH55-Index.pak", "w") as z:
        z.writestr("scripts/H55-Version.lua", f'H55_Version = "{version}"\n')
    (Path(b) / "55u.dll").write_bytes(u55)
    return sha(Path(b) / "55u.dll")


def make_steam_for(steam, libraries, posix_paths):
    """make_steam, with the library paths written the way the scripts of the platform read them."""
    make_steam(steam, libraries, escape_backslashes=not posix_paths)
    if posix_paths:
        v = Path(steam) / "steamapps" / "libraryfolders.vdf"
        data = v.read_bytes()
        for x in [steam, *libraries]:
            data = data.replace(str(x).encode(), posix(x).encode())
        v.write_bytes(data)


def game_tests(who, pkg, work, install, uninstall, o, p, start_fake_game):
    """What the scripts find, show and ask: the game's folder, language and exes, MMH5.5 and its
    version, warnings, several games, a wrong folder, a game that is running.

    install(*args, stdin="") and uninstall(...) run the scripts with those arguments and return
    (exit code, output). o: the names of their options (game, steam, known). p: writes a path the
    way the platform's scripts read it. start_fake_game(bin_folder) starts a program that looks
    like the running game and returns a function that ends it."""
    print(f"\nFinding the game, and what the installer says ({who})")
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    posix_paths = who != "Windows"
    version = package_version(pkg)

    # Steam, with its file for the game: the language, and a MMH5.5 that we know
    steam, lib = work / "steam a", work / "lib a"
    make_steam_for(steam, [steam, lib], posix_paths)
    b = make_game(lib)
    make_manifest(lib, "french")
    h = add_mmh55(b, "19e", b"the 55u.dll of a version we know")
    code, out = install(o["steam"], p(steam), o["known"], f"{h}=RC19k")
    check(code == 0 and (b / "dinput8.dll").exists() and "Found the game." in out,
          "game: a Steam game is found and installed, with no question asked", out)
    check("Found in:   Steam" in out and "Language:   French (Steam's setting for the game)" in out and GAME in out,
          "game: the found screen shows the folder, that it is a Steam copy and Steam's language", out)
    check("MMH5.5:     RC19k (tested with H5 Together)" in out and "Warning" not in out and "Reminder" not in out,
          "game: a known MMH5.5 is named (RC19k), with no warning", out)
    check(re.search(r"MMH55_Utility_64\.exe +MMH5\.5's co-op exe: H5 Together works with it", out)
          and re.search(r"H5_Game\.exe +not the 2008 game \(Steam's 2025 update\?\): H5 Together does nothing with it", out),
          "game: the exes are explained (the co-op exe works; another exe, not the 2008 game's, does nothing)", out)
    check("H5 Together has its panels in French" in out, "game: says the add-on's panels will be in the game's language", out)
    check(f"All done: H5 Together {version} is installed." in out and "What to do next" in out and "h5-together.log" in out
          and f'"H5 Together hook DLL {version} loaded"' in out,
          "game: the end says it worked and what to do next (the first line of h5-together.log)", out)

    # the same game, but an MMH5.5 that we have not tested
    code, out = install(o["game"], p(b))
    check(code == 0 and "MMH5.5:     a version we have not tested (its version file says 19e)" in out
          and "Warning: this MMH5.5 is a version we have not tested (tested: RC19d and RC19k)." in out
          and "Reminder: this MMH5.5 is a version we have not tested" in out,
          "game: an MMH5.5 that is not a known version: named as such with its version file, warned at the start and the end", out)

    # uninstall shows the game too
    code, out = uninstall(o["game"], p(b), stdin="y\n")
    check(code == 0 and "Your game" in out and "Found in:   Steam" in out and "Removing" in out and "H5 Together is removed" in out
          and not (b / "dinput8.dll").exists(),
          "game: the uninstaller shows the game it works on and says it is done", out)

    # no MMH5.5 and no 2008 exe (only Steam's 2025 rebuild): a plain warning at the start and the end, still installs
    b = make_game(work / "lib c", exes=("H5_Game.exe",))
    code, out = install(o["game"], p(b))
    check(code == 0 and (b / "dinput8.dll").exists() and "MMH5.5:     not found" in out
          and "Warning: MMH5.5 was not found in this folder (there is no MMH55_Utility_64.exe), nor the" in out
          and "Reminder: neither MMH5.5 nor the original 2008 game's exe was found" in out and "2025 rebuild" in out,
          "game: without MMH5.5 or the 2008 exe it says so plainly (warning and reminder), and still installs", out)
    check("Language:" not in out and "Found in:" not in out, "game: a folder that is not a Steam copy shows no store and no language", out)
    code, out = uninstall(o["game"], p(b), stdin="y\n")
    check(code == 0 and "Your game" in out and "MMH5.5:     not found" in out and "Warning" not in out,
          "game: the uninstaller shows the facts but does not warn about MMH5.5", out)

    # the original 2008 game without MMH5.5 (its exe: the 2008 date, no 55u.dll): works, no warning
    b = make_game(work / "lib g", exes=())
    (b / "H5_Game.exe").write_bytes(fake_pe(characteristics=0x0102, note=b"the 2008 game, um.dll", date=PE_DATE_2008))
    code, out = install(o["game"], p(b))
    check(code == 0 and (b / "dinput8.dll").exists()
          and "MMH5.5:     not found (H5 Together works with the original 2008 game)" in out
          and re.search(r"H5_Game\.exe +the original 2008 game: H5 Together works with it", out)
          and "Warning" not in out and "Reminder" not in out,
          "game: the original 2008 game without MMH5.5 is recognised, with no warning", out)

    # MMH5.5 with the 2008 game's exe kept as H5_GameOLD.exe (the GameRanger guide), and an
    # MMH5.5 exe under H5_Game.exe that is not a byte copy (the 2008 date, but it loads 55u.dll)
    b = make_game(work / "lib h")
    (b / "H5_GameOLD.exe").write_bytes(fake_pe(characteristics=0x0102, note=b"the 2008 game, um.dll", date=PE_DATE_2008))
    (b / "H5_Game.exe").write_bytes(fake_pe(characteristics=0x0102, note=b"MMH5.5 exe, 55u.dll", date=PE_DATE_2008))
    code, out = install(o["game"], p(b))
    check(code == 0 and re.search(r"H5_GameOLD\.exe +the original 2008 game: works too \(H5 Together hides MMH5\.5 from it\)", out)
          and re.search(r"H5_Game\.exe +not the 2008 game", out),
          "game: H5_GameOLD.exe (the 2008 game) is recognised next to MMH5.5; an MMH5.5 exe with the same date is not taken for it", out)

    # the original game with 4 GB: H5_Original_4GB.exe made from MMH5.5's installer file
    b = make_game(work / "lib 4gb")
    installer = work / "MMH55_RC19k stand-in.exe"
    fake_mmh55_installer(installer)
    code, out = install(o["game"], p(b), o["mmh55"], p(installer))
    made = b / "H5_Original_4GB.exe"
    data = made.read_bytes() if made.exists() else b""
    check(code == 0 and data[:0x3000] == fake_exe_2008(b"um.dll", 0x3000)[:0x96] + bytes([0x2E]) + fake_exe_2008(b"um.dll", 0x3000)[0x97:]
          and len(data) == 0x3000 and "made H5_Original_4GB.exe" in out and "MMH55_RC19k stand-in.exe" in out,
          "4 GB: the 2008 exe is copied out of MMH5.5's installer (not MMH5.5's own exe), with the 4 GB mark", out)
    code, out = install(o["game"], p(b))
    check(code == 0 and "H5_Original_4GB.exe (the original game with 4 GB of memory) is already there" in out
          and re.search(r"H5_Original_4GB\.exe +the original 2008 game", out),
          "4 GB: a second install keeps it, and the game's description names it as the original game", out)
    code, out = uninstall(o["game"], p(b), stdin="y\n")
    check(code == 0 and not made.exists() and "removed H5_Original_4GB.exe" in out,
          "4 GB: the uninstaller removes it", out)
    b = make_game(work / "lib 4gb none")
    other = work / "not the installer.exe"
    other.write_bytes(b"MZ nothing here " * 4000)
    code, out = install(o["game"], p(b), o["mmh55"], p(other))
    check(code == 0 and not (b / "H5_Original_4GB.exe").exists() and "Could not make H5_Original_4GB.exe" in out,
          "4 GB: a file without the 2008 exe: said plainly, nothing made, still installs", out)

    # H5_Game.exe that is a copy of the co-op exe (the GameRanger guide)
    b = make_game(work / "lib d")
    (b / "H5_Game.exe").write_bytes((b / "MMH55_Utility_64.exe").read_bytes())
    code, out = install(o["game"], p(b))
    check(code == 0 and re.search(r"H5_Game\.exe +a copy of the co-op exe \(made for GameRanger\): works too", out),
          "game: an H5_Game.exe that is a copy of the co-op exe is recognised", out)

    # the language: no texts for it, English, and no language line
    for language, shown, expect in (("german", "German", "H5 Together has no texts in German yet: its panels will be in English"),
                                    ("english", "English", "H5 Together has its panels in English"),
                                    (None, None, None)):
        b = make_game(work / f"lib e {language}")
        make_manifest(work / f"lib e {language}", language)
        code, out = install(o["game"], p(b))
        if language:
            check(code == 0 and f"Language:   {shown} (Steam's setting for the game)" in out and expect in out,
                  f"game: Steam language {language}: shown, and what the add-on will use", out)
        else:
            check(code == 0 and "Language:" not in out and "H5 Together has" not in out,
                  "game: a Steam file without a language line: no language shown", out)

    # the folder named in Steam's file for the game (not only the usual name)
    steam, lib = work / "steam f", work / "lib f"
    make_steam_for(steam, [steam, lib], posix_paths)
    b = make_game(lib, name="Heroes Own Folder Name")
    make_manifest(lib, "english", installdir="Heroes Own Folder Name")
    code, out = install(o["steam"], p(steam))
    check(code == 0 and (b / "dinput8.dll").exists() and "Found the game." in out,
          "game: the folder that Steam's file names (installdir) is found, whatever its name", out)
    (lib / "steamapps" / "appmanifest_15370.acf").unlink()
    code, out = install(o["steam"], p(steam), stdin="")
    check(code == 4 and "could not find the game" in out, "game: without that file the odd folder name is not found (it asks)", out)

    # two games: the list says what each is; a number that is not in it; a folder that is not the game
    steam, la, lb = work / "steam h", work / "lib h1", work / "lib h2"
    make_steam_for(steam, [steam, la, lb], posix_paths)
    b1 = make_game(la)
    make_manifest(la, "french")
    add_mmh55(b1, "19d", b"an unknown 55u.dll")
    b2 = make_game(lb, exes=("H5_Game.exe",))
    code, out = install(o["steam"], p(steam), stdin=f"9\n{p(work)}\n2\n")
    check("The game is in several places:" in out and "1) " in out and "2) " in out
          and "Steam; MMH5.5: a version we have not tested (its version file says 19d)" in out and "MMH5.5: not found" in out,
          "game: several games: the list says for each where it comes from and its MMH5.5", out)
    check("There is no number 9" in out and "That is not the game folder" in out and code == 0
          and (b2 / "dinput8.dll").exists() and not (b1 / "dinput8.dll").exists(),
          "game: a wrong number and a wrong folder are explained; then number 2 installs into the second game only", out)
    code, out = install(o["steam"], p(steam), stdin=f"{p(b1.parent)}\n")
    check(code == 0 and (b1 / "dinput8.dll").exists(), "game: with several games a pasted folder works too", out)
    uninstall(o["game"], p(b1), stdin="y\n")
    uninstall(o["game"], p(b2), stdin="y\n")

    # nothing found: a wrong folder is explained, then Enter cancels
    empty = work / "steam empty"
    make_steam_for(empty, [empty], posix_paths)
    code, out = install(o["steam"], p(empty), stdin=f"{p(work)}\n\n")
    check(code == 4 and "That is not the game folder" in out and "Try again" in out and "No game folder, so nothing was installed" in out,
          "game: none found, a wrong folder: explained, then Enter cancels with exit 4", out)

    # the game is running: the installer and the uninstaller ask to close it
    print(f"\nThe game is running ({who})")
    b = make_game(work / "lib j")
    code, out = install(o["game"], p(b))
    fresh = make_game(work / "lib j2")
    stop = start_fake_game(b)
    try:
        before = tree(b)
        code, out = install(o["game"], p(b), stdin="q\n")
        check(code == 8 and "The game is running (H5_Game.exe)" in out and "Close it and run install" in out
              and "nothing was installed" in out and tree(b) == before,
              "running: the installer asks to close the game; Q stops it (exit 8) and nothing is changed", out)
        code, out = install(o["game"], p(b), stdin="")
        check(code == 8, "running: with no answer at all it stops (exit 8)", out)
        code, out = uninstall(o["game"], p(b), stdin="q\n")
        check(code == 8 and "The game is running" in out and "nothing was removed" in out and tree(b) == before,
              "running: the uninstaller asks the same, and removes nothing (exit 8)", out)
        if who == "Windows":
            code, out = install(o["game"], p(fresh), stdin="q\n")
            check(code == 0 and "The game is running" not in out and (fresh / "dinput8.dll").exists(),
                  "running: a game that runs from another folder does not stop an install into this one", out)
        code, out = install(o["game"], p(b), stdin="s\n")
        check(code == 0 and "The game is running" in out and "All done" in out, "running: S goes on anyway", out)
    finally:
        stop()
    code, out = install(o["game"], p(b))
    check(code == 0 and "The game is running" not in out, "running: once the game is closed it just installs", out)
    code, out = uninstall(o["game"], p(b), stdin="y\n")
    check(code == 0 and not (b / "dinput8.dll").exists(), "running: and the uninstaller removes", out)
    uninstall(o["game"], p(fresh), stdin="y\n")


def windows_only_game_tests(pkg, work, install, uninstall):
    """GOG and MMH5.5's own entry in the registry (a key of our own with the same layout as
    HKLM:\\SOFTWARE), the folder window (answered by -TestPicker), the pause at the end."""
    print("\nFinding the game on Windows: GOG, MMH5.5's installer, the folder window, the pause")
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    key = f"HKCU:\\Software\\H5TogetherTest-{uuid.uuid4().hex[:10]}"

    def reg_set(path, name, value):
        code, out = run(["powershell", "-NoProfile", "-Command",
                         f"New-Item -Path '{path}' -Force | Out-Null; Set-ItemProperty -LiteralPath '{path}' -Name '{name}' -Value '{value}'"])
        if code != 0:
            raise RuntimeError(f"could not write the test registry key {path}: {out}")

    empty = work / "steam empty"
    make_steam(empty, [empty])
    # a GOG copy: its root holds goggame-<id>.info; and Heroes III, which must not count as ours
    gog = work / "GOG Games" / "Heroes of Might and Magic 5"
    gb = gog / "bin"
    gb.mkdir(parents=True)
    for e in ("H5_Game.exe", "MMH55_Utility_64.exe"):
        (gb / e).write_bytes(b"fake " + e.encode())
    (gog / "goggame-1207658787.info").write_bytes(b"{}")
    homm3 = work / "GOG Games" / "HoMM 3 Complete"
    homm3.mkdir()
    (homm3 / "Heroes3.exe").write_bytes(b"fake")
    try:
        reg_set(f"{key}\\GOG.com\\Games\\1207658787", "gameName", "Heroes of Might and Magic 3 Complete")
        reg_set(f"{key}\\GOG.com\\Games\\1207658787", "path", str(homm3))
        reg_set(f"{key}\\GOG.com\\Games\\1207658800", "gameName", "Heroes of Might and Magic 5")
        reg_set(f"{key}\\GOG.com\\Games\\1207658800", "path", str(gog) + "\\")
        reg_set(f"{key}\\GOG.com\\Games\\1111111111", "path", str(work / "does not exist"))
        code, out = install("-SteamDir", str(empty), "-TestRegistry", key)
        check(code == 0 and (gb / "dinput8.dll").exists() and "Found the game." in out and "Found in:   GOG" in out,
              "GOG: a game in GOG's registry list is found (path with a trailing backslash), marked GOG, no question asked", out)
        check("Heroes3" not in out and "HoMM 3" not in out and "several places" not in out,
              "GOG: Heroes III in the same list, and a folder that is gone, are ignored", out)
        uninstall("-GameDir", str(gb), stdin="y\n")

        # MMH5.5's own installer wrote an uninstall entry with the folder it installed into
        mm = work / "other copy"
        mmb = make_game(mm)
        reg2 = key + "2"
        reg_set(f"{reg2}\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{{CEC93C99-EB3D-4EBE-B0BE-AE228E5B47AE}}_is1",
                "InstallLocation", str(mmb.parent) + "\\")
        try:
            code, out = install("-SteamDir", str(empty), "-TestRegistry", reg2)
            check(code == 0 and (mmb / "dinput8.dll").exists() and "Found the game." in out,
                  "MMH5.5's installer: the folder in its uninstall entry is found (any copy of the game)", out)
            uninstall("-GameDir", str(mmb), stdin="y\n")
        finally:
            run(["powershell", "-NoProfile", "-Command", f"Remove-Item -LiteralPath '{reg2}' -Recurse -Force -ErrorAction SilentlyContinue"])

        # Steam and GOG both have it: a list with both; the same game listed twice counts once
        steam, lib = work / "steam both", work / "lib both"
        make_steam(steam, [steam, lib])
        sb = make_game(lib)
        make_manifest(lib, "french")
        reg_set(f"{key}\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{{CEC93C99-EB3D-4EBE-B0BE-AE228E5B47AE}}_is1",
                "InstallLocation", str(gog))
        code, out = install("-SteamDir", str(steam), "-TestRegistry", key, stdin="2\n")
        check(code == 0 and "several places" in out and "1) " in out and "2) " in out and "3) " not in out
              and "Steam; MMH5.5: a version we have not tested" in out and "GOG; MMH5.5: a version we have not tested" in out
              and (gb / "dinput8.dll").exists() and not (sb / "dinput8.dll").exists(),
              "GOG and Steam: both are listed (the GOG game once, though two entries name it); number 2 installs into it", out)
        uninstall("-GameDir", str(gb), stdin="y\n")

        # the folder window: B opens it (here answered by -TestPicker), and a cancelled window asks again
        code, out = install("-SteamDir", str(empty), "-TestRegistry", key + "-none", "-TestPicker", str(gog), stdin="b\n")
        check(code == 0 and (gb / "dinput8.dll").exists() and "Type B and press Enter" in out,
              "folder window: B (none found) chooses the folder in a window; the choice is checked and installed", out)
        uninstall("-GameDir", str(gb), stdin="y\n")
        code, out = install("-SteamDir", str(empty), "-TestRegistry", key + "-none", "-TestPicker", "cancel", stdin="b\n\n")
        check(code == 4 and "No folder chosen" in out and not (gb / "dinput8.dll").exists(),
              "folder window: closing the window without a choice asks again; Enter cancels (exit 4)", out)
        code, out = install("-SteamDir", str(empty), "-TestRegistry", key + "-none", "-TestPicker", str(work), stdin="b\n\n")
        check(code == 4 and "That is not the game folder" in out and "Try again" in out,
              "folder window: a folder that is not the game is refused with an explanation", out)
        two = work / "steam two"
        make_steam(two, [two, lib, work / "lib two"])
        make_game(work / "lib two")
        code, out = install("-SteamDir", str(two), "-TestRegistry", key + "-none", "-TestPicker", str(gog), stdin="b\n")
        check(code == 0 and (gb / "dinput8.dll").exists() and "Or type B to choose another folder in a window" in out,
              "folder window: with several games B also offers the window", out)
        uninstall("-GameDir", str(gb), stdin="y\n")
    finally:
        run(["powershell", "-NoProfile", "-Command",
             f"foreach ($k in '{key}', '{key}2', '{key}-none') {{ Remove-Item -LiteralPath $k -Recurse -Force -ErrorAction SilentlyContinue }}"])

    # the window itself can be built here (the types exist in this PowerShell); it is not shown
    code, out = run(["powershell", "-NoProfile", "-Command",
                     "Add-Type -AssemblyName System.Windows.Forms; $f = New-Object System.Windows.Forms.Form; $f.TopMost = $true; "
                     "$d = New-Object System.Windows.Forms.FolderBrowserDialog; $d.ShowNewFolderButton = $false; 'window ok'"])
    check(code == 0 and "window ok" in out, "folder window: the folder dialog can be created on this Windows PowerShell", out)

    # the window at the end: without -NoPause the script waits for Enter, so that a window opened by
    # a double click stays on screen (Read-Host's prompt is not in a pipe's output: watch it wait)
    b = make_game(work / "lib pause")
    no_registry = key + "-none"
    waits, code, out = run_until_it_waits(bat(str(pkg / "install.bat"), "-GameDir", str(b), "-TestRegistry", no_registry),
                                          "To remove H5 Together")
    check(waits and code == 0 and "All done" in out, "pause: the installer waits for Enter at the end (and ends with exit 0 after it)", out)
    waits, code, out = run_until_it_waits(bat(str(pkg / "install.bat"), "-GameDir", str(work / "nope"), "-TestRegistry", no_registry),
                                          "so nothing was installed")
    check(waits and code == 4, "pause: it also waits when it stops early (no game found), then exits 4", out)
    waits, code, out = run_until_it_waits(bat(str(pkg / "uninstall.bat"), "-GameDir", str(b), "-TestRegistry", no_registry),
                                          "To put it back later", answer="y\n")
    check(waits and code == 0 and not (b / "dinput8.dll").exists(), "pause: the uninstaller waits for Enter at the end too", out)


def run_until_it_waits(cmd, marker, answer="\n", wait=4.0):
    """Starts a command, waits until its output shows the marker and then `wait` more seconds, and
    reports whether it is still running (waiting for a key). Then answers it. Returns (still running,
    exit code, output)."""
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    chunks = []

    def read():
        for line in iter(proc.stdout.readline, b""):
            chunks.append(line)
    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    deadline = time.time() + 120
    while time.time() < deadline and proc.poll() is None and marker.encode() not in b"".join(chunks):
        time.sleep(0.2)
    time.sleep(wait)
    waiting = proc.poll() is None
    try:
        if answer:
            proc.stdin.write(answer.encode())
        proc.stdin.close()
        proc.wait(timeout=30)
    except Exception:
        proc.kill()
        proc.wait()
    reader.join(5)
    return waiting, proc.returncode, b"".join(chunks).decode("utf-8", errors="replace")


def start_fake_game_windows(bin_folder):
    """Makes H5_Game.exe in the folder a copy of cmd.exe and starts it (it waits a minute for
    nothing), so that Windows lists a program called H5_Game that runs from that folder."""
    exe = Path(bin_folder) / "H5_Game.exe"
    shutil.copyfile(Path(os.environ["SystemRoot"]) / "System32" / "cmd.exe", exe)
    proc = subprocess.Popen([str(exe), "/c", "ping -n 60 127.0.0.1 >nul"],
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)

    def stop():
        proc.kill()
        proc.wait()
        time.sleep(0.3)
        exe.write_bytes(b"fake H5_Game.exe")
    return stop


def start_fake_game_linux(proc_dir):
    """A process whose command line names H5_Game.exe, as the game's process does under Wine. The
    scripts read H5_TOGETHER_PROC_DIR (set for all Linux tests, an empty folder to begin with)
    instead of /proc, so this is a folder 4242/cmdline holding that command line: nothing real runs
    (a real fake process did not always vanish from Git Bash's /proc when killed)."""
    def start(bin_folder):
        f = Path(proc_dir) / "4242" / "cmdline"
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_bytes(b"Z:\\home\\player\\game\\bin\\H5_Game.exe\0")

        def stop():
            f.unlink()
            f.parent.rmdir()
        return stop
    return start


# ---------------------------------------------------------------- the bug report script

# What the report may hold: these four files and system.txt, nothing else.
REPORT_FILES = ("h5-together.log", "h5-together.old.log", "h5-together.ini", "h5-together-skins.ini")
RC19K_DOC = REPO / "docs" / "plans" / "mmh55-rc19k.md"


def package_version(pkg):
    """The version the package's dinput8.dll says in its text, like 0.2.7."""
    return re.search(rb"H5 Together hook DLL (\S+) loaded", (Path(pkg) / "dinput8.dll").read_bytes()).group(1).decode("ascii")


def make_report_game(pkg, library, add_on=True, pak_version="19k"):
    """A fake game folder for the bug report tests. Returns its bin folder.

    With add_on: our dinput8.dll, a log, an older log, the settings and the skins file. Always
    MMH5.5's version pak, a 55u.dll, and things that must NOT end up in a report (a save, a map,
    an old copy of the settings, another file in bin)."""
    b = make_game(library)
    root = b.parent
    if add_on:
        shutil.copy(Path(pkg) / "dinput8.dll", b / "dinput8.dll")
        shutil.copy(Path(pkg) / "h5-together.ini", b / "h5-together.ini")
        (b / "h5-together.log").write_bytes(b"       0 [ 1234] H5 Together hook DLL test loaded\n    1000 [ 1234] a line of the log\n")
        (b / "h5-together.old.log").write_bytes(b"       0 [ 4321] an older log\n")
        (b / "h5-together-skins.ini").write_bytes(b"[skins]\nhero=1\n")
    (b / "55u.dll").write_bytes(b"pretend 55u.dll")
    (b / "other-mod.asi").write_bytes(b"pretend asi")
    (b / "secret.txt").write_bytes(b"not for the report")
    (b / "h5-together.ini.bak").write_bytes(b"an old copy of the settings")
    (root / "Profiles").mkdir(exist_ok=True)
    (root / "Profiles" / "game1.sav").write_bytes(b"a save")
    (root / "Maps").mkdir(exist_ok=True)
    (root / "Maps" / "x.h5m").write_bytes(b"a map")
    (root / "data").mkdir(exist_ok=True)
    with zipfile.ZipFile(root / "data" / "MMH55-Index.pak", "w") as z:
        z.writestr("scripts/Other.lua", "-- not it\n")
        if pak_version:
            z.writestr("scripts/H55-Version.lua", f'H55_Version = "{pak_version}"\n-- more\n')
    return b


def read_report(path):
    """{name: bytes} of a .zip or .tar.gz report."""
    path = Path(path)
    if path.name.endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            return {i.filename: z.read(i) for i in z.infolist()}
    import tarfile
    with tarfile.open(path) as t:
        return {m.name: t.extractfile(m).read() for m in t.getmembers() if m.isfile()}


def report_files(folder, prefix="h5-together-bug-report-"):
    return sorted(p for p in Path(folder).iterdir() if p.name.startswith(prefix)) if Path(folder).is_dir() else []


def bug_report_package_checks(pkg, zip_path):
    print("\nBug report script in the package")
    with zipfile.ZipFile(zip_path) as z:
        info = {i.filename.split("/", 1)[1]: i for i in z.infolist() if "/" in i.filename}
    check({"bug-report.bat", "bug-report.ps1", "bug-report.sh"} <= set(info), "bug report: the 3 files are in the zip")
    check(((info["bug-report.sh"].external_attr >> 16) & 0o777) == 0o755, "bug report: bug-report.sh is executable in the zip")
    for name in ("bug-report.bat", "bug-report.ps1"):
        raw = (pkg / name).read_bytes()
        check(raw.count(b"\r\n") == raw.count(b"\n") and raw.count(b"\n") > 0 and all(b < 0x80 for b in raw), f"bug report: {name} is plain ASCII with CRLF line endings")
    raw = (pkg / "bug-report.sh").read_bytes()
    check(b"\r" not in raw and all(b < 0x80 for b in raw), "bug report: bug-report.sh is plain ASCII with LF line endings")
    readme = (pkg / "README.txt").read_text(encoding="ascii")
    check("bug-report.bat" in readme and "bug-report.sh" in readme, "bug report: README.txt tells how to make one")
    # the 55u.dll hashes that name an MMH5.5 version come from the notes in docs/plans/mmh55-rc19k.md
    row = next((l for l in RC19K_DOC.read_text(encoding="utf-8").splitlines() if "`bin\\55u.dll`" in l), "")
    hashes = re.findall(r"`([0-9a-f]{64})`", row)
    check(len(hashes) == 2, "bug report: docs/plans/mmh55-rc19k.md still lists the 55u.dll hashes of RC19d and RC19k", row)
    if len(hashes) == 2:
        forms = {"ps1": '{h}" = "{v}"', "sh": '{h}) echo "{v}"'}
        for script in ("bug-report.ps1", "bug-report.sh", "install.ps1", "install.sh", "uninstall.ps1", "uninstall.sh"):
            text = (pkg / script).read_text(encoding="ascii")
            form = forms[script.rsplit(".", 1)[1]]
            check(form.format(h=hashes[0], v="RC19d") in text and form.format(h=hashes[1], v="RC19k") in text,
                  f"MMH5.5 versions: {script} names 55u.dll {hashes[0][:8]}... RC19d and {hashes[1][:8]}... RC19k")
        for script in ("install.sh", "uninstall.sh", "bug-report.sh"):
            check('KNOWN_55U_NAMES="RC19d and RC19k"' in (pkg / script).read_text(encoding="ascii"),
                  f"MMH5.5 versions: {script} lists the tested versions (RC19d and RC19k) in its warning")


def system_text(files):
    return files["system.txt"].decode("utf-8-sig").replace("\r\n", "\n")


def bug_report_tests(who, pkg, work, run_report, fmt=".zip", home_short=False, no_dump="-NoDump", known_flag=""):
    """The bug report script against fake game folders.

    run_report(bin_folder, out_folder, *extra) runs the script and returns (exit code, output).
    fmt: ".zip", or ".tar.gz" when the script is told to make that (a tuple: either).
    home_short: the game folder is below the script's $HOME, so system.txt writes it as ~/...
    no_dump: the script's option that leaves the crash report's .dmp out.
    known_flag: the script's test option that counts a 55u.dll as a known MMH5.5 version."""
    print(f"\nBug report script ({who})")
    version = package_version(pkg)
    shutil.rmtree(work, ignore_errors=True)  # (so that a second run in the same --work-dir counts the reports again)
    out = work / "reports"
    out.mkdir(parents=True, exist_ok=True)
    b = make_report_game(pkg, work / "report lib")
    sources = {n: (b / n).read_bytes() for n in REPORT_FILES}

    code, text = run_report(b, out)
    made = report_files(out)
    check(code == 0 and len(made) == 1 and made[0].name.endswith(fmt), f"bug report: exit 0 and one {fmt} file", text)
    if len(made) != 1:
        return
    files = read_report(made[0])
    check(sorted(files) == sorted(list(REPORT_FILES) + ["system.txt"]),
          "bug report: holds the log, the older log, the settings, the skins file and system.txt, and nothing else (no save, no map, no .bak)", str(sorted(files)))
    check(all(files.get(n) == sources[n] for n in REPORT_FILES), "bug report: the four files are copied byte for byte")
    check(str(made[0]) in text.replace("\r", "") or made[0].name in text, "bug report: says where the file is", text)
    check("Nothing was sent anywhere" in text and "player names" in text, "bug report: says it sends nothing, and that the log has player names", text)
    check("Your game" in text and "MMH5.5:     a version we have not tested (its version file says 19k)" in text,
          "bug report: shows what it found of the game (the same screen as the installer), MMH5.5's version file included", text)
    s = system_text(files)
    check(f"dinput8.dll in the game folder: H5 Together {version}" in s, "bug report: system.txt names the add-on version in the game folder", s)
    check(f"the package this script came from: H5 Together {version}" in s, "bug report: system.txt names the version of the package it came from")
    check('H55_Version in data' in s and s.split("H55_Version in data", 1)[1].split("\n", 1)[0].strip().endswith("19k"),
          "bug report: system.txt has MMH5.5's H55_Version (read from the zip MMH55-Index.pak)", s)
    check(f"55u.dll: a version we have not tested (sha256 {sha(b / '55u.dll')})" in s, "bug report: system.txt names 55u.dll with its hash, and says when it is not a tested version", s)
    exes_ok = all(re.search(rf"^\s+{re.escape(n)}\s+{(b / n).stat().st_size} bytes\b.*{sha(b / n)}", s, re.M) for n in ("H5_Game.exe", "MMH55_Utility_64.exe"))
    check(exes_ok, "bug report: system.txt lists the game exes with size and sha256", s)
    check(re.search(r"^\s+55u\.dll\s+15$", s, re.M) and re.search(r"^\s+other-mod\.asi\s+11$", s, re.M) and re.search(r"^\s+dinput8\.dll\s+\d+$", s, re.M),
          "bug report: system.txt lists the other .dll and .asi files in bin", s)
    check("secret.txt" not in s and "game1.sav" not in s, "bug report: system.txt does not mention other files")
    if home_short:
        check("game folder: ~/report lib/steamapps/common/" in s, "bug report: system.txt writes the home folder as ~", s)
    user = os.environ.get("USERNAME") or os.environ.get("USER") or ""
    check(len(user) < 4 or user not in s, "bug report: system.txt does not carry the user name", s)

    # a 55u.dll that is a known MMH5.5 version is named in the report
    known_out = work / "reports known"
    code, text = run_report(b, known_out, known_flag, f"{sha(b / '55u.dll')}=RC19k")
    made = report_files(known_out)
    check(code == 0 and made and f"55u.dll: RC19k (sha256 {sha(b / '55u.dll')})" in system_text(read_report(made[0]))
          and "MMH5.5:     RC19k (tested with H5 Together)" in text,
          "bug report: a known 55u.dll is named (RC19k) in system.txt and on the found screen", text)

    # a second report in the same minute replaces the first; a new one never touches the game folder
    code, text = run_report(b, out)
    check(code == 0 and len(report_files(out)) <= 2, "bug report: running it again works", text)
    check(all((b / n).read_bytes() == sources[n] for n in REPORT_FILES) and (b / "dinput8.dll").exists(),
          "bug report: the game folder is left as it was")

    # no add-on installed, no log: a report with only the facts, and it says so
    b2 = make_report_game(pkg, work / "report lib 2", add_on=False)
    out2 = work / "reports2"
    code, text = run_report(b2, out2)
    made = report_files(out2)
    check(code == 0 and len(made) == 1 and sorted(read_report(made[0])) == ["system.txt"], "bug report: without the add-on or a log: only system.txt", text)
    check("There is no h5-together.log" in text, "bug report: without a log it says so", text)
    if made:
        s = system_text(read_report(made[0]))
        check("none (the add-on is not installed here)" in s and "there is no h5-together.log" in s, "bug report: system.txt says the add-on is not installed", s)

    # an install of 0.2.7, not updated yet: its files under the old name (open-homm5)
    b7 = make_report_game(pkg, work / "report lib 7", add_on=False)
    old_files = {"open-homm5.log": b"       0 [ 1234] open-homm5 hook DLL 0.2.7 loaded\n", "open-homm5.old.log": b"an older log\n",
                 "open-homm5.ini": V027_INI, "open-homm5-skins.ini": V027_SKINS}
    (b7 / "dinput8.dll").write_bytes(V027_DLL)
    for name, data in old_files.items():
        (b7 / name).write_bytes(data)
    out7 = work / "reports7"
    code, text = run_report(b7, out7)
    made = report_files(out7)
    files = read_report(made[0]) if made else {}
    check(code == 0 and sorted(files) == sorted(list(old_files) + ["system.txt"]) and all(files[n] == d for n, d in old_files.items()),
          "bug report: a 0.2.7 install: its four files under the old name are packed, byte for byte", text)
    check("There is no" not in text and "dinput8.dll in the game folder: open-homm5 0.2.7" in (system_text(files) if files else ""),
          "bug report: a 0.2.7 install: its log counts, and system.txt names the add-on by its old name and version", text)

    # another mod's dinput8.dll
    b3 = make_report_game(pkg, work / "report lib 3", add_on=False)
    (b3 / "dinput8.dll").write_bytes(b"MZ some other mod")
    out3 = work / "reports3"
    code, text = run_report(b3, out3)
    made = report_files(out3)
    check(code == 0 and made and "NOT the H5 Together one" in system_text(read_report(made[0])) and (b3 / "dinput8.dll").read_bytes() == b"MZ some other mod",
          "bug report: another mod's dinput8.dll is named as such and left alone", text)

    # crash reports of the add-on: the newest .txt and its .dmp go in (not the older ones, not a robot copy's)
    b8 = make_report_game(pkg, work / "report lib 8")
    crash = {"h5-together-crash-20261030-101500.txt": b"an old crash", "h5-together-crash-20261030-101500.dmp": b"MDMP old",
             "h5-together-crash-20261102-153012.txt": b"H5 Together crash report\r\nwhat: exception c0000005\r\n",
             "h5-together-crash-20261102-153012.dmp": b"MDMP\x93\xa7 the newest dump",
             "h5-together-host-crash-20261103-000000.txt": b"a robot copy's", "h5-together-crash-notes.txt": b"not a report"}
    for name, data in crash.items():
        (b8 / name).write_bytes(data)
    newest = ["h5-together-crash-20261102-153012.txt", "h5-together-crash-20261102-153012.dmp"]
    out8 = work / "reports8"
    code, text = run_report(b8, out8)
    made = report_files(out8)
    files = read_report(made[0]) if made else {}
    check(code == 0 and sorted(files) == sorted(list(REPORT_FILES) + newest + ["system.txt"]) and all(files[n] == crash[n] for n in newest),
          "bug report: the newest crash report (.txt and .dmp) goes in, byte for byte, and no older or other one", str(sorted(files)))
    s8 = system_text(files) if files else ""
    check("2 kept; the newest is in this zip: h5-together-crash-20261102-153012.txt and h5-together-crash-20261102-153012.dmp" in s8,
          "bug report: system.txt says which crash report is in it", s8)
    check("newest crash report too" in text and "part of the game's memory" in text and no_dump in text,
          "bug report: says it holds the crash report, that the .dmp holds game memory, and how to leave it out", text)
    out9 = work / "reports9"
    code, text = run_report(b8, out9, no_dump)
    made = report_files(out9)
    files = read_report(made[0]) if made else {}
    check(code == 0 and sorted(files) == sorted(list(REPORT_FILES) + [newest[0], "system.txt"])
          and "(its .dmp left out: " + no_dump + ")" in (system_text(files) if files else ""),
          f"bug report: {no_dump} leaves the .dmp out (the .txt stays) and system.txt says so", str(sorted(files)))
    check(all((b8 / n).read_bytes() == d for n, d in crash.items()), "bug report: the crash reports stay in the game folder")
    s0 = system_text(read_report(report_files(out)[0])) if report_files(out) else ""
    check("Crash reports of the add-on" in s0 and "\n  none\n" in s0.split("Crash reports of the add-on", 1)[-1][:120],
          "bug report: without crash reports system.txt says none", s0)

    # MMH5.5 not there, or a pak without the version script
    b4 = make_report_game(pkg, work / "report lib 4", pak_version=None)
    out4 = work / "reports4"
    code, text = run_report(b4, out4)
    made = report_files(out4)
    check(code == 0 and made and "could not read the version" in system_text(read_report(made[0])), "bug report: a pak without H55-Version.lua: says it could not read the version", text)
    b5 = make_report_game(pkg, work / "report lib 5")
    (b5.parent / "data" / "MMH55-Index.pak").unlink()
    out5 = work / "reports5"
    code, text = run_report(b5, out5)
    made = report_files(out5)
    check(code == 0 and made and "MMH55-Index.pak: not found" in system_text(read_report(made[0])), "bug report: no MMH55-Index.pak: says MMH5.5 was not found", text)

    # not a game folder: nothing is made
    out6 = work / "reports6"
    code, text = run_report(work / "nope", out6)
    check(code == 4 and not report_files(out6), "bug report: not a game folder: exit 4, no file", text)


def hold_open_like_the_game(path):
    """Opens a file the way the game holds its log (for appending, others may read but not write).
    Returns a function that closes it."""
    import ctypes
    from ctypes import wintypes
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    k.CreateFileW.restype = wintypes.HANDLE
    k.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                              wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    k.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = k.CreateFileW(str(path), 0x4, 0x1, None, 3, 0x80, None)  # FILE_APPEND_DATA, FILE_SHARE_READ, OPEN_EXISTING
    if handle in (None, wintypes.HANDLE(-1).value):
        raise OSError(f"cannot open {path} the way the game does (error {ctypes.get_last_error()})")
    return lambda: k.CloseHandle(handle)


def bug_report_windows_extra(pkg, work, run_report):
    """What only Windows can show: a log in use, the Desktop, an output folder that cannot be made."""
    print("\nBug report script (Windows only)")
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True, exist_ok=True)
    b = make_report_game(pkg, work / "lib")

    # the game is running: its log is open and others may only read it
    close = hold_open_like_the_game(b / "h5-together.log")
    try:
        out = work / "running"
        code, text = run_report(b, out)
    finally:
        close()
    made = report_files(out)
    check(code == 0 and len(made) == 1 and read_report(made[0]).get("h5-together.log") == (b / "h5-together.log").read_bytes(),
          "bug report: a log that the running game holds open is read and packed", text)

    # no -OutDir: on the Desktop (also when OneDrive moved it), else in the home folder
    code, desktop = run(["powershell", "-NoProfile", "-Command",
                         "$p = [Environment]::GetFolderPath('Desktop'); if (-not (Test-Path -LiteralPath $p -PathType Container)) { $p = [Environment]::GetFolderPath('UserProfile') }; $p"])
    desktop = Path(desktop.strip())
    code, text = run(bat(str(pkg / "bug-report.bat"), "-NoPause", "-GameDir", str(b)))
    m = re.search(r"this file:\r?\n\s+(.+?\.zip)\s*\r?\n", text)
    made = Path(m.group(1)) if m else None
    try:
        check(code == 0 and made is not None and made.is_file() and made.parent.resolve() == desktop.resolve()
              and re.fullmatch(r"h5-together-bug-report-\d{4}-\d\d-\d\d-\d{4}\.zip", made.name) is not None,
              f"bug report: without -OutDir the file lands on the Desktop ({desktop}), named h5-together-bug-report-<date>.zip", text)
    finally:
        if made is not None and made.parent.resolve() == desktop.resolve() and made.name.startswith("h5-together-bug-report-"):
            made.unlink(missing_ok=True)

    # an output folder that cannot be made
    blocker = work / "blocker"
    blocker.write_bytes(b"a file, not a folder")
    code, text = run_report(b, blocker / "sub")
    check(code == 5 and "Could not use the folder" in text, "bug report: an output folder that cannot be made: exit 5 and a message", text)


# ---------------------------------------------------------------- Windows

def windows_tests(pkg, work, pkg_steam):
    print("\nWindows scripts (install.bat / uninstall.bat)")
    work.mkdir(parents=True, exist_ok=True)
    # The installer looks for MMH5.5's installer file in Downloads (the original game's 4 GB
    # exe): a fake profile folder, not this PC's real one (whose Downloads may hold it).
    real_profile = os.environ.get("USERPROFILE", "")
    fake_profile = work / "fake profile"
    (fake_profile / "Downloads").mkdir(parents=True, exist_ok=True)
    os.environ["USERPROFILE"] = str(fake_profile)
    # (PowerShell keeps a module cache under the profile; with the fake one it wrote it into
    # the current folder)
    os.environ["PSModuleAnalysisCachePath"] = str(work / "ps module cache")
    steam_only = work / "steam_only"          # Steam folder, game in a second library with accents
    lib2 = work / "Bibliotheque \u00c9t\u00e9 Steam"
    steam_only.mkdir()
    make_steam(steam_only, [steam_only, lib2])
    bin2 = make_game(lib2)

    # (-TestRegistry names a key that does not exist, so that the GOG and MMH5.5 entries that this
    # PC may really have in HKLM are not looked at; the tests of those pass a key of their own)
    no_registry = "HKCU:\\Software\\H5TogetherTest-none"

    def install(*args, stdin=""):
        extra = [] if "-TestRegistry" in args else ["-TestRegistry", no_registry]
        return run(bat(str(pkg / "install.bat"), "-NoPause", *extra, *args), stdin)

    def uninstall(*args, stdin=""):
        extra = [] if "-TestRegistry" in args else ["-TestRegistry", no_registry]
        return run(bat(str(pkg / "uninstall.bat"), "-NoPause", *extra, *args), stdin)

    our_dll = (pkg / "dinput8.dll").read_bytes()
    our_ini = (pkg / "h5-together.ini").read_bytes()
    before = snapshot(bin2)

    # fresh install, game found through the fake libraryfolders.vdf
    code, out = install("-SteamDir", str(steam_only))
    check(code == 0, "fresh install: exit 0", out)
    check((bin2 / "dinput8.dll").read_bytes() == our_dll, "fresh install: dinput8.dll copied")
    check((bin2 / "h5-together.ini").read_bytes() == our_ini, "fresh install: h5-together.ini copied")
    after = snapshot(bin2)
    check({k: v for k, v in after.items() if k in before} == before, "fresh install: game files unchanged")
    check(set(after) - set(before) == {"dinput8.dll", "h5-together.ini"}, "fresh install: only our 2 files added")
    check("h5-together.log" in out and "added dinput8.dll" in out, "fresh install: says what it did and how to check", out)
    check(sorted(p.name for p in (bin2 / "h5-together" / "templates").iterdir()) == sorted(p.name for p in template_dir(pkg).iterdir()),
          "fresh install: the templates are installed in bin/h5-together/templates")

    # install again over our own DLL
    code, out = install("-SteamDir", str(steam_only))
    check(code == 0 and "up to date" in out, "re-install over our DLL: exit 0, says up to date", out)
    (bin2 / "dinput8.dll").write_bytes(OURS_MARK + b" older build")
    code, out = install("-SteamDir", str(steam_only))
    check(code == 0 and "replaced the older" in out, "re-install over an older build of ours: replaced", out)
    check((bin2 / "dinput8.dll").read_bytes() == our_dll, "re-install: new DLL in place")

    # an existing ini is refreshed: this version's file, with the player's values
    (bin2 / "h5-together.ini").write_bytes(b"[net]\r\ntuning=0\r\n")
    code, out = install("-SteamDir", str(steam_only))
    check(code == 0 and (bin2 / "h5-together.ini").read_bytes() == our_ini.replace(b"\r\n;tuning=1\r\n", b"\r\ntuning=0\r\n")
          and (bin2 / "h5-together.ini.bak").read_bytes() == b"[net]\r\ntuning=0\r\n",
          "existing h5-together.ini: refreshed, the player's value kept, the old file in .bak", out)
    check("settings file refreshed: 1 of your settings kept" in out, "existing ini: says it refreshed it", out)

    # a foreign dinput8.dll is refused
    (bin2 / "dinput8.dll").write_bytes(b"MZ some other mod's dinput8")
    code, out = install("-SteamDir", str(steam_only))
    check(code == 3 and "Stopped" in out, "foreign dinput8.dll: refused with exit 3", out)
    check("dinput8_next.dll" in out, "foreign dinput8.dll: says how to keep it (rename it dinput8_next.dll)", out)
    check((bin2 / "dinput8.dll").read_bytes() == b"MZ some other mod's dinput8", "foreign dinput8.dll: left untouched")
    code, out = uninstall("-SteamDir", str(steam_only), stdin="y\n")
    check(code == 0 and "NOT the H5 Together one" in out, "uninstall leaves a foreign dinput8.dll alone", out)
    check((bin2 / "dinput8.dll").read_bytes() == b"MZ some other mod's dinput8", "uninstall: foreign dll still there")
    # the other mod renamed dinput8_next.dll (chain-loading): installed beside it, and the uninstaller says how to put it back
    (bin2 / "dinput8.dll").rename(bin2 / "dinput8_next.dll")
    code, out = install("-SteamDir", str(steam_only))
    check(code == 0 and (bin2 / "dinput8_next.dll").read_bytes() == b"MZ some other mod's dinput8", "dinput8_next.dll: installed beside it, untouched", out)
    code, out = uninstall("-SteamDir", str(steam_only), stdin="y\n")
    check(code == 0 and "rename it back to dinput8.dll" in out and (bin2 / "dinput8_next.dll").exists(),
          "uninstall with dinput8_next.dll: left alone, and says to rename it back", out)
    (bin2 / "dinput8_next.dll").unlink()
    (bin2 / "h5-together.ini").unlink(missing_ok=True)

    # uninstall: ours removed, log removed, ini only after asking
    code, out = install("-SteamDir", str(steam_only))
    (bin2 / "h5-together.log").write_bytes(b"log")
    (bin2 / "h5-together.old.log").write_bytes(b"older log")
    code, out = uninstall("-SteamDir", str(steam_only), stdin="n\n")
    check(code == 0, "uninstall (answer n): exit 0", out)
    check(not (bin2 / "dinput8.dll").exists() and not (bin2 / "h5-together.log").exists() and not (bin2 / "h5-together.old.log").exists(),
          "uninstall: dinput8.dll, h5-together.log and the old log (h5-together.old.log) removed")
    check((bin2 / "h5-together.ini").exists(), "uninstall (answer n): ini kept")
    code, out = uninstall("-SteamDir", str(steam_only), stdin="y\n")
    check(code == 0 and not (bin2 / "h5-together.ini").exists(), "uninstall (answer y): ini removed", out)
    check(snapshot(bin2) == before, "uninstall: folder back to exactly how it was")
    check(not (bin2 / "h5-together").exists(), "uninstall: the (empty) h5-together folder is gone too")

    # the game folder or its bin folder can be given directly
    game_folder = bin2.parent
    code, out = install("-GameDir", str(game_folder))
    check(code == 0 and (bin2 / "dinput8.dll").exists(), "-GameDir <game folder> works", out)
    uninstall("-GameDir", str(bin2), stdin="y\n")
    code, out = install("-GameDir", str(bin2))
    check(code == 0 and (bin2 / "dinput8.dll").exists(), "-GameDir <bin folder> works", out)
    uninstall("-GameDir", str(bin2), stdin="y\n")
    code, out = install("-GameDir", str(work / "nope"))
    check(code == 4 and "not the game folder" in out, "-GameDir with a wrong folder: exit 4", out)

    # no game found: asks, accepts a pasted path with quotes, or cancels
    empty_steam = work / "empty_steam"
    make_steam(empty_steam, [empty_steam])
    # (an ASCII path with a space: piped input is not read as Unicode, unlike a real console)
    plain_bin = make_game(work / "plain lib")
    code, out = install("-SteamDir", str(empty_steam), stdin=f'"{plain_bin.parent}"\n')
    check(code == 0 and (plain_bin / "dinput8.dll").exists() and "could not find the game" in out,
          "no game found: asks for the folder, accepts a quoted path", out)
    uninstall("-GameDir", str(plain_bin), stdin="y\n")
    code, out = install("-SteamDir", str(empty_steam), stdin="\n")
    check(code == 4 and not (plain_bin / "dinput8.dll").exists(), "no game found, Enter alone: cancelled, exit 4", out)
    code, out = install("-SteamDir", str(empty_steam), stdin="")
    check(code == 4, "no game found, no input at all: cancelled, exit 4", out)

    # two games found: numbered list
    lib3 = work / "lib3"
    bin3 = make_game(lib3)
    two = work / "two_games"
    make_steam(two, [lib2, lib3])
    code, out = install("-SteamDir", str(two), stdin="2\n")
    check(code == 0 and (bin3 / "dinput8.dll").exists() and not (bin2 / "dinput8.dll").exists(),
          "two games: picking number 2 installs into the second only", out)
    check("several places" in out, "two games: shows the list")
    uninstall("-GameDir", str(bin3), stdin="y\n")

    # old-style vdf ("1" "D:\\path") and single-backslash tolerance
    old = work / "old_steam"
    (old / "steamapps").mkdir(parents=True)
    (old / "steamapps" / "libraryfolders.vdf").write_bytes(
        ('"LibraryFolders"\n{\n\t"TimeNextStatsReport"\t\t"1"\n\t"1"\t\t"' + str(lib3).replace("\\", "\\\\") + '"\n}\n').encode())
    code, out = install("-SteamDir", str(old))
    check(code == 0 and (bin3 / "dinput8.dll").exists(), "old-style libraryfolders.vdf is read", out)
    uninstall("-GameDir", str(bin3), stdin="y\n")

    # no libraryfolders.vdf at all, game in Steam's own folder
    bare = work / "bare_steam"
    bin_bare = make_game(bare)
    code, out = install("-SteamDir", str(bare))
    check(code == 0 and (bin_bare / "dinput8.dll").exists(), "no libraryfolders.vdf: game in Steam's own folder is found", out)
    uninstall("-GameDir", str(bin_bare), stdin="y\n")

    # only H5_Game.exe: installs, but warns that MMH5.5 is missing
    lib4 = work / "lib4"
    bin4 = make_game(lib4, exes=("H5_Game.exe",))
    code, out = install("-GameDir", str(bin4))
    check(code == 0 and "no MMH55_Utility_64.exe" in out, "game without MMH5.5: installs with a note", out)

    # install.bat alone (as when double-clicked inside the zip): explains, installs nothing
    lone = work / "lone"
    lone.mkdir()
    shutil.copy(pkg / "install.bat", lone)
    code, out = run(bat(str(lone / "install.bat")), stdin="\n")
    check(code == 7 and "extract the whole zip" in out, "install.bat without install.ps1: asks to extract the zip", out)
    shutil.copy(pkg / "install.ps1", lone)
    code, out = run(bat(str(lone / "install.bat"), "-NoPause", "-GameDir", str(bin2)), stdin="")
    check(code == 7 and not (bin2 / "dinput8.dll").exists(), "install.ps1 without dinput8.dll: exit 7, nothing installed", out)

    # the package extracted into a folder with spaces and parentheses
    odd = work / "Pack (x86) & more"
    shutil.copytree(pkg, odd)
    code, out = run(bat(str(odd / "install.bat"), "-NoPause", "-GameDir", str(bin2)))
    check(code == 0 and (bin2 / "dinput8.dll").exists(), "package in a folder with spaces and parentheses", out)
    code, out = run(bat(str(odd / "uninstall.bat"), "-NoPause", "-GameDir", str(bin2)), "y\n")
    check(code == 0 and not (bin2 / "dinput8.dll").exists(), "uninstall from that folder", out)

    # the ZeroTier hint (only looks at the network profiles; here faked)
    code, out = install("-GameDir", str(bin2), "-TestProfiles", 'ZeroTier One [abc]|Public;Ethernet|Private')
    check(code == 0 and "Heads-up" in out and "Private" in out and "I did not change anything in Windows" in out,
          "ZeroTier on a Public network: explains how to make it Private", out)
    uninstall("-GameDir", str(bin2), stdin="y\n")
    code, out = install("-GameDir", str(bin2), "-TestProfiles", 'ZeroTier One [abc]|Private;Ethernet|Public')
    check(code == 0 and "Heads-up" not in out, "ZeroTier on a Private network: no hint", out)
    uninstall("-GameDir", str(bin2), stdin="y\n")

    # the settings file: refreshed, values kept, backups
    ini_bin = make_game(work / "ini lib")
    results = settings_tests("Windows", pkg, ini_bin,
                             lambda: install("-GameDir", str(ini_bin)),
                             lambda answer: uninstall("-GameDir", str(ini_bin), stdin=answer))

    # an install over open-homm5 0.2.7 (the old name): the files get the new names
    old_bin = make_game(work / "old name lib")
    results.update(migration_tests("Windows", pkg, old_bin,
                                   lambda: install("-GameDir", str(old_bin)),
                                   lambda answer: uninstall("-GameDir", str(old_bin), stdin=answer)))

    # random map templates: install, a player's own files, reinstall, uninstall
    tpl_bin = make_game(work / "tpl lib")
    template_tests("Windows", pkg, tpl_bin,
                   lambda: install("-GameDir", str(tpl_bin)),
                   lambda: uninstall("-GameDir", str(tpl_bin), stdin="y\n"))
    txt_bin = make_game(work / "txt lib")
    texts_tests("Windows", pkg, txt_bin,
                lambda: install("-GameDir", str(txt_bin)),
                lambda: uninstall("-GameDir", str(txt_bin), stdin="y\n"))

    # Valve's steam_api.dll: from a package that has it (a fake one), and from one that has not
    if pkg_steam:
        steam_bin = make_game(work / "valve dll lib")  # (no "steam_api" in the path: the tests look for that word)
        steam_api_tests("Windows", pkg, pkg_steam, steam_bin,
                        lambda p: run(bat(str(p / "install.bat"), "-NoPause", "-TestRegistry", no_registry, "-GameDir", str(steam_bin))),
                        lambda p: run(bat(str(p / "uninstall.bat"), "-NoPause", "-TestRegistry", no_registry, "-GameDir", str(steam_bin)), "y\n"))

    # a package without its templates folder is incomplete: exit 7, nothing installed
    no_tpl = work / "pkg without templates"
    shutil.copytree(pkg, no_tpl, ignore=shutil.ignore_patterns("h5-together"))
    code, out = run(bat(str(no_tpl / "install.bat"), "-NoPause", "-GameDir", str(tpl_bin)))
    check(code == 7 and "extract the whole zip" in out and not (tpl_bin / "dinput8.dll").exists() and not (tpl_bin / "h5-together").exists(),
          "package without the templates folder: exit 7, nothing installed", out)
    no_txt = work / "pkg without texts"
    shutil.copytree(pkg, no_txt, ignore=shutil.ignore_patterns("texts"))
    code, out = run(bat(str(no_txt / "install.bat"), "-NoPause", "-GameDir", str(tpl_bin)))
    check(code == 7 and "h5-together\\texts" in out and not (tpl_bin / "dinput8.dll").exists() and not (tpl_bin / "h5-together").exists(),
          "package without the language files folder: exit 7, nothing installed", out)

    # what the scripts find and say: the found screen, MMH5.5 and its version, warnings, several
    # games, a game that is running; then what only Windows has (GOG, the folder window, the pause)
    game_tests("Windows", pkg, work / "game", install, uninstall,
               {"game": "-GameDir", "steam": "-SteamDir", "known": "-TestKnown55u", "mmh55": "-Mmh55Installer"}, str, start_fake_game_windows)
    windows_only_game_tests(pkg, work / "game windows", install, uninstall)

    # the bug report script
    def run_report(game_bin, out_dir, *extra):
        return run(bat(str(pkg / "bug-report.bat"), "-NoPause", "-TestRegistry", no_registry,
                       "-GameDir", str(game_bin), "-OutDir", str(out_dir), *extra))

    os.environ["USERPROFILE"] = real_profile  # (the bug report's checks use the real profile)
    bug_report_tests("Windows", pkg, work / "bug report", run_report, known_flag="-TestKnown55u")
    bug_report_windows_extra(pkg, work / "bug report extra", run_report)

    # PowerShell 5.1 specifically (the script must run there)
    code, out = run(["powershell", "-NoProfile", "-Command", "$PSVersionTable.PSVersion.ToString()"])
    check(out.strip().startswith("5."), f"tests ran on Windows PowerShell 5.x (found {out.strip()})")
    return results


# ---------------------------------------------------------------- Linux

def posix(p):
    return Path(p).as_posix()


def linux_tests(pkg, work, bash, pkg_steam):
    print(f"\nLinux scripts (install.sh / uninstall.sh) with {bash}")
    work.mkdir(parents=True, exist_ok=True)
    # what the scripts see as the running programs: an empty folder (so a real game on this PC does not matter)
    proc_dir = work / "proc"
    proc_dir.mkdir(exist_ok=True)
    os.environ["H5_TOGETHER_PROC_DIR"] = posix(proc_dir)
    home = work / "home"
    steam = home / ".local" / "share" / "Steam"
    flat = home / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam"
    lib2 = work / "SteamLibrary"
    home.mkdir()
    steam.mkdir(parents=True)
    make_steam(steam, [steam, lib2], escape_backslashes=False)
    # fix the paths in the vdf to forward slashes (they are plain paths on Linux)
    vdf = steam / "steamapps" / "libraryfolders.vdf"
    vdf.write_bytes(vdf.read_bytes().replace(str(steam).encode(), posix(steam).encode()).replace(str(lib2).encode(), posix(lib2).encode()))
    bin2 = make_game(lib2)
    before = snapshot(bin2)
    env = dict(os.environ, HOME=posix(home))

    def sh(script, *args, stdin=""):
        return run([bash, str(pkg / script), *args], stdin, env=env)

    check(run([bash, "-n", str(pkg / "install.sh")])[0] == 0, "bash -n install.sh")
    check(run([bash, "-n", str(pkg / "uninstall.sh")])[0] == 0, "bash -n uninstall.sh")
    check(b"\r" not in (pkg / "install.sh").read_bytes() and b"\r" not in (pkg / "uninstall.sh").read_bytes(),
          "shell scripts have LF line endings")

    our_dll = (pkg / "dinput8.dll").read_bytes()
    code, out = sh("install.sh")
    check(code == 0, "fresh install (Steam in ~/.local/share/Steam): exit 0", out)
    check((bin2 / "dinput8.dll").read_bytes() == our_dll and (bin2 / "h5-together.ini").exists(), "fresh install: files copied")
    after = snapshot(bin2)
    check({k: v for k, v in after.items() if k in before} == before and set(after) - set(before) == {"dinput8.dll", "h5-together.ini"},
          "fresh install: game files unchanged, only our 2 files added")
    check('WINEDLLOVERRIDES="dinput8=n,b" %command%' in out and "MMH55_Utility_64.exe" in out and "h5-together.log" in out,
          "prints the Steam launch option, the exe to start and how to check", out)
    check(sorted(p.name for p in (bin2 / "h5-together" / "templates").iterdir()) == sorted(p.name for p in template_dir(pkg).iterdir()),
          "fresh install: the templates are installed in bin/h5-together/templates")

    code, out = sh("install.sh")
    check(code == 0 and "up to date" in out, "re-install over our DLL: up to date", out)
    (bin2 / "dinput8.dll").write_bytes(OURS_MARK + b" older build")
    code, out = sh("install.sh")
    check(code == 0 and "replaced the older" in out and (bin2 / "dinput8.dll").read_bytes() == our_dll, "re-install over an older build: replaced", out)

    (bin2 / "h5-together.ini").write_bytes(b"[net]\ntuning=0\n")
    code, out = sh("install.sh")
    our_ini = (pkg / "h5-together.ini").read_bytes()
    check(code == 0 and (bin2 / "h5-together.ini").read_bytes() == our_ini.replace(b"\r\n;tuning=1\r\n", b"\r\ntuning=0\r\n")
          and (bin2 / "h5-together.ini.bak").read_bytes() == b"[net]\ntuning=0\n" and "settings file refreshed: 1 of your settings kept" in out,
          "existing ini: refreshed, the player's value kept, the old file in .bak", out)

    (bin2 / "dinput8.dll").write_bytes(b"MZ some other mod")
    code, out = sh("install.sh")
    check(code == 3 and (bin2 / "dinput8.dll").read_bytes() == b"MZ some other mod", "foreign dinput8.dll: refused (exit 3), untouched", out)
    check("dinput8_next.dll" in out, "foreign dinput8.dll: says how to keep it (rename it dinput8_next.dll)", out)
    code, out = sh("uninstall.sh", stdin="y\n")
    check(code == 0 and (bin2 / "dinput8.dll").exists() and "NOT the H5 Together one" in out, "uninstall leaves a foreign dll alone", out)
    (bin2 / "dinput8.dll").rename(bin2 / "dinput8_next.dll")
    code, out = sh("install.sh")
    check(code == 0 and (bin2 / "dinput8_next.dll").read_bytes() == b"MZ some other mod", "dinput8_next.dll: installed beside it, untouched", out)
    code, out = sh("uninstall.sh", stdin="y\n")
    check(code == 0 and "rename it back to dinput8.dll" in out and (bin2 / "dinput8_next.dll").exists(),
          "uninstall with dinput8_next.dll: left alone, and says to rename it back", out)
    (bin2 / "dinput8_next.dll").unlink()
    (bin2 / "h5-together.ini").unlink(missing_ok=True)

    sh("install.sh")
    (bin2 / "h5-together.log").write_bytes(b"log")
    (bin2 / "h5-together.old.log").write_bytes(b"older log")
    code, out = sh("uninstall.sh", stdin="n\n")
    check(code == 0 and not (bin2 / "dinput8.dll").exists() and not (bin2 / "h5-together.log").exists()
          and not (bin2 / "h5-together.old.log").exists() and (bin2 / "h5-together.ini").exists(),
          "uninstall (n): dll, log and old log removed, ini kept", out)
    code, out = sh("uninstall.sh", stdin="y\n")
    check(code == 0 and snapshot(bin2) == before, "uninstall (y): folder back to how it was", out)
    check(not (bin2 / "h5-together").exists(), "uninstall: the (empty) h5-together folder is gone too")

    code, out = sh("install.sh", "--game-dir", posix(bin2.parent))
    check(code == 0 and (bin2 / "dinput8.dll").exists(), "--game-dir <game folder>", out)
    sh("uninstall.sh", "--game-dir", posix(bin2), stdin="y\n")
    code, out = sh("install.sh", "--game-dir", posix(work / "nope"))
    check(code == 4, "--game-dir with a wrong folder: exit 4", out)

    # Flatpak Steam only, and no Steam at all (asks)
    home2 = work / "home2"
    flat2 = home2 / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam"
    lib5 = work / "lib5"
    make_steam(flat2, [lib5], escape_backslashes=False)
    v2 = flat2 / "steamapps" / "libraryfolders.vdf"
    v2.write_bytes(v2.read_bytes().replace(str(lib5).encode(), posix(lib5).encode()))
    bin5 = make_game(lib5)
    env2 = dict(os.environ, HOME=posix(home2))
    code, out = run([bash, str(pkg / "install.sh")], "", env=env2)
    check(code == 0 and (bin5 / "dinput8.dll").exists(), "Flatpak Steam folder is found", out)
    run([bash, str(pkg / "uninstall.sh")], "y\n", env=env2)
    home3 = work / "home3"
    home3.mkdir()
    env3 = dict(os.environ, HOME=posix(home3))
    code, out = run([bash, str(pkg / "install.sh")], f"'{posix(bin5.parent)}'\n", env=env3)
    check(code == 0 and (bin5 / "dinput8.dll").exists() and "could not find the game" in out, "no Steam: asks for the folder (quoted path)", out)
    run([bash, str(pkg / "uninstall.sh"), "--game-dir", posix(bin5)], "y\n", env=env3)
    code, out = run([bash, str(pkg / "install.sh")], "\n", env=env3)
    check(code == 4 and not (bin5 / "dinput8.dll").exists(), "no Steam, Enter alone: cancelled (exit 4)", out)

    # two games: numbered list
    two = work / "home4"
    steam4 = two / ".steam" / "steam"
    lib6 = work / "lib6"
    make_steam(steam4, [lib5, lib6], escape_backslashes=False)
    v4 = steam4 / "steamapps" / "libraryfolders.vdf"
    v4.write_bytes(v4.read_bytes().replace(str(lib5).encode(), posix(lib5).encode()).replace(str(lib6).encode(), posix(lib6).encode()))
    bin6 = make_game(lib6)
    code, out = run([bash, str(pkg / "install.sh")], "2\n", env=dict(os.environ, HOME=posix(two)))
    check(code == 0 and (bin6 / "dinput8.dll").exists() and not (bin5 / "dinput8.dll").exists() and "several places" in out,
          "two games: number 2 installs into the second only", out)

    # no MMH5.5 exe
    lib7 = work / "lib7"
    bin7 = make_game(lib7, exes=("H5_Game.exe",))
    code, out = sh("install.sh", "--game-dir", posix(bin7))
    check(code == 0 and "no MMH55_Utility_64.exe" in out, "game without MMH5.5: installs with a note", out)

    # extracted without the DLL
    lone = work / "lone_sh"
    lone.mkdir()
    shutil.copy(pkg / "install.sh", lone)
    code, out = run([bash, str(lone / "install.sh"), "--game-dir", posix(bin2)], "", env=env)
    check(code == 7 and not (bin2 / "dinput8.dll").exists(), "install.sh without dinput8.dll: exit 7", out)

    # what the scripts find and say: the found screen, MMH5.5 and its version, warnings, several
    # games, a game that is running
    game_tests("Linux", pkg, work / "game", lambda *a, stdin="": sh("install.sh", *a, stdin=stdin),
               lambda *a, stdin="": sh("uninstall.sh", *a, stdin=stdin),
               {"game": "--game-dir", "steam": "--steam-dir", "known": "--test-known-55u", "mmh55": "--mmh55-installer"}, posix, start_fake_game_linux(proc_dir))

    # Steam installed as a Snap
    home_snap = work / "home_snap"
    steam_snap = home_snap / "snap" / "steam" / "common" / ".local" / "share" / "Steam"
    lib_snap = work / "lib_snap"
    make_steam_for(steam_snap, [lib_snap], True)
    bin_snap = make_game(lib_snap)
    code, out = run([bash, str(pkg / "install.sh")], "", env=dict(os.environ, HOME=posix(home_snap)))
    check(code == 0 and (bin_snap / "dinput8.dll").exists(), "Snap Steam folder is found", out)

    # the settings file: refreshed, values kept, backups
    ini_bin = make_game(work / "ini_lib")
    results = settings_tests("Linux", pkg, ini_bin,
                             lambda: sh("install.sh", "--game-dir", posix(ini_bin)),
                             lambda answer: sh("uninstall.sh", "--game-dir", posix(ini_bin), stdin=answer))

    # random map templates: install, a player's own files, reinstall, uninstall
    tpl_bin = make_game(work / "tpl_lib")
    template_tests("Linux", pkg, tpl_bin,
                   lambda: sh("install.sh", "--game-dir", posix(tpl_bin)),
                   lambda: sh("uninstall.sh", "--game-dir", posix(tpl_bin), stdin="y\n"))
    txt_bin = make_game(work / "txt_lib")
    texts_tests("Linux", pkg, txt_bin,
                lambda: sh("install.sh", "--game-dir", posix(txt_bin)),
                lambda: sh("uninstall.sh", "--game-dir", posix(txt_bin), stdin="y\n"))

    # Valve's steam_api.dll: from a package that has it (a fake one), and from one that has not
    if pkg_steam:
        steam_bin = make_game(work / "valve_dll_lib")  # (no "steam_api" in the path: the tests look for that word)
        steam_api_tests("Linux", pkg, pkg_steam, steam_bin,
                        lambda p: run([bash, str(p / "install.sh"), "--game-dir", posix(steam_bin)], "", env=env),
                        lambda p: run([bash, str(p / "uninstall.sh"), "--game-dir", posix(steam_bin)], "y\n", env=env))
        # a copy under another spelling (Wine finds H5-Together/Steam_API.dll too) is overwritten, not doubled
        ci_steam = make_game(work / "valve_dll_ci_lib")
        (ci_steam / "H5-Together").mkdir()
        (ci_steam / "H5-Together" / "Steam_API.dll").write_bytes(b"an older copy, another spelling")
        code, out = run([bash, str(pkg_steam / "install.sh"), "--game-dir", posix(ci_steam)], "", env=env)
        check(code == 0 and sorted(p.name for p in (ci_steam / "H5-Together").iterdir()) == ["Steam_API.dll", "templates", "texts"]
              and (ci_steam / "H5-Together" / "Steam_API.dll").read_bytes() == (pkg_steam / "h5-together" / "steam_api.dll").read_bytes(),
              "steam_api: a copy under another spelling (H5-Together/Steam_API.dll) is overwritten, not doubled", out)
        code, out = run([bash, str(pkg_steam / "uninstall.sh"), "--game-dir", posix(ci_steam)], "y\n", env=env)
        check(code == 0 and "removed steam_api.dll" in out and not (ci_steam / "H5-Together").exists(),
              "steam_api: the uninstaller removes it under that spelling too, then the empty folder", out)

    # a package without its templates folder is incomplete: exit 7, nothing installed
    no_tpl = work / "pkg_without_templates"
    shutil.copytree(pkg, no_tpl, ignore=shutil.ignore_patterns("h5-together"))
    code, out = run([bash, str(no_tpl / "install.sh"), "--game-dir", posix(tpl_bin)], "", env=env)
    check(code == 7 and not (tpl_bin / "dinput8.dll").exists() and not (tpl_bin / "h5-together").exists(),
          "package without the templates folder: exit 7, nothing installed", out)
    no_txt = work / "pkg_without_texts"
    shutil.copytree(pkg, no_txt, ignore=shutil.ignore_patterns("texts"))
    code, out = run([bash, str(no_txt / "install.sh"), "--game-dir", posix(tpl_bin)], "", env=env)
    check(code == 7 and "h5-together/texts" in out and not (tpl_bin / "dinput8.dll").exists() and not (tpl_bin / "h5-together").exists(),
          "package without the language files folder: exit 7, nothing installed", out)

    # folders that exist under another spelling (Linux file names are case sensitive,
    # Wine finds them anyway): the existing folders and files are reused, none is duplicated
    ci_bin = make_game(work / "ci_lib")
    old = ci_bin / "H5-Together" / "Templates"
    old.mkdir(parents=True)
    first = sorted(p.name for p in template_dir(pkg).iterdir())[0]
    (old / first.lower()).write_bytes(b"an old copy of ours, in lower case")
    code, out = sh("install.sh", "--game-dir", posix(ci_bin))
    check(code == 0 and [p.name for p in ci_bin.iterdir() if p.is_dir()] == ["H5-Together"], "templates: an existing H5-Together folder is reused", out)
    check(sorted(p.name for p in old.iterdir()) == sorted([first.lower()] + [p.name for p in template_dir(pkg).iterdir() if p.name != first])
          and (old / first.lower()).read_bytes() == (template_dir(pkg) / first).read_bytes(),
          "templates: an existing file of ours under another spelling is overwritten, not duplicated")

    # the old name's folder under another spelling (Open-Homm5/Templates): it gets the new name
    ci_old = make_game(work / "ci_old_lib")
    (ci_old / "Open-Homm5" / "Templates").mkdir(parents=True)
    (ci_old / "Open-Homm5" / "Templates" / "OH-mine.xdb").write_bytes(b"the player's own")
    (ci_old / "Open-Homm5.ini").write_bytes(V027_INI)
    code, out = sh("install.sh", "--game-dir", posix(ci_old))
    dirs = [p.name for p in ci_old.iterdir() if p.is_dir()]
    check(code == 0 and dirs == ["h5-together"] and (ci_old / "h5-together" / "Templates" / "OH-mine.xdb").read_bytes() == b"the player's own"
          and (ci_old / "h5-together.ini").exists() and not (ci_old / "Open-Homm5.ini").exists(),
          "old name: Open-Homm5 and Open-Homm5.ini under another spelling get the new name too", f"{dirs}\n{out}")

    # an install over open-homm5 0.2.7 (the old name): the files get the new names
    old_bin = make_game(work / "old_name_lib")
    results.update(migration_tests("Linux", pkg, old_bin,
                                   lambda: sh("install.sh", "--game-dir", posix(old_bin)),
                                   lambda answer: sh("uninstall.sh", "--game-dir", posix(old_bin), stdin=answer)))

    # the bug report script. HOME is the folder above the fake game folders, as a player's
    # game folder is below their home (the report writes that part of the path as ~)
    def report_runner(report_work, *always):
        report_env = dict(os.environ, HOME=posix(report_work))

        def run_report(game_bin, out_dir, *extra):
            return run([bash, str(pkg / "bug-report.sh"), "--game-dir", posix(game_bin), "--out-dir", posix(out_dir), *always, *extra],
                       "", env=report_env)
        return run_report

    run_report = report_runner(work / "bug_report")
    bug_report_tests("Linux", pkg, work / "bug_report", run_report, fmt=(".zip", ".tar.gz"), home_short=True,
                     no_dump="--no-dump", known_flag="--test-known-55u")
    bug_report_tests("Linux, --tar", pkg, work / "bug_report_tar", report_runner(work / "bug_report_tar", "--tar"), fmt=".tar.gz",
                     home_short=True, no_dump="--no-dump", known_flag="--test-known-55u")

    print("\nBug report script (Linux only)")
    extra = work / "bug_report_extra"
    shutil.rmtree(extra, ignore_errors=True)
    extra.mkdir(parents=True, exist_ok=True)
    b = make_report_game(pkg, extra / "lib")
    here = extra / "run here"
    here.mkdir()
    code, out = run([bash, str(pkg / "bug-report.sh"), "--game-dir", posix(b)], "", env=env, cwd=str(here))
    check(code == 0 and len(report_files(here)) == 1, "bug report: without --out-dir the file is made in the folder you run it from", out)
    blocker = extra / "blocker"
    blocker.write_bytes(b"a file, not a folder")
    code, out = run_report(b, blocker / "sub")
    check(code == 5 and "Cannot write" in out, "bug report: an output folder that cannot be made: exit 5 and a message", out)
    check(run([bash, "-n", str(pkg / "bug-report.sh")])[0] == 0, "bash -n bug-report.sh")
    code, out = run([bash, str(pkg / "bug-report.sh"), "--help"], "", env=env)
    check(code == 0 and "bug report" in out and "--out-dir" in out, "bug report: --help prints the header", out)
    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bash", help="bash to test the .sh scripts with (default: bash from PATH, if it is not WSL's)")
    ap.add_argument("--work-dir", type=Path, help="folder for the fake trees (default: a new temp folder)")
    ap.add_argument("--keep", action="store_true", help="do not delete the work folder at the end")
    ap.add_argument("--only", choices=["windows", "linux"], help="run only the Windows or only the Linux script tests (faster while working)")
    args = ap.parse_args()

    work = args.work_dir or Path(tempfile.mkdtemp(prefix="h5-together-test-"))
    work.mkdir(parents=True, exist_ok=True)
    out_dir = work / "out"
    r = subprocess.run([sys.executable, str(REPO / "tools" / "package" / "make_package.py"), "--version", "0.0.0-test",
                        "--out-dir", str(out_dir)])
    if r.returncode != 0:
        sys.exit("make_package.py failed")
    with zipfile.ZipFile(out_dir / "h5-together-0.0.0-test.zip") as z:
        z.extractall(work / "package")
    pkg = work / "package" / "h5-together-0.0.0-test"  # the zip's one top folder
    package_checks(pkg, out_dir / "h5-together-0.0.0-test.zip")
    texts_package_checks(pkg, out_dir / "h5-together-0.0.0-test.zip")
    licence_checks(pkg, out_dir / "h5-together-0.0.0-test.zip")
    bug_report_package_checks(pkg, out_dir / "h5-together-0.0.0-test.zip")
    pkg_steam = steam_api_package_checks(work / "valve dll", out_dir / "h5-together-0.0.0-test.zip")
    if not (pkg_steam / "h5-together" / "steam_api.dll").is_file():
        pkg_steam = None  # (a check above failed; the script tests with it are skipped)

    win = lin = None
    if os.name == "nt" and args.only != "linux":
        win = windows_tests(pkg, work / "win", pkg_steam)
    else:
        print("\n(skipping the Windows script tests: " + ("--only linux" if args.only == "linux" else "not on Windows") + ")")

    bash = args.bash or shutil.which("bash")
    if args.only == "windows":
        bash = None
    if bash and os.name == "nt" and "system32" in bash.lower():
        bash = None  # that is WSL's launcher
    if bash:
        (work / "lin").mkdir(exist_ok=True)
        lin = linux_tests(pkg, work / "lin", bash, pkg_steam)
    else:
        print("\n(skipping the Linux script tests: no bash; pass --bash)")

    if win and lin:
        print("\nThe settings file: Windows and Linux")
        for case in win:
            check(win[case] == lin.get(case), f"settings: {case}: install.ps1 and install.sh write the same bytes")

    print(f"\n{checks - len(failures)} of {checks} checks passed")
    if failures:
        print("FAILED:\n  " + "\n  ".join(failures))
    if not args.keep and not args.work_dir:
        shutil.rmtree(work, ignore_errors=True)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
