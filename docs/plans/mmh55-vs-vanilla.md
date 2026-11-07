# Plan: MMH5.5 versus the original Tribes of the East, and "restore OG" options

**Update 2026-11-06: the original game works (branch `vanilla-support`, for 0.3.5).** The owner asked "Could we work on making the mod 5.5-independant ?" (target: the 2008 game first) and for "an alternate version where we only keep the technical aspects of 5.5 ? Like improved AI (if it's really improved) and better textures". Done and checked with the robot (details in [src/mods/README.md](../../src/mods/README.md)):

- **The 2008 exe works with every mod that does not act on MMH5.5's data** (110 hooks). Computer turns, same map and settings: A1M4, 8 turns: 62 s with MMH5.5, 63 s with the 2008 game; A2M5 (big, 3 computers), 15 turns: 119 s with MMH5.5, **92 s** with the 2008 game (MMH5.5's scripts run every turn). LAN with simultaneous turns: world checks all equal, 0.2 s per AI step.
- **In a folder where MMH5.5 is installed** (`H5_GameOLD.exe` in the owner's group), `mmh55_files.cpp` hides MMH5.5's archives from the 2008 exe, which is then the original game (98 heroes, as without MMH5.5); its own profile and saves (`Documents\H5 Together\Original game`: an MMH5.5 save crashes the 2008 game); `[video] hd_textures=1` keeps MMH5.5's HD textures (each PC may choose: world checks equal with it on one PC only). So **A.4's "a DLL switch that hides MMH5.5's paks"** is built: no second game copy is needed.
- **PEST for the 2008 game** (`endless_turns.cpp`, `[coop] endless_turns`, the host's choice, off): the same 8 changes as MMH5.5's 55u.dll. Without it, the 2008 game's simultaneous turns end when heroes get close (robot: `C C W` then `C W W`); with it they stay (`C C W` to the end).
- **"Improved AI" in MMH5.5 is not a separate part:** co-op runs the original 2008 computer player (the Quantomas AI is only in MMH5.5's single-player exe); what MMH5.5 adds for the computer is its weekly bonus (`H55_AICheatMode`), the Savage/Nightmare modes, and battle fixes mixed with its rule changes in `55u.dll`. Nothing to keep apart; a smarter AI is our phase 5.
- **Hidden on the 2008 game:** the F2 rules panel and its Rules... button (every rule is MMH5.5's; the waiting room's versions line stays), the Options rows original music, black map edge, MMH5.5's script pace, the rules' key and button. Shown only there: endless turns, HD textures.
- **Installers** (install, uninstall, bug report, Windows and Linux) recognise the 2008 exe (`H5_Game.exe` or `H5_GameOLD.exe`: its header's date, and no `55u.dll` in it) and no longer say "H5 Together needs MMH5.5" when it is there.
- **The owner's first evening on the original game (2026-11-06):** Steam's protected `H5_GameOLD.exe` starts only through Steam (a double-click makes Steam start the launch options' exe), and a changed copy of it does not start ("Application load error": the protection checks the header). The original exe may use only **2 GB** (the 2025 rebuild too); MMH5.5's `_64` exes are marked "large address aware" (4 GB). A big map at 2560x1440 ran out: crashes in the random map generator's cache save and when buying a building (robot, single player, those settings: 1.86 GB of the 2 at most). The fix: the installer makes `bin\H5_Original_4GB.exe` from the 2008 exe that MMH5.5's installer carries without Steam's protection (`Install-Original4Gb` / `copy_exe_2008`: found by its header, cut out of the installer file, the 4 GB mark set): a double-click starts it. The owner's next session on it: "super smoothly".
- **Left:** the French pack split (A.4 point 3); hiding maps made for MMH5.5 from the original game's map list (they crash it); Steam's 2025 rebuild (a separate, large port, not planned; also 2 GB only).

Status (below, unchanged): **research only** (2026-11-01). Static reading of our code, of the game and MMH5.5 data in the test copies (`C:\Games\H5_test` = MMH5.5 RC19d, `C:\Games\H5_test_k` = RC19k, both read only), of MMH5.5's `55u.dll`, and of MMH5.5's public patch files on GitHub. No game or robot was run, nothing was downloaded, the Steam folder was not touched. Game files and texts stay out of git; this doc only holds counts, names and our own notes.

The owner asked:
> "Can you run an analysis on how HoMM 5.5 dependent our mod is right now? The goal would be to list the changes/features HoMM 5.5 brings on HoMM 5. My brother and I are very fond of the vanilla HoMM V ToE game and think options should exist to restore some OG functionalities (no governor, only vanilla artifacts, no skill-wheel lock per class, etc...)."

Words used below:
- **Vanilla** or **OG**: Heroes V Tribes of the East 3.1, as Ubisoft/Nival made it (2008).
- **MMH5.5**: the "Might & Magic: Heroes 5.5" mod (RC19d today, RC19k next).
- **The co-op exe**: `MMH55_Utility_64.exe`, the program GameRanger starts. It is the vanilla 3.1 program plus 12 small patches and MMH5.5's DLL `55u.dll`.
- **Pak**: a zip file of game data in `data\`. When two paks hold the same file, the newer copy wins.
- **Our file hook (VFS hook)**: our DLL sits where the game opens its data files and can hand the game a different copy of a file (this is how the original music, the French texts and the black map edge already work). Nothing on disk changes.
- **Desync**: two PCs computing different game worlds. Every rule that changes the world (skills offered, artifacts found, gold, battles) must be **the same on every PC**. Pure presentation (music, texts, textures, camera, volume) may differ per PC; our LAN robot runs already proved that for texts, camera, sky and volume.
- **Session**: one Claude work session, as in our other plans.

## In short

- **Part A, how MMH5.5-dependent we are: very little.** Our add-on was built on the vanilla 3.1 program. MMH5.5's co-op exe has the same code at the same addresses, so almost every mod works on both. A few mods exist *because* of MMH5.5 (original music, the black map edge, the French gap texts and lobby labels) and simply have nothing to do on vanilla. **The real obstacle to vanilla is the exe:** Steam now installs a 2025 rebuild of the game (`H5_Game.exe` 3.1.2.99), which our DLL does not recognise, so it switches itself off there. Vanilla works with the 2008 program (`H5_GameOLD.exe` or an unwrapped copy).
- **Part B, what MMH5.5 changes: almost everything.** 179 of 190 creatures, 107 of 112 spells and 221 of 328 town building costs have other numbers; 99 artifacts are added (96 → 195); 8 classes become 24, each with a **fixed list of 12 skills** it can ever be offered; 128 of 221 skills and abilities have other requirements; a large Lua script layer adds governors, town conversion, the town gate, mine guards, AI cheats, game modes, mixed neutral stacks and more; and `55u.dll` patches **222 spots** of the program's code at start-up (combat, spells, movement, levels up to 99, 8 skill slots, simultaneous turns...).
- **Part C, restoring OG: many wishes are free.** MMH5.5 already has switches for governors, town conversion, the town gate, mine guards, AI cheats, negative artifacts, mixed stacks and more (`H55-Settings.lua`), and an `enabled` switch on most of its code patches (`MMH55Stats.xml`). What has **no** switch: the class skill lock, MMH5.5-only artifacts, the game modes in place of ToE's start bonus, and MMH5.5's numbers for creatures and spells. Those need our DLL.
- **Recommended first batch (quick wins):** (1) an "OG" version of the group's settings pak, no code; (2) turn on the two OG mods we already have (original music, black edge); (3) a DLL check that every PC has the same rules files; (4) the DLL serving the rules chosen by the host, per game. Then the bigger items: vanilla classes, vanilla artifacts only, ToE start bonuses, and a "pure vanilla" mode.

---

## Part A: how MMH5.5-dependent is our add-on?

### A.1 Which program our DLL works in

`src/hook/game_build.cpp` checks the program before installing anything: PE date 2008-06-04, image size, the AI table entry at `0x00e4c8bc`, 12 code bytes of the AI turn driver, and the byte at `0x0081eeb7` (`74` = vanilla, `EB` = MMH5.5's patch). Every hook then checks its own spot before changing it.

| Program | What it is | Recognised as | Our mods |
|---|---|---|---|
| `H5_GameOLD.exe` (Steam DRM) or the unwrapped reference `ref/H5_Game_31_vanilla.exe` | vanilla ToE 3.1, 2008 | `vanilla ToE 3.1 (2008)` | **all on** |
| `bin\H5_Game.exe` shipped inside MMH5.5 RC19k's installer (2008 vanilla without DRM, v3.1.2.98) | vanilla ToE 3.1, 2008 | `vanilla ToE 3.1 (2008)` (same code as our reference) | all on (not run yet) |
| `MMH55_Utility.exe` / `MMH55_Utility_64.exe` | vanilla 3.1 + 12 MMH5.5 patches + `55u.dll` (co-op exe) | `ToE 3.1 + MMH5.5 patches` | **all on** (what the group plays) |
| `MMH55.exe` / `MMH55_64.exe` | the 2011 "3.1j" build with the Quantomas AI, single player only | unknown | all off, game unchanged |
| Today's Steam `bin\H5_Game.exe` | Ubisoft's 2025 rebuild (v3.1.2.99, Visual Studio 2019) | unknown | all off, game unchanged |

Sources: [reference-binaries.md](../reference-binaries.md), [mmh55-rc19k.md](mmh55-rc19k.md) (setup log).

### A.2 Every mod, one by one

"Vanilla exe" means the 2008 program with vanilla data (no MMH5.5 paks). "In principle" means the code has nothing MMH5.5-specific but it was never run there (the robot has only played MMH5.5 copies).

| Mod (file) | Vanilla exe? | MMH5.5 co-op exe? | Needs MMH5.5 data? | Exists because of MMH5.5? | Notes |
|---|---|---|---|---|---|
| AI turn hook, AI trace (`src/hook/hooks.cpp`, `ai_trace.cpp`) | yes | yes | no | no | vanilla code at fixed addresses |
| Hang watchdog (`src/hook/watchdog.cpp`) | yes | yes | no | no | |
| Overlay and panels (`src/hook/d3d9_hook.cpp`, `overlay*.cpp`) | yes | yes | no | no | textures are read through the game's file system, so MMH5.5's paks count when present |
| Net tuning (`net_tuning.cpp`) | yes | yes | no | partly | accepts the vanilla 50 ms and MMH5.5 RC19k's own 20 ms (from `MMH55Stats.xml`) before writing 10 ms |
| LAN games over VPNs (`lan_discovery.cpp`) | yes | yes | no | no | |
| Co-op timing guard (`coop_guard.cpp`) | yes | yes | no | no | |
| Lockstep drain (`lockstep.cpp`) | yes | yes | no | no | |
| One AI decision per frame (`ai_pacing.cpp`) | yes | yes | no | no | |
| Quicker autosaves (`autosave.cpp`) | yes | yes | no | no | |
| "Your turn" notification (`notify.cpp`) | yes | yes | no | no | handles simultaneous and normal turns |
| Player list (`player_list*.cpp`) | yes | yes | no | no | made for simultaneous turns. Without MMH5.5's PEST patch, vanilla's simultaneous turns end when players get close; the list then shows normal turns |
| Fast AI pathfinding (`fast_paths*.cpp`) | yes | yes | no | no | calls the game's step-cost function, so MMH5.5's movement costs (a `55u.dll` patch) are kept. `path_check.py` equality was proven on RC19d only |
| Teams on random maps (`rmg_teams.cpp`) | yes | yes | no | no | the engine's own teams; MMH5.5's "humans vs AI" script is separate |
| Our random map templates (`rmg_templates.cpp`, `rmg_maker.cpp`, `rmg_recipe.cpp`) | in principle | yes | no file, but the recipes copy **MMH5.5's balance numbers** | no | `LandCartographer` places a cartographer in vanilla and a dimension door in MMH5.5 (its exe renames that object) |
| Template builder, random map screen button (`rmg_builder*.cpp`, `rmg_screen.cpp`) | in principle | yes | no | no | the "ARMG Templates" box is the game's own custom-template option (`EP_CUSTOM_TEMPLATE`, also in vanilla ToE's screen); MMH5.5 only renamed it. "Use" already picks the monster level with ×1 guards for each exe (weak in MMH5.5, medium in vanilla) |
| Joining random map games (`rmg_join.cpp`) | yes | yes | no | no | |
| Start terrain matching factions (`rmg_factions.cpp`) | in principle | yes (robot-checked) | no | no | |
| Original music (`original_music*.cpp`) | runs, nothing to undo | yes | it undoes MMH5.5's music; reads the original `data.pak` | **yes** | |
| French texts (`languages*.cpp`) | **partly** | yes | MMH5.5's French pack + ours | **mostly** | on vanilla, our pack's 3.1-patch texts help, but MMH5.5's French pack and our MMH5.5 gap texts (e.g. the lobby "modes") would describe MMH5.5's rules, not vanilla's |
| Lobby mode labels (4 French tooltips in our private gap pack) | not needed | yes | yes | **yes** | MMH5.5 turned ToE's start bonus into game modes |
| Camera zoom (`camera.cpp`) | yes | yes | no | no | battle limits follow whatever the game and `55u.dll` set |
| Mouse wheel under Wine (`wheel*.cpp`) | yes | yes | no | no | |
| Wider volume range (`audio_volume.cpp`) | yes | yes | no | no | |
| Black past the map edge (`edge_sky.cpp`) | runs, nothing to do | yes | **yes**: the violet sky dome exists only in MMH5.5's copy of that light | **yes** | |
| Hero looks (branch `plan-hero-skins`, not merged) | engine yes; the Look button is placed for **MMH5.5's hero screen** layout | yes | no | no | vanilla has 37 map looks, MMH5.5 41 |
| Robot (test tool, `src/hook/robot*.cpp`) | not tested | yes | its lobby steps click MMH5.5's lobby panel | n/a | |

What MMH5.5 things our code reads or touches, in one list:
- **`55u.dll`'s patches:** net tuning knows its 20 ms value; fast paths calls its patched step cost; the camera reads its battle limits. `tools/inventory/mmh55_patch_sites.py` finds 3 functions where a `55u.dll` spot and our code meet (step cost `0x00ca9a70`, send timer `0x0047e553`, a comment at `0x00998060`).
- **MMH5.5's exe patches:** the fingerprint byte `0x0081eeb7`; the random map monster table (`rmg_screen.cpp` tells the two apart).
- **MMH5.5's data:** music (original music), the dusk light (edge sky), its texts (French), its hero screen layout (hero looks button), its lobby panel (robot). Nothing of ours reads MMH5.5's scripts, templates, `55e.dll` or `55.dll`.
- **PEST** (MMH5.5's endless simultaneous turns): used by the player list, notify and the robot, needed by none.

### A.3 Features that exist only because MMH5.5 changed something

| Feature | What MMH5.5 changed |
|---|---|
| Original music (`[audio] original_music=1`) | 32 new tracks replacing terrain, menu, credits, battle and hero music |
| Black map edge (`[camera] edge_sky=none`) | gave one of the 4 random-map lights a violet sky dome |
| French gap texts | MMH5.5's 5,840 English text files replace French ones, and MMH5.5's French pack lags behind (RC19i for RC19k) |
| Lobby mode labels | reused ToE's per-player start bonus as a game-wide mode (Default, Savage, Nightmare) |
| Net tuning's 20 ms case | RC19k sets the send timer to 20 ms itself |
| Camera zoom in battles | bigger battlefields (14 × 12) with a closer zoom (`CombatCameraLimits`) |
| "Weak" monsters in the builder's "Use" | MMH5.5's exe makes "weak" mean ×1.0 guards |

### A.4 What it would take to run fully on vanilla ToE

1. **The exe (the real blocker).** Our DLL knows only the 2008 program. Choices:
   - use `H5_GameOLD.exe` (the owner has it: the GameRanger guide had players rename the old `H5_Game.exe`). Friends who installed after July 2025 may only have the 2025 rebuild;
   - use the 2008 `H5_Game.exe` that MMH5.5 RC19k's installer carries (same code as our reference, no DRM). Its installer keeps Steam's newer file, so it would have to be copied out by hand;
   - teach the DLL the 2025 rebuild. Our code names about **440 program addresses**; all of them move in a recompiled program. That is a port of every hook: many sessions, not recommended.
   - GameRanger starts `bin\H5_Game.exe`, so for vanilla co-op that file must be the 2008 program (the same trick the GameRanger guide uses for MMH5.5).
2. **The data.** A game folder without MMH5.5's paks (`data\MMH55-*.pak`, `UserMODs\MMH55-*.h5u`). Either a second copy of the game (about 4 GB), or a later DLL switch that hides MMH5.5's paks from the game (research needed: the game's pak scan at start-up).
3. **Small fixes in our mods.**
   - French texts: split our pack into "3.1 patch" texts (fine on vanilla) and "MMH5.5" texts (only with MMH5.5), and don't use MMH5.5's French pack on vanilla.
   - The robot's lobby steps: check them on vanilla's lobby.
   - The hero looks button: a vanilla position.
4. **Tests.** The robot suite on a vanilla copy (needs the owner's OK for a new copy), `path_check.py` on vanilla saves, a LAN run with checksums, one random map from our templates.

**Effort:** about **2 to 4 sessions** with the 2008 exe and a separate vanilla copy. Supporting the 2025 rebuild would be a separate, large project.

---

## Part B: what MMH5.5 changes compared with vanilla ToE 3.1

### B.1 Where the changes live

| Layer | What | Size (RC19k) | Can each PC differ? |
|---|---|---|---|
| **Data paks** (`data\MMH55-*.pak`) | creatures, spells, artifacts, classes, skills, buildings, heroes, map objects, templates, texts, interface, textures | 10 paks; about 4,600 base files replaced and 19,600 new | rules: no; interface and textures: probably yes |
| **12 fixed patches in the exe** | bigger limits (artifacts, hero classes), random map numbers, dimension doors, `55u.dll` in place of `um.dll` | 526 bytes | no |
| **`55u.dll` at start-up** | 222 code spots, switched by `MMH55Stats.xml` (`MMH55-Stats.pak`) | 222 spots (RC19d: 202) | no ("must use the exact same MMH55Stats file") |
| **Lua map scripts** (`scripts\H55-*.lua` in `MMH55-Index.pak`) | governors, conversion, town gate, AI cheats, modes, mixed stacks, quests, potions, class-based witch huts... | about 1.1 MB of Lua, 445 functions | no |
| **Settings** | `H55-Settings.lua` (`MMH55-Settings.pak`), `MMH55Stats.xml` | 38 settings + about 75 switches | no |
| **Music, videos** | `music\MMH55\` (32 tracks), intro and outro | | yes |
| **Other programs** | `MMH55_64.exe` (Quantomas AI, single player), editor, Mapmixer | | n/a |

How we counted: every MMH5.5 pak member was compared by path and checksum with `data.pak`, `a2p1-data.pak` and the text and sound paks (`og` scripts in the session scratchpad). The text counts compare with the French Steam texts, so they say "replaced", not "changed in meaning".

### B.2 Game rules

#### Heroes, classes and the skill wheel

| | Vanilla ToE 3.1 | MMH5.5 (RC19d = RC19k) |
|---|---|---|
| Hero files | 118 | 203, campaign copies included (the feature list says 18 heroes per faction, 6 per class) |
| Hero classes | 8, one per faction | **24, three per faction** (a might, a magic and a balanced class) |
| Skills a class can be offered | **all 12 common skills + its faction skill** (13), some at 2% | **a fixed list of 12**. Example, MMH5.5's Knight class: never Sorcery, Light, Destruction or Summoning magic; it gets the Barbarian-style "Shatter" skills instead |
| Class names | tell the faction | reused across factions: 20 Haven heroes carry `HERO_CLASS_RANGER` (vanilla's Sylvan class) |
| Skill slots | 6 | 8 (`55u.dll` patch "hero skills count change") |
| Experience table | vanilla's | rewritten up to level 99; the same as vanilla's below level 25, flatter after (`55u.dll` patch "Levels V2") |
| Attribute growth | per class | per class, other numbers |

Sources: `GameMechanics/RefTables/HeroClass.xdb` (vanilla in `a2p1-data.pak`, MMH5.5 in `MMH55-Index.pak`), [lobby-audio-classes.md](lobby-audio-classes.md) item 4, [hero-skins plan](https://github.com/jozrcr/h5-together/blob/plan-hero-skins/docs/plans/hero-skins.md) (branch), MMH5.5's [levelsV2.yml](https://github.com/Might-Magic-Heroes-5-5/MMH55/blob/master/assembly/patches/production/migrated/levelsV2.yml) and [hero_skills_count_change.yml](https://github.com/Might-Magic-Heroes-5-5/MMH55/blob/master/assembly/patches/production/migrated/hero_skills_count_change.yml), the [feature list](https://www.gamepressure.com/download/heroes-of-might-and-magic-v-tribes-of-the-east-might-magic-heroe/z2f291) (a copy of ModDB's).

**This is the "skill-wheel lock per class".** The lock is in the class table: a class can only be offered the skills on its list. MMH5.5's in-game **skill wheel** (`MMH55-Skillwheel.pak`, a round button on the hero screen and 3,890 pages) shows each class's tree, and its witch-hut script uses its own class list (`H55_GetHeroClass` in `H55-Core.lua`).

#### Skills and abilities (perks)

- Same 221 skills and abilities as vanilla (MMH5.5 cannot add new ones), but **128 have other requirements** and 43 another type. Vanilla's 23 "class" and 8 "unique" abilities became ordinary ones (`GameMechanics/RefTables/Skills.xdb`).
- Many abilities and specialities work differently, through `55u.dll` patches (about 24 spots: Enlightenment, Protector, Barbarian spellbook, morale by alignment, Gating and pendants...) and through scripts (the scripts test heroes' skills about 480 times).

#### Spells

- **107 of 112 spell files changed:** mana cost in 82, circle (level) in 52, damage in 45, duration in 28, school in 10 (`GameMechanics/Spell`).
- About 42 `55u.dll` spots change how spells work: opposite spells no longer cancel each other, magic resistance rules, mass spells (cost ×4 instead of ×2, other strength), Empowered spells (+30% + 1% per level instead of +50%), Earthquake outside sieges, Armageddon fixes, Erratic Mana, spell-boosting artifacts...
- New adventure spells (feature list).

#### Creatures and their numbers

- **179 of 190 creature files changed**: hit points in 174, gold cost in 139, experience in 122, speed in 93, defence in 92, damage in about 90, attack in 86, abilities in 76, initiative in 72, weekly growth in 17 (`GameMechanics/Creature`).
- Creature abilities trigger more often (40% ± 5 instead of 25% ± 3, `CreatureAbilitiesChance`), plus about 40 spots for single abilities (Avenger, Whip Strike, Battle Dive, Chilling Bones, Last Stand, Matron Salvo, Paw Strike...).
- Neutral stacks can be **mixed** (several creature types in one stack; 602 new army templates), grow on banks, and scale with the player's strength (scripts).
- A "creature framework" for up to 999 creatures (`MMH55-Frame.pak` holds 823 placeholder creatures for it, numbered from 250; as far as we can tell, normal maps do not use them).

#### Artifacts

| | Vanilla | MMH5.5 |
|---|---|---|
| Artifacts | 96 | **195** (99 new) |
| Minor / major / relic | 28 / 37 / 31 | 72 / 64 / 59 |
| Vanilla artifacts with another price | | 95 of 96 |
| ... with other hero bonuses | | 62 |
| ... moved to another class (minor/major/relic) | | 24 (e.g. Titan's Trident minor → relic) |

- **New families:** resource artifacts (6), Legion pieces (7, one per tier), books and tomes (19), Shantiri pieces (5), potions and medical items (7), upgraded armours, helms, staffs and swords (12), Orbs (4), Gears (8), Governor artifacts (2), Urgash and Elrath relics (4), the Pirate (6), Monk (4), Guardian (3) and Life (4) sets, and single pieces (Draconic, Sentinel, Eightfold, Codex, a buckler, Steadfast, the two prank artifacts). Six of them cost 80,000 gold and are never sold (Draconic, Sentinel, Eightfold, Codex, the last Shantiri piece, one upgraded helm): probably the feature list's "6 new ultimate items".
- **Negative "prank" artifacts** from Heroes II (Medal of Fizbin and one more): a 2% chance on some rewards (`H55_DocumentAward`), off with `H55_NegativeArtifacts = 0`.
- **Sets rewritten for the 24 classes** (feature list), Sar-Issus set bonuses (`MMH55Stats.xml`).
- **Creature artifacts** (Academy's) can go on neutral creatures; stat caps per artifact (`CreatureArtifacts`).
- The program's artifact limit is raised in the exe (97 → 121) and again by `55u.dll` in RC19k.

Source: `GameMechanics/RefTables/Artifacts.xdb` (vanilla `a2p1-data.pak`, MMH5.5 `MMH55-Index.pak`), `H55-Tables.lua`.

#### Town buildings and dwellings

- **221 of 328 building files cost other resources** (wood in 118, ore in 110, gold in 96...), 7 have other requirements (`GameMechanics/TownBuildingSharedStats`). 281 town specialisation files changed.
- Haven's Training Grounds no longer need the Training skill (`HavenTraining`), the Sacrificial Pit's numbers changed, siege towers hit differently (`TowerDamage`).
- RC19k: the tier 4–7 military posts became **32 per-faction dwellings**.
- On random maps every town starts with a mage guild level 1 (`H55_CompetitiveARMGTowns`).

#### The governor and the other town systems (scripts)

- **Governance:** a second hero can become a town's **governor** (standing in the gate of an own town of its faction, through the town management ability). A governor is tied to its town, gains experience and skills there and gives the town bonuses. One per town. Switch: `H55_GovernanceEnabled`. When off, MMH5.5 lowers the AI's weekly bonus slightly (`H55_AIDifficultyCoef`).
- **Town and dwelling conversion:** convert a captured town or dwelling to your faction, a limited number of times (`H55_TownConvEnabled`, `H55_AmountExtraTownsDwellings`).
- **Town gate:** an advanced town portal between own towns (`H55_TownGateEnabled`).
- **Mine control:** mines can get self-sustaining guard armies (`H55_MineControl`).
- **Dimension doors** on random maps, in place of cartographers (an exe patch plus `H55_DimensionDoors`).
- These three (governance, conversion, mine control) are always off on campaign and scenario maps.

Sources: `H55-Settings.lua`, `H55-Core.lua`; [Steam discussion of MMH5.5](https://steamcommunity.com/app/15370/discussions/0/523897277922518736) (governance and conversion described); MMH5.5's governance tutorial on ModDB (`moddb.com/tutorials/mmh55-governance`, which our tools could not read: ModDB answers 403).

#### Economy

- Creature and building prices (above), artifact prices (95 of 96).
- Income patches: Estates give gold per Knowledge, gold artifacts per Knowledge (`Income`).
- Map gold piles ×2 and common resources more likely on random maps (RC19e notes).
- `H55_LowResGame` (start poorer), potions for sale (`H55_RPGPotions`), weekly and daily payouts by script.

#### AI cheats and game modes

- **AI cheat (scripts, `H55_AICheatMode`, default 4):** each week every computer player gets extra gold (about 20,000 × a factor per town) and extra creatures in its town dwellings. The factor grows with difficulty, map size, game length and the setting. MMH5.5 says this makes up for the losses the AI takes in quick combat. 0 = off.
- **Game modes in place of ToE's start bonus:** Default, Savage (AI heroes never flee; humans and AI on opposite teams), Nightmare (also: AI heroes cannot be looted), chosen through the lobby's bonus arrows; the real bonus is taken back and MMH5.5's own (a minor artifact and creatures for every starting hero) is given. Details: [lobby-audio-classes.md](lobby-audio-classes.md) item 2–3.
- **AI teams up against the humans** in Savage and Nightmare (`H55_NoAutoTeamHumans` stops only the humans' auto team).
- **The AI itself:** co-op uses the original ToE AI (the Quantomas AI is in the single-player exe only). RC19e–k fixed several combat AI faults, some for the original AI too.
- MMH5.5's scripts also hold console helpers that set `ai_time_limit`, the AI's time per turn (`H55_BreakAI` 0.18 s, `H55_CutOffAI` 1.8 s, `H55_RestoreAI` 180 s): the same knob our AI work studies. Nothing in the scripts calls them; a player would type them in the console.

#### Map scripts (H55 Lua)

- `advmap-common.lua` loads `H55-Core.lua`, which loads the settings and `H55-AdvMap.lua`, on every adventure map. **RC19k turns map scripts on for random maps by default** (RC19f–k notes); the RC19k robot's console on a random map shows them running ("Towns prepared for Competitive ARMG Map", "All AI Players have allied against the human players"). With RC19d, in a multiplayer game, they run only on maps with a map script (single player runs them on every map). Details, and the host rule `map_scripts` that decides it for every PC: [host-rules.md](host-rules.md#mmh55s-scripts-on-random-maps-map_scripts-2026-11-01).
- What they do, besides the systems above: mixed neutral stacks, bank and dwelling armies, weekly events, witch huts and memory mentors by class, obelisk and Shantiri quests with forced rewards, the moon disc quest, prisoners, potions, the duel template, start bonuses and modes, AI cheats.
- They are part of the game world: they run on every PC in step, so every PC must read the same scripts and settings.

#### The random map generator and templates (ARMG)

- 94 templates (`MMH55-Templates.pak`), replacing 22 of vanilla's 23; MMH5.5 calls it the Advanced RMG and ships a manual (`ARMG Manual V2.7.pdf`).
- Exe patches: guard strength per monster level (×1.0 / 1.9 / 2.7 / 3.5 / 7.0 instead of ×0.4 / 0.9 / 1.7 / 4 / 12), upgrade-building multipliers, dimension doors.
- Its own preset table, tiles, neutral army templates, lights (the violet sky), and in RC19k per-faction dwellings and other gold and resource numbers.
- Source: [../subsystems/rmg.md](../subsystems/rmg.md), [mmh55-rc19k.md](mmh55-rc19k.md).

#### PEST: simultaneous turns that never end

- The original game's simultaneous turns stop when players get close to each other. **PEST** ("Permanent and Endless Simultaneous Turns", by Deflaktor, 8 `55u.dll` spots) keeps them for the whole game; impossible actions such as three-way battles are ignored. Switch: `<Pest enabled="true"/>`; the host ticks simultaneous turns in the lobby.
- Source: [pest.yml](https://github.com/Might-Magic-Heroes-5-5/MMH55/blob/master/assembly/patches/production/migrated/pest.yml).

#### Combat

- Bigger battlefield (14 × 12), other start-ATB limits and wait cost, a turn timer shown in battle, combat scripts allowed in LAN games.
- War machines: tier 8, hit points grow with hero level, a reworked First Aid Tent, ballista and catapult shots per ability (about 15 spots).
- Splitting a stack before battle moves 1 creature, not half.
- Many single changes (Rune of Berserking, Retaliation Strike against shooters, Hellfire chance, Fear, morale caps...).

### B.3 Interface

| Pak | What | Files |
|---|---|---|
| `MMH55-Skillwheel.pak` | the in-game skill wheel: a round button on the hero screen and its pages per class | 3,907 |
| `MMH55-Creaturepedia.pak` | a creature encyclopedia, opened from the hire and upgrade screens | 4,015 |
| `MMH55-Data.pak` (interface part) | battle screen (417), hero screen (144 + 149 textures), level-up box, hot seat, Academy creature artifacts; 153 portraits instead of 106 | |
| `MMH55-Frame.pak` | not an interface frame: the **frameworks' data** (823 placeholder creatures and 750 of their scripts, 1,004 map object links, 806 cameras, artifact map objects) and MMH5.5's copy of the game's interface root (its lists, e.g. the random map screen options) | 3,815 |
| `MMH55-HDTex.pak` | HD textures: terrain, town exteriors, terrain objects, sky domes | 653 |
| `55u.dll` | the hero screen's Skills and Biography pages merged into one, the backpack scrolls a page at a time | 8 spots |

### B.4 Exe and DLL patches

**The 12 fixed patches in the co-op exe** (`pe_diff.py`, [reference-binaries.md](../reference-binaries.md), purposes from MMH5.5's [migration_not_possible](https://github.com/Might-Magic-Heroes-5-5/MMH55/tree/master/assembly/patches/production/migration_not_possible) files):

| Address | Purpose |
|---|---|
| `0x0081eeb7` | a check always skipped (purpose not found yet; our fingerprint byte) |
| `0x00847891`, `0x0084c9c0`, `0x0084ca7f`, `0x0084cad8` | more artifacts (97 → 121) |
| `0x0090760a` | random map: upgrade-building multipliers |
| `0x00920cb2` + `Cartographer` → `RMGDimensionDoor` | random map: dimension doors |
| `0x00ae29e0` | hero classes 9 → 74 ("New Hero Class Framework") |
| `0x00ba77a9`, `0x00ba79d4` | random map: guard strength per monster level |
| `0x00d11f3c` → `0x00d122e1` | a start-up call into new code (purpose not confirmed) |
| header, import | the 64-bit-memory flag (`_64` exes), `um.dll` → `55u.dll` |

**The 222 run-time spots of `55u.dll` (RC19k), by purpose.** MMH5.5 publishes the patches it moved into `55u.dll` as [yml files](https://github.com/Might-Magic-Heroes-5-5/MMH55/tree/master/assembly/patches/production/migrated). We matched each `55u.dll` spot (`tools/inventory/mmh55_patch_sites.py`) to those files by address, or by the game function it falls in (`progress/inventory.csv`). 165 of 222 matched.

| Purpose | Spots | Examples (MMH5.5's patch names) | Switch in `MMH55Stats.xml`? |
|---|---:|---|---|
| Spells, magic resistance, spell artifacts | ~42 | disable magic opposites, artefacts spellproof, protection perk, spell boost artifacts, erratic mana, earthquake, empowered armageddon, disruptor merge, mass spells | mostly yes |
| Combat: ATB, creature abilities, battlefield | ~42 | atb, avenger chance, whip strike, ability chance, suffering strike, dwarven formations, matron salvo, split fix, sacrificial pit, battlefield size, combat scripts in multiplayer | mostly yes |
| Adventure map: movement, weeks, PEST, random maps | ~34 | global movement (12), week effects (10), PEST (8), new dwelling framework, RMG creature strength | yes (`Movement`, `Weeks`, `Pest`) |
| Heroes: levels, skill slots, abilities | ~24 | levels V2 (12), hero skills count (2), hero morale, enlightenment, barbarian spellbook, protector, pendant and gating, mini artifacts | partly; **none found** for levels and skill slots |
| Combat: war machines and towers | ~15 | first aid tent, ballista/catapult shots, war machine tier, tower damage | yes |
| Interface | 8 | hero screen pages, backpack paging | **none found** |
| Not matched to a file | 57 | battle camera (6, `CombatCameraLimits`), network send timer (1, `TransmissionInterval`), artifact limit (4); 46 others in combat creature, hero and random map code | some |

RC19k added 26 spots and dropped 6 (RC19d had 202). The meaning of each spot's last table field is still unknown.

### B.5 Settings MMH5.5 already offers

#### `H55-Settings.lua` (in `MMH55-Settings.pak`), every setting

Values are RC19k's, the same as RC19d's. "Yours" is the owner's RC19d file. Descriptions are ours, from the file's comments and the scripts.

| Setting | What it does | MMH5.5 default | Yours | OG value |
|---|---|---|---|---|
| `H55_TownGateEnabled` | town gate: heroes travel between own towns | 1 | 1 | **0** |
| `H55_TownConvEnabled` | town and dwelling conversion | 1 | 1 | **0** |
| `H55_AmountExtraTownsDwellings` | how many extra conversions are allowed (1 = MMH5.5's balance, 0 allowed) | 1 | **5** | (unused when conversion is off) |
| `H55_GovernanceEnabled` | governor heroes | 1 | 1 | **0** |
| `H55_GovernorExpCoef` | governors' experience multiplier (never 0) | 1 | 1 | |
| `H55_MineControl` | self-sustaining mine guard armies | 1 | 1 | **0** |
| `H55_DimensionDoors` | dimension doors on random maps can visit towns remotely | 1 | 1 | **0** (the objects stay: exe patch) |
| `H55_DimensionDoorsOnThisMap` | for map makers only; leave 0 | 0 | 0 | 0 |
| `H55_IgnoreExpAdjustment` | 1 = experience not adjusted to neutral stack size | 0 | 0 | |
| `H55_NoMentoring` | 1 = Memory Mentors give +2 Spell Power or Knowledge instead | 0 | 0 | 0 (vanilla has mentoring) |
| `H55_NoHillFortsOnARMG` | 1 = Hill Forts on random maps give +1 Attack or Defence instead | 0 | 0 | 0 |
| `H55_BuildNeutralTowns` | pre-build neutral towns, 0–5 (~20% each) | 0 | 0 | 0 |
| `H55_BuildPlayerTowns` | pre-build player towns, 0–5 | 0 | 0 | 0 |
| `H55_CompetitiveARMGTowns` | random map towns start with mage guild 1 | 1 | 1 | **0** |
| `H55_NoAutoTeamHumans` | 1 = humans do not team up automatically in Savage/Nightmare (AI still does) | 0 | 0 | |
| `H55_AICheatMode` | AI weekly gold and creatures, 0 = off, 1–6 = 40% to 140% (4 = 100%) | 4 | **0** | **0** |
| `H55_LowResGame` | 1 = every player starts with 10 wood, 10 ore, 5 of each rare, 10,000 gold less | 0 | 0 | 0 |
| `H55_GenerateMixedStacks` | mixed neutral stacks by script | 1 | 1 | **0** |
| `H55_MixStacksThreshold` | no mixing on maps with more neutral stacks than this | 200 | 200 | |
| `H55_AskForMixedStacks` | 1 = ask the first human on multiplayer maps | 0 | 0 | |
| `H55_ForceMixedStacks` | 1 = always mix on multiplayer maps | 0 | 0 | |
| `H55_BanksDifficulty` | multiplier for bank (battle site) guards | 1 | 1 | |
| `H55_NeutralStackSize` | multiplier for neutral stacks on the map | 1 | 1 | |
| `H55_PrisonersAdjustToHeroRace` | 1 = prisoner rewards follow the hero's faction, not the player's | 0 | 0 | |
| `H55_ManaObservatories` | Redwood Observatories also restore mana on random maps | 1 | 1 | **0** |
| `H55_NoArtifactMerchantsInTowns` | 1 = no artifact merchants in towns | 0 | 0 | 0 (vanilla has them) |
| `H55_ForcedObelisk` | force the obelisk quest's reward on random maps (0 random, 1–9 a piece) | 0 | 0 | |
| `H55_ForcedShantiri` | the same for the Shantiri quest | 0 | 0 | |
| `H55_NegativeArtifacts` | the negative "prank" artifacts | 1 | **0** | **0** |
| `H55_MoonDiscQuest` | the moon disc quest on every map (not recommended by MMH5.5) | 0 | 0 | 0 |
| `H55_MoonDiscOnARMGMaps` | the moon disc quest on random maps | 1 | 1 | **0** |
| `H55_RPGPotions` | potions for sale on multiplayer and random maps | 0 | **1** | **0** |
| `H55_Duel_WeeksGrowth`, `_HeroLevel`, `_Relics`, `_Majors`, `_Minors`, `_BankDifficulty` | the Duel template's army, level (25), artifacts (4 / 8 / 4) and banks (6) | 8, 25, 4, 8, 4, 6 | same | |

Two traps:
- `MMH55-Index.pak` holds an **older copy** of this file with governance, conversion and mine control at **0**, and `H55-Core.lua` has the same 0 values as fallbacks. The settings pak wins only because its file is newer (2025-10-12). A group settings pak must keep a newer date than that.
- The file is read when a map starts, by every PC. Everyone must have the same file (we compare its SHA-256 today).

#### `MMH55Stats.xml` (in `MMH55-Stats.pak`), every switch

MMH5.5's warning in the file: all multiplayer players must use the exact same file. Another copy can sit in `UserMODs\MMH55-CustomStats.h5u`; RC19k also reads `MMH55-Stats1..9.pak` (the highest number wins). Every switch below is `enabled="true"` in RC19k except where noted; `enabled="false"` turns one off. "Vanilla" values are given where the file or MMH5.5's patch files say them.

| Group | Switch (MMH5.5 value; vanilla value when known) |
|---|---|
| **Adventure map** | `Movement` (home terrain 100, road 75, sea 75 (vanilla 100), dirt/lava/taiga/subterranean penalty 10 (vanilla 25), sand/snow 10 (vanilla 50), diagonal ×1.41, Logistics 5% per level, Boots of Speed 15%, Navigation 25% (vanilla 50%), Pathfinding, Tracker); `InstantTravelMovementRequirements` (20% of movement instead of 50%); `Weeks` (milder week effects); `Pest` (endless simultaneous turns); `SpellShrineExperience` (Barbarians 500 × level instead of 1000) |
| **Battle size, camera, system** | `BattlefieldSize` (14 × 12); `CombatCameraLimits` (pitch −1.6 to −0.3, vanilla −1.05 to −0.61; zoom in 8, vanilla 14.4; zoom out 28); `ShowPlayerTurnTimerInCombat`; `MultiplayerCombatScripts`; `CreatureFramework` (999 creatures, vanilla 179); `System/Networking` (`TransmissionInterval` 0.02 s, vanilla 0.05, RC19k only); `QaiFixes` (the single-player AI only) |
| **ATB (turn order)** | `AbilitiesAtbReset` (ATB after Encourage, Prayer, Banshee Howl, Banish, Avatar of Death, Refresh Rune, Retaliation Strike, Demonic Strike, Spirit Link, Mark of the Sorcerer, Chain Attack, Mass spell cast; start ATB at most 0.15; Wait 0.5; Sorcery 0.08 per level); `ImbueBalistaATB` (fix) |
| **Creature abilities** | `CreatureAbilitiesChance` (40% ± 5, vanilla 25% ± 3); `CreatureAbilities/Fear` (no ATB reset); `Avenger` (crit chance and damage); `CastersLuckDamage_base` (25 + level); `HellfireTriggerChance` (66%); `PawStrikeDivisor` (5); `RuneOfBerserkingChange` (no retaliation instead of double attack); `RangedRetaliationStrike`; `RuneOfTheDragonForm` (not on Fire Dragons); `SacrificialPitHealth` (10); `PowerfeedCapped` (66%); `DjinDarkLightAbility` (shorter spell list); `ChainShotImmunityForCrystalScales`; `ColdDeathImmunityForCrystalScalesAndMechanical`; `ConsumeCorpseSqrtHp`; `DwarfFormationsForAll`; `OneStackSplit` (split moves 1 creature) |
| **Ability and spell modifiers** (`Modifiers`) | `AbsoluteCharge` (×1.5 instead of ×3); `BattleDiveDamage` (75%); `ChillingBones`, `ChillingSteel` (10%); `EnergyChannel` (5%); `Enlightenment` (7); `ErraticMana` (10–30%); `HeroAlignmentMoraleBonus` (0); `LastStandTrigger` (4); `MatronSalvo` (30 + level, vanilla 10 + 2 × level); `OccultismPenetration` (30/40/50/50); `PowerOfStone` (Deflect Arrows); `PerkStats` (Lord of the Undead +2 Knowledge); `ResurrectAllies` (80); `RuneOfBattleRageDamageModifier` (0.5); `SpellProof` (Protection 20%, Spell-proof Bones 20%, Fire Protection 50%); `StaffOfTheNetherworld` (12%); `StormWind` (single-player AI only); `WhipStrike` (Slow, Weakness, Curse) |
| **Spells** | `DisableMagicOpposites`; `DisableMagicResistForNonDamagingSpells`; `EarthquakeTremors`; `EmpoweredArmageddonFix`; `EmpoweredSpellsDamage_base` (30 + 1% per level, vanilla 50%); `FlamingArrowsDamageAndDefense`; `MassSpells` (strength per mass spell); `SpellCost` (mass spells ×4 instead of ×2, Sar-Issus 25% instead of 50%); `SpiritLink` (5); `PyromancyBonus` (removes the −40%); `AvatarOfDeathNoMana`; `DestructionSkill` (shock 0.4 ATB); `SnareFix` (crash fix) |
| **Heroes and stats** | `CreatureStatsModifiers` (Sar-Issus set ×1.5 / ×2; Rune of Dragonform +10 attack and defence; Shield Allies 2% per unit, max 50%; Undead Leadership initiative 4%; resistance of artifacts 173, 174, Forgotten Hero's plate 8, Boots of Interference 14, the Magic Resistance perk 12, Staff of the Magi −50; Rune of Charge +3 speed, frozen −2; Fright Aura −2 morale, vanilla −3; regeneration pool 15%); `ProtectorSpecialization` (1 HP per 10 levels instead of 5); `EnlightenmentForBarbsFix`; `SwapBarbarianSpellbook`; `HavenTraining` (no skill needed for the Training Grounds) |
| **Artifacts and income** | `CreatureArtifacts` (bonuses and caps; on neutral creatures yes, on all creatures **no** (`enabled="false"`)); `ExtendedElementalArtifacts` (×1.15 original set, ×1.25 MMH5.5's set); `WandArtifact` (Spellpower 20 and charges per spell); `Income` (Crown of Courage, Valorous Armour, gold per Knowledge for the bag, sack, horn and Gear 08; Estates 25 per Knowledge) |
| **War machines and sieges** | `WarMachines/Health` (grows with hero level); `WarMachines/Tent` (heal by Defence, cleansing by Light Magic); `WarMachines/Tier` (8 instead of 3); `TowerDamage` (main 200, side 140) |

**What has no switch** in either file: the class table and its skill lists, the skills' requirements, the creatures', spells' and buildings' numbers, the 99 new artifacts, the game modes, the 8 skill slots, levels up to 99, the hero screen layout, the music, the templates.

---

## Part C: "restore OG" options

### C.1 How a rule choice can reach every PC

| Way | How it works | When it applies | Safe? |
|---|---|---|---|
| **1. The same settings pak on every PC** (today) | everyone puts the group's `MMH55-Settings.pak` (and `MMH55-Stats.pak`) in `data\`; we compare SHA-256 | from the next game | yes if everyone copied it; a wrong file desyncs silently |
| **2. A DLL check** | each PC's DLL hashes the rules files as the game reads them (and our own rule settings) and sends the hash in the lobby hello (`rmg_factions.cpp`) and the co-op hello (`coop_guard.cpp`); the log and a message name the PC that differs | at once | it warns; the co-op hello comes ~10 s into a game, the lobby hello before the start |
| **3. Rules served by the DLL, per session** | `[rules]` in `h5-together.ini`: our file hook serves `H55-Settings.lua` (and later other files) with those values; same ini on every PC, checked by way 2 | from the next game start | yes with way 2 |
| **4. Rules from the host, per game** | the host's choice travels in the random map's name (`Maps/RMG/<guid>-OH1-...`, the faction-terrain channel); every PC's DLL serves the matching files; a PC without the DLL cannot even find that map | each random map game | yes, by construction; hand-made maps need another carrier (not studied) |

**Built (2026-11-01): way 4 for any map, with way 2.** The host's choice travels in one more chunk of the start message (`NMP::CMPStart`), which a PC without the mod skips; it is sent only when every human said our lobby hello, so nobody starts on other rules. Hand-made maps work too. The lobby hello also carries the hashes of way 2. See [host-rules.md](host-rules.md). Found there: on a random map straight from the generator, in multiplayer, MMH5.5's scripts do not run at all (no map script), so `H55-Settings.lua` changes nothing on those maps.

Two things to check before ways 3 and 4: whether a **saved game** re-reads `H55-Settings.lua` on load (MMH5.5's scripts keep their variables in the save, probably), and that the file is read at map start on every PC (our file hook sees it; the order was not logged yet).

### C.2 The options, one by one

(a) = does MMH5.5 already have a switch? (b) = how we restore vanilla if not (or how we expose it). (c) = desync handling. (d) = effort and risk.

| # | OG option | (a) MMH5.5 switch | (b) How | (c) Desync | (d) Effort, risk |
|---|---|---|---|---|---|
| 1 | **No governors** | **yes**: `H55_GovernanceEnabled = 0` | edit the group pak; later ways 3–4 | world rule: ways 1–4 | **free**; MMH5.5 lowers the AI cheat a little to match |
| 2 | No town or dwelling conversion | **yes**: `H55_TownConvEnabled = 0` | as 1 | as 1 | free |
| 3 | No town gate | **yes**: `H55_TownGateEnabled = 0` | as 1 | as 1 | free |
| 4 | No mine guard armies | **yes**: `H55_MineControl = 0` | as 1 | as 1 | free |
| 5 | No AI cheats | **yes**: `H55_AICheatMode = 0` (you already) | as 1 | as 1 | free; the AI gets weaker over a long game |
| 6 | No prank artifacts | **yes**: `H55_NegativeArtifacts = 0` (you already) | as 1 | as 1 | free |
| 7 | No mixed neutral stacks | **yes**: `H55_GenerateMixedStacks = 0` | as 1 | as 1 | free |
| 8 | No MMH5.5 extras on random maps (mage guild 1 in every town, mana at observatories, moon disc quest, remote-visit dimension doors, potions) | **yes**: `H55_CompetitiveARMGTowns`, `H55_ManaObservatories`, `H55_MoonDiscOnARMGMaps`, `H55_DimensionDoors` = 0, `H55_RPGPotions` = 0 | as 1 | as 1 | free; the dimension door objects themselves stay (exe patch) |
| 9 | **No class skill lock** (any skill can be offered, as in vanilla) | **no** | Serve `HeroClass.xdb` and the hero files (`<Class>`) through our file hook. Two variants: **(i) vanilla classes**: 8 classes with vanilla chances, read at run time from `a2p1-data.pak`, every hero back to its faction's class; **(ii) unlocked MMH5.5 classes**: keep the 24 classes and their chances, add the missing skills at a small chance (vanilla gives 2%) | world rule (skill offers use the shared random numbers): way 3 first, then way 4 | (i) 3 sessions per session, 5–7 per game ([lobby-audio-classes.md](lobby-audio-classes.md) item 4); (ii) about the same, simpler on hero files; risk: the "Shatter" skills may exclude the magic schools in the engine (to check). See interactions below. **(ii) built 2026-11-02** as host rules step 3 (`class_skills=1`, [host-rules.md](host-rules.md) "Step 3"): `HeroClass.xdb` is read once at start-up, so the table is edited in memory, not served; the Shatter skills exclude nothing in the engine; a save keeps the rule |
| 10 | Vanilla skill and ability tree (requirements) | **no** | serve vanilla `Skills.xdb` | world rule | **not recommended alone**: 128 abilities differ, and `55u.dll` and the scripts implement MMH5.5's meanings. Part of "pure vanilla" (#18) |
| 11 | **Only vanilla artifacts** (MMH5.5's 99 never appear) | **partly**: prank artifacts, potions, moon disc, merchants have switches; nothing for the rest | Three places hand out artifacts: **map objects** (random artifacts on random maps: put the 99 into the map's banned list, `artifactIDs` in `map.xdb`, when the map is built; to confirm it is a ban list), **merchants** (serve `Artifacts.xdb` with `CanBeGeneratedToSell` false for the 99), **scripts** (start bonus, banks, quests, documents: serve `H55-Tables.lua` with the pools filtered) | world rule: the map part through way 4 (the map is rebuilt with the name), the rest ways 3–4 | 1 session to research, 2–3 to build; risk: a script that gives a fixed MMH5.5 artifact (quests) must be found one by one. **Vanilla artifact numbers** (95 prices, 62 bonuses, sets) would be another, bigger step |
| 12 | **ToE start bonuses** instead of MMH5.5's modes | **no** (only `H55_NoAutoTeamHumans`) | serve `H55-AdvMap.lua` with the "StartingBonus" block changed ([lobby-audio-classes.md](lobby-audio-classes.md) item 3; you chose option A, each player picks) | world rule: way 4 | 2–3 sessions (host bonus) or 5–6 (lobby chooser). **Built 2026-11-01** as host rules (the start message, not the map name): `game_mode`, `toe_bonuses` ([host-rules.md](host-rules.md), "Step 2") |
| 13 | Normal simultaneous turns (end on contact, as vanilla) | **yes**: `<Pest enabled="false"/>` in a custom stats file; or untick simultaneous turns in the lobby for normal turns | MMH5.5's own custom stats file (`UserMODs\MMH55-CustomStats.h5u` or `MMH55-Stats1..9.pak`) | world rule: way 1 + way 2 | free, but co-op is much slower without PEST: **not recommended** |
| 14 | Vanilla movement costs | **yes**: `<Movement enabled="false">` | custom stats file | world rule: ways 1–2 | free; our fast paths must be re-checked (`path_check.py`) |
| 15 | Other vanilla combat rules (ATB, battlefield size, ability chances, war machines, mass spells...) | **yes**, one switch each | custom stats file | world rule: ways 1–2 | free per switch, but MMH5.5 tuned these with its own creature and spell numbers: the result is neither MMH5.5 nor vanilla |
| 16 | Hide MMH5.5's skill wheel | **no** | our file hook ignores `MMH55-Skillwheel.pak`'s 10 hero screen files (the button and its pages) | presentation: each PC may choose | 0.5 session; goes with #9 (the wheel would show MMH5.5's trees) |
| 17 | Original textures, original music | music **built** (`[audio] original_music=1`); textures: no | textures: our file hook ignores `MMH55-HDTex.pak` | presentation: each PC | music 0; textures 0.5 session + a LAN check |
| 18 | **"Pure vanilla" mode** (everything OG: numbers, artifacts, classes, scripts, combat) | no | the 2008 exe with no MMH5.5 data: a second game copy, or a DLL switch that hides MMH5.5's paks (research). See A.4 | everyone plays the same copy: ways 1–2 | 2–4 sessions + a test copy; the cleanest OG |
| 19 | MMH5.5's data but **no MMH5.5 scripts** | no | serve vanilla `advmap-common.lua` (no `H55-Core.lua`) | world rule | **not recommended**: the scripts check 104 artifacts and many specialities; those would silently stop working |
| 20 | Vanilla random map templates and generator numbers | no | our template list hides MMH5.5's 94 and serves vanilla's 23 from `data.pak`; the exe's monster table is MMH5.5's (fixed patch) | template paths must match: way 4 | 1–2 sessions; low priority (our own templates already exist) |
| 21 | Skip a `55u.dll` patch that has no switch (8 skill slots, levels to 99, hero screen layout) | no | after `55u.dll` has patched, our DLL writes the original bytes back at all spots of that patch (they are still in the exe file on disk) | rules (slots, levels): ways 3–4; interface: each PC | 1–2 sessions per patch; risk: a patch must be reverted whole, and MMH5.5 data may expect it (e.g. 8 skills with 12-skill class lists) |

### C.3 Interactions to keep in mind

- **Classes, skill wheel, abilities, scripts.** Vanilla classes (#9 i) change *which skills are offered*. They do not change MMH5.5's ability tree (#10), its 8 skill slots, its specialities, or the witch huts (which use MMH5.5's own class list in the scripts). MMH5.5's skill wheel would show the wrong trees, so #16 should go with #9. The hero screen would show vanilla class names. Attribute growth is part of the class (ask: skills only, or attributes too?).
- **Artifacts.** Turning MMH5.5's artifacts off (#11) leaves the vanilla artifacts with MMH5.5's numbers and sets. Some MMH5.5 features hand out their own artifacts (governor artifacts with governance, Shantiri pieces with the Shantiri quest): switching those features off (#1, #8) removes part of the problem for free.
- **Governance and the AI.** MMH5.5 gives the AI a little more weekly cheat when governance is on. With `H55_AICheatMode = 0` this no longer matters.
- **Modes and teams.** Savage and Nightmare put all AIs on one team against the humans. ToE start bonuses (#12) must keep or replace that.
- **Saves.** A game saved with one set of rules must be continued with the same rules. Way 4 carries the rules in the map name, which the save keeps; ways 1 and 3 rely on everyone keeping the same files.
- **Campaigns and scenarios.** MMH5.5 already switches governance, conversion and mine control off there.
- **Fast paths.** Any movement rule change (#14, #18) needs a new `path_check.py` run.
- **MMH5.5 upgrades.** Every served file (scripts, tables) must be matched to the MMH5.5 version; our DLL should refuse a file it does not know (as the music and builder mods do).

### C.4 Recommended first batch (quick wins first)

1. **An "OG" group settings pak (no code, 0.25 session).** Make the RC19k group pak with the OG column of B.5 that you pick (governance, conversion, town gate, mine guards, AI cheat, prank artifacts, mixed stacks, random-map extras, potions). Everyone uses it, as today. Its date must stay newer than MMH5.5's (2025-10-12). Covers options 1 to 8.
2. **Turn on what we already built (0 session):** `[audio] original_music=1`, `[camera] edge_sky=none` (each PC chooses).
3. **The rules check (1 session):** the DLL hashes `H55-Settings.lua`, `MMH55Stats.xml` and our rule settings and compares them in the lobby and co-op hellos (C.1 way 2). This protects batch 1 and everything after it.
4. **Rules from the host (2–3 sessions):** `[rules]` settings on the host (governor, conversion, town gate, mine guards, AI cheat, mixed stacks, prank artifacts...), served as `H55-Settings.lua` on every PC and carried in the random map's name (way 4). No more pak swapping; a small lobby panel can come later from your sketch.

Then, in the order you prefer:
5. No class skill lock (#9) with the skill wheel hidden (#16): 3.5 sessions per session, 5–7 per game.
6. Only vanilla artifacts (#11): 3–4 sessions.
7. ToE start bonuses (#12): 2–3 or 5–6 sessions.
8. Pure vanilla mode (#18): 2–4 sessions, when you want the whole OG game.

### C.5 Questions for the owner (and your brother)

1. **Which MMH5.5 switches do you want off** in the group's file? Tick in the B.5 table's "OG value" column. In particular: the town gate, mixed neutral stacks, mage guild 1 in every random map town, mana at observatories, the moon disc quest, potions (you turned them **on**).
2. **"Only vanilla artifacts":** (a) MMH5.5's 99 new artifacts never appear, or (b) also the original artifacts' numbers and set bonuses? (b) is much bigger.
3. **The skill lock:** (i) vanilla classes (8 classes, any skill possible, vanilla chances), or (ii) keep MMH5.5's 24 classes but let every skill be possible? Skills only, or attribute growth too?
4. **Per game in the lobby, or one group setting** for these rules? (You said per game for the classes.)
5. **Do you sometimes want the pure vanilla game** (no MMH5.5 at all)? Then a separate vanilla copy with the 2008 exe is the simplest.
6. **PEST:** keep endless simultaneous turns? (We assume yes.)
7. **What do you like in MMH5.5** and want to keep (e.g. its random maps, the creaturepedia, HD textures, the 8 skill slots, levels above 40)? It saves work on things you would not switch off.
8. Anything else on your brother's list (original creature or spell numbers, original movement, original town costs)?

## Backlog rows

Added to [backlog.md](backlog.md):

| Idea | From | Notes |
|---|---|---|
| **"OG" options: restore vanilla rules** (research done: [mmh55-vs-vanilla.md](mmh55-vs-vanilla.md)) | owner, 2026-11-01 | Batch 1: an OG group settings pak (MMH5.5's own switches: governors, conversion, town gate, mine guards, AI cheat, prank artifacts, mixed stacks; 0.25 session), original music and black edge on, a rules hash check in the hellos (1 session), then host rules served as `H55-Settings.lua` and carried in the random map's name (2–3 sessions). |
| **Only vanilla artifacts** (later) | owner, 2026-11-01 | [mmh55-vs-vanilla.md](mmh55-vs-vanilla.md) C.2 #11. MMH5.5 adds 99 artifacts (96 → 195). Ban them on random maps (`artifactIDs`), at merchants (`CanBeGeneratedToSell`) and in the scripts' pools (`H55-Tables.lua`). World rule: carried in the map name. 3–4 sessions. |
| **Pure vanilla mode** (later) | owner, 2026-11-01 | [mmh55-vs-vanilla.md](mmh55-vs-vanilla.md) A.4 and C.2 #18. The 2008 exe (`H5_GameOLD.exe`; Steam's 2025 rebuild is not recognised) with no MMH5.5 data. Split the French pack, check the robot, a vanilla test copy (owner's OK). 2–4 sessions. |

## Sources

- **Our code and docs:** `src/hook/game_build.cpp`, `src/mods/*.cpp`, [src/mods/README.md](../../src/mods/README.md), [reference-binaries.md](../reference-binaries.md), [mmh55-rc19k.md](mmh55-rc19k.md), [lobby-audio-classes.md](lobby-audio-classes.md), [hero-skins.md](https://github.com/jozrcr/h5-together/blob/plan-hero-skins/docs/plans/hero-skins.md) (branch `plan-hero-skins`), [../subsystems/rmg.md](../subsystems/rmg.md), [languages.md](languages.md).
- **Game and MMH5.5 data** (read only, RC19k test copy; RC19d where noted): `scripts/H55-Settings.lua` (`MMH55-Settings.pak` and the older copy in `MMH55-Index.pak`), `MMH55Stats.xml` (`MMH55-Stats.pak`), `scripts/H55-Core.lua`, `H55-AdvMap.lua`, `H55-Tables.lua`, `advmap-common.lua` (`MMH55-Index.pak`), `GameMechanics/RefTables/HeroClass.xdb`, `Artifacts.xdb`, `Skills.xdb`, `GameMechanics/Creature`, `Spell`, `TownBuildingSharedStats` (MMH5.5 against `a2p1-data.pak` and `data.pak`), `UI/UIGameRoot.(UIGameRoot).xdb`, a random map's `map.xdb`, the robot's `bin\console.txt`, `bin\55u.dll` (RC19d and RC19k).
- **MMH5.5's own patch files:** [assembly/patches/production](https://github.com/Might-Magic-Heroes-5-5/MMH55/tree/master/assembly/patches/production) (`migrated/`, `migration_not_possible/`, `before_migration/`) and [Frameworks](https://github.com/Might-Magic-Heroes-5-5/MMH55/tree/master/Frameworks) on GitHub; e.g. [pest.yml](https://github.com/Might-Magic-Heroes-5-5/MMH55/blob/master/assembly/patches/production/migrated/pest.yml), [levelsV2.yml](https://github.com/Might-Magic-Heroes-5-5/MMH55/blob/master/assembly/patches/production/migrated/levelsV2.yml), [global_movement.yml](https://github.com/Might-Magic-Heroes-5-5/MMH55/blob/master/assembly/patches/production/migrated/global_movement.yml), [NewHeroClassFramework.yml](https://github.com/Might-Magic-Heroes-5-5/MMH55/blob/master/assembly/patches/production/migration_not_possible/NewHeroClassFramework.yml), [maxArtifacts_v1.4.yml](https://github.com/Might-Magic-Heroes-5-5/MMH55/blob/master/assembly/patches/production/migration_not_possible/maxArtifacts_v1.4.yml).
- **MMH5.5's feature list:** [gamepressure.com's copy of the RC19e description](https://www.gamepressure.com/download/heroes-of-might-and-magic-v-tribes-of-the-east-might-magic-heroe/z2f291) (ModDB itself answered 403 to our tools); [Steam discussion "Might & Magic Heroes 5.5"](https://steamcommunity.com/app/15370/discussions/0/523897277922518736) (governance, conversion, town gate switches); release notes [RC19e](https://www.moddb.com/mods/might-magic-heroes-55/news/mmh55-release-notes-rc19e) and [RC19f–k](https://www.moddb.com/mods/might-magic-heroes-55/news/mmh55-release-notes-rc19f) (as summarised in [mmh55-rc19k.md](mmh55-rc19k.md)).

### How the numbers were made (for a later session)

Small read-only Python scripts in the session scratchpad (not committed; they print counts, never game text):
- `override_survey.py`: each `MMH55-*.pak` member against the base paks by path and CRC;
- `tables.py`, `creature_diff.py`: artifact, class, skill, creature, spell and building tables, field by field;
- `map_sites.py`: `mmh55_patch_sites.py`'s spots matched to the GitHub yml files (their `patchAddress` comments hold the program address) and, failing that, to the yml patches in the same game function.

Any of them can be rewritten in a few minutes from these descriptions.
