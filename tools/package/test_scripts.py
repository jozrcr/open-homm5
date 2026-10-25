#!/usr/bin/env python3
"""Tests the install and uninstall scripts of the package against FAKE folders.

    python tools/package/test_scripts.py [--bash PATH] [--work-dir DIR] [--keep]

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
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
GAME = "Heroes of Might and Magic 5 Tribes of the East"
OURS_MARK = b"open-homm5 hook DLL"

failures = []
checks = 0


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


def make_game(library, exes=("H5_Game.exe", "MMH55_Utility_64.exe")):
    """A fake game folder with empty-ish exes. Returns its bin folder."""
    b = Path(library) / "steamapps" / "common" / GAME / "bin"
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
    return Path(pkg) / "open-homm5" / "templates"


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
    check(packed == wanted, "package: open-homm5/templates holds every src/mods/rmg/templates/*.xdb and nothing else", f"{packed}\n{wanted}")
    check(same_bytes(template_dir(pkg), TEMPLATES_SRC, wanted), "package: the templates are byte for byte the source files")
    data = [(template_dir(pkg) / n).read_bytes() for n in packed]
    check(all(d.startswith(b"\xef\xbb\xbf") and b"\r\n" in d and b"\n" not in d.replace(b"\r\n", b"") for d in data),
          "package: the templates keep their UTF-8 BOM and CRLF line endings")
    with zipfile.ZipFile(zip_path) as z:
        entries = [i for i in z.infolist() if "/open-homm5/templates/" in i.filename]
        check(len(entries) == len(wanted) and all(i.filename.count("/") == 3 for i in entries),
              "package: the templates sit in the zip under <top folder>/open-homm5/templates/")
        check(all(((i.external_attr >> 16) & 0o777) == 0o644 for i in entries), "package: the templates are plain data files (mode 644)")


def template_tests(who, pkg, game_bin, install, uninstall):
    """The templates in a fake game folder: install, a player's own files, reinstall, uninstall.

    install() and uninstall() run the scripts for game_bin and return (exit code, output)."""
    print(f"\nRandom map templates ({who})")
    src = template_dir(pkg)
    ours = sorted(p.name for p in src.iterdir())
    n = len(ours)
    tdir = game_bin / "open-homm5" / "templates"
    before = tree(game_bin)

    def names():
        return sorted(p.name for p in tdir.iterdir()) if tdir.is_dir() else None

    code, out = install()
    check(code == 0, "templates: fresh install: exit 0", out)
    check(names() == ours and same_bytes(tdir, src, ours), "templates: fresh install: exactly our templates, byte for byte, in bin/open-homm5/templates")
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
    check(code == 0 and code2 == 0 and not (game_bin / "open-homm5").exists(), "templates: uninstall removes the empty open-homm5 folders", out2)
    check(tree(game_bin) == before, "templates: after install and uninstall the game folder is exactly as it was")

    # a folder open-homm5 that holds something else stays
    other = game_bin / "open-homm5" / "other.txt"
    other.parent.mkdir()
    other.write_bytes(b"not ours")
    install()
    code, out = uninstall()
    check(code == 0 and other.read_bytes() == b"not ours" and not tdir.exists(),
          "templates: a folder open-homm5 with other things in it stays; the empty templates folder goes", out)
    other.unlink()
    other.parent.rmdir()

    # nothing installed: nothing to do, no error
    code, out = uninstall()
    check(code == 0 and "no random map templates" in out and tree(game_bin) == before, "templates: uninstall with nothing installed: no error, nothing changed", out)


# ---------------------------------------------------------------- Windows

def windows_tests(pkg, work):
    print("\nWindows scripts (install.bat / uninstall.bat)")
    work.mkdir(parents=True, exist_ok=True)
    steam_only = work / "steam_only"          # Steam folder, game in a second library with accents
    lib2 = work / "Bibliotheque \u00c9t\u00e9 Steam"
    steam_only.mkdir()
    make_steam(steam_only, [steam_only, lib2])
    bin2 = make_game(lib2)

    def install(*args, stdin=""):
        return run(bat(str(pkg / "install.bat"), "-NoPause", *args), stdin)

    def uninstall(*args, stdin=""):
        return run(bat(str(pkg / "uninstall.bat"), "-NoPause", *args), stdin)

    our_dll = (pkg / "dinput8.dll").read_bytes()
    our_ini = (pkg / "open-homm5.ini").read_bytes()
    before = snapshot(bin2)

    # fresh install, game found through the fake libraryfolders.vdf
    code, out = install("-SteamDir", str(steam_only))
    check(code == 0, "fresh install: exit 0", out)
    check((bin2 / "dinput8.dll").read_bytes() == our_dll, "fresh install: dinput8.dll copied")
    check((bin2 / "open-homm5.ini").read_bytes() == our_ini, "fresh install: open-homm5.ini copied")
    after = snapshot(bin2)
    check({k: v for k, v in after.items() if k in before} == before, "fresh install: game files unchanged")
    check(set(after) - set(before) == {"dinput8.dll", "open-homm5.ini"}, "fresh install: only our 2 files added")
    check("open-homm5.log" in out and "added dinput8.dll" in out, "fresh install: says what it did and how to check", out)
    check(sorted(p.name for p in (bin2 / "open-homm5" / "templates").iterdir()) == sorted(p.name for p in template_dir(pkg).iterdir()),
          "fresh install: the templates are installed in bin/open-homm5/templates")

    # install again over our own DLL
    code, out = install("-SteamDir", str(steam_only))
    check(code == 0 and "up to date" in out, "re-install over our DLL: exit 0, says up to date", out)
    (bin2 / "dinput8.dll").write_bytes(OURS_MARK + b" older build")
    code, out = install("-SteamDir", str(steam_only))
    check(code == 0 and "replaced the older" in out, "re-install over an older build of ours: replaced", out)
    check((bin2 / "dinput8.dll").read_bytes() == our_dll, "re-install: new DLL in place")

    # existing ini is kept
    (bin2 / "open-homm5.ini").write_bytes(b"[net]\r\ntuning=0\r\n")
    code, out = install("-SteamDir", str(steam_only))
    check(code == 0 and (bin2 / "open-homm5.ini").read_bytes() == b"[net]\r\ntuning=0\r\n",
          "existing open-homm5.ini is kept", out)
    check("kept your existing open-homm5.ini" in out, "existing ini: says it kept it")

    # a foreign dinput8.dll is refused
    (bin2 / "dinput8.dll").write_bytes(b"MZ some other mod's dinput8")
    code, out = install("-SteamDir", str(steam_only))
    check(code == 3 and "Stopped" in out, "foreign dinput8.dll: refused with exit 3", out)
    check((bin2 / "dinput8.dll").read_bytes() == b"MZ some other mod's dinput8", "foreign dinput8.dll: left untouched")
    code, out = uninstall("-SteamDir", str(steam_only), stdin="y\n")
    check(code == 0 and "NOT the open-homm5 one" in out, "uninstall leaves a foreign dinput8.dll alone", out)
    check((bin2 / "dinput8.dll").read_bytes() == b"MZ some other mod's dinput8", "uninstall: foreign dll still there")
    (bin2 / "dinput8.dll").unlink()
    (bin2 / "open-homm5.ini").unlink(missing_ok=True)

    # uninstall: ours removed, log removed, ini only after asking
    code, out = install("-SteamDir", str(steam_only))
    (bin2 / "open-homm5.log").write_bytes(b"log")
    code, out = uninstall("-SteamDir", str(steam_only), stdin="n\n")
    check(code == 0, "uninstall (answer n): exit 0", out)
    check(not (bin2 / "dinput8.dll").exists() and not (bin2 / "open-homm5.log").exists(),
          "uninstall: dinput8.dll and open-homm5.log removed")
    check((bin2 / "open-homm5.ini").exists(), "uninstall (answer n): ini kept")
    code, out = uninstall("-SteamDir", str(steam_only), stdin="y\n")
    check(code == 0 and not (bin2 / "open-homm5.ini").exists(), "uninstall (answer y): ini removed", out)
    check(snapshot(bin2) == before, "uninstall: folder back to exactly how it was")
    check(not (bin2 / "open-homm5").exists(), "uninstall: the (empty) open-homm5 folder is gone too")

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

    # random map templates: install, a player's own files, reinstall, uninstall
    tpl_bin = make_game(work / "tpl lib")
    template_tests("Windows", pkg, tpl_bin,
                   lambda: install("-GameDir", str(tpl_bin)),
                   lambda: uninstall("-GameDir", str(tpl_bin), stdin="y\n"))

    # a package without its templates folder is incomplete: exit 7, nothing installed
    no_tpl = work / "pkg without templates"
    shutil.copytree(pkg, no_tpl, ignore=shutil.ignore_patterns("open-homm5"))
    code, out = run(bat(str(no_tpl / "install.bat"), "-NoPause", "-GameDir", str(tpl_bin)))
    check(code == 7 and "extract the whole zip" in out and not (tpl_bin / "dinput8.dll").exists() and not (tpl_bin / "open-homm5").exists(),
          "package without the templates folder: exit 7, nothing installed", out)

    # PowerShell 5.1 specifically (the script must run there)
    code, out = run(["powershell", "-NoProfile", "-Command", "$PSVersionTable.PSVersion.ToString()"])
    check(out.strip().startswith("5."), f"tests ran on Windows PowerShell 5.x (found {out.strip()})")


# ---------------------------------------------------------------- Linux

def posix(p):
    return Path(p).as_posix()


def linux_tests(pkg, work, bash):
    print(f"\nLinux scripts (install.sh / uninstall.sh) with {bash}")
    work.mkdir(parents=True, exist_ok=True)
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
    check((bin2 / "dinput8.dll").read_bytes() == our_dll and (bin2 / "open-homm5.ini").exists(), "fresh install: files copied")
    after = snapshot(bin2)
    check({k: v for k, v in after.items() if k in before} == before and set(after) - set(before) == {"dinput8.dll", "open-homm5.ini"},
          "fresh install: game files unchanged, only our 2 files added")
    check('WINEDLLOVERRIDES="dinput8=n,b" %command%' in out and "MMH55_Utility_64.exe" in out and "open-homm5.log" in out,
          "prints the Steam launch option, the exe to start and how to check", out)
    check(sorted(p.name for p in (bin2 / "open-homm5" / "templates").iterdir()) == sorted(p.name for p in template_dir(pkg).iterdir()),
          "fresh install: the templates are installed in bin/open-homm5/templates")

    code, out = sh("install.sh")
    check(code == 0 and "up to date" in out, "re-install over our DLL: up to date", out)
    (bin2 / "dinput8.dll").write_bytes(OURS_MARK + b" older build")
    code, out = sh("install.sh")
    check(code == 0 and "replaced the older" in out and (bin2 / "dinput8.dll").read_bytes() == our_dll, "re-install over an older build: replaced", out)

    (bin2 / "open-homm5.ini").write_bytes(b"[net]\ntuning=0\n")
    code, out = sh("install.sh")
    check(code == 0 and (bin2 / "open-homm5.ini").read_bytes() == b"[net]\ntuning=0\n" and "kept your existing" in out, "existing ini is kept", out)

    (bin2 / "dinput8.dll").write_bytes(b"MZ some other mod")
    code, out = sh("install.sh")
    check(code == 3 and (bin2 / "dinput8.dll").read_bytes() == b"MZ some other mod", "foreign dinput8.dll: refused (exit 3), untouched", out)
    code, out = sh("uninstall.sh", stdin="y\n")
    check(code == 0 and (bin2 / "dinput8.dll").exists() and "NOT the open-homm5 one" in out, "uninstall leaves a foreign dll alone", out)
    (bin2 / "dinput8.dll").unlink()
    (bin2 / "open-homm5.ini").unlink(missing_ok=True)

    sh("install.sh")
    (bin2 / "open-homm5.log").write_bytes(b"log")
    code, out = sh("uninstall.sh", stdin="n\n")
    check(code == 0 and not (bin2 / "dinput8.dll").exists() and not (bin2 / "open-homm5.log").exists() and (bin2 / "open-homm5.ini").exists(),
          "uninstall (n): dll and log removed, ini kept", out)
    code, out = sh("uninstall.sh", stdin="y\n")
    check(code == 0 and snapshot(bin2) == before, "uninstall (y): folder back to how it was", out)
    check(not (bin2 / "open-homm5").exists(), "uninstall: the (empty) open-homm5 folder is gone too")

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

    # random map templates: install, a player's own files, reinstall, uninstall
    tpl_bin = make_game(work / "tpl_lib")
    template_tests("Linux", pkg, tpl_bin,
                   lambda: sh("install.sh", "--game-dir", posix(tpl_bin)),
                   lambda: sh("uninstall.sh", "--game-dir", posix(tpl_bin), stdin="y\n"))

    # a package without its templates folder is incomplete: exit 7, nothing installed
    no_tpl = work / "pkg_without_templates"
    shutil.copytree(pkg, no_tpl, ignore=shutil.ignore_patterns("open-homm5"))
    code, out = run([bash, str(no_tpl / "install.sh"), "--game-dir", posix(tpl_bin)], "", env=env)
    check(code == 7 and not (tpl_bin / "dinput8.dll").exists() and not (tpl_bin / "open-homm5").exists(),
          "package without the templates folder: exit 7, nothing installed", out)

    # folders that exist under another spelling (Linux file names are case sensitive,
    # Wine finds them anyway): the existing folders and files are reused, none is duplicated
    ci_bin = make_game(work / "ci_lib")
    old = ci_bin / "Open-Homm5" / "Templates"
    old.mkdir(parents=True)
    first = sorted(p.name for p in template_dir(pkg).iterdir())[0]
    (old / first.lower()).write_bytes(b"an old copy of ours, in lower case")
    code, out = sh("install.sh", "--game-dir", posix(ci_bin))
    check(code == 0 and [p.name for p in ci_bin.iterdir() if p.is_dir()] == ["Open-Homm5"], "templates: an existing Open-Homm5 folder is reused", out)
    check(sorted(p.name for p in old.iterdir()) == sorted([first.lower()] + [p.name for p in template_dir(pkg).iterdir() if p.name != first])
          and (old / first.lower()).read_bytes() == (template_dir(pkg) / first).read_bytes(),
          "templates: an existing file of ours under another spelling is overwritten, not duplicated")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bash", help="bash to test the .sh scripts with (default: bash from PATH, if it is not WSL's)")
    ap.add_argument("--work-dir", type=Path, help="folder for the fake trees (default: a new temp folder)")
    ap.add_argument("--keep", action="store_true", help="do not delete the work folder at the end")
    args = ap.parse_args()

    work = args.work_dir or Path(tempfile.mkdtemp(prefix="open-homm5-test-"))
    work.mkdir(parents=True, exist_ok=True)
    out_dir = work / "out"
    r = subprocess.run([sys.executable, str(REPO / "tools" / "package" / "make_package.py"), "--version", "0.0.0-test",
                        "--out-dir", str(out_dir)])
    if r.returncode != 0:
        sys.exit("make_package.py failed")
    with zipfile.ZipFile(out_dir / "open-homm5-0.0.0-test.zip") as z:
        z.extractall(work / "package")
    pkg = work / "package" / "open-homm5-0.0.0-test"  # the zip's one top folder
    package_checks(pkg, out_dir / "open-homm5-0.0.0-test.zip")

    if os.name == "nt":
        windows_tests(pkg, work / "win")
    else:
        print("\n(skipping the Windows script tests: not on Windows)")

    bash = args.bash or shutil.which("bash")
    if bash and os.name == "nt" and "system32" in bash.lower():
        bash = None  # that is WSL's launcher
    if bash:
        (work / "lin").mkdir(exist_ok=True)
        linux_tests(pkg, work / "lin", bash)
    else:
        print("\n(skipping the Linux script tests: no bash; pass --bash)")

    print(f"\n{checks - len(failures)} of {checks} checks passed")
    if failures:
        print("FAILED:\n  " + "\n  ".join(failures))
    if not args.keep and not args.work_dir:
        shutil.rmtree(work, ignore_errors=True)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
