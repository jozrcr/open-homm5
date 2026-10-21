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
  4. waits until the DLL closes the game (after --turns AI turns or --seconds);
     the DLL saves the game's frames as screenshots now and then (--shot-every);
  5. copies the DLL's log and writes a summary to build/robot/<time>/.

It never runs against the Steam install: --game must be a separate copy.
"""
import argparse
import codecs
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


def install_dll(game):
    dll = REPO / "build" / "hook" / "dinput8.dll"
    if not dll.is_file():
        sys.exit("build the DLL first: powershell -File src\\hook\\build.ps1")
    shutil.copy2(dll, game / "bin" / "dinput8.dll")


def instance_files(game, instance):
    """Robot config and DLL log of one copy of the game. Several copies can run at
    once from the same folder: the DLL adds OPEN_HOMM5_INSTANCE to the names."""
    suffix = "-" + instance if instance else ""
    bin_dir = game / "bin"
    return bin_dir / ("open-homm5-robot%s.ini" % suffix), bin_dir / ("open-homm5%s.log" % suffix)


def prepare(game, args, out, instance="", net_port=None):
    bin_dir = game / "bin"
    ini, log = instance_files(game, instance)

    home = game / ("robot_home-" + instance if instance else "robot_home")
    profiles = home / PROFILE_DIR
    (profiles / "Robot").mkdir(parents=True, exist_ok=True)
    port = "setvar net_game_port = %d\n" % net_port if net_port else ""
    (profiles / "global_a2.cfg").write_text("setvar profile_name = Robot\n" + port, encoding="ascii")
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
    command = "" if args.no_map else (args.command or ("advmap %s 0 %s 1" % (dbid, args.difficulty)))
    # extra script steps: "<seconds>|console|<text>", "<seconds>|menu|<page> <command>", "<seconds>|shot|<label>"
    # the DLL's robot config (src/hook/robot.h lists the keys)
    config = [
        ("home", home),
        ("quit_after_ai_turns", args.turns),
        ("quit_after_seconds", args.seconds),
        ("command", command),
        ("command_delay_seconds", args.command_delay),
        ("end_turn_every_seconds", args.end_turn_every),
        ("press_enter_every_seconds", args.press_enter_every),
        ("repeat_command", args.repeat_command or ""),
        ("repeat_every_seconds", args.repeat_every),
        ("trace_segments", int(bool(args.trace_segments))),
        ("trace_mp", int(bool(getattr(args, "trace_mp", False)))),
        ("segment_ms", args.segment_ms),
        # screenshots: the DLL saves the game's own frames there as .bmp (src/hook/robot_screen.cpp)
        ("screenshot_dir", out),
        ("screenshot_every_seconds", args.shot_every),
        ("net_log", int(bool(getattr(args, "net_log", False)))),
        ("net_share_udp_ports", int(bool(getattr(args, "share_udp_ports", False)))),
        ("net_lan_relay_ports", getattr(args, "relay_ports", "")),
        ("net_log_packets", getattr(args, "net_log_packets", -1)),
        ("net_keepalive_ms", getattr(args, "net_keepalive_ms", 0)),
        ("net_send_interval_ms", getattr(args, "net_send_interval_ms", 0)),
    ] + [("step", s) for s in args.step]
    ini.write_text("".join("%s=%s\n" % kv for kv in config), encoding="ascii")

    # Skip the start-up videos: move them aside in the TEST COPY (restore with --restore-intro).
    video, parked = game / "video", game / "video" / "robot_disabled"
    for name in INTRO_VIDEOS:
        if (video / name).exists():
            parked.mkdir(exist_ok=True)
            shutil.move(str(video / name), str(parked / name))
    for stale in (log, bin_dir / "console.txt"):
        if stale.exists():
            stale.unlink()
    return command


def restore_intro(game):
    parked = game / "video" / "robot_disabled"
    for name in INTRO_VIDEOS:
        if (parked / name).exists():
            shutil.move(str(parked / name), str(game / "video" / name))
    print("intro videos restored in", game / "video")


# --- screenshots ----------------------------------------------------------------
# The game draws nothing on the desktop while it runs in the background, so the
# DLL saves its frames itself (.bmp). Convert them to .png (much smaller).
def bmp_to_png(folder):
    bmps = sorted(folder.glob("frame-*.bmp"))
    if not bmps:
        return 0
    ps = ("Add-Type -AssemblyName System.Drawing; Get-ChildItem -LiteralPath '%s' -Filter 'frame-*.bmp' | "
          "ForEach-Object { $i = [System.Drawing.Image]::FromFile($_.FullName); "
          "$i.Save([IO.Path]::ChangeExtension($_.FullName, 'png'), [System.Drawing.Imaging.ImageFormat]::Png); "
          "$i.Dispose(); Remove-Item -LiteralPath $_.FullName }" % folder)
    subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True)
    return len(bmps)


# --- log analysis ---------------------------------------------------------------
STEP = re.compile(r"ai-step ai=(\w+) flag=(\d+) stage (\d+)->(\d+) calls=(\d+) cmds=(-?\d+) "
                  r"turn_age=(\d+)ms think=([\d.]+)ms gap=(-?[\d.]+)ms(?: types=([0-9a-f,]*))?")


def vtable_names():
    """vftable address -> class name, from analysis/classes.txt."""
    names = {}
    path = REPO / "analysis" / "classes.txt"
    if path.is_file():
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) == 3:
                for vt in filter(None, parts[2].split(", ")):
                    names[int(vt.split("+")[0], 16)] = parts[0].split("::")[-1]
    return names


def wait_after_steps(steps):
    """Median wait before the SAME AI's next step, grouped by what this step sent."""
    names = vtable_names()
    by_kind = {}
    for i, s in enumerate(steps):
        ai, types = s[0], s[9] or ""
        nxt = next((t for t in steps[i + 1:] if t[0] == ai), None)
        if not nxt or float(nxt[8]) < 0:
            continue
        kinds = sorted({names.get(int(v, 16), v) for v in types.split(",") if v})
        key = "+".join(kinds) or "(no commands)"
        by_kind.setdefault(key, []).append(float(nxt[8]))
    return {k: {"count": len(v), "median_wait_ms": round(statistics.median(v), 1)}
            for k, v in sorted(by_kind.items(), key=lambda kv: -len(kv[1]))}


def summarize(log_text, exit_code, elapsed):
    lines = log_text.splitlines()
    steps = [m.groups() for m in map(STEP.search, lines) if m]
    per_ai = {}
    for ai, _flag, _s0, _s1, _calls, cmds, _age, think, gap, _types in steps:
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
        "wait_after_step_kind": wait_after_steps(steps),
    }


def launch(game, exe, instance=""):
    bin_dir = game / "bin"
    env = dict(os.environ)
    env.pop("OPEN_HOMM5_INSTANCE", None)
    if instance:
        env["OPEN_HOMM5_INSTANCE"] = instance
    return subprocess.Popen([str(bin_dir / exe)], cwd=bin_dir, env=env)


def wait(procs, limit_s):
    """Wait for the games to close themselves; kill them after limit_s seconds."""
    start = time.time()
    try:
        while any(p.poll() is None for p in procs):
            if time.time() - start > limit_s:
                print("game did not close itself in time: killing it")
                break
            time.sleep(1)
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
    return [p.wait() for p in procs], time.time() - start


def collect(game, out, instance, exit_code, elapsed):
    """Copy one copy's log (and the console output) to out/, remove its robot
    config, and return its summary."""
    ini, log = instance_files(game, instance)
    # Leave the test copy playable by hand: without this file the DLL does not
    # enter robot mode (it only logs AI steps).
    ini.unlink(missing_ok=True)
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    (out / "open-homm5.log").write_text(text, encoding="utf-8")
    # the game's own console output (game_writelog = 1 writes it to console.txt, UTF-16)
    console = game / "bin" / "console.txt"
    if console.exists():
        raw = console.read_bytes()
        enc = "utf-16" if raw[:2] in (codecs.BOM_UTF16_LE, codecs.BOM_UTF16_BE) else "utf-16-le" if raw[1:2] == bytes(1) else "latin-1"
        (out / "console.txt").write_text(raw.decode(enc, errors="replace"), encoding="utf-8")
    summary = summarize(text, exit_code, elapsed)
    summary["screenshots"] = bmp_to_png(out)
    return summary


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--game", default=r"C:\Games\H5_test", help="test copy of the game (never the Steam folder)")
    p.add_argument("--exe", default="MMH55_Utility_64.exe")
    p.add_argument("--map", default="/Maps/Multiplayer/A1M4/map.xdb", help="map file inside the game data")
    p.add_argument("--difficulty", default="1", help="0 normal, 1 hard, 2 heroic")
    p.add_argument("--turns", type=int, default=8, help="stop after this many AI turns (all AI players together)")
    p.add_argument("--seconds", type=int, default=600, help="hard time limit")
    p.add_argument("--setvar", action="append", default=[], help='extra profile setting, e.g. "ai_time_limit = 20000"')
    p.add_argument("--shot-every", type=int, default=30, help="seconds between screenshots of the game's frames (0 = only shot steps)")
    p.add_argument("--command-delay", type=int, default=8, help="seconds after start-up before the map is started")
    p.add_argument("--end-turn-every", type=int, default=5,
                   help="the DLL ends the passive human's turn (when no AI is playing) at most every N seconds")
    p.add_argument("--press-enter-every", type=int, default=0,
                   help="robot keyboard: press Enter every N seconds (experimental; 0 = never)")
    p.add_argument("--segment-ms", type=int, default=0,
                   help="EXPERIMENT: single-player command segment length in ms (game default 200)")
    p.add_argument("--trace-segments",action="store_true", help="log every single-player command segment")
    p.add_argument("--repeat-command",help="console command the DLL runs every --repeat-every seconds")
    p.add_argument("--repeat-every", type=int, default=0)
    p.add_argument("--step", action="append", default=[],
                   help='extra robot script step "<seconds>|<kind>|<text>", kind = console, menu, ui, msg, map '
                        'or shot (see src/hook/robot.cpp)')
    p.add_argument("--no-map", action="store_true", help="do not start a map (for menu experiments)")
    p.add_argument("--command",help="console command to run instead of the default advmap line")
    p.add_argument("--restore-intro", action="store_true", help="put the intro videos back in the test copy and exit")
    args = p.parse_args()

    game = pathlib.Path(args.game)
    check_game_dir(game)
    if args.restore_intro:
        restore_intro(game)
        return
    out = REPO / "build" / "robot" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True)
    install_dll(game)
    command = prepare(game, args, out)

    # Not "-advmap" on the command line: WinMain runs the console command
    # "mainmenu" right after it (0x00d112a7), so the game always lands in the
    # main menu. The DLL runs the "advmap" command itself instead.
    exe = game / "bin" / args.exe
    (out / "command.txt").write_text("%s\nconsole: %s\n" % (exe, command), encoding="utf-8")
    print("starting:", exe, "| console command:", command)
    (exit_code,), elapsed = wait([launch(game, args.exe)], args.seconds + 60)
    summary = collect(game, out, "", exit_code, elapsed)
    summary["command"] = [str(exe)]
    summary["console_command"] = command
    (out / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    print("results in", out)


if __name__ == "__main__":
    main()
