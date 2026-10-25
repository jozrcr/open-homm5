"""Towns of a generated random map: position, floor, owner, and each owned town's nearest neighbour.

    python tools/rmg/map_towns.py C:/Games/H5_test/data/RMGTemp/CurrentMap/map.xdb

Used to check where the generator put the players (tools/rmg/README.md).
"""
import math
import re
import sys

text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
towns = []
for m in re.finditer(r"<AdvMapTown>(.*?)</AdvMapTown>", text, re.S):
    body = m.group(1)
    pos = re.search(r"<Pos>\s*<x>([^<]*)</x>\s*<y>([^<]*)</y>", body)
    floor = re.search(r"<Floor>([^<]*)</Floor>", body)
    owner = re.search(r"<PlayerID>([^<]*)</PlayerID>", body)
    shared = re.search(r'<Shared href="/MapObjects/([^.]*)', body)
    towns.append({
        "x": float(pos.group(1)), "y": float(pos.group(2)),
        "floor": floor.group(1) if floor else "?",
        "owner": owner.group(1) if owner else None,
        "town": shared.group(1) if shared else "?",
    })

print("%d towns" % len(towns))
for i, t in enumerate(towns):
    print("  %d: %-14s owner %-11s floor %s at (%.0f, %.0f)" % (i, t["town"], t["owner"], t["floor"], t["x"], t["y"]))

owned = [t for t in towns if t["owner"] and t["owner"] != "PLAYER_NONE"]
print("\nnearest other owned town:")
for t in owned:
    u = min((u for u in owned if u is not t), key=lambda u: math.hypot(u["x"] - t["x"], u["y"] - t["y"]))
    print("  %-11s -> %-11s %.0f tiles%s" % (t["owner"], u["owner"], math.hypot(u["x"] - t["x"], u["y"] - t["y"]),
                                          "" if u["floor"] == t["floor"] else " (other floor)"))

print("\nTeam fields:", re.findall(r"<Team>([^<]*)</Team>", text)[:8],
      " CustomTeams:", re.findall(r"<CustomTeams>([^<]*)</CustomTeams>", text))
