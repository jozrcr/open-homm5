#!/usr/bin/env python3
"""H5 Together, Steam test 3 (the virtual LAN over Steam): the laptop's side.

Run it from a kit folder that holds dinput8.dll and steam_api.dll, or from the repo
(tools/robot/) with --dll pointing at the dinput8.dll to test (for example the one in the
package zip that GitHub builds for the pull request). steam_api.dll is only needed when
bin/h5-together/ has none yet.

    python3 laptop_test.py install [--dll PATH]   # our DLL in the game, steam_api.dll, the package's texts, [online] mode=steam
    python3 laptop_test.py robot       # optional: the in-DLL robot joins the first LAN game by itself
    (start the game: /snap/bin/steam steam://rungameid/15370)
    python3 laptop_test.py collect     # after the game: logs and screenshots to ~/h5-steam-test3-results/<time>/
    python3 laptop_test.py clean       # remove the robot's files, put the intro videos back
    python3 laptop_test.py uninstall   # also put back the DLL and the settings file from before the test

--game DIR changes the game folder (default: the Snap Steam library below).
Everything it replaces is kept in <game>/bin/h5-together-test3-backup/ first.
"""
import argparse
import datetime
import pathlib
import shutil
import sys

KIT = pathlib.Path(__file__).resolve().parent
DEFAULT_GAME = pathlib.Path.home() / "snap/steam/common/.local/share/Steam/steamapps/common/" \
    "Heroes of Might and Magic 5 Tribes of the East"
INTRO_VIDEOS = ["logo-ubi6.ogg", "logo-nival.ogg", "intro.ogg"]
PROFILE_DIR = pathlib.Path("My Games") / "Heroes of Might and Magic V - Tribes of the East" / "Profiles"
ROBOT_SETTINGS = """
// ---- H5 Together robot overrides (Steam test 3) ----
setvar gfx_fullscreen = 0
setvar gfx_resolution = 1024x768
setvar gfx_window_size = 1024x768
setvar sound_musicvolume = 0
setvar sound_sfxvolume = 0
setvar sound_ambient_enable = 0
setvar skip_adventure_flyby = 1
setvar game_writelog = 1
setvar pwl_press_any_key_enabled = 0
setvar lan_last_login = Laptop
setvar mp_check_crc = 1
"""
# The robot's script for the joiner: open the LAN list, pick the first game, join, get
# ready (seconds after the game's window is up). One join only: the host's game must be
# waiting already (it shows in the list a few seconds after the list opens). "ready" is
# pressed again every 20 s: it is only pressed on a row that is not ready yet.
CLIENT_STEPS = ["10|menu|multi lan", "35|shot|client-lobby", "40|game|0", "42|ui|NUI::CLobbyScreen join",
                "50|shot|client-joined"]
CLIENT_STEPS += ["%d|arrow|1 ready" % t for t in range(60, 461, 20)]
CLIENT_STEPS += ["300|shot|client-waiting", "480|shot|client-started", "600|shot|client-playing"]


def wine_path(path):
    """/home/joz/x -> Z:\\home\\joz\\x (the game runs under Proton; Z: is the Linux root)."""
    return "Z:" + str(path).replace("/", "\\")


def paths(game):
    bin_dir = game / "bin"
    return bin_dir, bin_dir / "h5-together-test3-backup"


def check_game(game):
    if not (game / "bin").is_dir():
        sys.exit("no game at %s (use --game DIR)" % game)
    # MMH5.5 RC19k's exe, or the original 2008 game (since 0.3.5: the installer's
    # H5_Original_4GB.exe, or the 2008 exe itself as H5_Game.exe / H5_GameOLD.exe)
    names = ["MMH55_Utility_64.exe", "H5_Original_4GB.exe", "H5_Game.exe", "H5_GameOLD.exe"]
    if not any((game / "bin" / name).is_file() for name in names):
        sys.exit("no game exe in %s (one of %s)" % (game / "bin", ", ".join(names)))


def set_online(ini_path):
    """[online] mode=steam in h5-together.ini; any other [online] keys (the spike's) go."""
    text = ini_path.read_text(encoding="utf-8", errors="replace") if ini_path.exists() else ""
    newline = "\r\n" if "\r\n" in text else "\n"
    out, skipping = [], False
    for line in text.splitlines():
        stripped = line.strip().lower()
        if stripped.startswith("["):
            skipping = stripped == "[online]"
        if not skipping:
            out.append(line)
    while out and not out[-1].strip():
        out.pop()
    out += ["", "[online]", "mode=steam"] if out else ["[online]", "mode=steam"]
    ini_path.write_text(newline.join(out) + newline, encoding="utf-8")


def install_texts(dll, steam_dir, backup):
    """The package's h5-together/texts/ (next to its dinput8.dll), as the installer copies it:
    without fr.txt our texts show in English. Files there before the test are kept first."""
    texts = dll.resolve().parent / "h5-together" / "texts"
    if not texts.is_dir():
        print("no texts folder next to", dll, "(our texts stay as they are)")
        return
    dest = steam_dir / "texts"
    if not dest.exists():
        (backup / "texts.added").write_text("added by the test\n")
    elif not (backup / "texts.added").exists() and not (backup / "texts").exists():
        shutil.copytree(dest, backup / "texts")
    dest.mkdir(exist_ok=True)
    names = sorted(f.name for f in texts.glob("*.txt"))
    for name in names:
        shutil.copy2(texts / name, dest / name)
    print("installed the texts (%s) in %s" % (", ".join(names), dest))


def install(game, dll):
    bin_dir, backup = paths(game)
    backup.mkdir(exist_ok=True)
    for name in ("dinput8.dll", "h5-together.ini"):
        src, kept = bin_dir / name, backup / name
        if src.exists() and not kept.exists():
            shutil.copy2(src, kept)
            print("kept", src, "->", kept)
        elif not src.exists() and not (backup / (name + ".absent")).exists() and not kept.exists():
            (backup / (name + ".absent")).write_text("there was none before the test\n")
    shutil.copy2(dll, bin_dir / "dinput8.dll")
    print("installed our dinput8.dll from", dll)
    steam_dir = bin_dir / "h5-together"
    steam_dir.mkdir(exist_ok=True)
    if not (steam_dir / "steam_api.dll").exists():
        shutil.copy2(KIT / "steam_api.dll", steam_dir / "steam_api.dll")
        (backup / "steam_api.dll.added").write_text("added by the test\n")
        print("installed", steam_dir / "steam_api.dll")
    else:
        print("steam_api.dll already in", steam_dir, "(kept)")
    install_texts(dll, steam_dir, backup)
    set_online(bin_dir / "h5-together.ini")
    print("h5-together.ini: [online] mode=steam")
    print("Steam's launch option must keep WINEDLLOVERRIDES=\"dinput8=n,b\" (so the game loads our DLL),")
    print("and the part that starts MMH55_Utility_64.exe instead of H5_Game.exe if it has one (the virtual LAN needs MMH5.5's exe)")


def robot(game, seconds):
    bin_dir, _backup = paths(game)
    home = game / "robot_home"
    profiles = home / PROFILE_DIR
    (profiles / "Robot").mkdir(parents=True, exist_ok=True)
    (profiles / "global_a2.cfg").write_text("setvar profile_name = Robot\n", encoding="ascii")
    default = game / "profiles" / "default_profile"
    base = (default / "user_a2.cfg").read_text(encoding="latin-1") if (default / "user_a2.cfg").is_file() else ""
    (profiles / "Robot" / "user_a2.cfg").write_text(base + ROBOT_SETTINGS, encoding="latin-1")
    if (default / "input_a2.cfg").is_file():
        shutil.copy2(default / "input_a2.cfg", profiles / "Robot" / "input_a2.cfg")
    shots = game / "robot_shots"
    shots.mkdir(exist_ok=True)
    config = [
        ("home", wine_path(home)),
        ("quit_after_ai_turns", 0),
        ("quit_after_seconds", seconds),
        ("command", ""),
        ("command_delay_seconds", 8),
        ("end_turn_every_seconds", 5),  # (the laptop's human turns: the robot ends them)
        ("stuck_exit", 0),
        ("trace_crc", 1),
        ("net_log", 1),
        ("net_scan_addresses", 1),
        ("screenshot_dir", wine_path(shots)),
        ("screenshot_every_seconds", 0),
    ] + [("step", s) for s in CLIENT_STEPS]
    (bin_dir / "h5-together-robot.ini").write_text("".join("%s=%s\n" % kv for kv in config), encoding="ascii")
    video, parked = game / "video", game / "video" / "robot_disabled"
    for name in INTRO_VIDEOS:
        if (video / name).exists():
            parked.mkdir(exist_ok=True)
            shutil.move(str(video / name), str(parked / name))
    print("robot set up: 10 s after the game's window it opens Multiplayer > LAN, at 40 s it joins the first game in")
    print("the list, then presses ready, ends its own turns, and closes the game after %d s" % seconds)


def collect(game):
    bin_dir, _backup = paths(game)
    out = pathlib.Path.home() / "h5-steam-test3-results" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True)
    for name in ("h5-together.log", "h5-together.old.log", "console.txt", "h5-together.ini"):
        if (bin_dir / name).exists():
            shutil.copy2(bin_dir / name, out / name)
    for crash in bin_dir.glob("h5-together-crash-*"):
        shutil.copy2(crash, out / crash.name)
    shots = game / "robot_shots"
    if shots.is_dir():
        for f in shots.glob("*.bmp"):
            shutil.copy2(f, out / f.name)
    print("results in", out)


def clean(game):
    bin_dir, _backup = paths(game)
    (bin_dir / "h5-together-robot.ini").unlink(missing_ok=True)
    for folder in (game / "robot_home", game / "robot_shots"):
        if folder.is_dir():
            shutil.rmtree(folder)
    parked = game / "video" / "robot_disabled"
    for name in INTRO_VIDEOS:
        if (parked / name).exists():
            shutil.move(str(parked / name), str(game / "video" / name))
    if parked.is_dir() and not any(parked.iterdir()):
        parked.rmdir()
    print("robot files removed, intro videos back (our DLL and mode=steam stay)")


def uninstall(game):
    clean(game)
    bin_dir, backup = paths(game)
    for name in ("dinput8.dll", "h5-together.ini"):
        if (backup / name).exists():
            shutil.copy2(backup / name, bin_dir / name)
            print("put back", bin_dir / name)
        elif (backup / (name + ".absent")).exists():
            (bin_dir / name).unlink(missing_ok=True)
            print("removed", bin_dir / name, "(there was none before)")
    if (backup / "steam_api.dll.added").exists():
        (bin_dir / "h5-together" / "steam_api.dll").unlink(missing_ok=True)
        print("removed steam_api.dll")
    if (backup / "texts.added").exists():
        shutil.rmtree(bin_dir / "h5-together" / "texts", ignore_errors=True)
        print("removed the texts folder (there was none before)")
    elif (backup / "texts").is_dir():
        shutil.rmtree(bin_dir / "h5-together" / "texts", ignore_errors=True)
        shutil.copytree(backup / "texts", bin_dir / "h5-together" / "texts")
        print("put back the texts folder from before the test")
    shutil.rmtree(backup, ignore_errors=True)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("what", choices=["install", "robot", "collect", "clean", "uninstall"])
    p.add_argument("--game", default=str(DEFAULT_GAME))
    p.add_argument("--seconds", type=int, default=1200, help="robot: close the game after this long")
    p.add_argument("--dll", default=str(KIT / "dinput8.dll"), help="install: the dinput8.dll to test")
    args = p.parse_args()
    game = pathlib.Path(args.game)
    check_game(game)
    dll = pathlib.Path(args.dll)
    if args.what == "install" and not dll.is_file():
        sys.exit("no %s (use --dll PATH)" % dll)
    {"install": lambda: install(game, dll),"robot": lambda: robot(game, args.seconds), "collect": lambda: collect(game),
     "clean": lambda: clean(game), "uninstall": lambda: uninstall(game)}[args.what]()


if __name__ == "__main__":
    main()
