# Random map templates

Step 1 of the map generator plan ([docs/plans/map-generator.md](../../docs/plans/map-generator.md)): templates for the game's own random map generator, made from a few choices. How templates work: [docs/subsystems/rmg.md](../../docs/subsystems/rmg.md).

| File | What it is |
|---|---|
| `template_maker.js` | Builds a template (`NDb::SRMGTemplate` XML) from choices. One file for both the page and Node. |
| `template-maker.html` | The page, published privately for the owner at https://claude.ai/artifact/9Pi8pvjCUEy78TkVb6PMRK. It loads `template_maker.js` and gives a `.zip` to extract into the game folder. |
| `make_templates.js` | Writes the ready templates (the list `READY`) to `src/mods/rmg/templates/`. The install package ships them. |
| `test_template_maker.js` | Checks the ready templates and 800 random choices. With `--expected <folder>`, also writes what the C++ maker must build (below). |
| `x1_fingerprints.txt` | What 302 recipe names build, recorded once: a test makes sure they never change ([Recipe names](#recipe-names)). |
| `../../src/mods/rmg_maker.cpp` | The same maker in C++, inside the hook DLL: the same choices, checks and bytes. |
| `../../src/mods/rmg_recipe.cpp` | Recipe names: choices to `OH-X1-...` and back. |

```
node tools/rmg/test_template_maker.js
node tools/rmg/make_templates.js
```

**The page and the DLL must agree byte for byte**: two players whose templates differ would build different maps. `powershell -File src\hook\build.ps1 -Test` runs `node tools/rmg/test_template_maker.js --expected build/rmg_parity`, then `src/hook/test/rmg_maker_test.cpp` compares the C++ maker's output with Node's: the 24 ready templates, 400 random choices over every value the page and the recipe names offer, a few special cases (typed names with `&`, `<`, accents; 2 and 67 zones; ...), and 12 choices both must refuse with the same message. **Change `template_maker.js` and `rmg_maker.cpp` together, or neither.** `rmg_maker.cpp` follows the JavaScript function by function, with the same names.

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

| Template | Players | Zones (neutral) | Map | Tiles per zone | Recipe name |
|---|---|---|---|---|---|
| `OH-2v2-Lu-Normal` | 4 | 9 (5) | Large, 176x176, underground | 6884 | `OH-X1-221110nnxxxxxxxa` |
| `OH-2v2-Lu-Normal-Towns` | 4 | 9 (5) | Large, 176x176, underground | 6884 | `OH-X1-221111nnxxxxxxxa` |
| `OH-2v2-Lu-Rich` | 4 | 12 (8) | Large, 176x176, underground | 5163 | `OH-X1-221220rnxxxxxxxa` |
| `OH-2v2-Lu-Rich-Towns` | 4 | 12 (8) | Large, 176x176, underground | 5163 | `OH-X1-221221rnxxxxxxxa` |
| `OH-2v2v2-Lu-Normal` | 6 | 13 (7) | Large, 176x176, underground | 4766 | `OH-X1-321110nnxxxxxxxa` |
| `OH-2v2v2-Lu-Normal-Towns` | 6 | 13 (7) | Large, 176x176, underground | 4766 | `OH-X1-321111nnxxxxxxxa` |
| `OH-2v2v2-XLu-Rich` | 6 | 16 (10) | Extra large, 216x216, underground | 5832 | `OH-X1-321210rnxxxxxxxa` |
| `OH-2v2v2-XLu-Rich-Towns` | 6 | 16 (10) | Extra large, 216x216, underground | 5832 | `OH-X1-321211rnxxxxxxxa` |
| `OH-3v3-Lu-Normal` | 6 | 11 (5) | Large, 176x176, underground | 5632 | `OH-X1-231110nnxxxxxxxa` |
| `OH-3v3-XLu-Rich` | 6 | 14 (8) | Extra large, 216x216, underground | 6665 | `OH-X1-231220rnxxxxxxxa` |
| `OH-3v3-XLu-Veryrich-Towns` | 6 | 14 (8) | Extra large, 216x216, underground | 6665 | `OH-X1-231221vnxxxxxxxa` |
| `OH-4v4-Lu-Normal` | 8 | 13 (5) | Large, 176x176, underground | 4766 | `OH-X1-241110nnxxxxxxxa` |
| `OH-4v4-Lu-Normal-Towns` | 8 | 13 (5) | Large, 176x176, underground | 4766 | `OH-X1-241111nnxxxxxxxa` |
| `OH-4v4-XLu-Rich` | 8 | 18 (10) | Extra large, 216x216, underground | 5184 | `OH-X1-242220rnxxxxxxxa` |
| `OH-4v4-XLu-Rich-Towns` | 8 | 18 (10) | Extra large, 216x216, underground | 5184 | `OH-X1-242221rnxxxxxxxa` |
| `OH-3P-FFA-Lu-Normal` | 3 | 10 (7) | Large, 176x176, underground | 6195 | `OH-X1-311110nnxxxxxxxa` |
| `OH-3P-FFA-Lu-Rich` | 3 | 10 (7) | Large, 176x176, underground | 6195 | `OH-X1-311110rnxxxxxxxa` |
| `OH-4P-FFA-XLu-Normal` | 4 | 17 (13) | Extra large, 216x216, underground | 5489 | `OH-X1-411210nnxxxxxxxa` |
| `OH-4P-FFA-XLu-Rich` | 4 | 17 (13) | Extra large, 216x216, underground | 5489 | `OH-X1-411210rnxxxxxxxa` |
| `OH-6P-FFA-XLu-Normal` | 6 | 19 (13) | Extra large, 216x216, underground | 4911 | `OH-X1-611110nnxxxxxxxa` |
| `OH-6P-FFA-XLu-Normal-Towns` | 6 | 19 (13) | Extra large, 216x216, underground | 4911 | `OH-X1-611111nnxxxxxxxa` |
| `OH-6P-FFA-XLu-Rich` | 6 | 19 (13) | Extra large, 216x216, underground | 4911 | `OH-X1-611110rnxxxxxxxa` |
| `OH-8P-FFA-Hu-Normal` | 8 | 25 (17) | Huge, 256x256, underground | 5243 | `OH-X1-811110nnxxxxxxxa` |
| `OH-8P-FFA-Hu-Rich` | 8 | 25 (17) | Huge, 256x256, underground | 5243 | `OH-X1-811110rnxxxxxxxa` |

Notes:
- Every Rich template (centre treasure 340,000, Veryrich 428,000) is above the highest shipped treasure value (252,000), so the maker prints its "not tested in the game yet" note for them. The robot built and played `OH-3v3-XLu-Veryrich-Towns` (see below); the others were only checked by the tests above.
- **Once a name has shipped, do not change what it builds.** Add a new entry instead, because every player must have the same file. Adding or removing entries is fine, but a removed template stays on the players' PCs (uninstalling from a newer package only removes the names that package holds).
- **Recipe name:** the same template by its recipe (next section). It needs no file: the hook DLL builds it from the name. Its bytes are the ready file's, except the name in the game's list, which gets a `*` (`OH-2v2v2-Lu-Normal*`), so the two can be told apart when both are listed.

## Recipe names

A template's file name can hold all its choices: `OH-X1-` and 16 characters, one per choice. The hook DLL builds the template from the name alone (`src/mods/rmg_recipe.cpp`, then the C++ maker), so a recipe template needs no file: every PC with the DLL gets the same bytes. In multiplayer the game already sends the template's path to every PC, so nothing else has to travel. Why and how: [docs/plans/in-game-generator.md](../../docs/plans/in-game-generator.md).

Example: **`OH-X1-321110nnxxxxxxxa`** is 3 teams of 2 (2v2v2), 1 team zone, 1 border zone, 1 centre zone, no extras, Normal loot, Normal guards, nothing by distance, best size, underground. The game asks for `RMG/Templates/OH-X1-321110nnxxxxxxxa.xdb` (40 characters).

| Place | Choice (as in `template_maker.js`) | Letters |
|---|---|---|
| 1 | `teams` | `2` to `8` |
| 2 | `perTeam` | `1` to `4` |
| 3 | `teamZones` | `0` to `3` |
| 4 | `borderZones` | `0` to `4` |
| 5 | `centreZones` | `0` to `3` |
| 6 | five yes/no choices | a number from 0 to 31 written `0`-`9`, `a`-`v` (base 32). Add 1 for `borderTowns`, 2 for `teamTowns`, 4 for `allyLink` "open", 8 for `startMines` "woodore", 16 for **no** underground |
| 7 | `loot` | `p` poor, `n` normal, `r` rich, `v` veryrich |
| 8 | `guards` | `w` weak, `n` normal, `s` strong |
| 9 to 12 | `lootByDistance` 0, 1, 2, 3 | `x` the same as `loot`, or a loot letter |
| 13 to 15 | `guardsByDistance` 1, 2, 3 | `x` the same as `guards`, or a guard letter |
| 16 | `size` | `a` best fit, or `T` `S` `M` `L` `X` (XL) `H` `I` |

Rules:
- At most 8 players: `teams` × `perTeam` ≤ 8.
- Names are read in any case, as the game's file names. They are written as above: the sizes in capitals, everything else small.
- Not in a name: a typed name, and guards for the start zones (`guardsByDistance` 0; the page has no such choice either). Choices with one of these have no recipe name.
- **What a name builds:** the maker's template for those choices (`makeTemplate` with no `name`), with `*` added to its name in the game's list. For example `OH-X1-321110nnxxxxxxxa` is listed as `OH-2v2v2-Lu-Normal*`.
- A few templates have two names: a tier set to the overall level (`n` with Normal loot) builds the same as `x`, and a size equal to the best fit builds the same as `a`. Both names work.
- Anything else is not a recipe: the DLL leaves it to the game.

**The whole format is two tables** at the top of `src/mods/rmg_recipe.cpp`: one row per place (the choice's `template_maker.js` name, its letters, its values in the same order) and the five yes/no bits. To show recipe names on the page, copy those two tables into JavaScript as they are; `x1_fingerprints.txt` (below) can then check the page too.

**`X1` never changes.** `X1` is the version of the format. What an `X1` name builds must stay the same forever: each player builds the template from its name on their own PC, and saved maps keep the name. So:
- the tables of `rmg_recipe.cpp` never change;
- `template_maker.js` and `rmg_maker.cpp` never change what they build for choices an `X1` name can hold (the same rule as for the ready templates);
- new choices, new values, or different zones for old choices get a new version, `X2`, and the DLL keeps building `X1` names as they are.

`rmg_recipe_test.cpp` enforces it: `x1_fingerprints.txt` records, for 302 `X1` names (the ready templates, every letter at every place, and 200 random names), the size and a fingerprint (FNV-1a, 64 bits) of what each builds. If one changes, the build fails. Never change or remove a line of that file to make the test pass; adding lines is fine.

To offer recipe templates in the game's list before the in-game builder exists, name them in `open-homm5.ini`:

```ini
[rmg]
recipes=OH-X1-321110nnxxxxxxxa,OH-X1-611111nnxxxxxxxa
```

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
- whether treasure values above the highest shipped one (252,000) really give more treasure. They do generate: `OH-3v3-XLu-Veryrich-Towns` (428,000 in the centre) built and played in the robot. The generator's "pure road algo failed" messages come about once per zone with every template, MMH5.5's own included, so they are normal.

To test:
```
copy src\mods\rmg\templates\OH-2v2v2-Lu-Normal.xdb C:\Games\H5_test\data\RMG\Templates\
python tools\robot\run_ai_test.py --no-map --seconds 240 --turns 1 --quick-combat --step "8|console|rmg RMG/Templates/OH-2v2v2-Lu-Normal.xdb -players 6 -size 3 -underground 1 start"
```

The generated map is then in `C:\Games\H5_test\data\RMGTemp\CurrentMap\`.
