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
- The format of `GroundTerrain.bin`: the only unknown for writing whole maps ourselves; `map.xdb` is fully described by `types.xml`.
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
