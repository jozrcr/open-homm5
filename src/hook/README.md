# Hook DLL (`dinput8.dll` proxy)

A DLL the game loads at start-up. It lets our code run inside the game, so improvements can ship before the decompilation is complete (the "hook track" in CLAUDE.md).

**What it does today:** forwards DirectInput to the real Windows DLL (so the game works normally), and, if the exe is a build we know, logs every step of every AI turn with its timing to `open-homm5.log` next to the exe. Nothing else; it changes no game behaviour.

## How it gets into the game

The game imports one function from `DINPUT8.dll` (`DirectInput8Create`). Windows looks for DLLs in the exe's own folder first, so a `dinput8.dll` placed next to the exe is loaded instead of the system one. Ours loads the real `dinput8.dll` from the Windows system folder and passes every call through.

It **adds** one file and replaces none, it does not collide with MMH5.5 (which uses `55u.dll` in the `um.dll` import slot), and it stays away from Direct3D (on Linux, Proton/DXVK supply their own `d3d9.dll`).

## Which exes it hooks

On the first `DirectInput8Create` call (after start-up, when even the Steam-wrapped exe has decrypted its code) it checks the exe: load address, link timestamp, image size, a vtable entry and the first bytes of the AI turn driver. It installs hooks only for:

- vanilla ToE 3.1 (2008): the unwrapped reference exe or the Steam `H5_GameOLD.exe`;
- `MMH55_Utility.exe` / `MMH55_Utility_64.exe` (the co-op exe), which has the same code.

In any other exe (3.1j `MMH55(_64).exe`, the 2025 `H5_Game.exe`, …) it only forwards and logs "unknown build".

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

This builds `build\hook\dinput8.dll` and runs two tests outside the game:

- `test/proxy_test.cpp`: loads the DLL, calls `DirectInput8Create`, and checks that a real DirectInput object comes back.
- `test/ai_trace_test.cpp`: calls the AI hook exactly the way the game calls the AI (`__thiscall`, command list then flag) on a fake AI object, built with the compiler's runtime stack check (`/RTCs`). It checks that the arguments reach the original function unchanged and that the log line is right. A calling-convention mistake here would crash the game, so this test matters.

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
| `hooks.cpp/.h` | installs all hooks (honours `open-homm5.nohooks`) |
| `ai_trace.cpp` | the AI turn trace |
| `log.cpp/.h` | `open-homm5.log` writer |
| `robot.cpp/.h`, `robot_endturn.cpp`, `robot_input.cpp` | robot mode for unattended test runs; only active when `open-homm5-robot.ini` is next to the exe (see `tools/robot/README.md`) |
| `test/proxy_test.cpp` | smoke test |
| `build.ps1` | build script |
