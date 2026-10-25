# Saves and autosaves

Status: **checked in a running game** (2026-10-25, robot LAN games with the autosave mod's trace, `src/mods/autosave.cpp`). Addresses are in the vanilla 3.1 exe (same in `MMH55_Utility(_64).exe`).

Why we looked: in co-op games the autosave at the start of each round freezes every PC for a long time.

## Short answer

- **One PC asks, every PC saves.** Each round, only the PC of the first living human asks for the autosave. Every PC then writes its own file, inside the lockstep, so everyone waits for the slowest one.
- **The freeze is mostly the game looking through the saves folder, not the writing.** To pick a name and a file, the game reads the header of every multiplayer save in the folder: 3 times on the PC that asks, once more on every PC. With the owner's 17 old multiplayer saves (copied into the robot's profile), each round froze for **about 10 s**: 7 s for the request, 3.3 s for the save. With only the 3 autosave files of a small map: 0.1 s and 0.15 s.
- **Writing the world** is 0.1 s on the robot's small map. On the owner's big maps (43 to 106 MB before compression) the zlib part alone takes 0.7 to 1.75 s at the game's level. Level 1 takes a third of that, for files 24% bigger.

## How an autosave happens

| Step | Where | What |
|---|---|---|
| Trigger | `NUI::CAdventureController` frame update `0x006082f0`, at `0x006087d7`..`0x00608865` | When the day changes. In a multiplayer game it reads the lobby's option (world settings `+0x38`, see below); otherwise the console variable `autosave` (`0x00fd9e82`, `setvar autosave = 1` in the default profile). |
| Who | `0x00603ca0(world, controller+0x3c)` | Multiplayer: true only on the PC whose player is the first living human (players 1..8, slot `0x184` human, slot `0x6c` == 1), or the player in world settings `+0x30` if one is set there. With world settings `+1` set (perhaps hotseat) and in single player: true when that player is a living human. **So one PC per round asks**, whatever the number of PCs (robot: only the host asked, in normal and in simultaneous turns). |
| Second trigger | `0x00607d50`, at `0x00607ee9`..`0x00607f1d` | Console variable `autosave` and world settings `+0x1c`. Never seen in robot games, also not with "simultaneous turns". |
| Request | `NUI::CSavesCreator` (controller `+0xb0`) slot 7 = `0x007f9e10` (vtable cell `0x00e462e0`); `__thiscall(ITransceiver *, const char *key)`, `ret 8`, key `"autosavename"` (`0x00e299c0`) | Builds "Autosave 1", "2", "3" (in French "Sauvegarde auto N"), looks each one up (`0x007f6050`, `0x007e6d70`: a full scan of the saves folder each), takes the first that does not exist or the oldest. Single player: queues a local save (`0x007f9090`). Multiplayer: builds `NMP::CMPSaveWorldCmd` (`0x008236d0`, a new GUID from `CoCreateGuid` at `0x008236de`, ctor `0x00824c80`) and sends it with the transceiver's slot 0 (`0x007c9810`). |
| Save | `CMPSaveWorldCmd::Execute` = `0x008256a0` (vtable `0x00e487a0`, cell `0x00e487bc`; `__thiscall()`, returns `al` = 1) | Runs on every PC in the segment runner. The command holds the GUID at `+0x0c` (16 bytes), the name at `+0x1c` (wide string: begin, end, capacity) and a flag at `+0x28`. A progress object (`0x007f72e0`) and `0x008249b0` (with the name) come first; then the save proper `0x00822950` (called at `0x00825735`, `__fastcall(ecx, edx)`) checks the disk (`0x007f6af0`), and `0x00822670` generates the file name (`0x006f8e70`, console "Generated filename : ...": it reads every save's header again, to reuse the file of the save with the same name), writes the header (`0x00822cf0`) and the world (`0x008219a0` → saver flush `0x00cb7210`, called at `0x00cb80be`). Afterwards `0x007f84a0` shows "save complete" (`ID_SAVE_COMPLETE`). |
| Compression | save stream ctor `0x00d10310`: `deflateInit_(strm, -1, "1.2.3", 0x38)` at `0x00d10353` (IAT `0x00e0a768`) | Level -1 = zlib's 6. `deflate` (`0x00e0a76c`) is called from `0x00d103b0` with a 32 KB output buffer (`0x010dd5f0`), `deflateEnd` (`0x00e0a770`) from `0x00d104a0` / `0x00d102e0`. The game's other `deflateInit_` callers (`0x0082d830`, `0x008c803a`) are not saves. `zlib1.dll` 1.2.3. |
| File | `0x00d10000`..`0x00d10300` | A memory-mapped file (`CreateFileMappingA`, `MapViewOfFile`, `FlushViewOfFile`). The flushes take under 1 ms. |

A save is 4 zlib streams after an uncompressed header of about 300 KB (the picture of the map): two empty ones, the world (most of it), and a small one. The zlib header shows the level: `78 9c` = 6, `78 01` = 1.

The lobby's autosave option: the setup screen's checkbox named **`realtime`** (`NUI::CMPCreateOptions` `+0x20`) becomes the game option `"autosave_enabled"` (`0x00708f10`, at `0x007090a5`), shown as "autosave on/off" in the lobby (`0x00712d40`). The checkbox named `simultaneus` (`+0x3c`) is the simultaneous turns option. (In the robot: `option|realtime 0`, `run_lan_test.py --autosave`.)

**A save does not change the game world**, so the PCs stay in step whatever each one does while saving (robot: world checksums equal in every run).

## Why reading the saves folder is slow

A profile (robot, a temporary build whose sampler ran during the request and the save) puts 97% of the time in the folder scan `0x007f30d0`, which for each file calls `0x007e67b0`: open the file (`0x008c8150`), read the header chunk, and deserialize the save's info (`0x00753110`). That info contains the game setup (`NWorld::CPlayersStartInfo`, through `0x005a1400` → `0x005a1430` → `0x00ca0c70`), whose constructor `0x00ca0240` computes the hero pool: `0x00c9f4a0` (95% of the time) takes every hero of the game ("HEROES_ANY"; MMH5.5 has many) and, for each, looks through the map's objects. So the cost grows with the number of multiplayer saves and the size of their maps:

| Saves folder (robot LAN game, A1M4) | Request (3 lookups) | Each PC's save | Freeze per round |
|---|---|---|---|
| 1 to 3 robot autosaves (small map) | 0 to 0.1 s | 0.16 to 0.21 s | ~0.3 s |
| + the owner's 29 saves (17 multiplayer, 12 single player) | 6.4 to 8.5 s | 3.2 to 3.6 s, of which 3.1 to 3.5 s before the first zlib stream | ~10 s |
| + only the owner's 17 multiplayer saves | 6.6 to 8.0 s | 3.3 to 3.6 s | ~10 s |

The 12 single-player saves cost nothing in a multiplayer game; each multiplayer save on a big map costs about 0.15 s per lookup, a robot autosave of the small map about 0.03 s. (In single player a request took 30 to 370 ms with 7 saves in the folder.)

"Freeze" here is how long each PC closed no lockstep segment (the trace's "closed no segment" lines): the PC that asks freezes during the request, then every PC during its save, one after the other.

## Compression (zlib level)

The game's own `zlib1.dll` (from the test copy), on the 106 MB world of the owner's biggest save (`save_11`), fed like the game feeds it (`ztime.c`, a small test program):

| Level | Time | Size |
|---|---|---|
| 6 (the game) | 1.75 s | 11.2 MB (the same bytes as the real save) |
| 1 | 0.60 s | 13.8 MB (+24%) |
| 2 | 0.68 s | 13.2 MB |
| 3 | 0.73 s | 12.7 MB |

In the robot game (5 MB world): deflate 92 to 115 ms at level 6, 31 to 39 ms at level 1; the whole save 160 to 225 ms → 117 to 149 ms. A level-1 autosave loads normally (robot: a single-player autosave made at level 1, loaded and played on). The multiplayer saves are not in the single-player load list, so their loading in a multiplayer game is not robot-tested yet.

## Robot runs (2026-10-25)

LAN, 2 copies on one PC (host = player 0, client = player 1, 1 AI), map A1M4, 6 AI turns, `run_lan_test.py --autosave`. One request per round in every run, always from the host; each PC saved once per round.

| Run | Saves folder | Request (host) | Save, host / client | zlib (deflate) | World checksums |
|---|---|---|---|---|---|
| (a) game's level 6 | 1 to 3 autosaves | 0 to 103 ms | 159–214 / 164–225 ms | 102–111 ms, 5.0 MB → 652 KB | 953, all equal |
| (a) + simultaneous turns | 1 to 3 autosaves | 85 to 106 ms | 176–194 / 175–212 ms | 92–105 ms | 578, all equal |
| (b) level 1 (default) | 3 autosaves | 87 to 102 ms | 117–149 / 126–148 ms | 31–39 ms, 5.0 MB → 817 KB | 960, all equal |
| (a') level 6 | + owner's 29 saves | 6.4 to 8.5 s | 3.2–3.6 / 3.2–4.6 s | 92–108 ms | 431, all equal |
| (b') level 1 | + owner's 29 saves | 6.4 to 7.3 s | 3.2–3.3 / 3.2–4.0 s | 31–34 ms | 385, all equal |
| (c) level 1, `autosave_every=2` | + owner's 17 multiplayer saves | 6.7 to 8.0 s, days 3, 5, 7 only | 3.3–3.6 / 3.3–4.5 s | 31–40 ms | 702, all equal |

In (a'), (b') and (c) the host closed no segment for 7 to 9 s during the request and 3.2 to 3.7 s during its save; the client for 6.6 to 10 s.

## The mod (`src/mods/autosave.cpp`)

- `trace_autosave=1`: one line per request and per save, with each phase; and "closed no segment for N ms" for the freeze.
- `compression=1`: the zlib level of every save (only the save stream's call to `deflateInit_`, recognized by its return address `0x00d10359`).
- `autosave_every=N`: autosave only on days 1, 1+N, 1+2N... The day comes from the world (`+0x48`, CWorld slot `0xa8` = `0x00c61ab0`, 0 on the first day), the same on every PC.

Not built: making only the host ask (`autosave_host_only`). The robot showed that the game already sends one request per round, and the PC that asks is the one of the first living human, not always the host: skipping its request would lose the autosave.

## Next

1. **Quick fix, no code:** move old multiplayer saves out of the `Saves` folder of the profile (keep the ones you still want somewhere else). The game only reads the folder itself.
2. **Mod, bigger:** remember each save's info between lookups (key: file name, size and time), so a round reads each header at most once, or never if the file did not change. The info is a 0x28c-byte object with strings (copied by `0x007f1290`, freed by `0x006f9ff0`), and `0x007e67b0` takes its path in `eax`, so this needs care.
3. Measure the owner's real game with the trace on: the freeze per PC, and how many saves each friend has.
