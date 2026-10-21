# Robot: unattended game runs

`run_ai_test.py` plays the game on its own in a **test copy** of the game and reports AI timings. No clicking, no window focus needed (the game runs in a window in the background).

```
powershell -File src\hook\build.ps1                  # build the hook DLL first
python tools\robot\run_ai_test.py                     # defaults: map A1M4, 8 AI turns
python tools\robot\run_ai_test.py --turns 12 --setvar "ai_time_limit = 20000"
python tools\robot\run_ai_test.py --restore-intro     # put the intro videos back in the test copy
```

Results go to `build/robot/<date-time>/` (gitignored): `summary.json`, the DLL log `open-homm5.log`, the game's own console `console.txt`, and screenshots of the game window.

## How it works

The runner prepares the test copy (default `C:\Games\H5_test`, never the Steam folder):

- copies `build/hook/dinput8.dll` into `bin/` and writes `bin/open-homm5-robot.ini`, which turns on robot mode in the DLL (`src/hook/robot*.cpp`). The ini is deleted after the run, so the copy stays playable by hand;
- moves the intro videos (`video/logo-ubi6.ogg`, `logo-nival.ogg`, `intro.ogg`) to `video/robot_disabled/`;
- writes a robot profile in `<test copy>/robot_home/My Games/...`: windowed 1024×768, muted, no "press any key" on the loading screen (`pwl_press_any_key_enabled = 0`), console log on (`game_writelog = 1`).

In robot mode the DLL:

| What | How (data patches only, no game code is modified) |
|---|---|
| Uses the robot profile, never yours | redirects `SHGetFolderPathA(CSIDL_PERSONAL)` (import table) to `robot_home` |
| Keeps the game running at full speed in the background | wraps the game's window procedure (class `"H5"`, via `RegisterClassA` in the import table) so it never hears that it lost activation or focus |
| Starts the map | from the game's main loop (`PeekMessageA` import), runs the console command `advmap <map> 0 <difficulty> 1` the way WinMain runs `mainmenu` (0x00456220 + 0x00c125f0) |
| Plays player 0 as a passive human | when no AI has taken a step for 3 s, calls `NUI::CAdventureController::EndTurn` (0x00604e80); the controller's address comes from its vftable slot 0 |
| Measures the AI | the AI turn trace (`src/hook/ai_trace.cpp`): one log line per AI step |
| Stops | after `--turns` AI turns, or `--seconds` |
| Shows what the game says | copies `printf` / `fprintf` / `OutputDebugStringA` output to the log; the console goes to `console.txt` |

## Things learned getting here (2026-10-21)

- `-advmap <map>` on the command line is useless: WinMain always runs `mainmenu` right after it.
- `advmap <map> -1 ...` ("no human players", as the command's help says) is rejected: "Invalid player index -1!". The AI-test mode (`aai_testmap`) is not registered, and the "AI plays for humans" toggle (0x00590830) is dead code.
- The game ignores keyboard input when its window is in the background (DirectInput is polled once, window messages are ignored), so the robot does not "press keys"; `robot_input.cpp` has a DirectInput keyboard that is kept for later experiments.

## First result

In single player on A1M4, the AI **thinks ~90 ms per turn** but **waits ~400 ms after every step** (25–30 steps per turn), whatever the AI animation speed (`adventure_speed_ai` 1 or 10) or `AI_SEGMENT_DURATION`. See `docs/subsystems/ai-turn.md`.
