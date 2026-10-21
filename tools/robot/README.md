# Robot: unattended game runs

`run_ai_test.py` plays the game on its own in a **test copy** of the game and reports AI timings. No clicking, no window focus needed (the game runs in a window in the background).

```
powershell -File src\hook\build.ps1                  # build the hook DLL first
python tools\robot\run_ai_test.py                     # defaults: map A1M4, 8 AI turns
python tools\robot\run_ai_test.py --turns 12 --setvar "ai_time_limit = 20000"
python tools\robot\run_ai_test.py --restore-intro     # put the intro videos back in the test copy
```

Results go to `build/robot/<date-time>/` (gitignored): `summary.json`, the DLL log `open-homm5.log`, the game's own console `console.txt`, and screenshots (`frame-*.png`, the game's own frames saved by the DLL).

`run_lan_test.py` runs **two copies of the game at once** on this PC, a LAN host and a LAN client (see "LAN games" below):

```
python toolsobotun_lan_test.py                      # host creates a game on A1M4, client joins
python toolsobotun_lan_test.py --client-step "27|peek|NUI::CLobbyScreen 1dc 8"
```

Results go to `build/robot/<date-time>-lan/host/` and `.../client/`.

## Robot script

Besides starting a map, the DLL runs a script of timed steps (`--step`, or `--host-step` / `--client-step`), `"<seconds>|<kind>|<text>"`, seconds counted from the first main-loop call:

| Kind | Text | What it does |
|---|---|---|
| `console` | a console command | runs it like the game's console |
| `menu` | `<page> <command>` | a main-menu button: pages `main`, `single`, `multi`, `hotseat`, `mission`, `campaign`, `replay` (e.g. `multi lan`); `robot_menu.cpp` |
| `ui` | `<screen class> <command>` | a button of any screen that takes commands (`IProgrammedReactionsAndChecks`), e.g. `NUI::CLobbyScreen create`; the screen is found through the game's RTTI and a memory scan (`robot_ui.cpp`) |
| `msg` | `<name> [number]` | posts a "game message", like buttons with an `ARSendGameMessage` reaction: e.g. `create`, `load`, `back` on the multiplayer setup screen, `start` in the waiting room (names from `UI/<screen>/*.(ARSendGameMessage).xdb` in the game data) |
| `map` | part of a map file path | highlights that map on the multiplayer setup screen, e.g. `A1M4/` (`robot_lobby.cpp`) |
| `shot` | a label | saves the next frame as `frame-NNN-<label>.png` |
| `peek` | `<screen class> <hex offset> ...` | research: logs the objects along a pointer chain from a screen, with their class names |

When the game hangs, the DLL logs where: if the main loop stops calling `PeekMessageA`, or stops showing frames, for 3 s, it logs the main thread's position and the game's return addresses on its stack (`robot: stall ...`), and how many game messages are queued.

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
| Shows what the game says | copies `printf` / `fprintf` / `OutputDebugStringA` output to the log; the console goes to `console.txt`; message boxes are logged (and plain "OK" boxes answered) |
| Takes screenshots | hooks `IDirect3DDevice9::Present` (through the `Direct3DCreate9` import and two vtable entries of the system `d3d9.dll`) and saves the back buffer (`robot_screen.cpp`) |

## LAN games (two copies on one PC)

Each copy gets `OPEN_HOMM5_INSTANCE=host` or `client` in its environment; the DLL then uses `open-homm5-robot-<instance>.ini` and `open-homm5-<instance>.log`, the runner gives it its own profile (`robot_home-<instance>`), and the DLL renames the game's "already running" mutex (`NIVAL_H5`).

LAN games are found through UDP broadcasts on the discovery port (`net_lan_match_maker_port`, default 8889): every copy sends a 5-byte "02" packet each second, and a host also sends its game (an 826-byte "03" packet) to the copies it heard from. Two copies cannot both bind port 8889 ("address in use", the second copy's LAN screen never opened), and with a shared port (`SO_REUSEADDR`) they could not tell each other apart. So each copy gets its own discovery port (host 8889, client 8891) and the DLL also sends every discovery broadcast to the other copy's port on 127.0.0.1 (`net_lan_relay_ports`, `robot_net.cpp`). The game ports are 8888 (host) and 8890 (client). `net_log=1` logs the socket calls and the first packets.

## Things learned getting here (2026-10-21)

- `-advmap <map>` on the command line is useless: WinMain always runs `mainmenu` right after it.
- `advmap <map> -1 ...` ("no human players", as the command's help says) is rejected: "Invalid player index -1!". The AI-test mode (`aai_testmap`) is not registered, and the "AI plays for humans" toggle (0x00590830) is dead code.
- The game ignores keyboard input when its window is in the background (DirectInput is polled once, window messages are ignored), so the robot does not "press keys"; `robot_input.cpp` has a DirectInput keyboard that is kept for later experiments.

## First result

In single player on A1M4, the AI **thinks ~90 ms per turn** but **waits ~400 ms after every step** (25–30 steps per turn), whatever the AI animation speed (`adventure_speed_ai` 1 or 10) or `AI_SEGMENT_DURATION`. See `docs/subsystems/ai-turn.md`.
