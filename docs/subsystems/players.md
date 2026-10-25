# Players, turns and heroes (what the player list reads)

Status: **checked in running games** (2026-10-25, robot: LAN games with 2 humans and 1 computer player, with and without simultaneous turns, with and without teams; a single-player game). Addresses are in the vanilla 3.1 exe (the same in `MMH55_Utility(_64).exe`). The player list overlay (`src/mods/player_list*.cpp`) reads all of this; it never writes to the game.

## Short answer

- **The world** holds the players, in turn order. **Each player** has its number, colour, team, name, heroes, and two turn flags: "it is my turn now" and "I pressed End Turn".
- **The world is the same on every PC** (lockstep, see [multiplayer-sync.md](multiplayer-sync.md)), so every PC can show the same list, the clients too. "The AI is moving" is also visible on clients, even though only the host runs the AI.
- **A hero's portrait** is a `.dds` file named in the hero's game data. We read it through the game's own file system, so MMH5.5's files count as they do for the game.

## The world: `NWorld::CWorld` (vtable `0x00e6260c`, 136 slots)

| What | Where | Notes |
|---|---|---|
| The players | `+0x24` / `+0x28` (vector of `NWorld::IPlayer *`) | Always 8 entries, `PLAYER_1`..`PLAYER_8`, in turn order. Slot `0x64` returns `&+0x24`. Slot `0x68` (`0x0098d2c0`) finds a player by number. |
| The day | `+0x48`, 0 on the first day | Slot `0xa8` (`0x00c61ab0`, `lea eax,[ecx+0x48]`). The adventure screen asks every frame: the overlay notes the world there. The adventure controller also holds it at `+0x18` (`src/mods/notify.cpp`). |
| Settings | `+0x88` (slot `0x14` returns `&+0x88`) | `+0x90` (byte): simultaneous turns are on. Slot `0x20` clears it (`CBreakSimultaneousTurnsCmd`, when human heroes meet). |
| End a turn | slot `0xd0` = `0x00998760` | Called by `NWorld::CEndOfTurn` (vtable `0x00e0d8e0`, execute = slot 7 = `0x00427140`, fields `+0x0c` world, `+0x10` player). See "Turns" below. |

## A player: `NWorld::CPlayer` (0x6bc bytes)

Made by `0x004ff2a0` (through `0x004ffa50`) from the game setup's slots, in `0x00c9fa80`. The world and the rest of the game point to its `IPlayer` part at `+0x1c` (vtable `0x00e1a09c`, 142 slots). Offsets below are from that `IPlayer` pointer.

| What | Where | Getter (IPlayer slot) | Notes |
|---|---|---|---|
| Heroes | `+0x18` / `+0x1c` (vector of `NWorld::IHero *`) | `0xc0` returns `&+0x18` | Slot `0xb4` compares their number with the world's maximum. Towns are at `+0x98` (slot `0xc4`). |
| Human or computer | `+0x74`: 0 = human, else the AI kind | `0x184` (`0x00500d60`), `0x188` | From the setup slot's state (2 = human). |
| Player number | `+0x78`, 1..8 | `0x2c` (`0x00720e90`) | Also the turn order. |
| In the game | `+0x7c`: 1 = playing | `0x6c` (`0x00c61a70`) | 4 for the unused colours of the map. |
| Colour | `+0x140`, 1..8 | `0x30` (`0x00af8160`) | From the setup slot `+8`. 1 red, 2 blue, 3 green, 4 yellow, 5 orange, 6 teal, 7 purple, 8 brown (`UI/RefTables/PlayerColourSchemes.xdb`). |
| Race | `+0x17c` | `0x34` | From the setup slot `+0x18`. |
| **Pressed End Turn** | `+0x178` (byte) | `0x4c` get (`0x00500c40`), `0x50` set | Set by the world's "end turn" for humans; cleared for everyone when the next turn starts. |
| **Team** | `+0x1cc`, 0, 1, 2... | `0x14` get (`0x00500bc0`), `0x18` set | From the setup slot `+0x1c`. Map scripts can change it: Lua `SetPlayerTeam` → `CSetPlayerTeamCmd` (execute `0x00428e60`) → slot `0x18`. In two robot games on A1M4 every player started alone (the setup's teams 0, 1, 2) and then had teams 0, 0, 1 on the map: something changed them after the start, probably MMH5.5's map scripts. In later games they stayed 0, 1, 2. |
| **Selected hero** | `+0x1d0` (`NWorld::IHero *`) | `0x0c` (`0x004fcc40`, returns 0 for a hero being destroyed), `0x10` set | `+0x1d4` is the selected town (slots `0x04` / `0x08`). The world keeps them, so every PC knows every player's selection. |
| **Name** | `+0x1fc` (wide string: begin, end, capacity) | `0x38` returns `&+0x1fc`; `0x3c` set | From the setup slot `+0xc`: the lobby name (`lan_last_login` in the robot), `"AI 2"` for computer players in single player, empty for computer players in a LAN game (the overlay then shows "Computer 1", "Computer 2"...). |
| **Turn state** | `+0x634`: 0 = it is this player's turn | `0x44` get (`0x00500c10`), `0x48` set | Set for every player by `0x00998060` ("GiveTurnToNextPlayers"). `notify.cpp` uses it too. |

The game's "is this object being destroyed" test, used before reading a pointer to a player or a hero: the virtual base's flags, `(*(obj + vbtable[1] + 0xb) & 0x80)` (as in `0x004fcc40`).

## Turns

- **End Turn** (`CEndOfTurn` → world slot `0xd0`, `0x00998760`): only for a player whose turn state is 0. A human (slot `0x190`: human, and not the debug flag `0x00fd5c9c`) gets "pressed End Turn" (`+0x178` = 1). A computer player's turn ends at once: `0x00998060`.
- **The next turn** (`0x00998060`, it prints `GiveTurnToNextPlayers before: ... after: ...` to the game's console, one letter per player): clears every player's `+0x178`, then sets each player's turn state. With simultaneous turns, every living human gets the turn at once; the computer players follow one after another, in player order.
- **What the robot saw** (LAN, 2 humans + 1 computer, `--simultaneous`): at the start of a day both humans are at state 0. A human who presses End Turn gets `+0x178` = 1, and often state 1 at once. When the last human is done, all `+0x178` go back to 0 and the computer player gets state 0 while it moves. Without simultaneous turns: one player at state 0 at a time, in player order.

**The overlay's statuses** (`player_list_game.cpp`):

| Status | When |
|---|---|
| Playing | a human at state 0 who has not pressed End Turn |
| Turn ended | a human at state 0 with `+0x178` = 1; or anyone who had state 0 earlier today and does not now |
| Thinking | a computer player at state 0 (the AI is moving) |
| Waiting | not yet today |

When the overlay starts in the middle of a day (a loaded game), the players before the first one at state 0 count as done (with simultaneous turns: all humans, once a computer player moves).

## Heroes: `NWorld::CHero`

The rest of the game points to its `IHero` part at `+0x1c` (vtable `0x00e7e484`, 230 slots). Offsets from that pointer:

| What | Where | Notes |
|---|---|---|
| Internal name | `+0x24` (narrow string) | e.g. `Gilraen` |
| **Name** | `+0x6c` / `+0x70` (wide string) | Slot `0x268` (`0x00b4d4a0`) copies it; the adventure UI shows it as `hero_name`. |
| Its game data | `+0xd0`: `CDBPtr<NDb::SAdvMapHeroShared>` (first word = the object) | Slot `0x34` (`0x00b5ae40`) reads it. |
| Owner | `+0x128` (the `CPlayer`) | |

`NDb::SAdvMapHeroShared` (vtable `0x00e6d450`; its fields are read by `0x00a0c3b0`): `+0x20` its own file (`/MapObjects/Necropolis/GiovanniMP.(AdvMapHeroShared).xdb#...`), `+0xec` InternalName, **`+0x12c` FaceTexture** (`CDBPtr<NDb::STexture>`), `+0x134` FaceTextureSmall, `+0x16c` the name's text file.

`NDb::STexture` (vtable `0x00e90318`; fields read by `0x00c7a680`): `+0x20` its own file, **`+0x40` DestName, already a full path in the data**: `UI/H5A1/Icons/Heroes/Portrets128x128/Giovanni.(Texture).dds`; `+0x5c` / `+0x60` width and height. The portraits are 128×128, uncompressed 32-bit.

## Reading the game's data files

The game's file system object (`NVFS::CWinVFS`, vtable `0x00e66500`) opens every data file: slot 7 (`0x009e0020`, `__thiscall(vfs, const nstl::string *path)`) returns a stream with the whole file in memory (see `src/mods/original_music.cpp`). Wrapping that slot gives us the object; calling it reads any file with the game's priorities (MMH5.5's paks over the original ones). Paths have no leading `/`.

Useful files:
- `Text/Game/Player/Colors/red_player.txt`: "Joueur rouge" in a French game; the overlay picks its language from it.
- `Textures/AdventureScreen-FPP-2/ResourceBar-heavy.dds`: the top resource bar (1024×64, DXT3; the frame is rows 1..43, end caps at columns 2..36 and 660..694). The overlay's panels are cut from it.
- The game's fonts are bitmaps: `UI/Fonts/<name>.(Font).xdb` → a 256×256 texture and a glyph table `Bin/Fonts/<uid>` (format not decoded yet). "System" (bold) is the resource bar's font; "Default" is the italic one of the banners.

## The screen on top

The screens are in a list whose sentinel is at `[0x00fe719c]` (node `+0` next, `+4` previous, `+8` the screen); the last one is on top (`0x008385a0` returns it). Its RTTI class tells which: `NUI::CAdventureScreen` on the map; seen too: `CMainMenuScreen`, `CLobbyScreen`, `CPWLScreenInterface` (loading). The overlay draws only when the adventure screen is on top.

## Screen sizes

The game's interface is laid out for 768 lines and **scales with the screen height** (robot screenshots, windowed): at 1920×1080 the resource bar, the minimap and the action wheel are 1.4 times their size at 1024×768; at 2560×1080 they are as at 1920×1080, the bar stays centred and the minimap keeps its distance to the left edge. The overlay follows the same rule: its sizes and its left inset are fractions of the screen height (sizes `k = height / 820`, the owner's sketch), so it keeps the same size next to the game's own interface at every resolution.

## Direct3D

The game draws with Direct3D 9 (`Direct3DCreate9` is an import of the exe) and creates 3 or 4 devices at start-up (640×480, then the real one). **Each device has a vtable of its own** in Windows' `d3d9.dll` (on the heap, just after the device): a later device can get the same address as a freed one with an unhooked vtable. So the hooks (`src/hook/d3d9_hook.cpp`) check every new device's entries.

## Not known yet

- What turn state 2 means (`0x00998060` sets it in one branch, for multiplayer games with some world settings).
- Which `+0x7c` value a defeated player gets (the overlay shows only players at 1).
- This PC's own player: the adventure controller `+0x3c` (`notify.cpp`); the overlay does not mark it yet.
- The glyph table format of the game's bitmap fonts.
