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
python tools\robot\run_lan_test.py                      # host creates a game on A1M4, client joins
python tools\robot\run_lan_test.py --client-step "27|peek|NUI::CLobbyScreen 1dc 8"
python tools\robot\run_lan_test.py --seconds 400 --turns 6 --trace-mp      # time 6 AI turns, log the lockstep
python tools\robot\run_lan_test.py --seconds 400 --turns 6 --net-tuning off  # the game's own network timings
python tools\robot\lan_report.py --last 2                                   # compare the last two LAN runs
```

Results go to `build/robot/<date-time>-lan/host/` and `.../client/`. The host creates a game on the chosen map, the client joins, both press "ready", the host sets the third slot to a computer player and starts; then the robot ends both humans' turns and the DLL times every AI step on both copies.

Both copies run the LAN link tuning mod (`src/mods/README.md`) unless `--net-tuning off`; the runner writes its settings to `bin/open-homm5-<host|client>.ini` and removes them afterwards.

## Checking the AI and its speed-ups (saves, battles, virtual clock)

```
python tools\robot\run_ai_test.py --save-file <save> --load-save save_11 --quick-combat --virtual-clock 50 --turns 4 --trace-paths
python tools\robot\path_check.py save_11 save_10 --turns 4 --ai-setting port_search=1 --robot-arg=--quick-combat --robot-arg=--virtual-clock=50
python tools\robot\path_report.py build\robot\<plain run> build\robot\<mod run>
```

- `--load-save` / `--save-file`: start from one of the owner's saves (copied into the robot's own profile).
- **Battles:** when the AI attacks the robot's (passive) human player, the robot starts the battle (`input_start_combat`), turns on auto-combat (`input_Combat_AutoCombat`) and answers the retreat box (flee). With `--quick-combat` (`adventure_quick_combat = 1`) battles resolve at once. A run that still cannot go on closes the game and reports `stuck` instead of idling until its time limit.
- `--virtual-clock [ms]`: the game's main thread sees a clock that advances by a fixed step per frame (and by the time slept), not with real time. The AI's decisions depend on how fast it runs (`docs/subsystems/ai-pathfinder.md`); with this clock a save replays the same however fast the code is, so a plain run and a mod run can be compared over whole turns. 50 ms per frame also makes animations and hero walks pass faster.
- `--trace-paths` / `--trace-paths-all`: fingerprints of the AI's path searches (big ones / all), compared by `path_report.py`; `path_check.py` does plain, plain again (reproducible?) and mod for several saves.
- `--fast-paths on`, `--ai-setting key=value`: the pathfinding mod and its parts (`src/mods/README.md`).
- `--profile-ai`: where the AI's thinking time goes (`profile_report.py`; samples in our DLL can be mapped with `build/hook/dinput8.map`).

Some saves do not replay the same even with the virtual clock (sauvegarde_14): `path_check.py` reports and skips them.

## Checking the music (the "original music" mod)

```
python tools\robot\run_ai_test.py --turns 1 --quick-combat --audio-setting log_music=1                            # MMH5.5's music
python tools\robot\run_ai_test.py --turns 1 --quick-combat --audio-setting original_music=1 --audio-setting log_music=1
python tools\robot\run_ai_test.py --no-map --seconds 20 --step "8|console|credits" --audio-setting log_music=1       # the credits
python tools\robot\run_ai_test.py --no-map --seconds 150 --turns 1 --quick-combat --step "8|console|rmg RMG/Templates/2P-Duel-T.xdb -players 2 start" --audio-setting log_music=1
python tools\robot\run_lan_test.py --turns 3 --host-audio original_music=1 --client-audio log_music=1
```

- `--audio-setting key=value` writes `[audio]` settings (`src/mods/README.md`); `run_lan_test.py` has `--host-audio` / `--client-audio`.
- `log_music=1`: the DLL logs every music file the game starts (`music file 2: ..\Music\AdvMap-Terrain\Terrain-Grass.ogg`). Music still streams with the robot's volume 0, so nothing is heard.
- The `rmg` console command makes and starts a random map (template path without a leading `/`). Each run gives other factions, so the hero starts on other terrains (sand, dirt, snow, ...); the map is written to the test copy's `data\RMGTemp`. The test maps (A1M4, A2M1, ...) all start on grass.

## Checking autosaves (the autosave mod)

```
python tools\robot\run_lan_test.py --seconds 400 --turns 6 --autosave                          # the mod's defaults
python tools\robot\run_lan_test.py --seconds 400 --turns 6 --autosave --save-setting compression=-1   # the game's zlib level
python tools\robot\run_ai_test.py --load-save "Sauvegarde auto" --turns 2 --quick-combat         # load an autosave
```

- `--autosave`: the host ticks the setup screen's autosave option (the checkbox named `realtime`), so every PC autosaves at the start of each day. Without it, multiplayer games never autosave (the console variable `autosave` only counts in single player).
- `--save-setting KEY=VALUE` (both copies), `--host-save` / `--client-save`: `[save]` settings (`src/mods/README.md`). `run_ai_test.py` has `--save-setting` too.
- The trace lines start with `autosave:` in both logs. Robot autosaves go to `<test copy>/robot_home-<host|client>/.../Profiles/Robot/Saves/` and stay there between runs (3 files, "Sauvegarde auto 1..3"). With `fast_save_list=0`, the time of each autosave grows with the multiplayer saves in that folder (docs/subsystems/save.md). Saves copied there whose name is an autosave's ("Sauvegarde auto 2") get overwritten: the game reuses the file of the save with the same name.
- Multiplayer saves do not show in the single-player load list, so the load check uses a single-player autosave (`run_ai_test.py` makes one each day: the robot profile has `autosave = 1`).

## Checking the "your turn" notification (the notify mod)

```
python tools\robot\run_lan_test.py --seconds 300 --turns 4 --notify-setting trace=1
python tools\robot\run_lan_test.py --seconds 300 --turns 4 --notify-setting sound=1 --notify-setting popup=1 --host-step "16|option|simultaneus 0"
```

- The robot's windows are in the background, so each new turn of a copy's player gives a `notify: your turn (day N): ...` line (none on the first turn).
- Robot runs are quiet by default (`[notify] sound=0 popup=0`: no sound, no pop-up on the desktop; the taskbar flash and the log lines stay). `--notify-setting KEY=VALUE` (both copies; `run_ai_test.py` has it too) changes that; `trace=1` logs every change of the players' turn states.
- `--host-step "16|option|simultaneus 0"` ticks the setup screen's simultaneous turns option.
- The client's robot ends its player's turn every few seconds without knowing whose turn it is. When that end-of-turn command runs in the same frame as the turn starts, the turn is over before anyone could see it, and there is (rightly) no notification for it.

## Checking the player list (the overlay)

```
python tools\robot\run_lan_test.py --seconds 240 --turns 4 --simultaneous --shot-every 6 --ui-setting trace_players=1
python tools\robot\run_lan_test.py --seconds 240 --turns 4 --host-rmg teams=humans_vs_ai --shot-every 6 --ui-setting trace_players=1
```

- The screenshots (`frame-*.png`) are taken after the overlay has drawn, so they show the panel as a player sees it.
- `--simultaneous`: the host ticks the setup screen's simultaneous turns option (`option|simultaneus 0`).
- `--ui-setting KEY=VALUE` (both copies; `--host-ui` / `--client-ui` for one; `run_ai_test.py` has `--ui-setting` too): `[ui]` settings (`src/mods/README.md`). `trace_players=1` logs each change of a player's status (`players: day 3 (simultaneous turns): player 2 "Robot-client" (human, colour 2, team 0) hero "Anwen": turn ended (turn=1 ended=1)`); `trace_players=2` also dumps the game's raw player and hero data once (research).
- `--host-rmg teams=humans_vs_ai` puts the two humans on one team (A1M4 has no teams of its own), to see the team buttons.
- `--resolution WxH` (both runners; default 1024x768) sets the game windows' size, e.g. `1920x1080` or `2560x1080`, to see how the list scales next to the game's own interface.
- `--client-ui player_list_test_rows=8` makes the client show 8 rows (copies of the real players, in 4 teams of 2: the tallest list), to check that it still fits above the chat box and the minimap.
- Not possible with the robot: F9 and the fade under the mouse (the windows are in the background).

## Robot script

Besides starting a map, the DLL runs a script of timed steps (`--step`, or `--host-step` / `--client-step`), `"<seconds>|<kind>|<text>"`, seconds counted from the first main-loop call:

| Kind | Text | What it does |
|---|---|---|
| `console` | a console command | runs it like the game's console |
| `menu` | `<page> <command>` | a main-menu button: pages `main`, `single`, `multi`, `hotseat`, `mission`, `campaign`, `replay` (e.g. `multi lan`); `robot_menu.cpp` |
| `ui` | `<screen class> <command>` | a button of any screen that takes commands (`IProgrammedReactionsAndChecks`), e.g. `NUI::CLobbyScreen create`; the screen is found through the game's RTTI and a memory scan (`robot_ui.cpp`) |
| `msg` | `<name> [number]` | posts a "game message", like buttons with an `ARSendGameMessage` reaction: e.g. `create`, `load`, `back` on the multiplayer setup screen, `start` in the waiting room (names from `UI/<screen>/*.(ARSendGameMessage).xdb` in the game data) |
| `map` | part of a map file path | highlights that map on the multiplayer setup screen, e.g. `A1M4/` (`robot_lobby.cpp`) |
| `save` | part of a save's name | highlights that save on the single-player load screen (`menu|single load_game`) and selects it; then `msg|load` loads it (`run_ai_test.py --save-file <path> --load-save <name>` does it all) |
| `option` | `<checkbox> <state>` | a checkbox of the multiplayer setup screen: `quick_combat`, `ghostmode`, `realtime` (it is the autosave option), `simultaneus` (simultaneous turns); state 0 = checked (`run_lan_test.py` turns quick combat on, so battles resolve by themselves) |
| `game` | a number | highlights that game (0 = first) in the LAN lobby's list, before `ui|NUI::CLobbyScreen join` |
| `arrow` | `<row> left\|right` | presses a player row's state arrow in the multiplayer waiting room: on your own row it toggles "ready", on an empty slot it cycles open → computer → closed |
| `slots` | (none) | research: logs the waiting room's rows (state, player) and whether the host can start |
| `mem` | hex addresses | research: logs the 32-bit values there (e.g. `f27398` = `net_batched_segments`) |
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
| Takes screenshots | saves the back buffer just before `IDirect3DDevice9::Present` shows it, after the player list overlay drew on it (`robot_screen.cpp`, through the shared Direct3D hooks of `src/hook/d3d9_hook.cpp`) |

## LAN games (two copies on one PC)

Each copy gets `OPEN_HOMM5_INSTANCE=host` or `client` in its environment; the DLL then uses `open-homm5-robot-<instance>.ini` and `open-homm5-<instance>.log`, the runner gives it its own profile (`robot_home-<instance>`), and the DLL renames the game's "already running" mutex (`NIVAL_H5`).

LAN games are found through UDP broadcasts on the discovery port (`net_lan_match_maker_port`, default 8889): every copy sends a 5-byte "02" packet each second, and a host also sends its game (an 826-byte "03" packet) to the copies it heard from. Two copies cannot both bind port 8889 ("address in use", the second copy's LAN screen never opened), and with a shared port (`SO_REUSEADDR`) they could not tell each other apart. So each copy gets its own discovery port (host 8889, client 8891) and the DLL also sends every discovery broadcast to the other copy's port on 127.0.0.1 (`net_lan_relay_ports`, `robot_net.cpp`). The game ports are 8888 (host) and 8890 (client). `net_log=1` logs the socket calls and the first packets.

**Desync check:** the runner sets `mp_check_crc = 1` on both copies (`--no-crc` leaves it off); without it the game computes no world checksums at all. Both copies log every checksum (`trace_crc`), and `lan_report.py` says "all equal on both copies (no desync)" or where they first differ. `--fast-paths on` and `--ai-setting` set the fast pathfinding mod on both copies, as in `run_ai_test.py`. The client never runs the AI (the host does), so its "stuck" exit is off (`stuck_exit=0`). Not handled yet: a battle between the AI and the client's human player can leave the host waiting (the host then exits as stuck).

## Things learned getting here (2026-10-21)

- `-advmap <map>` on the command line is useless: WinMain always runs `mainmenu` right after it.
- `advmap <map> -1 ...` ("no human players", as the command's help says) is rejected: "Invalid player index -1!". The AI-test mode (`aai_testmap`) is not registered, and the "AI plays for humans" toggle (0x00590830) is dead code.
- The game ignores keyboard input when its window is in the background (DirectInput is polled once, window messages are ignored), so the robot does not "press keys"; `robot_input.cpp` has a DirectInput keyboard that is kept for later experiments.

## First result

In single player on A1M4, the AI **thinks ~90 ms per turn** but **waits ~400 ms after every step** (25–30 steps per turn), whatever the AI animation speed (`adventure_speed_ai` 1 or 10) or `AI_SEGMENT_DURATION`. See `docs/subsystems/ai-turn.md`.
