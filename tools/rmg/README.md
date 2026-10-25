# Random map templates

Step 1 of the map generator plan ([docs/plans/map-generator.md](../../docs/plans/map-generator.md)): templates for the game's own random map generator, made from a few choices. How templates work: [docs/subsystems/rmg.md](../../docs/subsystems/rmg.md).

| File | What it is |
|---|---|
| `template_maker.js` | Builds a template (`NDb::SRMGTemplate` XML) from choices. One file for both the page and Node. |
| `template-maker.html` | The page, published privately for the owner at https://claude.ai/artifact/9Pi8pvjCUEy78TkVb6PMRK. It loads `template_maker.js` and gives a `.zip` to extract into the game folder. |
| `make_templates.js` | Writes the ready templates (the list `READY`) to `src/mods/rmg/templates/`. The install package ships them. |
| `test_template_maker.js` | Checks the ready templates and 400 random choices. |

```
node tools/rmg/test_template_maker.js
node tools/rmg/make_templates.js
```

The test checks that:
- the XML is well formed;
- there is one start zone per player;
- teammates' start zones are linked;
- every zone can be reached;
- no path is doubled;
- the map size gives zones of 4,000 to 7,500 tiles, or a warning says why it can't.

For the ready templates it also checks that:
- names are unique, start with `OH-` and are plain file names (the installer and uninstaller rely on that);
- no ready template warns about zone sizes;
- every layout below is there, in Normal and in Rich loot;
- the files in `src/mods/rmg/templates/` are exactly what the maker builds now (after changing `READY` or the maker, run `make_templates.js` and commit the files; a template that left `READY` is deleted from the folder).

## The ready templates (24)

The install package puts these in the game's `bin\open-homm5\templates\` (see [../package/README.md](../package/README.md)), and the hook DLL adds them to the random map list. Every player of a multiplayer game needs the same files. The map size is automatic: the maker picks the size that gives zones closest to 5,800 tiles. `Lu` = Large with underground, `XLu` = Extra large with underground, `Hu` = Huge with underground.

How to read the names: `OH-<layout>-<size>-<loot>[-Towns]`.
- **Normal** is the basic shape of a layout: a treasure zone behind each team, one neutral zone between neighbours, one rich zone in the middle. On the team layouts, **Rich** has richer loot, more neutral zones and a bigger map (2v2: two neutral zones per side and two in the middle; the larger layouts also go up a map size). In the free-for-all layouts, Rich only changes the loot.
- **Towns** puts a town in each neutral border zone. **Veryrich** has still more loot than Rich.

| Template | Players | Zones (neutral) | Map | Tiles per zone |
|---|---|---|---|---|
| `OH-2v2-Lu-Normal` | 4 | 9 (5) | Large, 176x176, underground | 6884 |
| `OH-2v2-Lu-Normal-Towns` | 4 | 9 (5) | Large, 176x176, underground | 6884 |
| `OH-2v2-Lu-Rich` | 4 | 12 (8) | Large, 176x176, underground | 5163 |
| `OH-2v2-Lu-Rich-Towns` | 4 | 12 (8) | Large, 176x176, underground | 5163 |
| `OH-2v2v2-Lu-Normal` | 6 | 13 (7) | Large, 176x176, underground | 4766 |
| `OH-2v2v2-Lu-Normal-Towns` | 6 | 13 (7) | Large, 176x176, underground | 4766 |
| `OH-2v2v2-XLu-Rich` | 6 | 16 (10) | Extra large, 216x216, underground | 5832 |
| `OH-2v2v2-XLu-Rich-Towns` | 6 | 16 (10) | Extra large, 216x216, underground | 5832 |
| `OH-3v3-Lu-Normal` | 6 | 11 (5) | Large, 176x176, underground | 5632 |
| `OH-3v3-XLu-Rich` | 6 | 14 (8) | Extra large, 216x216, underground | 6665 |
| `OH-3v3-XLu-Veryrich-Towns` | 6 | 14 (8) | Extra large, 216x216, underground | 6665 |
| `OH-4v4-Lu-Normal` | 8 | 13 (5) | Large, 176x176, underground | 4766 |
| `OH-4v4-Lu-Normal-Towns` | 8 | 13 (5) | Large, 176x176, underground | 4766 |
| `OH-4v4-XLu-Rich` | 8 | 18 (10) | Extra large, 216x216, underground | 5184 |
| `OH-4v4-XLu-Rich-Towns` | 8 | 18 (10) | Extra large, 216x216, underground | 5184 |
| `OH-3P-FFA-Lu-Normal` | 3 | 10 (7) | Large, 176x176, underground | 6195 |
| `OH-3P-FFA-Lu-Rich` | 3 | 10 (7) | Large, 176x176, underground | 6195 |
| `OH-4P-FFA-XLu-Normal` | 4 | 17 (13) | Extra large, 216x216, underground | 5489 |
| `OH-4P-FFA-XLu-Rich` | 4 | 17 (13) | Extra large, 216x216, underground | 5489 |
| `OH-6P-FFA-XLu-Normal` | 6 | 19 (13) | Extra large, 216x216, underground | 4911 |
| `OH-6P-FFA-XLu-Normal-Towns` | 6 | 19 (13) | Extra large, 216x216, underground | 4911 |
| `OH-6P-FFA-XLu-Rich` | 6 | 19 (13) | Extra large, 216x216, underground | 4911 |
| `OH-8P-FFA-Hu-Normal` | 8 | 25 (17) | Huge, 256x256, underground | 5243 |
| `OH-8P-FFA-Hu-Rich` | 8 | 25 (17) | Huge, 256x256, underground | 5243 |

Notes:
- Every Rich template (centre treasure 340,000, Veryrich 428,000) is above the highest shipped treasure value (252,000), so the maker prints its "not tested in the game yet" note for them. The robot built and played `OH-3v3-XLu-Veryrich-Towns` (see below); the others were only checked by the tests above.
- **Once a name has shipped, do not change what it builds.** Add a new entry instead, because every player must have the same file. Adding or removing entries is fine, but a removed template stays on the players' PCs (uninstalling from a newer package only removes the names that package holds).

## The layout

Teams sit around a circle:
- Each team has its players' start zones, linked to each other by a weak guard (or an open path), and optionally its own treasure zones behind them.
- "Border" zones lie between neighbouring teams.
- Rich centre zones sit behind strong guards.

Start zones come first, teammates as neighbours in the numbering (zones 1+2, 3+4, ...), as in MMH5.5's team templates. The game gives player N zone N, but it places the zones itself (see below).

Zone contents start from MMH5.5's balanced templates:
- start zone: 6P-Triangle zone 1;
- border zone: zone 7;
- centre: zone 13;
- team zone: 4P-Team-Mu zone 5.

Loot scales the treasure values and the treasure and shrine points, and guards scale the path guards. The map size is picked so that zones get about 5,800 tiles (MMH5.5's ARMG manual: its templates are tuned for 5,000 to 6,600).

## Tested in the game (2026-10-25, robot on the test copy)

**Generation works.** A loose template file in the test copy's `data\RMG\Templates\` loads through the console (`rmg RMG/Templates/<file>.xdb ...`), and the generator builds the map. The robot generated `OH-2v2v2-Lu-Normal` (Large, underground) and `OH-2v2v2-H-Normal` (Huge, no underground), and each played 2 AI turns. Each map has 6 start towns, one per player, with random factions.

**Players get start zones in order:** the town of player N sits in zone N. We checked this against the zones' starting points in the generator's log, `Zone #N, started at x:y`.

**The generator does not place joined zones side by side.** Our teammates (zones 1+2, 3+4, 5+6) started 50 to 108 tiles apart, and MMH5.5's own `6P-Team-XL` (`S6-11P2-8Z8K2.4a`) did no better: player 5 and player 6 were 139 tiles apart. Zone positions come from the generator's own placement, and a connection only promises a path, which can be long or a teleport. Keeping teammates together needs a change in the generator's placement code (step 3 of the plan), not a template. Until then, the "Teams" layouts are about who reaches which treasure, not about who starts next to whom.

Tools:
- `python tools/rmg/map_towns.py <map.xdb>` lists a generated map's towns, owners and floors.
- `python tools/rmg/terrain_layers.py <GroundTerrain.bin> [x,y ...]` lists the map's ground layers (tile, share of the map, weight near the given tiles). Format: [../../docs/subsystems/rmg.md](../../docs/subsystems/rmg.md), "The terrain file".
- Teams on the map stay as the generator writes them (`Team` 0, `CustomTeams` false); setting them is step 2.

Still to check:
- whether the template shows up in the generator screen's list (the console test bypasses the list);
- whether treasure values above the highest shipped one (252,000) really give more treasure. They do generate: `OH-3v3-XLu-Veryrich-Towns` (428,000 in the centre) built and played in the robot. The generator's "pure road algo failed" messages come about once per zone with every template, MMH5.5's own included, so they are normal.

To test:
```
copy src\mods\rmg\templates\OH-2v2v2-Lu-Normal.xdb C:\Games\H5_test\data\RMG\Templates\
python tools\robot\run_ai_test.py --no-map --seconds 240 --turns 1 --quick-combat --step "8|console|rmg RMG/Templates/OH-2v2v2-Lu-Normal.xdb -players 6 -size 3 -underground 1 start"
```

The generated map is then in `C:\Games\H5_test\data\RMGTemp\CurrentMap\`.
