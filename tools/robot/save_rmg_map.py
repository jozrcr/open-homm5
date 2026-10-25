"""Save the test copy's last generated random map as a map file, for LAN robot runs.

    python tools/robot/save_rmg_map.py OH-X1-robot-2v2            # -> <test copy>/Maps/OH-X1-robot-2v2.h5m
    python tools/robot/save_rmg_map.py --remove OH-X1-robot-2v2

The console command `rmg ... start` writes the map to data/RMGTemp/CurrentMap.
This packs that folder the way the generator screen's Save does (checked
against MMH5.5's saved random maps): Maps/<name>.h5m, a zip holding
Maps/RMG/<RMGguid>/... A map saved like that keeps its random map settings
(sRMGProps), so in a LAN game every client rebuilds it from its template, as
with a map saved from the screen. The LAN runner then picks it with
--map <RMGguid> (the guid is printed). Remove it after the tests.
"""
import argparse
import pathlib
import re
import sys
import zipfile

p = argparse.ArgumentParser()
p.add_argument("name", help="file name in the test copy's Maps folder, without .h5m")
p.add_argument("--game", default=r"C:\Games\H5_test", help="test copy of the game (never the Steam folder)")
p.add_argument("--remove", action="store_true", help="delete Maps/<name>.h5m instead")
args = p.parse_args()

game = pathlib.Path(args.game)
if "steamapps" in str(game).lower():
    sys.exit("refusing to use %s: only a separate test copy of the game" % game)
out = game / "Maps" / (args.name + ".h5m")
if args.remove:
    if out.exists():
        out.unlink()
        print("removed", out)
    sys.exit(0)

current = game / "data" / "RMGTemp" / "CurrentMap"
xdb = (current / "map.xdb").read_text(encoding="utf-8", errors="replace")
guid = re.search(r"<RMGguid>([^<]+)</RMGguid>", xdb).group(1)
template = re.search(r"<Template href=\"([^\"]+)\"", xdb).group(1)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(current.iterdir()):
        z.write(f, "Maps/RMG/%s/%s" % (guid, f.name))
print("saved", out)
print("guid", guid, "(run_lan_test.py --map %s)" % guid)
print("template", template)
