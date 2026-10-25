"""Robot: a LAN game between two copies of the game on this PC (host + client).

Usage:
    python tools/robot/run_lan_test.py
    python tools/robot/run_lan_test.py --map A1M4/ --seconds 300 --turns 12

Both copies run the same exe from the TEST COPY of the game, each with its own
robot profile, robot config, DLL log and screenshots (the runner sets
OPEN_HOMM5_INSTANCE=host / client; see src/hook/log.h). The robot script:
  host:   Multiplayer > LAN > Create > pick the map > create the game;
  client: Multiplayer > LAN > join the host's game;
  both:   press their "ready" arrow; the host sets the third slot to computer
          and starts.
Once the game starts, both copies run it in lockstep and the DLL logs every AI
step in both, which shows how long AI turns take in a multiplayer game (see
docs/subsystems/multiplayer-sync.md).

Results go to build/robot/<time>-lan/{host,client}/ (log, frames, summary).
It never runs against the Steam install: --game must be a separate copy.
"""
import argparse
import datetime
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import run_ai_test as robot  # noqa: E402

# Each copy gets its own ports: game (net_game_port) and LAN discovery
# (net_lan_match_maker_port; the game's default is 8889). The DLL copies every
# discovery broadcast to the other copy's port on 127.0.0.1 (src/hook/robot_net.cpp).
PORTS = {"host": (8888, 8889), "client": (8890, 8891)}


def instance_args(args, name, steps):
    """The settings run_ai_test.prepare() expects, for one copy of the game."""
    _game_port, discovery = PORTS[name]
    others = ",".join(str(d) for n, (_g, d) in PORTS.items() if n != name)
    return argparse.Namespace(
        map=args.map, difficulty="1", no_map=True, command=None,
        # mp_check_crc: without it the game computes no world checksums at all
        # (0x007c9be0), and the desync check below has nothing to compare
        setvar=["lan_last_login = Robot-%s" % name, "net_lan_match_maker_port = %d" % discovery] +
               (["mp_check_crc = 1"] if args.crc else []) + args.setvar,
        turns=args.turns, seconds=args.seconds, command_delay=8,
        end_turn_every=args.end_turn_every, press_enter_every=0,
        repeat_command=None, repeat_every=0, trace_segments=False, segment_ms=0,
        shot_every=args.shot_every, step=steps, net_log=True, relay_ports=others, trace_mp=args.trace_mp, trace_crc=True,
        net_log_packets=args.net_log_packets, net_tuning=args.net_tuning,
        net_keepalive_ms=args.net_keepalive_ms, net_send_interval_ms=args.net_send_interval_ms,
        net_batched_segments=args.net_batched_segments, net_setting=args.net_setting, mp_segment_ms=args.mp_segment_ms,
        ai_walk_speed_scale=args.ai_walk_speed_scale, walk_step_ms=args.walk_step_ms,
        trace_ai_busy=args.trace_ai_busy, ai_busy_ignore_end=args.ai_busy_ignore_end, trace_hello=args.trace_hello,
        trace_moves=args.trace_moves,
        coop_setting=args.host_coop if name == "host" else args.client_coop,
        audio_setting=args.host_audio if name == "host" else args.client_audio,
        rmg_setting=args.host_rmg if name == "host" else args.client_rmg,
        save_setting=args.save_setting + (args.host_save if name == "host" else args.client_save),
        notify_setting=args.notify_setting,
        ui_setting=args.ui_setting + (args.host_ui if name == "host" else args.client_ui),
        lang_setting=args.host_lang if name == "host" else args.client_lang,
        camera_setting=args.camera_setting + (args.host_camera if name == "host" else args.client_camera),
        debug_setting=args.host_debug if name == "host" else args.client_debug,
        resolution=args.resolution,
        fast_paths=args.fast_paths, ai_setting=args.ai_setting,
        # the client never runs the AI (the host does), so "no AI step for 4
        # turn ends" means nothing there
        stuck_exit=name == "host")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--game", default=r"C:\Games\H5_test", help="test copy of the game (never the Steam folder)")
    p.add_argument("--exe", default="MMH55_Utility_64.exe")
    p.add_argument("--map", default="A1M4/", help="part of the map file path (e.g. A1M4/ = Trefle)")
    p.add_argument("--turns", type=int, default=12, help="each copy stops after this many AI turns")
    p.add_argument("--seconds", type=int, default=180, help="hard time limit")
    p.add_argument("--shot-every", type=int, default=0, help="seconds between screenshots (0 = only shot steps)")
    p.add_argument("--end-turn-every", type=int, default=5,
                   help="the DLL ends its human player's turn (when no AI is playing) at most every N seconds")
    p.add_argument("--setvar", action="append", default=[], help="extra profile setting for both copies")
    p.add_argument("--start-at", type=int, default=43, help="script time of the host's start (seconds)")
    p.add_argument("--client-ready-at", type=int, default=36,
                   help="script time of the client's 'ready' (later for a random map, which the client rebuilds "
                        "when it joins; --client-step 'N|arrow|1 ready' repeats it safely)")
    p.add_argument("--trace-mp", action="store_true", help="log every multiplayer segment step and command sent")
    p.add_argument("--mp-segment-ms", type=int, default=0,
                   help="EXPERIMENT: multiplayer segment length 100 or 50 ms on both copies (game: 200)")
    p.add_argument("--ai-walk-speed-scale", type=int, default=0,
                   help="EXPERIMENT: AI heroes walk N times faster on both copies")
    p.add_argument("--walk-step-ms", type=int, default=0,
                   help="EXPERIMENT: the hero movement update's 200 ms step on both copies")
    p.add_argument("--trace-ai-busy", action="store_true",
                   help="log each change of the AI manager's 'player busy' check (host)")
    p.add_argument("--ai-busy-ignore-end", action="store_true",
                   help="EXPERIMENT: the host's AI does not wait for its heroes' movement end times")
    p.add_argument("--host-coop", action="append", default=[], metavar="KEY=VALUE",
                   help="[coop] setting for the host copy only, e.g. fast_segments=0 (co-op timing guard)")
    p.add_argument("--client-coop", action="append", default=[], metavar="KEY=VALUE",
                   help="[coop] setting for the client copy only, e.g. guard=0 (as if it had no DLL)")
    p.add_argument("--host-audio", action="append", default=[], metavar="KEY=VALUE",
                   help="[audio] setting for the host copy only, e.g. original_music=1")
    p.add_argument("--client-audio", action="append", default=[], metavar="KEY=VALUE",
                   help="[audio] setting for the client copy only, e.g. log_music=1")
    p.add_argument("--host-lang", action="append", default=[], metavar="KEY=VALUE",
                   help="[lang] setting for the host copy only, e.g. texts=fr (src/mods/languages.cpp)")
    p.add_argument("--client-lang", action="append", default=[], metavar="KEY=VALUE",
                   help="[lang] setting for the client copy only")
    p.add_argument("--host-rmg", action="append", default=[], metavar="KEY=VALUE",
                   help="[rmg] setting for the host copy, e.g. teams=humans_vs_ai (src/mods/rmg_teams.cpp)")
    p.add_argument("--client-rmg", action="append", default=[], metavar="KEY=VALUE",
                   help="[rmg] setting for the client copy")
    p.add_argument("--save-setting", action="append", default=[], metavar="KEY=VALUE",
                   help="[save] setting for both copies, e.g. autosave_host_only=0 or compression=-1 "
                        "(src/mods/autosave.cpp)")
    p.add_argument("--host-save", action="append", default=[], metavar="KEY=VALUE",
                   help="[save] setting for the host copy only")
    p.add_argument("--client-save", action="append", default=[], metavar="KEY=VALUE",
                   help="[save] setting for the client copy only")
    p.add_argument("--notify-setting", action="append", default=[], metavar="KEY=VALUE",
                   help="[notify] setting for both copies, e.g. sound=1 or trace=1 (src/mods/notify.cpp; robot "
                        "runs default to sound=0 popup=0)")
    p.add_argument("--autosave", action="store_true",
                   help="the host ticks the setup screen's autosave option (the checkbox named 'realtime'), so "
                        "every PC autosaves at the start of each day")
    p.add_argument("--ui-setting", action="append", default=[], metavar="KEY=VALUE",
                   help="[ui] setting for both copies, e.g. trace_players=1 (src/mods/player_list.cpp)")
    p.add_argument("--host-ui", action="append", default=[], metavar="KEY=VALUE", help="[ui] setting, host only")
    p.add_argument("--host-debug", action="append", default=[], metavar="KEY=VALUE",
                   help="[debug] setting, host only, e.g. hang_test_at=120 (src/hook/watchdog.cpp)")
    p.add_argument("--client-debug", action="append", default=[], metavar="KEY=VALUE", help="[debug] setting, client only")
    p.add_argument("--client-ui", action="append", default=[], metavar="KEY=VALUE", help="[ui] setting, client only")
    p.add_argument("--camera-setting", action="append", default=[], metavar="KEY=VALUE",
                   help="[camera] setting for both copies, e.g. zoom_out=150 (src/mods/camera.cpp)")
    p.add_argument("--host-camera", action="append", default=[], metavar="KEY=VALUE",
                   help="[camera] setting, host only")
    p.add_argument("--client-camera", action="append", default=[], metavar="KEY=VALUE",
                   help="[camera] setting, client only")
    p.add_argument("--resolution", default="1024x768", metavar="WxH",
                   help="both game windows' size, e.g. 1920x1080 (default 1024x768)")
    p.add_argument("--simultaneous", action="store_true",
                   help="the host ticks the setup screen's simultaneous turns option (the checkbox named "
                        "simultaneus): all humans play at once")
    p.add_argument("--trace-moves", action="store_true", help="log the start-move command and the instant-walk decision")
    p.add_argument("--trace-hello", action="store_true",
                   help="EXPERIMENT: both copies send co-op guard hellos and log their order")
    p.add_argument("--net-log-packets", type=int, default=-1, help="packets logged per socket and direction")
    p.add_argument("--net-tuning", choices=["on", "off"], default="on",
                   help="the LAN link tuning mod (src/mods/net_tuning.cpp) on both copies; off = the game's timings")
    p.add_argument("--net-keepalive-ms", type=int, help="mod setting: idle links send every N ms (mod default 50)")
    p.add_argument("--net-send-interval-ms", type=int, help="mod setting: links may send every N ms (mod default 10)")
    p.add_argument("--net-batched-segments", type=int,
                   help="mod setting: idle PCs batch up to N segment reports (mod default 0)")
    p.add_argument("--net-setting", action="append", default=[], metavar="KEY=VALUE",
                   help="extra [net] setting for both copies, e.g. drain_reports=0 or view_delay_ms=200 (lockstep mod)")
    p.add_argument("--no-crc", dest="crc", action="store_false",
                   help="leave the game's world checksums off (mp_check_crc; on by default for the desync check)")
    p.add_argument("--fast-paths", choices=["on", "off"], default="on",
                   help="the fast pathfinding mod (src/mods/fast_paths.cpp) on both copies (default on, like for "
                        "players; off = the game's own pathfinding)")
    p.add_argument("--ai-setting", action="append", default=[], metavar="KEY=VALUE",
                   help="extra [ai] setting for both copies, e.g. port_relax=1 (repeatable)")
    p.add_argument("--quick-combat-state", type=int, default=0,
                   help="state of the setup screen's quick combat checkbox (0 = on, so battles resolve by "
                        "themselves; -1 = leave it)")
    p.add_argument("--host-step", action="append", default=[], help="extra host script step (see run_ai_test.py)")
    p.add_argument("--client-step", action="append", default=[], help="extra client script step")
    args = p.parse_args()

    game = pathlib.Path(args.game)
    robot.check_game_dir(game)
    out = robot.REPO / "build" / "robot" / (datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-lan")
    host_steps = [
        "8|menu|multi lan",
        "11|ui|NUI::CLobbyScreen create",
        "15|map|" + args.map,
        "16|shot|host-options",
        "17|msg|create",
        "22|shot|host-waiting",
        # waiting room: rows 0 = host, 1 = client, 2 = the map's third player.
        # State arrows (src/hook/robot_lobby.cpp): the host gets ready; the empty
        # third slot goes open -> computer, so there is an AI to time.
        "36|arrow|0 ready",
        "37|arrow|2 right",
        "40|slots|",
        "41|shot|host-ready",
        "%d|msg|start" % args.start_at,
        "%d|shot|host-started" % (args.start_at + 12),
    ] + (["16|option|quick_combat %d" % args.quick_combat_state] if args.quick_combat_state >= 0 else []) \
      + (["16|option|realtime 0"] if args.autosave else []) \
      + (["16|option|simultaneus 0"] if args.simultaneous else []) + args.host_step
    client_steps = [
        "10|menu|multi lan",
        "24|shot|client-lobby",
        "26|game|0",
        "27|ui|NUI::CLobbyScreen join",
        "32|shot|client-joined",
        "%d|arrow|1 ready" % args.client_ready_at,
        "%d|shot|client-started" % (args.start_at + 12),
    ] + args.client_step

    robot.install_dll(game)
    copies = [("host", PORTS["host"][0], host_steps), ("client", PORTS["client"][0], client_steps)]
    for name, port, steps in copies:
        (out / name).mkdir(parents=True)
        robot.prepare(game, instance_args(args, name, steps), out / name, name, port)
    procs = []
    for name, _port, steps in copies:
        print("starting", name, "with steps:", steps)
        procs.append(robot.launch(game, args.exe, name))
    exit_codes, elapsed = robot.wait(procs, args.seconds + 60, leader=procs[0])

    summaries = {}
    for (name, _port, _steps), code in zip(copies, exit_codes):
        summaries[name] = robot.collect(game, out / name, name, code, elapsed)
        if name != "host" and summaries[name]["finished_by"].startswith("other") and procs[0].poll() is not None:
            summaries[name]["finished_by"] = "closed after the host"
        (out / name / "summary.json").write_text(json.dumps(summaries[name], indent=2), encoding="utf-8")
    print(json.dumps({n: {k: s[k] for k in ("exit_code", "finished_by", "ai_turns_started", "ai_steps", "screenshots")}
                      for n, s in summaries.items()}, indent=2))
    print("results in", out)


if __name__ == "__main__":
    main()
