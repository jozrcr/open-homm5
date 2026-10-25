# Plan: a start terrain that matches your faction

Status: **research only** (2026-10-25). Static reading of the vanilla 3.1 exe (Ghidra) and of the game data in the test copy. No game was run. Background: [../subsystems/rmg.md](../subsystems/rmg.md), "Factions and the start terrain".

The owner asked: "would it be possible to have the starter biome be race-adequate. If you pick Haven you spawn in grass, Academy sand etc... Is that too hard?"

The owner's decision (2026-10-25): **automatic from the lobby (B), or nothing.** Choosing the factions in the template builder (option A) is rejected: it would lock a map to its factions.

**The owner's answers (2026-10-25), after reading this plan:**
1. **B(a), the rebuild at start**, with its trade-offs: a longer start, only when a faction changed; and contents that may differ from the preview.
2. **Players left on "Random"** in the lobby keep the faction the map was made with.
3. **The lobby's faction arrows are unlocked on random maps**, as part of the same setting, so nobody has to tick "Random towns".
4. **Off by default:** `[rmg] faction_terrain=0`.

B(b) is used only if B(a) hits a wall, and the coordinator is told first.

## Built (2026-10-25): `src/mods/rmg_factions.cpp`, setting `[rmg] faction_terrain`

B(a) works in the robot's LAN games. How it is built, in short (details in [src/mods/README.md](../../src/mods/README.md)):
- **The lobby (host):** on random maps, every start slot may take any faction.
- **Agreeing before the map loads:** each client's DLL says hello to the host in the lobby, with a waiting-room request the game ignores. The host rebuilds only when every human client said hello.
- **The decision travels in the map's name:** the host's start message names `Maps/RMG/<guid>-OH1-<one digit per player>/` instead of the map. Every PC that has the mod rebuilds that map before the game looks it up, with the game's own generator, as a joining client does. A client does it when it reads the start message, the host when its world starts. A PC without the mod could not even find the map, so it can never start on a different world.
- **Players left on "Random"** get the faction the map was made with.

**What step 0 found** (the open points of section 4):
- **Which map the start loads:** the start message carries the map's id, a path. Each client looks it up when it reads the message (`0x00ca0c70`), the host when its world starts (`0x00820830`). So a new path, built just before, is loaded fresh, and the lobby's cached copy of the old map does not matter.
- **Where a joining client's map goes:** a new package `Maps\<map name>.N.h5m`, with the same files inside as the host's (`Maps/RMG/<guid>/`).
- **A second build in one game session** works.

**Robot tests, two copies on this PC:**

| Test | Rebuild time | Packages | World checksums |
|---|---|---|---|
| The host picked Haven, where its zone was made for Inferno | 9.2 s on the host, 8.7 s on the client | the same, byte for byte | 504, all equal |
| The client picked Inferno, where its zone was made for Dungeon | | the same | 507, all equal |
| Setting on, no faction changed | none ("the map stays") | | 508, all equal |

- **In the first test,** the host's start zone changed from Inferno ground and decorations to Haven grass and trees.
- **In the second test,** the zone is underground: cave ground whatever the faction, so only the town changed.

**Still open:**
1. **The lobby unlock on fixed-town maps,** which the robot cannot test yet (see the README).
2. **Linux (Proton).**
3. **A line on the loading screen** while the map is built.
4. **Every rebuild leaves a package in `Maps`,** as each join does in the game itself.
5. **After an MMH5.5 update,** an old `...-OH1-...` package of the same map and factions would be reused by the PCs that have one. Deleting old `-OH1-` packages after an update avoids that.

## Short version

- **Yes, it is possible.** Recommended: **B(a), rebuild the map at game start.** Once the lobby's factions are final, every PC rebuilds the random map with each player's faction. The game's own generator then paints each start zone for its player's faction: the town, the ground, the trees and rocks, and the dwellings.
- **The cost:**
  - **A longer start**, only in games where someone picked a faction other than the one the map was made with. Building a map took 2 to 25 seconds on this PC (3 to 14 zones). All PCs build at the same time.
  - **The map changes:** the zones stay in the same places, but the objects inside them (treasure, guards, decorations) may differ from the map the host previewed.
- **Effort:** about **4 sessions** (3 to 5).
- **It changes gameplay**, because heroes move faster on their own faction's ground. So it is a setting, **off by default**.
- **The other way, B(b), repaints only the start zones' ground.** That is feasible: the terrain file's format is now mostly understood. But the trees, rocks and dwellings would stay those of the old faction, and the town only follows when the map was made with "Random towns". It is the fallback if B(a) fails.

## 1. What the game does today

### The factions a zone can have

A template zone's `Setting` takes a value of the `Race` list in `types.xml`. The ground comes from that faction's entry in `GameMechanics/RefTables/RMGPresetTable.xdb` (MMH5.5's copy, in `MMH55-Index.pak`). Each zone gets one ground, picked from the entry's `DefaultTile` and `OtherTiles`.

| Value | Faction | Ground tiles | Terrain type |
|---|---|---|---|
| 0 `RACE_SPECIAL`, 1 `RACE_RANDOM_TYPE`, 2 `RACE_NO_TYPE` | none: the code treats all three as "random" (below) | | |
| 3 `RACE_HEAVEN` | Haven | `Grass`, `Dark_Grass`, `Mossy_Rock` | grass |
| 4 `RACE_PRESERVE` | Sylvan | `Dark_Grass`, `Grass`, `Sand_Stone` | grass |
| 5 `RACE_ACADEMY` | Academy | `Dunes`, `Sand-Dunes`, `River-bed`, `Sand_Cracked` | sand |
| 6 `RACE_DUNGEON` | Dungeon | `SubTerrain`, `SubTerrain_(2)`, `Dead_Land`, ... | subterranean |
| 7 `RACE_NECROMANCY` | Necropolis | `DarkGround`, `Ground 2`, `Weed`, ... | dirt |
| 8 `RACE_INFERNO` | Inferno | `Lava`, `Dead_Land`, `Dead_Land_3` | lava |
| 9 `RACE_DWARF` | Fortress | `Snow`, `Snow_(2)`, `Snow_(3)` | snow |
| 10 `RACE_STRONGHOLD` | Stronghold | `Nidle_Grass`, `Nidle_Dirt`, `Moss`, ... | taiga |

**There is already a "the player's own faction" value: a start zone left on `RACE_RANDOM_TYPE`.**
- When the generator reads the template (`0x0090bd80`), a start zone takes **its player's faction**: the one picked for that player on the random map screen (its "faction per player" lists).
- If that faction is "Random", the generator rolls one, and the roll becomes the player's faction.
- The town, the ground, the decorations and the dwellings then all follow that faction.
- Our template maker sets every zone to `RACE_RANDOM_TYPE`, so our start zones already work this way.

**The robot's test map shows it:** the Haven town stands on Haven grass, the Fortress towns on snow, the Sylvan town on grass. Each comes with its own faction's trees and rocks.

### Why the ground can still be wrong: the lobby

The lobby lets a player change faction only when the player's start town is a **random town**. It builds the list of allowed factions from the towns that player owns (`0x00c9e9f0`). The arrows are enabled only when that list holds more than one faction (`CanChangeRace`, `0x00c9d7b0`).
- **"Random towns" off** (the default): the generator places a town of the player's faction. The lobby then **locks** the faction, and the ground already matches it. *(This is static reading; the robot hasn't checked it, and MMH5.5's DLL could change it.)*
- **"Random towns" on** (the random map screen's check box, "use only random towns"): every town on the map is a random town, so the lobby lets each player pick. The town becomes the picked faction when the game starts. **But the ground stays the one the generator rolled.** That is the mismatch you see.

### When the ground is painted

- **Only on the random map screen (the host) and when a client joins (its rebuild).** The generator is reached from exactly three places:
  - the screen (`0x0079acc0`);
  - the join rebuild (`CRMGConnector`, `0x007011d0`);
  - the `rmg` console command.
- **It never runs at game start.** The world loads the saved map files.
- **Lobby choices left on "Random" are settled while the world is being created** (`0x00c9fa80`). This happens after the map is chosen, and it uses the game's shared random numbers, so every PC gets the same answer.
- **World checksums start after that.**

**So there is no moment built in for this. We have to make one:** after the host presses Start (the start message `CMPStart` reaches every PC with the final factions) and before the world loads the map (the start of `0x00820830`).

## 2. Does the ground change gameplay? Yes

**Movement.** From the movement cost code, `0x00ca9a70` (vanilla exe):

| Ground | Cost of one step |
|---|---|
| Road (any ground) | 75 |
| Grass, water, dwarven mines | 100 |
| Dirt, lava, subterranean, taiga, wasteland | 125 |
| Sand, snow | 150 |
| **The hero's native terrain** | **100** (no extra cost) |

- Pathfinding halves the extra cost in the vanilla code; MMH5.5's text says it removes it.
- MMH5.5's levitation boots remove it, and the Tracker specialty reduces it.
- **Which terrain is native to each hero is set in the code, not in the data.** Each faction's own ground is the likely answer, but this is not yet confirmed.
- **So on the right ground, your heroes may move up to a third faster at the start.**

**Other effects:**
- **Battles:** the battlefield is picked by the ground under the fight (`ByTileMaps` in `types.xml`).
- **MMH5.5's Road Home perk** gives +1 speed in battles on dirt.
- **Music and ambient sounds** follow the ground under the hero.

**So it is a setting, off by default,** like our other gameplay changes. For the setting's description, note that it gives what the original generator does when the factions are picked on the random map screen.

## 3. Option A: rejected

The owner rejected choosing the factions in the template builder: "locking a map to a faction isn't great to me."

## 4. Option B(a): rebuild the map at game start (recommended)

### How it would work

1. **In the lobby,** players pick their factions as usual. With "Random towns" off, our DLL unlocks the start towns' faction arrows on random maps. Every PC does this the same way.
2. **The host presses Start.** Just before the start message is built, the host's DLL settles every faction left on "Random". It keeps the faction the map was made with, so those players need no rebuild. This is the same hook point as the teams feature (`rmg_teams.cpp`), and every PC then receives the final factions.
3. **On every PC,** before the world loads the map, our DLL compares the final factions with the ones the map was made with (`sRMGProps` `PlayersInfo`).
   - If they all match, nothing happens.
   - Otherwise it rebuilds the map exactly as a joining client does: the same template, seed and options, with the players' factions changed. It uses the game's own code: `0x00701090` builds the settings, then the generator's Generate (slot 1) and Write (slot 4). Then it makes the world load the rebuilt map.
4. **The world starts.** Every PC has built the same map from the same inputs, so the worlds are identical.

### What is known and what isn't

| Part | State |
|---|---|
| The generator gives each start zone its player's faction | Known (code and test map) |
| A rebuild from the same inputs gives the same map on every PC | Known: joining clients already rely on it |
| The final factions reach every PC before the map loads | Known: they are in `CMPStart` (`0x0071feb0`, read by `0x00c9fa80`) |
| Settling "Random" on the host | Small: the same place as the teams hook |
| Unlocking the lobby arrows | Small to medium: widen the allowed list (`0x00c9e9f0`) for start towns, on random maps only |
| Making the world load the rebuilt map | **Unknown, the main risk.** The lobby has already loaded the map's description (`map.xdb`) into the game's data cache. Writing new files under the same path may not be enough. The safer way is to build into another folder and change which map the start uses (`0x00820830` looks it up by path). Step 0 decides. |
| All PCs agree to use the feature before the map loads | To design. A PC that doesn't rebuild loads the old map: an instant desync. The co-op guard's hello comes too late (after the start). The host's choice must travel in the start message or in the lobby chat. |

### Risks

- **Load time:**
  - Building took 2 s (3 zones), 8 to 12 s (13 zones) and 25 s (14 zones, extra large with underground) on this PC, writing included. Wine may be slower.
  - It only happens when someone picked a faction other than the one the map was made with.
  - All PCs build in parallel, so the slowest PC sets the wait.
  - We don't know yet whether the start has a time limit for slow PCs.
- **The map is not exactly the previewed one.** The generator places the zones before it uses the factions, so the layout should stay the same. But the start zones now take other objects from the generator's random numbers, so later objects can land elsewhere, even in other zones. *(Expected from the code; to be confirmed.)*
- **Desync:** the inputs are identical on every PC, and the generator is already deterministic for joining clients. The danger is a PC that does not rebuild, which the agreement above covers. A LAN robot run with world checksums proves it.
- **Saved games** hold the whole world, so loading a save never rebuilds anything.

### Effort

About **4 sessions** (3 to 5):

| Step | What | Test |
|---|---|---|
| 0 | Robot checks (needs the test copy): the lobby lock with and without "Random towns"; how the start finds the map; whether a second build in one game session works; which terrain is native to each faction; whether the start has a time limit | Robot logs and screenshots |
| 1 | Settle "Random" on the host; unlock the start towns' faction arrows on random maps; the agreement between PCs | LAN robot: both PCs log the same final factions |
| 2 | The rebuild at start and loading the rebuilt map, with a "Preparing your homelands..." line on the loading screen | Robot: Haven picked in a zone built for Inferno gives a Haven town on grass |
| 3 | Settings, docs, tests: a LAN robot run with 4+ AI turns and every world checksum equal; the owner on Linux (Proton) | LAN report; the owner's group |

## 5. Option B(b): repaint only the start zones (fallback)

### What's new: the terrain file format is mostly understood

`GroundTerrain.bin` (and `UndergroundTerrain.bin`) is the game's usual chunked binary. Each chunk is an id byte and a size, and chunks nest. In the test map (176×176 tiles):

| Chunk | What it holds |
|---|---|
| `4` | The version (4) |
| `1.1.2`, `1.1.3` | The width and height in tiles |
| `1.1.4` | **The ground layers** (17 in this map). Each layer is a tile file path (for example `/RMG/Tiles/Haven/Grass.xdb`) plus a weight map: one byte (0 to 255) per tile corner, 177×177. |
| `1.1.5` | The heights: one float per corner |
| `1.1.7`, `1.1.8`, `1.1.15` | More per-corner byte grids (not identified) |
| `1.1.10` | A 353×353 byte grid, at half-tile steps (not identified) |

**The game computes each tile's ground type from the layers when it loads the file** (`0x00a57d90` → `0x00c0faa0`). So moving weight from one layer to another, for example from `Lava` to `Grass` inside a start zone, would change both the look and the gameplay. Our file hook could serve the changed file at game start, as it serves templates today.

### What it would leave wrong

- **Trees, rocks and other decorations** are picked for the zone's faction, so lava rocks would stay on the new grass. Swapping them would change which tiles can be walked on: too risky.
- **The start zone's dwellings** would stay those of the old faction. Today's "Random towns" games have the same problem.
- **The town** only follows the lobby when the map was made with "Random towns". That box also makes the neutral towns random.
- **The zone's edges:** the map files don't record which tile belongs to which zone. Either we record the zones while the map is built (every PC builds the map, so every PC could record it), or we guess from the ground around the town. A neighbouring zone of the same faction would then be repainted too.
- **Underground start zones** use cave ground whatever the faction; they would stay as they are.

### Cost and risks

- **Load time:** almost none (a 1 MB file).
- **Desync:** the same change on the same file on every PC is safe, with the same agreement problem as B(a).
- **Effort:** about 3 to 4 sessions, plus tuning so the edges blend nicely.

## 6. Comparison

| | B(a) rebuild at start | B(b) repaint the ground |
|---|---|---|
| Ground, town, decorations, dwellings | All match the faction | Ground only (town only with "Random towns") |
| Extra time at start | 2 to 25 s, only when a faction changed | About 0 |
| Same map as the preview | Same layout, different contents | Yes, apart from the ground |
| New code | Medium: mostly calling the game's own generator | Medium: our own terrain painter |
| Main unknown | Making the start load the rebuilt map | The zone edges; how good the blend looks |
| Effort | About 4 sessions (3 to 5) | About 3 to 4 sessions |

## 7. Recommendation

**B(a).** The game's own generator does all the painting, so the result is the one the generator itself makes when you pick a faction on the random map screen. Everything matches, not just the ground. Most of the work is in our DLL's timing (when to build, and which map the start uses). If step 0 shows that the start cannot be made to load a rebuilt map, we switch to B(b).

**What the owner should decide:**
1. **The trade-offs of B(a):** a longer start, only when someone picks another faction than the one the map was made with; and the map's contents can differ from the host's preview. OK?
2. **Players left on "Random" in the lobby** keep the faction the map was made with, so they cause no rebuild. OK?
3. **The faction arrows in the lobby:** with "Random towns" off, the game locks each player's faction to the town the generator placed. Should our setting unlock them on random maps? Proposal: yes. Otherwise you must tick "Random towns", which also makes every neutral town random.
4. **Default:** off, as a setting in `open-homm5.ini` (`[rmg] faction_terrain=0`). Proposal: yes, per the "original by default" rule.
