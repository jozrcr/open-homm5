# Phase 0 report: first map of the game

Written 2026-10-20 at the end of phase 0 (session 1). It sums up what we now know about the game exe and recommends what to do in session 2. Details live in [reference-binaries.md](reference-binaries.md), [toolchain.md](toolchain.md) and [tools/inventory/README.md](../tools/inventory/README.md).

## In one paragraph

Our reference is the vanilla Tribes of the East 3.1 exe from 2008, unwrapped from Steam's DRM. It is also the exe our co-op games run, because MMH5.5's co-op exe (`MMH55_Utility_64.exe`) is that same exe with 12 tiny patches, all functions at the same addresses. It was built with Visual Studio .NET 2003 (compiler 13.10.3077), and we now have that exact compiler. The exe holds about **48,600 functions of real game code** in about 3,300 C++ classes. The AI turn and the multiplayer code are easy to find thanks to class names and debug strings, and the game already has built-in AI timing, AI time limits, desync detection and random-number logging. All of it is useful for phase 1.

## Totals

| What | Count |
|---|---:|
| Functions found (Ghidra 12.1.4 + our RTTI pass) | **67,486** |
| … game code (what we decompile) | **48,609** (8.5 MB of 9.2 MB of code) |
| … library code, counted separately | 2,049 |
| … compiler-generated helpers, counted separately | 16,828 |
| C++ classes recovered from RTTI | 3,342 (3,111 with virtual functions, 5,571 vtables) |
| Game functions already tied to a class | 14,764 |
| Source files (translation units), from the Rich header | ~1,209 C++ files + 54 C files |

Library code by kind: Ubisoft online SDK and ATL 1,280, Ogg/Vorbis/Theora 407, DLL import stubs 203, Lua ~113 (an undercount: Lua 5.0 has roughly 400 functions), C runtime 29, DirectX helpers 17.
Compiler-generated: 8,076 "adjustor thunks" (2–3 instruction stubs for multiple/virtual inheritance), 3,918 exception-cleanup snippets, 4,834 global-object setup/teardown stubs. The compiler writes these by itself when the parent code is compiled, so they are not separate work.

`progress/progress.json` has these totals for the dashboard. It also has a first guess at subsystems from class namespaces: adventure 3,879, ui 3,321, io 2,438, render 1,051, combat 942, net 852, ai 207. 35,919 functions are still unassigned (no class yet). Translation units are not mapped yet (`units` is empty).

## Where things are in the exe

The linker lays out code in the order of source files, then libraries, then compiler-generated sections:

| Address range | Contents |
|---|---|
| `0x401000`–`0xd119b0` | Game and engine code, Nival's own source files. Lua's `.c` files are compiled in between (clusters near `0x89cc60`, `0x8c0aa0`, `0xc0d3c0`, `0xc29b10`). |
| `0xd119b0`–`0xd12160` | C runtime pieces (program entry point, exception helpers, 64-bit math) |
| `0xd12160`–`0xd3e37e` | libogg / libvorbis (2003) / libtheora (2004): music and video |
| `0xd3e3f6`–`0xd59e90` | DirectX SDK helpers (error-string tables, D3DX) |
| `0xd59e90`–`0xd92d30` | ubi.com online services SDK, ATL, stack-protection code |
| `0xd92d30`–`0xda1950` | Exception "unwind" snippets |
| `0xda1950`–`0xe090fa` | Global object constructors/destructors |

Namespaces of the game's classes (count of class names): `NWorld` 818 (game world: adventure map, heroes, towns, AI, commands), `NUI` 627, `NDb` 327 (game database / data files), `NUbi` 191 (ubi.com and LAN), `NGScene` 168 (3D scene), `NSound` 44, `NGfx` 41, `NRender` 37, `NMP` 26 (multiplayer flow), `NAI` 12 (AI geometry), `NNet` 7 (network driver), and about 30 smaller ones.

## Where the AI turn lives

| What | Where | Notes |
|---|---|---|
| `NWorld::CPlayerAI` | `0x8667b0`–`0x8708d0` (13 functions tied to the class so far) | The adventure-map AI of one player. Its turn runs as a series of stages, named in a string table at `0x866550`: `INIT_DAY`, `MOVE_HERO`, `COLLECT_PASSABLE_OBJECTS`, `BUILD_HERO_CHOICES`, `ESTIMATE_CHOICES`, `EXECUTE_BEST_CHOICE`, `MANAGE_TOWNS`, `HIRE_HEROES`, … |
| `CPlayerAI::InitDay` | `0x867440` | One of the 11 functions the exe exports by name (`SFLB_InitDay`), so we know its address and real name for sure. |
| AI timing logs | `0x868eb0` "end turn: %i steps, %i.%i seconds", `0x86d750` "begin day / Clear pathfinders: %d ms / Solve puzzlemap: %d ms / CHeck threats: %d ms", `0x86c010` "Wave spread … time = %d ms" | **The AI already measures its own turn**, step by step. If we can switch this output on (or capture it with the hook), we get timings for free. |
| AI limits | `0x86eb60` reads `ai_time_limit` and `aai_step_limit`; `0x86dd40` reads `aai_proceed` and prints "time limit (%d seconds)" | Console variables. **Your profile (`profiles/default_profile/user_a2.cfg`) sets `ai_time_limit = 180000`, i.e. 180,000 ms = 3 minutes per AI turn.** |
| AI log file | `0xb60340` opens `../ai_#%i.log` (one per player) | Turned on by some setting we have not found yet. |
| Other AI pieces | `NWorld::CPlayerAIInformation` (`0xc280f0`), `NWorld::CStrategicGoals` (`0xc21210`), `NWorld::CCombatAI` (`0xc96d10`, combat AI), `NAI::CAIMap` (`0x466c50`, AI map geometry) | |
| Other AI settings | `adventure_ai_timing`, `town_ai_log`, `combat_ai_lookahead`, `combat_ai_retreat_value`, `combat_ai_use_magic_for_human`, `aai_play_for_humans`, `aai_testmap`, `show_ai_fog`, `show_ai_geometry` | `aai_play_for_humans` (AI plays for the human players) will help with automatic AI-vs-AI tests in phase 5. |
| AI commands | about 20 `NWorld::…Cmd` classes for AI (`CAIArrangeTownHeroesCmd`, `CSetHeroAITargetCmd`, `CSetPlayerAIThreatsInfoCmd`, …) and `NWorld::CAICmds` (`0x7c6d90`) | The AI changes the world through command objects, like human players do. |

## Where the network and sync code lives

| What | Where | Notes |
|---|---|---|
| Network driver | `NNet::CNetDriver` (`0x47c460`–`0x4f8c10`), `NNet::CLinksManager` (`0x484a30`) | Sockets through `wsock32.dll` (UDP `sendto`/`recvfrom` and TCP `connect`/`send`/`recv`). Settings `net_game_port`, `net_timeout`, `net_batched_segments`, `net_load_timeout`. |
| Command transport ("transceivers") | `0x7c4000`–`0x7cc000`: `NWorld::CMultiPlayerTransceiver`, `CSinglePlayerTransceiver`, `CReplayTransceiver` | Every change to the world travels as a command through a transceiver. There are 238 `NWorld::…Cmd` classes (move hero, end turn, AI commands, …). |
| Desync detection | `NWorld::CCrcTestCmd`; `0x7c7be0` ("world CRC", "CRC from reply", "game and replay are divergenting at this point"); `0x7c9410` ("Multiplayer get out of sync", then saves `async_%1d_%d.sav`); settings `mp_check_crc`, `mp_check_object_crc` (`0x7cb230`) | The game already computes a checksum of the world, compares it between players, and **saves the world to a file when they differ**. |
| Debug logs for desyncs | settings `log_random_calls` (`0xb26200`), `log_game_calcs`, `log_client` | Logging every random-number call is the classic way to find the first point where two PCs diverge. |
| Multiplayer flow | `NMP::…` (`CHostHandler` `0x8213b0`, `CMPLoad`, `CMPStart`, save/load commands) | Host/client game setup and loading. |
| LAN discovery | `NUbi::NLAN::…Packet` classes (game info, enter-game request/allowed/denied, chat), `SDriverIPNetworkBroadcast` | How games are found on a LAN, and what GameRanger emulates. |
| ubi.com online | `NUbi::…` (lobby, login, rooms), Ubisoft SDK at `0xd59e90`+ | Dead since Ubisoft shut down the servers; not relevant. |
| Turn flow | `NWorld::CEndOfTurn`, `CStartNextHotSeatTurnCmd`, `CBreakSimultaneousTurnsCmd` (the game has simultaneous turns) | |

**Working hypothesis for phase 1:** this is "lockstep" multiplayer. Every PC runs the full game simulation and only commands are sent over the network, kept in step by the world checksum. If so, the AI's decisions are probably computed on every PC, or computed on one and sent as commands. Which of the two is the first question phase 1 must answer, because it decides how much work "host-authoritative AI" (option A in CLAUDE.md) needs. The large set of AI command classes suggests the AI's output is already in a form that can be sent over the network.

## Surprises

1. **Four different game exes** in `bin\`: vanilla 3.1 (2008), the 3.1j "enhanced AI" build (2011, Quantomas AI, with the AI in a separate process `H5_AIProcess_31j.exe`, single player only), a **2025 Ubisoft rebuild** with a modern compiler (current `H5_Game.exe`, installed by a Steam update), and a Duel Mode rebuild.
2. **Steam DRM (SteamStub variant 2.1)** encrypts the code of the Steam exes. We unwrap our copy with Steamless. The wrapper also erased the exe's compiler fingerprint (Rich header), which made Ghidra skip its C++ class analysis and miss 13,700 virtual functions until we worked around it (see the inventory README).
3. **MMH5.5 already hooks the game** the way we plan to: it renames the `um.dll` import to its own `55u.dll` and patches 12 small code spots.
4. **No packers or obfuscation** beyond the Steam wrapper. The 11 exported `SFLB_…` functions look like markers left by the StarForce copy-protection tooling of the retail version. We have not seen any other trace of StarForce so far.
5. **Lua is compiled into the game project** (not a separate library), so its functions sit between game files.
6. **The AI is allowed 3 minutes per turn** (`ai_time_limit = 180000` in the user profile), and it already times itself.

## Recommendations for session 2

### 1. Hook DLL: a `dinput8.dll` proxy

- The game imports exactly one function from `DINPUT8.dll` (`DirectInput8Create`). Windows looks for that DLL in the game folder before the system folder, so a `dinput8.dll` placed next to the exe is loaded at startup. It forwards `DirectInput8Create` to the real system DLL, then installs our hooks.
- It **adds** a file and replaces none, it does not collide with MMH5.5's `55u.dll`, and it stays away from Direct3D (on Linux, Proton/DXVK provide their own `d3d9.dll`).
- On Linux (Wine/Proton) it needs the DLL override `WINEDLLOVERRIDES="dinput8=n,b"` (Steam launch option, or `winecfg` for GameRanger's Wine prefix).
- The DLL must check which exe it is in (by hashing `.text` or a few known bytes) and only hook known builds: vanilla 3.1 and `MMH55_Utility(_64)`, which share addresses. It does nothing in the 3.1j or 2025 builds.
- First hooks (meets the phase 0 gate "DLL loads and logs AI turns"): log process start, then log entry and exit of `CPlayerAI` turn processing (starting from `CPlayerAI::InitDay` at `0x867440` and the "end turn" logger at `0x868eb0`) with timestamps, to a log file next to the exe.
- Build with the Visual Studio 2022 Build Tools already on the PC (x86, `/MT` so it needs no runtime DLLs).
- Open decision: use a small hooking library (MinHook, BSD license) or write our own 5-byte jump patches. See `docs/questions.md`.

### 2. First byte-match oracle (`tools/verify/`)

- Compiler is ready: `C:\tools\vctoolkit2003` (cl 13.10.3077, verified).
- Plan: compile a source file with `cl /c`, read the COFF `.obj` (Python), and for each function compare its bytes with the exe at the known address. Relocation bytes are masked, and each relocation must point to the same symbol/address (resolved through `progress/inventory.csv` and a symbol map we maintain).
- First targets: a few small, self-contained game functions (leaf functions with no calls) to find the exact compiler flags (`/O2` vs `/O1`, `/Oy`, `/GS`, `/G6`/`/G7`, …). Then one complete small translation unit.
- CI later needs the compiler too. It must not be committed (Microsoft's license), so CI would fetch the Toolkit from archive.org and run our extractor (see `docs/questions.md`).

### 3. Quick experiments for the owner (phase 1, on a test copy of the game)

- Lower `ai_time_limit` in a **copy** of the game's profile (for example 20000 = 20 s) and see whether AI turns in a 2-player LAN game get faster without obviously dumber play.
- Turn on `mp_check_crc` and `log_random_calls` in a 2-client game to see what the game writes.

### 4. Inventory improvements (low priority)

- Lua and codec counts are approximate. Compiling Lua 5.0 and libvorbis/libtheora with our 2003 compiler and matching them (or building a Ghidra Function ID database from them) would make these exact and would name those functions for free.
- Map translation units: use RTTI class order, the order of global-object constructors (one block per source file) and string-table order to find file boundaries, and fill `units` in `progress.json`.

## What the owner should review

- This report and the PR summary.
- That the reference exe choice (vanilla 3.1, 2008) is still right after reading "Where the AI turn lives".
- The open questions in `docs/questions.md`.
