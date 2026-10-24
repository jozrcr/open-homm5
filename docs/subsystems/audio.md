# Audio: how the game picks and plays music

Status: **checked in a running game** (2026-10-24, robot runs with the "original music" mod, `src/mods/original_music.cpp`). Addresses are in the vanilla 3.1 exe (same in `MMH55_Utility(_64).exe`).

## Big picture

1. **Music files** are loose `.ogg` files in the game's `music\` folder (`music\AdvMap-Terrain\Terrain-Grass.ogg`, ...). MMH5.5 adds its own in `music\MMH55\`. They are not in the `.pak` files.
2. **Music definitions** are small data files in the `.pak` files, one per track: `Sounds/_(Music)/<folder>/<name>.xdb`, class `NDb::SMusic`:
   ```xml
   <Music>
       <FileName>..\Music\AdvMap-Terrain\Terrain-Grass.ogg</FileName>
       <FadeIn>2000</FadeIn> <FadeOut>2000</FadeOut>
       <MinPausedTime>500</MinPausedTime> <MaxPausedTime>15000</MaxPausedTime>
   </Music>
   ```
3. **Other data files point at definitions** (`href="/Sounds/_(Music)/....xdb#xpointer(/Music)"`):
   - every **map tile type** (`NDb::SAdvMapTile`, `MapObjects/_(AdvMapTile)/<terrain>/<tile>.xdb` and `RMG/Tiles/<zone>/<tile>.xdb` for random maps) has `<AdventureMusic>`: the music of that ground;
   - every **hero** (`MapObjects/<faction>/<hero>.(AdvMapHeroShared).xdb`) has `<AdventureMusic>`, usually empty; a few campaign heroes have a theme;
   - `Sounds/_(Music)/TableRaceMusic.xdb` (`NDb::STableRaceMusic`): per faction, the town, tavern, dwelling and siege music and a list of battle tracks (`combatMusics`).
4. At run time small **"music calculator"** objects decide what to play next, and the **music system** streams the chosen file with FMOD (`bin\fmod.dll`, FMOD 3.x, `FSOUND_Stream_*`).

The game finds every data file through its **virtual file system** (below). Later `.pak` files override earlier ones by path, which is how MMH5.5 changes the music without touching `data.pak`.

## Data classes

| Class | vftable | Notes |
|---|---|---|
| `NDb::SMusic` | `0x00e7b31c` | Size `0x5c`. `FileName` at `+0x40` (game string), `FadeIn` `+0x4c`, `FadeOut` `+0x50`, `MinPausedTime` `+0x54`, `MaxPausedTime` `+0x58`. Reader of the XML fields: `0x00b136d0`; type description: `0x00b13360`. |
| `NDb::SAdvMapTile` | (RTTI `0x00f2e17c`) | Its `AdventureMusic` is a lazy pointer (`CDBPtr<SMusic>`) at `+0x78`, read by the terrain calculator. |
| `NDb::STableRaceMusic` | `0x00e1c3f4` | Readers `0x0053a6f0`, `0x0053ae70` (strings `TownMusic`, `TavernMusic`, `DwellingMusic`, `combatMusics`, `SiegeMusic`, `WinCombatMusic`, `LooseCombatMusic`, `RetreatCombatMusic`, `WaitMusic`). |
| `NDb::SCombatMusicList` | `0x00e1c3c8` | `baseMusics`, `secretMusics`, `SecretMusicsProbability` (`0x0053a5e0`). |

`0x0050fc30` is `CDBPtr<SMusic>::Get`: the first time a reference is used, the definition is loaded from the data files (and checked to be an `NDb::SMusic`). So a music definition is only read when its track is about to play.

## Choosing the music: calculators (`NSound::IMusicCalculator`)

Each calculator has a "what should play now?" method in vftable slot 7 that returns an `SMusic *` or 0 for "nothing".

| Class | vftable | slot 7 | What it returns |
|---|---|---|---|
| `NSound::CTerrainMusicCalculator` | `0x00e47890` | `0x00816570` | The music of the tile under the current player's selected hero: hero position (x, y, floor packed in one word), the map floor's tile grid, the tile (`0x00c0ee90`), then its `AdventureMusic` (`+0x78`). |
| `NSound::CAdvMapHeroMusicCalculator` | `0x00e4786c` | `0x008164e0` | The selected hero's theme: the hero's data object (hero vfunc `+0x44`), its `AdventureMusic` at `+0x200`. Empty for most heroes. |
| `NSound::CConstantMusicCalculator` | `0x00e47b28` | `0x008191b0` | Always the same definition. Made by `0x00818e40` from one `SMusic *`; `0x00818fb0` turns a list of definitions into a composite of these. |
| `NSound::CArrayMusicCalculator` | `0x00e47c24` | `0x00819fe0` | Not looked at (by the name: one of a list). |
| `NSound::CCompositeMusicCalculator` | `0x00e47bc4` | `0x00819ae0` | Asks all its calculators, keeps the answers that are not empty and **picks one at random**, avoiding the same track a third time in a row (last track `+0x18`, repeat count `+0x20`). |
| `NSound::CAdvMapMusicCalculator` | `0x00e478b4` | `0x00816370` | The first time, its first calculator (start music); after that, its second. |
| `NSound::CSiegeMusicCalculator` | `0x00e47aa0` | `0x008188f0` | Town siege battles. |

The adventure map's calculator is built at `0x00815d60` (read roughly): a composite of **hero theme, terrain music and the map's own music** (constants), and, when the map has start music, a `CAdvMapMusicCalculator` that plays that first. So on the adventure map the game plays, at random, the selected hero's theme (if any), the music of the ground under that hero, or the map's own music. A tile or hero with an empty `AdventureMusic` simply drops out of the choice.

During AI turns other music plays (`music\AI-Themes\AITheme-<faction>.ogg`, `music\a2\AI_<faction>.ogg`).

Strings that name fixed tracks (not looked at further): `MAIN_MENU_MUSIC_ID` (`0x006931a0`), `MP_MAIN_MENU_MUSIC_ID` (`0x006938c0`), `CREDITS_SCREEN_MUSIC_ID` (`0x00793d20`), `HERO_FIND_GRAIL_MUSIC` (`0x005a5d90`), `BUILD_GRAIL_MUSIC` (`0x00615230`). The main menu plays `Sounds/_(Music)/MainMenu/a2-MainMenu.xdb`, the `credits` console command `Sounds/_(Music)/Credits/Final-Credits.xdb`.

## Playing: the music system and FMOD

Players and handles: `NSound::CMusicPlayer` (`0x00e47b74`), `CNonLoopMusicPlayer` (`0x00e47bb0`), `CMusicPlayerStack` (`0x00e47be8`), `CMusicHandle` (`0x00e96878`), `CMusicScene` (`0x00e96830`), `NFMSound::CMusicSystem` (`0x00e7c048`).

| Function | What it does |
|---|---|
| `0x00d04d10` | Starts a music stream: opens it if needed (`0x00b27010`), seeks, sets the end callback (`FSOUND_Stream_SetEndCallback`, for the pause between tracks), starts it, builds the text `"music start: ..."`. |
| `0x00b27010` | Opens a stream **by file name** (`FSOUND_Stream_SetBufferSize`, then `FSOUND_Stream_Open(name, 0x2000 \| loop flag, 0, 0)`). Music always goes this way. |
| `0x00b27070` | Opens a stream **from memory** (`FSOUND_Stream_Open(data, 0x8000 \| ..., 0, length)`, called from `0x00d03ae0`). |

The FMOD functions are imported from `fmod.dll` (`_FSOUND_Stream_Open@16` at import slot `0x00e0a5a4`; the game calls them through small jump stubs, e.g. `0x00d8df5e`). The `log_music` test setting of the mod watches that import.

Music still streams when `sound_musicvolume = 0` (the robot's muted profile), so music can be tested silently.

## Reading data: the virtual file system

| Class | vftable | Notes |
|---|---|---|
| `NVFS::CWinVFS` | `0x00e66500` | A folder and its `.pak` files (`\*.pak`, `0x009e08d0`). Slot 7 **OpenFile** (`0x009e0020`, jumps to `0x009dfef0`): `__thiscall(vfs, const nstl::string *path)`, returns a stream or 0. Slot 10 lists files (`0x009e02b0`). |
| `NVFS::CCombinerVFS` | `0x00e90ec4` | Several VFSes in one (the data folder, `UserMODs/*.h5u`, ...). OpenFile (`0x00b24f10`) asks them from last to first and returns the first answer. |
| `NVFS::CWinVFS::CZipFileEntry` | `0x00e664b4` | A file inside a pak. Opening it (`0x009e0cc0`) calls `NZip::CZipReader` (`0x0047bc70`). |
| `CMemoryStream` | `0x00e0a980` | Size `0x18`: vftable, buffer begin `+4`, buffer end `+8`, data end `+0xc`, read position `+0x10`, flags `+0x14` (bit 0 = broken). Constructor `0x004034d0`, "new buffer of n bytes" `0x008c7610`. |

A deflated zip entry is inflated at once into a new `CMemoryStream` (`0x008c7cf0`, zlib's `inflateBack` from `zlib1.dll`); a stored one is a mapped view (`CMemoryMappedFileFragment`, `0x0047c0e0`). Either way the whole file is in memory between `begin` and `data end` when OpenFile returns. Paths look like `Sounds/_(Music)/MainMenu/a2-MainMenu.xdb` (the game turns `/` into `\` inside).

Order seen in the robot (single player, `MMH55_Utility_64.exe`):

- ~0.8 s after start: `types.xml`, then `index.bin` (57 MB, in `data\`);
- ~1.5 s, **during the splash screen**: all heroes (`*.(AdvMapHeroShared).xdb`), before the game creates its DirectInput device (where the hook DLL normally installs its hooks);
- ~5 s: the main menu's music definition;
- at map load: the tile types, `TableRaceMusic.xdb`, then the definitions of the tracks as they start.

## What MMH5.5 changes (MMH5.5 2024 install, read 2026-10-24)

- **8 definitions with another track**: `AdvMap-Terrain/Grass, Lava, Swamp, Underground, Water`, `Credits/Final-Credits`, `MainMenu/a2-MainMenu`, `MainMenu/MP-MainMenu`.
- **36 new definitions**, 24 of them for its own tracks in `music\MMH55\`: more terrain music (`AdvMap-Terrain/Flowers`, `DarkGrass`, `Snow`, `Glacier`, ...), hero themes (`AdvMap-Heroes/Messiah`, ...) and battle music (`Battle-Themes/Haven6`, ...).
- **Map tiles**: 46 tile types of hand-made maps and 36 of random maps point at other definitions (`Grass/Flowers.xdb`: `AdvMap-Terrain/Grass.xdb` -> `AdvMap-Terrain/Flowers.xdb`), and several roads have no music any more (`Dirt/DirtRoad.xdb`, `Sand/SandRoad.xdb`: none; original: the terrain's music). 6 random-map tiles are new (MMH5.5 only).
- **Heroes**: 25 heroes have a theme the original game does not give them: 5 existing heroes (Cyrus, Nicolai, Axel, Nathaniel, Quroq) and 20 heroes only MMH5.5 has (Kastore, GodricMP, Erika, ...). The themes themselves are original tracks (`AdvMap-Heroes/Godric.xdb`, ...).
- **`TableRaceMusic.xdb`**: 11 more battle tracks.

Because one MMH5.5 track replaces different original tracks on different tiles (`Adventure_Terrain_Swamp_002.ogg` is played where the original played the dirt, grass or water music), the original music has to be restored at the level of definitions and references, not by swapping `.ogg` files. That is what the "original music" mod does (`src/mods/README.md`).
