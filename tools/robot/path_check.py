"""Check the fast paths mod against the game on several saves.

Usage:
    python tools/robot/path_check.py save_1 save_9 save_10 [--seconds 300] [--turns 3] [--ai-setting port_relax=1]

For each save (a name in the owner's Saves folder, see --saves), the robot plays
the AI's turns with the path trace on: twice with the game's own pathfinding,
then once with the mod (src/mods/fast_paths.cpp, plus any --ai-setting). The
two plain runs must match each other first: some saves are not reproducible
even without the mod (sauvegarde_14: different searches from the first one),
and those are reported and skipped. Then path_report.py compares the mod run
with the plain one, search by search. All runs use a frozen copy of the hook
DLL taken at the start, so rebuilding the DLL meanwhile does not mix versions.

Results: build/robot/<time>-check/summary.txt, and the runs next to it.
"""
import argparse
import datetime
import pathlib
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
SAVES = pathlib.Path.home() / "Documents" / "My Games" / "Heroes of Might and Magic V - Tribes of the East" / "Profiles" / "Joz" / "Saves"


def run(save, fast_paths, args, dll):
    cmd = [sys.executable, str(REPO / "tools" / "robot" / "run_ai_test.py"), "--save-file", str(args.saves / (save + ".sav")),
           "--load-save", save, "--trace-paths", "--fast-paths", fast_paths, "--seconds", str(args.seconds),
           "--turns", str(args.turns), "--dll", str(dll)]
    if fast_paths == "on":
        for setting in args.ai_setting:
            cmd += ["--ai-setting", setting]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith("results in "):
            return pathlib.Path(line[len("results in "):].strip())
    raise RuntimeError("robot run failed for %s:\n%s" % (save, out[-2000:]))


def report(a, b):
    return subprocess.run([sys.executable, str(REPO / "tools" / "robot" / "path_report.py"), str(a), str(b)],
                          capture_output=True, text=True).stdout


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("saves_to_check", nargs="+", metavar="save")
    p.add_argument("--saves", type=pathlib.Path, default=SAVES, help="folder with the .sav files")
    p.add_argument("--seconds", type=int, default=300)
    p.add_argument("--turns", type=int, default=3)
    p.add_argument("--ai-setting", action="append", default=[], metavar="KEY=VALUE",
                   help="extra [ai] setting for the mod run, e.g. port_relax=1")
    args = p.parse_args()
    out = REPO / "build" / "robot" / (datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-check")
    out.mkdir(parents=True)
    dll = out / "dinput8.dll"
    shutil.copy2(REPO / "build" / "hook" / "dinput8.dll", dll)
    lines = []
    for save in args.saves_to_check:
        plain = run(save, "off", args, dll)
        again = run(save, "off", args, dll)
        same = report(plain, again)
        if "first difference" in same:
            lines.append("#### %s: NOT REPRODUCIBLE without the mod (plain %s vs %s), skipped\n%s" %
                         (save, plain.name, again.name, same))
        else:
            mod = run(save, "on", args, dll)
            lines.append("#### %s: plain %s (reproduced by %s), mod %s\n%s" %
                         (save, plain.name, again.name, mod.name, report(plain, mod)))
        (out / "summary.txt").write_text("\n".join(lines), encoding="utf-8")
        print(lines[-1], flush=True)
    print("summary in", out / "summary.txt")


if __name__ == "__main__":
    main()
