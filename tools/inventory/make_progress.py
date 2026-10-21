"""Regenerate progress/progress.json (the dashboard data) from committed files only.

Usage:
    python tools/inventory/make_progress.py

Reads progress/inventory.csv, progress/matched.txt, progress/equivalent.txt and
src/decomp/*/unit.json. Needs no Ghidra export and no game files except the
reference hash, so CI can run it on every merge.
"""
import collections
import csv
import datetime
import json
import pathlib

REPO = pathlib.Path(__file__).resolve().parents[2]
REFERENCE_SHA256 = "94309157dc65498203c1ee0c357a30ae8788184b91d73bcd5eb5c3159aa81375"
LIBRARY_ORIGINS = {"crt", "lua", "xiph", "directx", "ubi", "thunk"}
GENERATED_ORIGINS = {"adjustor", "eh", "init"}
SUBSYSTEMS = ["ai", "net", "adventure", "combat", "ui", "render", "io"]


def read_list(name):
    """Addresses (first column) of progress/<name>, ignoring comments."""
    path = REPO / "progress" / name
    if not path.is_file():
        return {}
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            parts = line.split()
            out[int(parts[0], 16)] = parts[1] if len(parts) > 1 else ""
    return out


def main():
    with open(REPO / "progress" / "inventory.csv", encoding="utf-8") as f:
        funcs = list(csv.DictReader(f))
    matched = read_list("matched.txt")
    equivalent = read_list("equivalent.txt")

    game = [f for f in funcs if f["origin"] == "game"]
    by_sub = collections.defaultdict(lambda: [0, 0])
    for f in game:
        s = by_sub[f["subsystem"] or "unassigned"]
        s[0] += 1
        if int(f["address"], 16) in matched:
            s[1] += 1

    units = []
    for path in sorted((REPO / "src" / "decomp").glob("*/unit.json")):
        cfg = json.loads(path.read_text(encoding="utf-8"))
        addrs = [int(a, 16) for a in cfg.get("functions", {})]
        units.append({
            "name": path.parent.name,
            "subsystem": cfg.get("subsystem", ""),
            "functions": len(addrs),
            "matched": sum(1 for a in addrs if a in matched),
        })

    progress = {
        "generated": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "reference": "H5_Game.exe 3.1 (sha256 %s)" % REFERENCE_SHA256,
        "totals": {
            "functions": len(game),
            "library": sum(1 for f in funcs if f["origin"] in LIBRARY_ORIGINS),
            "generated": sum(1 for f in funcs if f["origin"] in GENERATED_ORIGINS),
            "matched": sum(1 for f in game if int(f["address"], 16) in matched),
            "equivalent": sum(1 for f in game if int(f["address"], 16) in equivalent),
        },
        "subsystems": [{"name": s, "functions": by_sub[s][0], "matched": by_sub[s][1]}
                       for s in SUBSYSTEMS + ["unassigned"]],
        "units": units,
    }
    with open(REPO / "progress" / "progress.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(progress, f, indent=2)
        f.write("\n")
    print(progress["totals"])


if __name__ == "__main__":
    main()
