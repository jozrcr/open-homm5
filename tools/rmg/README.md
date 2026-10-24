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

Start zones come first, teammates next to each other (zones 1+2, 3+4, ...), as in MMH5.5's team templates.

Zone contents start from MMH5.5's balanced templates:
- start zone: 6P-Triangle zone 1;
- border zone: zone 7;
- centre: zone 13;
- team zone: 4P-Team-Mu zone 5.

Loot scales the treasure values and the treasure and shrine points, and guards scale the path guards. The map size is picked so that zones get about 5,800 tiles (MMH5.5's ARMG manual: its templates are tuned for 5,000 to 6,600).

## Not yet tested in the game

The first test, on the test copy, failed before the generator ran: the game could not start Direct3D, because the PC's screen was locked.

Still to check:
- that a template in `data\RMG\Templates\` shows up and generates;
- treasure values above the highest shipped one (252,000), which "rich" reaches in the centre zones;
- whether the game hands out start zones in index order (teammates next to each other).

To test:
```
copy src\mods\rmg\templates\OH-2v2v2-Lu-Normal.xdb C:\Games\H5_test\data\RMG\Templates\
python tools\robot\run_ai_test.py --no-map --seconds 240 --turns 1 --quick-combat --step "8|console|rmg RMG/Templates/OH-2v2v2-Lu-Normal.xdb -players 6 -size 3 -underground 1 start"
```

The generated map is then in `C:\Games\H5_test\data\RMGTemp\CurrentMap\`.
