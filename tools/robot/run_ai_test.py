"""Robot: run an all-AI game in a TEST COPY of the game and report AI timings.

Usage:
    python tools/robot/run_ai_test.py                       # defaults below
    python tools/robot/run_ai_test.py --turns 8 --map /Maps/Multiplayer/A1M4/map.xdb

What it does:
  1. installs build/hook/dinput8.dll (build it with src/hook/build.ps1) into the
     test copy's bin/ folder, plus open-homm5-robot.ini (turns on robot mode);
  2. prepares a robot profile (windowed, muted) in <game>/robot_home. The DLL
     redirects "My Documents" there, so the player's real profile is untouched;
  3. starts the game straight into the map with the game's own "advmap" option,
     every player controlled by the AI ("-1" = no human players);
  4. waits until the DLL closes the game (after --turns AI turns or --seconds),
     taking screenshots of the game window now and then;
  5. copies the DLL's log and writes a summary to build/robot/<time>/.

It never runs against the Steam install: --game must be a separate copy.
"""
import argparse
import codecs
import ctypes
import ctypes.wintypes
import datetime
import json
import os
import pathlib
import re
import shutil
import statistics
import subprocess
import sys
import time

REPO = pathlib.Path(__file__).resolve().parents[2]
INTRO_VIDEOS = ["logo-ubi6.ogg", "logo-nival.ogg", "intro.ogg"]
PROFILE_DIR = pathlib.Path("My Games") / "Heroes of Might and Magic V - Tribes of the East" / "Profiles"

# Robot profile settings, appended after the game's default profile.
ROBOT_SETTINGS = """
// ---- open-homm5 robot overrides ----
setvar gfx_fullscreen = 0
setvar gfx_resolution = 1024x768
setvar gfx_window_size = 1024x768
setvar sound_musicvolume = 0
setvar sound_sfxvolume = 0
setvar sound_ambient_enable = 0
setvar skip_adventure_flyby = 1
setvar game_writelog = 1
setvar pwl_press_any_key_enabled = 0
"""


def check_game_dir(game):
    if "steamapps" in str(game).lower():
        sys.exit("refusing to use %s: the robot only runs in a separate test copy of the game" % game)
    if not (game / "bin").is_dir():
        sys.exit("%s does not look like a game folder (no bin/)" % game)


def prepare(game, args):
    bin_dir = game / "bin"
    dll = REPO / "build" / "hook" / "dinput8.dll"
    if not dll.is_file():
        sys.exit("build the DLL first: powershell -File src\\hook\\build.ps1")
    shutil.copy2(dll, bin_dir / "dinput8.dll")

    home = game / "robot_home"
    profiles = home / PROFILE_DIR
    (profiles / "Robot").mkdir(parents=True, exist_ok=True)
    (profiles / "global_a2.cfg").write_text("setvar profile_name = Robot\n", encoding="ascii")
    default = game / "profiles" / "default_profile"
    base = (default / "user_a2.cfg").read_text(encoding="latin-1") if (default / "user_a2.cfg").is_file() else ""
    extra = ROBOT_SETTINGS + "".join("setvar %s\n" % s for s in args.setvar)
    (profiles / "Robot" / "user_a2.cfg").write_text(base + extra, encoding="latin-1")
    if (default / "input_a2.cfg").is_file():
        shutil.copy2(default / "input_a2.cfg", profiles / "Robot" / "input_a2.cfg")

    # The console command that starts the map, run by the DLL from the game's
    # main loop once the main menu is up (src/hook/robot.cpp):
    #   advmap <map DBID> <human player ids, -1 = none> <difficulty> <start mode, 1 = skip cutscenes>
    dbid = args.map if "#xpointer" in args.map else args.map + "#xpointer(/AdvMapDesc)"
    # Player 0 is a passive "human" whose turns the DLL ends by pressing Enter.
    # ("-1" for "no humans" is rejected by this build: "Invalid player index -1!")
    command = args.command or ("advmap %s 0 %s 1" % (dbid, args.difficulty))
    (bin_dir / "open-homm5-robot.ini").write_text(
        "home=%s\nquit_after_ai_turns=%d\nquit_after_seconds=%d\ncommand=%s\ncommand_delay_seconds=%d\n"
        "end_turn_every_seconds=%d\npress_enter_every_seconds=%d\nrepeat_command=%s\nrepeat_every_seconds=%d\n"
        % (home, args.turns, args.seconds, command, args.command_delay, args.end_turn_every, args.press_enter_every,
           args.repeat_command or "", args.repeat_every), encoding="ascii")

    # Skip the start-up videos: move them aside in the TEST COPY (restore with --restore-intro).
    video, parked = game / "video", game / "video" / "robot_disabled"
    for name in INTRO_VIDEOS:
        if (video / name).exists():
            parked.mkdir(exist_ok=True)
            shutil.move(str(video / name), str(parked / name))
    log = bin_dir / "open-homm5.log"
    for stale in (log, bin_dir / "console.txt"):
        if stale.exists():
            stale.unlink()
    return bin_dir, log, command


def restore_intro(game):
    parked = game / "video" / "robot_disabled"
    for name in INTRO_VIDEOS:
        if (parked / name).exists():
            shutil.move(str(parked / name), str(game / "video" / name))
    print("intro videos restored in", game / "video")


# --- screenshots of the game window (Win32 + PowerShell/System.Drawing) -------
user32 = ctypes.windll.user32


def window_rect(pid):
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM)
    def callback(hwnd, _):
        owner = ctypes.wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            r = ctypes.wintypes.RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(r))
            if r.right - r.left > 200:
                found.append((r.left, r.top, r.right, r.bottom))
        return True

    user32.EnumWindows(callback, 0)
    return found[0] if found else None


def screenshot(pid, path):
    rect = window_rect(pid)
    if not rect:
        return False
    l, t, r, b = rect
    ps = ("Add-Type -AssemblyName System.Drawing; $b = New-Object System.Drawing.Bitmap %d, %d; "
          "$g = [System.Drawing.Graphics]::FromImage($b); $g.CopyFromScreen(%d, %d, 0, 0, $b.Size); "
          "$b.Save('%s', [System.Drawing.Imaging.ImageFormat]::Png)" % (r - l, b - t, l, t, path))
    subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True)
    return True


# --- log analysis ---------------------------------------------------------------
STEP = re.compile(r"ai-step ai=(\w+) flag=(\d+) stage (\d+)->(\d+) calls=(\d+) cmds=(-?\d+) "
                  r"turn_age=(\d+)ms think=([\d.]+)ms gap=(-?[\d.]+)ms")


def summarize(log_text, exit_code, elapsed):
    lines = log_text.splitlines()
    steps = [m.groups() for m in map(STEP.search, lines) if m]
    per_ai = {}
    for ai, _flag, _s0, _s1, _calls, cmds, _age, think, gap in steps:
        a = per_ai.setdefault(ai, {"steps": 0, "commands": 0, "think_ms": 0.0, "gaps_ms": []})
        a["steps"] += 1
        a["commands"] += max(0, int(cmds))
        a["think_ms"] += float(think)
        if float(gap) >= 0:
            a["gaps_ms"].append(float(gap))
    for a in per_ai.values():
        g = a.pop("gaps_ms")
        a["think_ms"] = round(a["think_ms"], 1)
        a["gap_median_ms"] = round(statistics.median(g), 1) if g else None
        a["gap_total_ms"] = round(sum(g), 1)
    def has(text):
        return any(text in l for l in lines)
    return {
        "exit_code": exit_code,
        "elapsed_s": round(elapsed, 1),
        "build": next((l.split("game build: ", 1)[1] for l in lines if "game build: " in l), None),
        "robot_mode": has("robot mode:"),
        "documents_redirected": has("robot: Documents ->"),
        "hooked": has("hooked AI turn driver"),
        "ai_turns_started": sum(1 for l in lines if "robot: AI turn" in l and "started" in l),
        "ai_steps": len(steps),
        "finished_by": "turn limit" if has("AI turns done") else "time limit" if has("time limit of") else "other (crash or closed)",
        "per_ai": per_ai,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--game", default=r"C:\Games\H5_test", help="test copy of the game (never the Steam folder)")
    p.add_argument("--exe", default="MMH55_Utility_64.exe")
    p.add_argument("--map", default="/Maps/Multiplayer/A1M4/map.xdb", help="map file inside the game data")
    p.add_argument("--difficulty", default="1", help="0 normal, 1 hard, 2 heroic")
    p.add_argument("--turns", type=int, default=8, help="stop after this many AI turns (all AI players together)")
    p.add_argument("--seconds", type=int, default=600, help="hard time limit")
    p.add_argument("--setvar", action="append", default=[], help='extra profile setting, e.g. "ai_time_limit = 20000"')
    p.add_argument("--shot-every", type=int, default=30, help="seconds between screenshots (0 = none)")
    p.add_argument("--command-delay", type=int, default=8, help="seconds after start-up before the map is started")
    p.add_argument("--end-turn-every", type=int, default=5,
                   help="the DLL ends the passive human's turn (when no AI is playing) at most every N seconds")
    p.add_argument("--press-enter-every", type=int, default=0,
                   help="robot keyboard: press Enter every N seconds (experimental; 0 = never)")
    p.add_argument("--repeat-command", help="console command the DLL runs every --repeat-every seconds")
    p.add_argument("--repeat-every", type=int, default=0)
    p.add_argument("--command",help="console command to run instead of the default advmap line")
    p.add_argument("--restore-intro", action="store_true", help="put the intro videos back in the test copy and exit")
    args = p.parse_args()

    game = pathlib.Path(args.game)
    check_game_dir(game)
    if args.restore_intro:
        restore_intro(game)
        return
    bin_dir, log, command = prepare(game, args)
    out = REPO / "build" / "robot" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True)

    # Not "-advmap" on the command line: WinMain runs the console command
    # "mainmenu" right after it (0x00d112a7), so the game always lands in the
    # main menu. The DLL runs the "advmap" command itself instead.
    cmd = [str(bin_dir / args.exe)]
    (out / "command.txt").write_text(" ".join(cmd) + "\nconsole: " + command + "\n", encoding="utf-8")
    print("starting:", " ".join(cmd), "| console command:", command)
    start = time.time()
    proc = subprocess.Popen(cmd, cwd=bin_dir)
    next_shot, shots = start + 10, 0
    try:
        while proc.poll() is None:
            if time.time() - start > args.seconds + 60:
                print("game did not close itself in time: killing it")
                proc.kill()
                break
            if args.shot_every and time.time() >= next_shot:
                if screenshot(proc.pid, out / ("shot-%03d.png" % shots)):
                    shots += 1
                next_shot = time.time() + args.shot_every
            time.sleep(1)
    finally:
        if proc.poll() is None:
            proc.kill()
    exit_code = proc.wait()
    elapsed = time.time() - start
    # Leave the test copy playable by hand: without this file the DLL does not
    # enter robot mode (it only logs AI steps).
    (bin_dir / "open-homm5-robot.ini").unlink(missing_ok=True)

    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    (out / "open-homm5.log").write_text(text, encoding="utf-8")
    # the game's own console output (game_writelog = 1 writes it to console.txt, UTF-16)
    console = bin_dir / "console.txt"
    if console.exists():
        raw = console.read_bytes()
        enc = "utf-16" if raw[:2] in (codecs.BOM_UTF16_LE, codecs.BOM_UTF16_BE) else "utf-16-le" if raw[1:2] == bytes(1) else "latin-1"
        (out / "console.txt").write_text(raw.decode(enc, errors="replace"), encoding="utf-8")
    summary = summarize(text, exit_code, elapsed)
    summary["command"] = cmd
    summary["console_command"] = command
    summary["screenshots"] = shots
    (out / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    print("results in", out)


if __name__ == "__main__":
    main()
