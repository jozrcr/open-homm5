# Random map templates

Step 1 of the map generator plan ([docs/plans/map-generator.md](../../docs/plans/map-generator.md)): templates for the game's own random map generator, made from a few choices. How templates work: [docs/subsystems/rmg.md](../../docs/subsystems/rmg.md).

| File | What it is |
|---|---|
| `template_maker.js` | Builds a template (`NDb::SRMGTemplate` XML) from choices. One file for both the page and Node. |
| `template-maker.html` | The page, published privately for the owner at https://claude.ai/artifact/9Pi8pvjCUEy78TkVb6PMRK. It loads `template_maker.js` and gives a `.zip` to extract into the game folder. |
| `make_templates.js` | Writes the ready templates (the owner's first picks) to `src/mods/rmg/templates/`. |
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
- Teams on the map stay as the generator writes them (`Team` 0, `CustomTeams` false); setting them is step 2.

Still to check:
- whether the template shows up in the generator screen's list (the console test bypasses the list);
- treasure values above the highest shipped one (252,000), which "rich" reaches in the centre zones.

To test:
```
copy src\mods\rmg\templates\OH-2v2v2-Lu-Normal.xdb C:\Games\H5_test\data\RMG\Templates\
python tools\robot\run_ai_test.py --no-map --seconds 240 --turns 1 --quick-combat --step "8|console|rmg RMG/Templates/OH-2v2v2-Lu-Normal.xdb -players 6 -size 3 -underground 1 start"
```

The generated map is then in `C:\Games\H5_test\data\RMGTemp\CurrentMap\`.
