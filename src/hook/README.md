# Hook DLL (`dinput8.dll` proxy)

A DLL the game loads at start-up. It lets our code run inside the game, so improvements can ship before the decompilation is complete (the "hook track" in CLAUDE.md).

**What it does today:** forwards DirectInput to the real Windows DLL (so the game works normally). If the exe is a build we know, it also:

- logs every step of every AI turn with its timing to `open-homm5.log` next to the exe;
- applies our mods from `src/mods/`. Today that's one: **LAN link tuning** (`net_tuning.cpp`), modern network timings that make co-op AI turns about 4× faster. It's on by default; `open-homm5.ini` next to the exe can turn it off (`[net]`, `tuning=0`). See `src/mods/README.md`.

Robot mode (`robot*.cpp`, used by `tools/robot/`) only turns on when an `open-homm5-robot.ini` file sits next to the exe, i.e. in the test copy during robot runs.

## How it gets into the game

The game imports one function from `DINPUT8.dll` (`DirectInput8Create`). Windows looks for DLLs in the exe's own folder first, so a `dinput8.dll` placed next to the exe is loaded instead of the system one. Ours loads the real `dinput8.dll` from the Windows system folder and passes every call through.

It **adds** one file and replaces none, and it does not collide with MMH5.5 (which uses `55u.dll` in the `um.dll` import slot). It does not replace `d3d9.dll` either (on Linux, Proton/DXVK supply their own): to draw the player list over the game, `d3d9_hook.cpp` only replaces the exe's `Direct3DCreate9` import entry and entries of the Direct3D objects' vtables, whichever `d3d9.dll` made them.

## Drawing over the game (`d3d9_hook.cpp`)

The player list overlay (`src/mods/player_list.cpp`) and the robot's screenshots share one set of Direct3D 9 hooks: `Direct3DCreate9` (the exe's import), `IDirect3D9::CreateDevice`, `IDirect3DDevice9::Present` and `Reset` (vtable entries). Each user registers a callback that runs before each frame is shown, in order: the overlay draws first, then the robot takes its screenshot, so the screenshots show the overlay. Windows' `d3d9.dll` gives **each device a vtable of its own** (on the heap): every new device's entries are checked and hooked, not only the first one's.

The overlay draws with **Dear ImGui v1.91.9b** (MIT, <https://github.com/ocornut/imgui>, tag `v1.91.9b`), in `third_party/imgui/`: its core files and its DirectX 9 renderer (`backends/imgui_impl_dx9.*`), unchanged, with its `LICENSE.txt`. Its Win32 backend is not used: it would change the mouse cursor and wants the window's messages; the overlay takes no input, so it reads the mouse position itself.

## Which exes it hooks

On the first `DirectInput8Create` call (after start-up, when even the Steam-wrapped exe has decrypted its code) it checks the exe: load address, link timestamp, image size, a vtable entry and the first bytes of the AI turn driver. It installs hooks only for:

- vanilla ToE 3.1 (2008): the unwrapped reference exe or the Steam `H5_GameOLD.exe`;
- `MMH55_Utility.exe` / `MMH55_Utility_64.exe` (the co-op exe), which has the same code.

In any other exe (3.1j `MMH55(_64).exe`, the 2025 `H5_Game.exe`, …) it only forwards and logs "unknown build".

One hook goes in earlier, when the DLL is loaded (`hooks::InstallEarly`), if the exe can already be recognized then (not the Steam-wrapped one): the "original music" mod's file system hook, only when that mod is on. The game reads its heroes during its splash screen, before the first `DirectInput8Create` call.

If the log says "unknown build" or "could not hook" in `MMH55_Utility`, MMH5.5's own DLL (`55u.dll`) may have changed the same vtable entry or code at run time. Our DLL then stays out of the way instead of guessing. Tell me, and we'll look at what `55u.dll` does.

## The AI trace

The AI turn driver (`IPlayerAI` slot 0, `0x0086dd40`, see `docs/subsystems/ai-turn.md`) is a virtual method, so we hook it by replacing one pointer in `CPlayerAI`'s vtable (`0x00e4c8bc`). No code is patched. Each call produces one log line:

```
  123456 [ 4321] ai-step ai=0A1B2C30 flag=0 stage 3->4 calls=17 cmds=1 turn_age=850ms think=12.34ms gap=180.5ms
```

| Field | Meaning |
|---|---|
| first number | ms since the DLL loaded |
| `[4321]` | thread id |
| `ai=` | address of the `CPlayerAI` object (one per AI player) |
| `stage a->b` | the turn's state-machine stage before and after this call |
| `calls=` | the AI's own call counter |
| `cmds=` | commands in the output list after the call |
| `turn_age=` | ms since this AI's turn started (its own `GetTickCount()` stamp) |
| `think=` | how long this call took (ms): the AI's thinking time |
| `gap=` | time between this AI's previous call returning and this call starting (ms), i.e. the time the AI waited. **This is the number to look at in co-op**: if hypothesis H1 in `docs/subsystems/ai-turn.md` is right, it is about one network round trip after each command in multiplayer, and near zero in single player. |

## Build

Needs Visual Studio 2019 or 2022 (C++ build tools, x86). Already on the owner's PC.

```powershell
powershell -File src\hook\build.ps1 -Test
```

This builds `build\hook\dinput8.dll` and runs these tests outside the game:

- `test/proxy_test.cpp`: loads the DLL, calls `DirectInput8Create`, and checks that a real DirectInput object comes back.
- `test/ai_trace_test.cpp`: calls the AI hook exactly the way the game calls the AI (`__thiscall`, command list then flag) on a fake AI object, built with the compiler's runtime stack check (`/RTCs`). It checks that the arguments reach the original function unchanged and that the log line is right. A calling-convention mistake here would crash the game, so this test matters.
- `test/original_music_test.cpp`: the "original music" mod's text work (`src/mods/original_music_xdb.cpp`) on small made-up data files: which music lines change and which stay.

The build runs in `src\hook` with relative source paths: with full paths, a deep folder (a git worktree) made the command line longer than `cmd.exe` accepts (8191 characters).

## Owner test (phase 0 gate: "DLL loads and logs AI turns; game runs normally")

Use a **separate copy** of the game, never the Steam folder (CLAUDE.md).

1. Copy `build\hook\dinput8.dll` into the copy's `bin\` folder, next to `MMH55_Utility_64.exe`.
2. Start a single-player skirmish **with `MMH55_Utility_64.exe`** (the 3.1j `MMH55_64.exe` is not hooked) against 1–2 AI players, and play 2–3 turns.
3. Check `bin\open-homm5.log`. It should say `game build: ToE 3.1 + MMH5.5 patches (MMH55_Utility)`, then `hooked AI turn driver`, then many `ai-step` lines during AI turns.
4. If anything goes wrong: delete `dinput8.dll` (or create an empty file `open-homm5.nohooks` in `bin\` to keep only forwarding), and send me the log.

On **Linux** (Wine/Proton) Wine prefers its own built-in `dinput8.dll`. Tell it to use ours first: Steam launch option `WINEDLLOVERRIDES="dinput8=n,b" %command%`, or in `winecfg` → Libraries → add `dinput8` as "native, builtin" (for GameRanger's Wine prefix).

## Files

| File | What |
|---|---|
| `dllmain.cpp` | DLL entry, the forwarded exports, lazy hook installation |
| `dinput8.def` | export names (same as the real `dinput8.dll`) |
| `game_build.cpp/.h` | which exe are we in? |
| `hooks.cpp/.h` | installs all hooks (honours `open-homm5.nohooks`); `InstallEarly` for the few that must be in before the game reads its data |
| `ai_trace.cpp` | the AI turn trace |
| `log.cpp/.h` | `open-homm5.log` writer |
| `d3d9_hook.cpp/.h` | the shared Direct3D 9 hooks (overlay, robot screenshots) |
| `third_party/imgui/` | Dear ImGui v1.91.9b (MIT): core + DirectX 9 renderer, for the player list overlay |
| `robot.cpp/.h`, `robot_endturn.cpp`, `robot_input.cpp` | robot mode for unattended test runs; only active when `open-homm5-robot.ini` is next to the exe (see `tools/robot/README.md`) |
| `test/proxy_test.cpp` | smoke test |
| `build.ps1` | build script |
