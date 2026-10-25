# Saves and autosaves

Status: **checked in a running game** (2026-10-25, robot LAN games with the autosave mod's trace, `src/mods/autosave.cpp`). Addresses are in the vanilla 3.1 exe (same in `MMH55_Utility(_64).exe`).

Why we looked: in co-op games the autosave at the start of each round freezes every PC for a long time.

## Short answer

- **One PC asks, every PC saves.** Each round, only the PC of the first living human asks for the autosave. Every PC then writes its own file, inside the lockstep, so everyone waits for the slowest one.
- **The freeze is mostly the game looking through the saves folder, not the writing.** To pick a name and a file, the game reads the header of every multiplayer save in the folder: 4 times on the PC that asks, twice more on every PC. With the owner's 17 old multiplayer saves (copied into the robot's profile), each round froze for **about 10 s**: 7 s for the request, 3.3 s for the save. With only the 3 autosave files of a small map: 0.1 s and 0.15 s.
- **Fixed by the mod (`fast_save_list`, on by default):** during those reads, the costly step (each save's hero pool) is skipped. The same round with the owner's saves: **0.08 s for the request and 0.12 s for each save** (the first autosave of a session: ~0.9 s).
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

The 12 single-player saves cost nothing in a multiplayer game. With 20 multiplayer saves in the folder, the trace counted 80 header reads in each request (4 folder scans: the 3 name lookups and one more) and 40 in each save (2 scans: the file name, and one more), so about 85 ms per read of a big-map save. (In single player a request took 30 to 370 ms with 7 saves in the folder.)

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

- `fast_save_list=1`: the fix for the folder reads, below.

Not built: making only the host ask (`autosave_host_only`). The robot showed that the game already sends one request per round, and the PC that asks is the one of the first living human, not always the host: skipping its request would lose the autosave.

## The fix for the folder reads (`fast_save_list`, 2026-10-25)

**What is skipped.** In the setup constructor `0x00ca0240`, the call at `0x00ca0af6` (`mov edx, esi; mov ecx, edi; call 0x00c9f4a0`: `__fastcall(map, setup)`, plain `ret`) is redirected to the mod. The mod skips that hero pool step only when both hold:
- the autosave request (`0x007f9e10`, autosave key only) or a multiplayer save (`CMPSaveWorldCmd::Execute`) is running on this thread (a thread flag set by the mod's hooks on them);
- the return address `0x007f320c` (after `call 0x007e67b0` at `0x007f3207`, the per-file read of the folder scan `0x007f30d0`) is on this thread's stack, at most 16 KB above.

Otherwise the game's step runs. The mod checks these bytes before patching: `0x00ca0af2` = `8b d6 8b cf e8 a5 e9 ff ff`, `0x00c9f4a0` = `83 ec 38 53 55 56 8b f2 33 db 3b f3 8b e9`, `0x007f3207` = `e8 a4 35 ff ff`.

**Why it is safe.**
- `0x00c9f4a0` writes nothing into the setup or the map. It only fills the global hero pool (the list per faction at `0x010db28c`) and notes which setup it belongs to (`0x010daf84`). Read from the disassembly: it stores only to those globals and its own locals, and the functions it calls fill or free the pool, or read the map and the game data.
- Only two functions read the pool: the setup's vtable slots 15 and 16 (`0x00c9e140`, `0x00c9e1e0`, the lobby's hero choice). Both first check that the setup is the one in `0x010daf84`.
- A probe (a temporary build) counted the calls of slot 15 in a robot LAN game: 27, all in the lobby, **none during an autosave**. Slot 16 calls slot 15 first.
- The folder scan throws its temporary setups away. The name lookup and the oldest-slot choice use the saves' names and times. The file name uses the names. The header the save writes comes from the running game, not from these reads. (Trace: "0 with it", so no other setup was built during the autosaves at all.)
- Only the globals differ afterwards. In the game they then belong to a setup that no longer exists; with the mod they keep their earlier owner. Both are lobby-only state, and they already differ from PC to PC in the game (each PC reads its own folder), so they cannot be part of the synced world.

**Results** (robot LAN, A1M4, the owner's 29 saves copied into both profiles, the final DLL with PR #35 merged):

| | Request (host) | Each PC's save | Frozen ("closed no segment") | World checksums |
|---|---|---|---|---|
| `fast_save_list=0` | 6.4–7.2 s | 3.2–3.3 s / 3.2–3.9 s | host 6.9–7.8 s + 3.2–3.4 s, client 3.8–4.7 s | 482, all equal |
| `fast_save_list=1` | 67–83 ms (first: 823 ms) | 110–116 ms / 114–131 ms (client's first: 855 ms) | host 80–123 ms + 129–162 ms, client 168–197 ms (first round ~1 s) | 976, all equal |

The first autosave of a session still takes ~0.9 s (the files' first read). The autosave names still rotate (auto 3, 1, 2, 3, 1, 2), and the game still reuses the file of the save with the same name. An autosave made with the fix shows normally in the multiplayer load list: name, time, map, picture, 3 players, "Multiplayer - LAN", size, difficulty. A single-player autosave made with it loads and plays on.

## Next

1. Measure the owner's real game with the trace on: the freeze per PC, and how many saves each friend has.
2. If the first autosave of a session (~1 s) matters: remember each save's info between lookups (key: file name, size and time). The info is a 0x28c-byte object with strings (copied by `0x007f1290`, freed by `0x006f9ff0`), and `0x007e67b0` takes its path in `eax`, so this needs care.
3. Without the mod, the quick fix is to move old multiplayer saves out of the profile's `Saves` folder.
