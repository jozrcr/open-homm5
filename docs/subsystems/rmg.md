# The random map generator (RMG)

Status: **first reading** (2026-10-24), from the game data (read only), the reference exe's strings and RTTI, and public sources. Nothing here has been changed or tested in a running game yet. Plan: [../plans/map-generator.md](../plans/map-generator.md).

## How it works, in one paragraph

A **template** describes a map as a graph: a list of **zones** (start zones, neutral zones), what each zone contains, and the **connections** between zones, with their guards. The player picks a template, a size and a few options on the generator screen. The exe then:
1. places the zones itself (templates have no coordinates);
2. paints terrain;
3. places towns, connections, the main objects, roads, statics and treasure blocks;
4. writes a complete map package to `data\RMGTemp\CurrentMap`, and loads it from there.

These steps come from the generator's log strings.

## Templates

- **Format:** XML `.xdb` files of type `NDb::SRMGTemplate` in `RMG/Templates/` inside the paks. The exe finds them by scanning the folder; `SRMGParameters.Templates` is empty in every pak. A new template should therefore just be a new file, in a pak or in `data\RMG\Templates`. **This is not yet tested.** The schema (from `types.xml`) is the same in ToE and MMH5.5.
- **Template fields:**
  - `Name`, `Zones[]`, `Connections[]`;
  - `MinPlayers` and `MaxPlayers` (at most 8);
  - `MinMapSize` and `MaxMapSize`: the map area in thousands of tiles, underground included (35–40 is medium with underground);
  - `Underground`, `GraalOnMap`, `TestTemplate`.
- **Zone fields (`ZoneDesc`):**
  - Basics: `Index`, and `Setting` (a faction, or `RACE_RANDOM_TYPE`; it picks the terrain and the object pool).
  - Shape and starts: `CanBeWater`, which is false in all 1275 shipped zones (water is a global switch), `Size` (a relative radius, mostly 10 or 20), and `CanBePlayerStart`.
  - Towns: `Town`, `TownGuardStrenght`, `Shipyard`.
  - Production: `Mines[7]`, `AbandonedMines`, `Dwellings[7]`.
  - Densities: `UpgBuildingsDensity`, `TreasureDensity`, `TreasureChestDensity`, `LuckMoralBuildingsDensity`, `ResourceBuildingsDensity`, `RedwoodObservatoryDensity`.
  - Points: `ShopPoints`, `ShrinePoints`, `TreasureBuildingPoints`, `BuffPoints`.
  - Treasure: `TreasureBlocksTotalValue` (0 to 252,000 in shipped templates).
  - Others: `Prisons`, `LandCartographer`, `DenOfThieves`.
- **Connection fields (`ZoneConnection`):** `SourceZoneIndex`, `DestZoneIndex`, `TwoWay`, `GuardStrenght`, `Guarded`, `Wide`.
- **Other data the generator reads:**
  - `RMG/Params/Default.xdb` (`SRMGParameters`): mine radii, guard levels, distances, stack-size limits, default tiles, texts.
  - `GameMechanics/RefTables/RMGPresetTable.xdb`: per faction, the town, tiles, hero pool, dwellings, blockers and **building pools**. Each pool entry has a `Value` and a `GuardStrenght`.
  - `RMG/CustomArmyTemplates/`: neutral stack recipes by power band.
- **Shipped templates:**
  - Vanilla has 19 and ToE adds 4.
  - MMH5.5's `MMH55-Templates.pak` holds 94, replacing 22 of the originals. It is part of its "Advanced RMG".
  - Display names differ from file names (`S1P2Z2M1` is "2P-Standard-M").
  - Shipped templates have between 0 and 73 neutral zones.
- **MMH5.5's "team" templates** (4P-Team-Mu, 4P-Team-XL, 6P-Team-XL, 6P-Team-H/Lu, 8P-Team-H/Lu, 8P-Team-I/XLu) are team-*shaped* only. For example, in 4P-Team-Mu:
  - start zones 1+2 and 3+4 are linked by weak guards (strength 2);
  - each pair shares a rich neutral zone (5 or 6, 168,000 treasure);
  - zones 5 and 6 are joined by a strong guard (14).

  Nothing in them says who is allied with whom.

## The generator screen and the console

- **Options:**
  - map size (7 fixed sizes: 72, 96, 136, 176, 216, 256 and 320, a table at `.rdata 0xe556ac`);
  - underground;
  - monster level, resources and experience (5 steps each);
  - water (none or islands; the enum also has `WATER_PRESENT`, which vanilla offers but ToE/MMH5.5 don't list);
  - number of players (2 to 8);
  - template;
  - hide minimap, truly random towns, grail;
  - a faction per player.
- **What is data and what is code:**
  - The list of options is data: `InfoRMGScreen` in `UI/UIGameRoot.xdb`.
  - Their meanings are a closed enum of 20 `EP_*` IDs in the exe. So **a new option needs code**.
  - A slider control type (`ECT_SLIDER`) exists in the format, but nothing uses it.
- **Step values:** the numeric effect of the 5 steps is compiled into the exe. Two 5-entry float tables in the RMG code (vanilla `0.2, 0.5, 1.0, 2.0, 4.0` at `0x0090760a`) were rewritten by MMH5.5's exe patch. Which options they scale is not yet known.
- **Console:** `rmg RMG/Templates/<file>.xdb [-players N] [-underground 0|1] [-size 0..6] [start]`, and `generatemap` is the same (handler `0x00833c60`). It cannot set water, monsters, resources, experience or factions. The robot uses it ([tools/robot/README.md](../../tools/robot/README.md)).

## Teams: not set by the generator

- **The generator ignores teams:**
  - Every generated map has `Team` = 0 for every player, one team per player in `map-tag.xdb`, and `CustomTeams` = false.
  - There is no team field in templates or parameters.
- **The engine does support teams:**
  - `Player.Team`, the tag's `teams`, `CustomTeams`;
  - a set-team command (`CSetPlayerTeamCmd`) and a lobby team change;
  - Lua `SetPlayerTeam`.
- **Today's workaround** (the owner's own maps): open the generated map in the editor, set teams, save it as a normal map. It is then a fixed map. Multiplayer can no longer regenerate it (see below), so it has to be sent as a file.
- **MMH5.5:**
  - It has a script, `H55_TeamHumansvsAI()` (`H55-Core.lua`), that puts all humans on team 1 and all AI on team 2.
  - It only runs when map scripts are active, which random LAN maps don't have. MMH5.5's `MMH55_Mapmixer.exe` post-processes a generated map to add them.

## Teams in the lobby (static reading, 2026-10-25; not yet checked in a game)

**On normal maps, the lobby doesn't choose teams either.** Teams are map data: `players[].Team` with `CustomTeams`. The lobby's team arrows only move a player to another **open start slot**, which belongs to some team. The sizes of the teams never change. The game's own manual says the same: alliances are fixed by the map.

**The pieces:**
- **UI:**
  - The multiplayer lobby is `UI/MPWait`. Each player row is `UI/MPWait/PlayersList/ItemElements`, with the arrows `team_color_left` and `team_color_right` ("Choose a team"), `TeamIcon` and `TeamNumber`.
  - The single-player custom game screen reuses the same rows.
  - The hotseat lobby has no team widgets.
- **Row:** `NUI::CMPWaitItem` (vtable `0x00e39c94`).
  - The arrow handlers `0x00713860` and `0x00713880` queue a request (type 2 = team, direction).
  - `0x0071ded0` sends it to the host as `CMPWaitChangeRequest`.
  - The host handles it in `NUI::CMPUbiComWaitHostLogic` `0x0071f3d0`, case 2.
  - The lock mask is built in `0x0071e7b0` (bit 4: team locked).
- **Data:** `NWorld::CPlayersStartInfo` (vtable `0x00e92744`, 0x74 bytes).
  - Its slots have a stride of 0x8c: +0 state (0 open, 1 AI, 2 human, 3 closed), +4 id, +8 shown position, +0x18 race, **+0x1c team**, +0x24 client id.
  - `CanChangeTeam` is `0x00c9d740` (vtable slot `0x00e92770`). It allows a change when another open slot exists, and never looks at `CustomTeams`, random maps or the game mode.
  - `SetTeam` is `0x00c9e660` (slot `0x00e92774`). It swaps the occupants of two slots; a slot's own team never changes.
  - The constructor `0x00ca0240` reads `CustomTeams`: if it is set, the slot teams come from `Player.Team`; otherwise every slot gets its own team.
- **How teams reach the other PCs:** through the **game setup**, not the map.
  - The host's start function `0x0071feb0` builds `NMP::CMPStart` (0x98 bytes), which carries the host's whole `CPlayersStartInfo`, teams included. Field 7 of the slot serializer `0x007386d0` is the team.
  - Clients create their players from it (`0x00c9fa80` → `0x004ffa50` → `0x004ff2a0`; the team is at player +0x15c).
  - The regenerated map's `Team` fields are not read again.
- **Lobby display:** the host's team command `CMPWaitChangeTeamUICommand` is only sent when a slot's shown position changes (`0x0071e67d`), not when only its team does.

**So for random maps:**
- **Changing only the host's slot teams before `CMPStart` is built** should give every PC the same teams, with no determinism needed. It might even work for players without our DLL. This is the plan's step B:
  1. a "humans vs AI" option;
  2. free team numbers on the existing arrows, by replacing `CanChangeTeam` and `SetTeam` for random maps and sending the team command when only the team changes.
- **To check with the robot:**
  - what the arrows do (dump the slots around `0x00c9e660`);
  - that the teams arrive on a client (`0x00820830`, player +0x15c) with no desync;
  - which player gets which start zone of a template;
  - the single-player screen's request path.

## Zone placement: links are not neighbours (robot test, 2026-10-25)

- **Player N always starts in zone N:** we checked town positions against the generator log's `Zone #N, started at x:y`.
- **The generator chooses where zones go, and does not keep linked zones next to each other.**
  - In our 2v2v2 templates, teammates (zones 1+2, 3+4, 5+6, linked by a weak guard) started 50 to 108 tiles apart.
  - In MMH5.5's own `6P-Team-XL` they started up to 139 tiles apart.
  - A link only promises a path, which may be long or go through a teleport (MMH5.5 turned the cartographer object into `RMGDimensionDoor`).
- **So templates cannot decide who starts next to whom.** That needs the placement code (step 3 of the plan). The log lines come from that code: zones are placed in an order that is not the index order (13, 5, 6, 7, 10, ... in one run), each with `k == 0.90, R = 21`.

## Factions and the start terrain (static reading, 2026-10-25; not yet checked in a game)

Plan: [../plans/faction-terrain.md](../plans/faction-terrain.md).

- **A zone's faction:**
  - Reading the template (`0x0090bd80`) treats `Setting` 0, 1 and 2 (`RACE_SPECIAL`, `RACE_RANDOM_TYPE`, `RACE_NO_TYPE`) all as "random": it rolls a faction for the zone.
  - **A start zone takes its player's faction instead,** from the generator's player list (`PlayersInfo`, filled from the random map screen's "faction per player").
  - If that player's faction is random, the roll becomes the player's faction.
  - The roll is made either way, so the random numbers used here don't depend on the players' factions.
- **What follows from the faction:**
  - the town (a fixed town of that faction, or a random town with "Random towns" on, `+0xa5` of the generator);
  - the ground, one tile per zone from `DefaultTile` and `OtherTiles` in `RMGPresetTable.xdb`;
  - the decorations and the dwellings.
- **Seen in the robot's test map:** a Haven town on Haven grass, Fortress towns on snow. The map's `sRMGProps.InitialParams.PlayersInfo` keeps the factions the map was made with.
- **The lobby** (`NWorld::CPlayersStartInfo`):
  - **Who may change faction:** `0x00c9e9f0` builds each slot's list of allowed factions (slot `+0x50`/`+0x54`) from the towns and heroes the player owns. A fixed town allows its own faction only; a random town (town type 1) allows several.
  - **The arrows:** `CanChangeRace` (`0x00c9d7b0`, vtable slot `+0x34`) is true when the list has more than one faction, and `0x00c9df40` (slot `+0x38`) cycles through it. The slot's faction is at `+0x18`, 1 = random.
  - **So with "Random towns" off, the lobby locks every player to the generated faction.**
- **"Random" is settled during world creation:** in `0x00c9fa80`, a slot whose faction is 1 takes a pick from its allowed list, with the world's random generator.
- **The generator runs only from the screen (`0x0079acc0`), the join rebuild (`0x007011d0`) and the console (`0x00833d89`).** These are the three callers of the generator getter `0x0090c6c0`; nothing rebuilds the map at game start.
- **Movement cost** (`0x00ca9a70`):
  - roads 75;
  - grass, water, dwarven mines 100;
  - dirt, lava, subterranean, taiga, wasteland 125;
  - sand, snow 150;
  - **no extra cost on the hero's native terrain** (movement settings `+0xc`, filled through the hero's vtable slot `+0x10c`). Which terrain is native to each faction is not yet known.
  - Pathfinding multiplies the extra cost by 0.5 (vanilla).

## The terrain file (`GroundTerrain.bin`, `UndergroundTerrain.bin`)

It uses Nival's chunked binary format. Each chunk is:
- an id byte;
- a size: if the next byte is even, the size is that byte / 2 (1 byte); if it is odd, the size is the next uint32 / 2 (4 bytes).

Chunks nest. Chunk `4` is the version (4); chunk `1.1` holds:

| Chunk | Content |
|---|---|
| `2`, `3` | width and height in tiles (176) |
| `4` | the ground layers: `2` = their count, then one `1` per layer, holding `2` = a weight map (`1`, `2` = 177, 177; `3` = one byte per tile corner) and `3` = the tile path (`/RMG/Tiles/Haven/Grass.xdb#xpointer(/AdvMapTile)`) |
| `5` | the heights, one float per corner |
| `7`, `8`, `15` | per-corner byte grids, not identified |
| `10` | a 353×353 byte grid (half-tile steps), not identified |
| `13`, `14`, `16` | small, not identified |

- **Who reads and writes it:** `0x00a57d90` → `0x00c0faa0` reads it with the structure serializer. The file names come from `0x00a57d10`.
- **Tile types come from the layers:** the reader then checks the layers (`0x00c0f4a0`, "TerrainInfo: Invalid layer detected") and the game works out each tile's ground from them.
- **A script lists the layers:** `python tools/rmg/terrain_layers.py <GroundTerrain.bin> [x,y ...]` prints each layer's tile, its share of the map, and its weight near the given tiles.

## Multiplayer: every PC generates the map itself

- **What travels:** the generated `map.xdb` ends with `sRMGProps` (template, parameters, seed). With `RMGmap` = true, each client **regenerates** the same map from those, instead of downloading it.
- **Consequences:**
  - Every player needs the same templates and data, and our DLL wherever it changes anything.
  - Editing a generated map breaks this: the message is "multiplayer regeneration won't work", and `RMG_MAP_FAIL` appears if the flag is still set.
- **Seeds** look like the Unix time. The editor has a seed field; the game screen doesn't.

## The code

- **Main block:** `0x00903cd0`..`0x0092acf0`, 281 functions, about 154 KB. It holds:
  - `NRMG::CRandomMapGenerator`, `CRandomMap` and `CTerrainProcessor`;
  - the zone classes: `CGameZone` (the base), `CGameDwarvenZone`, `CGameSubterraZone`, and part of `CGameWaterBorderedZone`.
- **Second area:** around `0x00b99550`..`0x00baeb00`, holding `CGameSubInfernoZone`, the rest of the water zone, `CMonsterSetter` and `CTreasureBlockDistributor`.
- **UI:** about 60 functions (`NUI::CRMGScreen` with its option classes, `CRMGConnector`).
- **In total:** roughly 400 functions and 200–250 KB, about 1.5% of the exe. The exe also hardcodes dozens of `/MapObjects/...` paths, for what each zone parameter places.
- **MMH5.5's exe patches:**
  - 5 byte patches inside this code: `0x0090760a`, `0x00920cb2`, `0x00ba77a9`, `0x00ba79d4`;
  - the object path `Cartographer` → `RMGDimensionDoor` (see [../reference-binaries.md](../reference-binaries.md)).
- **Its DLL,** `55u.dll`, adds RMG hooks with the names `DisableSingleStacks`, `DeployMines` and `IRandomMap`.
- The map editor contains the same generator.

## Not verified yet

- That a new template file shows up in the template list, and where it may live (pak, loose file, `.h5u`).
- Whether the lobby lets the host change teams on a random map, and whether a team change keeps multiplayer regeneration working.
- What the two float tables scale.
- The rest of `GroundTerrain.bin`'s format: the layers and heights are known (above); a few grids are not. `map.xdb` is fully described by `types.xml`.
- Why generated maps record `RMGversion` 34 while `Default.xdb` says 37.
- MMH5.5's ARMG manual says to generate maps only with `MMH55_Utility` or the editor, not `MMH55.exe`.

## Outside the game (for ideas)

- **Heroes III** templates (SoD `rmg.txt`, and HotA's in-game template editor with `.h3t` packs) have the same zone-and-connection model. HotA adds:
  - per-zone monster strength and join rules;
  - allowed factions per zone;
  - "conditions of existence" (zones that appear only at some player counts or sizes);
  - mirror templates;
  - a template comparison between players before a game.

  None of them has a team field. Team templates like `mt_TeamJebus` handle allies through the layout.
- **VCMI** (open source) uses JSON templates. Its algorithm:
  1. force-directed zone placement;
  2. Penrose-tiling tile split;
  3. "fractalized" paths;
  4. treasure piles placed most valuable first, guards by zone plus map strength.

  It doesn't customize teams either (a TODO in its source). Its code is **GPL**, so we may use its documented ideas but not copy its code into this MIT repo.
- **For Heroes V:**
  - a 2008 template editor (silpion) whose download is gone;
  - a few 2-player community packs;
  - MMH5.5's ARMG.
  - Nobody has published an external generator that writes `.h5m` maps.
