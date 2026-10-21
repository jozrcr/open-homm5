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
        setvar=["lan_last_login = Robot-%s" % name, "net_lan_match_maker_port = %d" % discovery] + args.setvar,
        turns=args.turns, seconds=args.seconds, command_delay=8,
        end_turn_every=args.end_turn_every, press_enter_every=0,
        repeat_command=None, repeat_every=0, trace_segments=False, segment_ms=0,
        shot_every=args.shot_every, step=steps, net_log=True, relay_ports=others, trace_mp=args.trace_mp,
        net_log_packets=args.net_log_packets)


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
    p.add_argument("--trace-mp", action="store_true", help="log every multiplayer segment step and command sent")
    p.add_argument("--net-log-packets", type=int, default=-1, help="packets logged per socket and direction")
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
        "17|msg|create",
        "22|shot|host-waiting",
        # waiting room: rows 0 = host, 1 = client, 2 = the map's third player.
        # State arrows (src/hook/robot_lobby.cpp): the host gets ready; the empty
        # third slot goes open -> computer, so there is an AI to time.
        "36|arrow|0 right",
        "37|arrow|2 right",
        "40|slots|",
        "41|shot|host-ready",
        "%d|msg|start" % args.start_at,
        "%d|shot|host-started" % (args.start_at + 12),
    ] + args.host_step
    client_steps = [
        "10|menu|multi lan",
        "24|shot|client-lobby",
        "26|game|0",
        "27|ui|NUI::CLobbyScreen join",
        "32|shot|client-joined",
        "36|arrow|1 right",
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
    exit_codes, elapsed = robot.wait(procs, args.seconds + 60)

    summaries = {}
    for (name, _port, _steps), code in zip(copies, exit_codes):
        summaries[name] = robot.collect(game, out / name, name, code, elapsed)
        (out / name / "summary.json").write_text(json.dumps(summaries[name], indent=2), encoding="utf-8")
    print(json.dumps({n: {k: s[k] for k in ("exit_code", "finished_by", "ai_turns_started", "ai_steps", "screenshots")}
                      for n, s in summaries.items()}, indent=2))
    print("results in", out)


if __name__ == "__main__":
    main()
