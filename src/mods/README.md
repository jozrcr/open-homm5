# Mods: our improvements to the game

Everything here changes how the original game behaves, on purpose. It is kept apart from the decompiled code (`src/decomp/`), which must stay byte-for-byte faithful. The mods are built into the hook DLL (`src/hook/`, `dinput8.dll`) and switched on and off with a settings file, `open-homm5.ini`, next to the game exe.

## LAN link tuning (`net_tuning.cpp`)

**What it fixes:** slow AI turns in co-op (LAN and GameRanger games).

In a multiplayer game only the host computes the AI, and each AI move has to reach the other PCs before the next one can start. The game's network link was tuned for 2006 modems. It sends at most one small packet every 50 ms (about 5 KB/s), and a PC with nothing to send acknowledges only once per second. During AI turns the host's link is full, every PC thinks the others are lagging and slows itself down, and each AI move waits 1 to 2.5 seconds.

The mod changes three network timings on the PC that runs it:

| Setting | Game | Mod |
|---|---|---|
| `keepalive_ms`: how often an idle link sends (and so acknowledges) | 1000 | 50 |
| `send_interval_ms`: how often a link may send a packet | 50 | 10 |
| `batched_segments`: how many "segment done" reports an idle PC holds back | 3 | 0 |

Game commands still run in the same order on every PC; only the network gets faster. **Every player should run it**: each PC only speeds up what it sends.

**Measured** with the robot (two copies of the game on one PC, 2 human players plus 1 computer player, map Trèfle), seconds per AI move:

| | per AI move | typical AI turn |
|---|---|---|
| Game's timings | 2.5 s | 15–36 s |
| With the mod | 0.6 s | 2–7 s |

(Each change alone helps less: send interval only 2.2 s, keepalive only 1.6 s, both 0.9 s, all three 0.6 s.) Over the Internet the ping between the PCs comes on top.

**Settings** (`open-homm5.ini` next to the game exe; without the file the mod is on with the values above):

```ini
[net]
tuning=1             ; 0 = the game's original timings
keepalive_ms=50
send_interval_ms=10
batched_segments=0
```

The DLL's log (`open-homm5.log`) says what it changed, for example `net tuning: links may send every 10 ms (game: 50)`.

## LAN games over VPNs (`lan_discovery.cpp`, on by default)

**What it fixes:** over a virtual network such as ZeroTier (for a friend on Linux, or without GameRanger), the host's game never shows up in the LAN list.

The game finds hosted games by broadcasting a small "any games here?" message about once a second. Windows and Linux send that broadcast out of the main network card only, so it never reaches a VPN. The mod also sends it into **every network adapter**, the VPN's included. For VPNs that carry no broadcasts at all (Tailscale, for example), list the host's address in `lan_peers`; the mod then also asks the host directly. The game's own packets are not changed.

The log says where the messages go, and where games were found and joined:

```
lan: game queries also go to 192.168.1.255 (Ethernet 3), 10.21.174.255 (ZeroTier One [...])
lan: game found at 10.21.174.28:8889 (826 bytes)
lan: join request to 10.21.174.28:8888
```

**Settings** (`open-homm5.ini`):

```ini
[net]
lan_all_adapters=1   ; 0 = only where the game itself sends (the original)
lan_peers=           ; e.g. 10.21.174.28 (comma-separated): also ask these hosts directly
```

Every player who looks for games needs it (the host only answers). On a Windows host the VPN's network must be marked **Private**, or the game's firewall rule blocks it.

## Faster co-op segments (`coop_guard.cpp`, on when every player agrees)

**What it fixes:** in co-op every AI move waits a few "segments" of 200 ms, the steps in which all PCs move the game forward together. Shorter segments make AI moves faster.

**Why it needs agreement:** the segment length decides when things happen in the game world, and every PC runs that world itself. So all PCs must use the same length, or the game desyncs.

**How:**
1. At the start of each multiplayer game, every PC's DLL sends a small hello to the others with the segment length it wants. A PC without our DLL ignores it.
2. Ten seconds in (segment 50), each DLL checks that **every** other PC sent the same wish.
3. If so, they all switch together at that segment. If not (someone has no DLL, an older one, or other settings), nobody switches and the game keeps its own timing.

`open-homm5.log` says which:

```
co-op timing: hello from player 1 (asks for 100 ms segments, protocol 1)
co-op timing: ON from segment 50: 100 ms segments (all 2 PCs agree)
```

**Measured** (robot LAN game, 2 humans + 1 AI, 8 AI turns, network tuning on):

| | game's 200 ms | 100 ms segments |
|---|---:|---:|
| per AI move | ~0.65 s | 0.53 s |
| AI hero move | ~0.84 s | 0.58 s |
| town swap | ~0.42 s | 0.34 s |

The world checksums stayed equal on both PCs (no desync). When the copies disagree, both stay OFF.

**Settings** (`open-homm5.ini`):

```ini
[coop]
guard=1           ; 0 = take no part (no hello; never switch), as without the DLL
fast_segments=1   ; 0 = ask for the game's 200 ms, so nobody switches
segment_ms=50     ; 50 or 100 (50 by default since 0.2.0)
```

## Quicker lockstep (`lockstep.cpp`; the drain and the trace on by default)

Why multiplayer clicks feel slow, and the measurements: [docs/plans/responsiveness.md](../../docs/plans/responsiveness.md). Each setting works on this PC only, so it needs no agreement between the PCs, and none of them changes what happens in the game world.

- **Drain:** the game's message pump (transceiver slot 3, 0x007ca7c0) takes in one "segment done" report per frame. With 4 PCs or a slow frame rate, that holds everyone back. The mod calls the pump again while it still takes in a report, up to two rounds of reports per frame.
- **View delay:** the game's "now" for the screen and the AI (slot 8, 0x007c8b00) is the timer minus 400 ms, so moves start at least 400 ms after their segment.
- **Catch-up (experiment):** the segment timer moves at most 150 ms per frame (0x00475d80), so a PC whose frame froze stays behind.
- **Pacing slope:** the game slows a PC that is ahead of the slowest one by 5% per segment of lead (slot 5, 0x007c8dd0: `speed = max(1 - 0.05 * lead, 0)`, the 0.05 a float at 0x00e12f18). That was made for 200 ms segments: with 50 ms segments, the same lead in milliseconds brakes 4 times harder, and a PC 1 s ahead stops. While co-op timing is ON, the mod points the instruction that reads it (`fmul` at 0x007c8ea2) at its own value, 0.05 × segment / 200 (0.0125 at 50 ms), and raises the lead's cap of 20 segments (0x007c8e7f, 0x007c8ef3; at the cap the speed is 0, which is what stops a PC from running further ahead) to 20 × 200 / segment (80). So the pacing is the game's per millisecond, and a PC stops 4 s ahead, as in the game. (The other `fmul`, at 0x007c8e64, is for a PC that only watches; it counts its lead in 200 ms units, so it stays as it is.) In the robot's games the host then made 17 to 20 segments a second, 20 being real time (without it: 15 to 18).
- **Finer clock:** Windows wakes sleeping threads only every 15.6 ms unless a program asks for more (`timeBeginPeriod`), and the game never does. While the drain or the trace is on, the DLL asks for 1 ms once. The game's network thread (0x0047cbd0) rests `Sleep(10)` between its rounds (`push 0x0a` at 0x0047cc05); `net_sleep_ms` makes that 2 ms, so the 10 ms send interval of the link tuning really is 10 ms.
- **Trace:** every 10 s, one log line with the wait after this PC's own report (average, p80 and longest), who reported last (count per player), frames per second, the drain's extra reports, and the spread of the players' segment counts. A second line times the commands this PC sent (clicks, and the AI's moves on the host), per kind: from sending to the run of their segment (average and longest), and to being shown on this PC (the run, or the margin after this PC's own report if that is later). It ends with a suggested `view_delay_ms`: the p80 of the waits, so that 4 moves in 5 start without a jump.

Example (robot, host, with the AI moving):

```
lockstep: 197 segments in 10 s, waited 117 ms on average (p80 208, longest 557) after this PC's own report; ...
lockstep: 31 commands from this PC, ms from sending to the run of their segment (and to being shown, margin 100 ms): CSetHeroAITargetCmd 8: 137 avg/239 max, shown 174; CSetWalkPathCmd 7: 122 avg/239 max, shown 165; ...; suggested view_delay_ms=208 (p80 of the waits 208 ms)
```

**Robot results** (2 copies on one PC; one run each, the AI decides differently each run):

| Setting | AI moves | AI hero moves | Checksums |
|---|---|---|---|
| Plain | 0.50 s | 0.61 s | all equal |
| Drain plus `view_delay_ms=200` | 0.44 s | 0.46 s | all equal |
| Drain plus `view_delay_ms=250` | 0.42 s | 0.57 s | all equal |
| Drain plus `catch_up_ms=600` | 0.45 s | 0.56 s | all equal |
| 0.2.0 defaults: 50 ms segments, view 100, catch-up 600 | 0.32 s | 0.30 s | 825, all equal |
| **Now:** plus pacing slope, finer clock, `net_sleep_ms=2` (two runs) | **0.27 s, 0.25 s** | **0.20 s, 0.26 s** | 895 and 788, all equal |
| The same with `scale_pacing=0` | 0.35 s | 0.39 s | 825, all equal |
| The same with `view_delay_ms=60` | 0.27 s | 0.23 s | 891, all equal |

```ini
[net]
drain_reports=1   ; 0 = the game's one report per frame
view_delay_ms=100 ; 50..400, only while co-op timing is ON (else the game's 400)
trace_lockstep=1  ; 0 = no summary lines
catch_up_ms=600   ; 150..1000, only while co-op timing is ON (the game: 150)
scale_pacing=1    ; 0 = the game's pacing slope also with co-op timing ON
net_sleep_ms=2    ; 1..10, the network thread's rest between rounds (the game: 10)
```

The robot passes them with `run_lan_test.py --net-setting KEY=VALUE`.

## Quicker autosaves (`autosave.cpp`; the trace and level 1 on by default)

**What it is for:** in co-op games the autosave at the start of each round freezes every PC for a long time. Findings and addresses: [docs/subsystems/save.md](../../docs/subsystems/save.md).

**What the robot found:**
- **One PC asks, every PC saves.** Each round only the PC of the first living human asks for the autosave (a network command); every PC then writes its own file, and everyone waits for the slowest one.
- **The freeze is mostly the game looking through the saves folder.** To pick the name and the file, it reads the header of every multiplayer save in the folder: 4 times on the PC that asks, twice more on every PC. Each header read of a big-map save takes about 85 ms (it rebuilds that game's hero pool).
- With the owner's 17 old multiplayer saves in the folder, a round froze for **about 10 s** (7 s asking, 3.3 s saving). With only the 3 autosaves of the small robot map: 0.1 s and 0.15 s.
- **Without the mod:** moving old multiplayer saves out of your profile's `Saves` folder helps the same way.

**What the mod does:**
- **Trace** (`trace_autosave=1`): one log line per request and per save, with each phase, and how long each PC was frozen:
  ```
  autosave: day 3 (day start): player 0 asks every PC to save (request a6820c3c; finding the oldest autosave and sending took 6763 ms)
  autosave: day 3: player 1 saved "Sauvegarde auto 1" (save 1 this round; request a6820c3c from another PC): 3299 ms in all: 2 ms preparing, 3230 ms until the first stream (disk check, file name, header), 66 ms serializing and compressing (deflate 33 ms, level 1, 5000 KB -> 815 KB in 4 streams), ...
  autosave: player 1 closed no segment for 6661 ms around the save
  ```
- **Faster compression** (`compression=1`): saves use zlib level 1 instead of the game's 6. On the owner's biggest save, the game's own zlib takes 0.60 s instead of 1.75 s, for a file 24% bigger (13.8 MB instead of 11.2). Same format: the game loads them as before (robot: a level-1 autosave loaded and played on). Only saves change, nothing else the game compresses.
- **Fewer autosaves** (`autosave_every=N`): autosave only on days 1, 1+N, 1+2N... (7 = the first day of each week). The day is part of the game, so every PC with the mod skips the same days.
- **Quick look through the saves folder** (`fast_save_list=1`): while the autosave is requested or a multiplayer save runs, the game's reads of the other saves' headers skip the step that rebuilds each one's hero pool. That step only fills a list for the lobby's hero choice, which nothing in a save uses. The names, the choice of the oldest autosave, the file names and the new save's header stay exactly the same. Details and checks in save.md.

A save does not change the game, so none of this can desync: the robot's world checksums stayed equal in every run (2 copies, 6 AI turns each, with and without the owner's saves in the folder).

**Robot results** (A1M4, small map; more in save.md):

| | Request (host) | Each PC's save | Frozen per round | zlib part |
|---|---|---|---|---|
| game's level 6, 3 autosaves in the folder | 0.1 s | 0.16–0.23 s | (not measured) | 0.10 s |
| level 1, 3 autosaves | 0.1 s | 0.12–0.15 s | ~0.3 s | 0.03 s |
| level 1, the owner's 29 saves copied in, `fast_save_list=0` | 6.4–7.2 s | 3.2–3.9 s | ~10 s | 0.03 s |
| **the defaults**, the owner's 29 saves copied in | **0.07–0.08 s** (first: 0.8 s) | **0.11–0.13 s** (first: 0.9 s) | **~0.3 s** (first: ~1 s) | 0.03 s |

```ini
[save]
trace_autosave=1   ; 0 = no autosave lines in the log
compression=1      ; zlib level of saves, 0..9 (-1 = the game's 6)
autosave_every=1   ; autosave every N days (1 = every day, as the game)
fast_save_list=1   ; 0 = the game's full read of every save in the folder
```

The robot passes them with `--save-setting KEY=VALUE`; `run_lan_test.py --autosave` ticks the lobby's autosave option.

**Not built:** making only the host ask. The game already sends one request per round, from the PC of the first living human (not always the host): skipping that PC's request would lose the autosave.

## "Your turn" notification (`notify.cpp`, on by default)

**What it is for:** while the computer players move, the friends switch to another window and miss the moment they can play again.

**What it does:** when this PC's player can play again (the computer players are done and a new day starts, or in a game without simultaneous turns the turn passes to this player) **and the game is not the window in front**:
- the game's **taskbar button flashes** until you come back to the game;
- a **short sound** plays: Windows' "System Notification" sound where Windows has one (Windows 8 and later), otherwise a short chime the mod makes in memory (Wine has no Windows sounds, so on Linux you hear the chime);
- a **notification pop-up** appears from a small icon in the notification area ("Your turn (day 5)."), and the icon goes away when you are back in the game. Where there is no notification area (often under Wine/Proton), nothing shows and nothing breaks.

Not on the very first turn of a game (when it starts, or after loading a save): you are at the screen then. The log has one line per turn:

```
notify: your turn (day 1): the first turn of this game, no notification
notify: your turn (day 2): taskbar flash, the system notification sound, pop-up
notify: your turn (day 3): the game is in front, no notification
```

**How it knows:** every player of the game world has a turn state (0 = playing now, 1 = waiting). After each frame of the adventure map, the mod looks at this PC's player: a living human whose state just became 0 can play. The game finds "the player whose turn it is" the same way. Details and addresses: [docs/subsystems/ai-turn.md](../../docs/subsystems/ai-turn.md), "Whose turn it is". It only reads the game: nothing in the game changes, so it cannot desync.

**Checked with the robot** (A1M4, game windows in the background, 4 AI turns per run):

| Game | Notifications | World checksums |
|---|---|---|
| LAN, 2 humans + 1 AI, turns one after another | host: days 2, 3, 4, 5; client: days 2, 4, 5 (on day 3 the client's robot ended the turn in the same frame it began) | 615, all equal |
| LAN, simultaneous turns, sound and pop-up on | both: days 2, 3, 4, 5 (sound and pop-up shown, no error) | 333, all equal |
| single player | day 2 | (one PC) |

None on day 1. Not checked by the robot: the game window in front (it would take the focus from whoever uses the PC), and Linux.

**Settings** (`open-homm5.ini`):

```ini
[notify]
turn=1    ; 0 = no notification at all
sound=1   ; 0 = no sound
popup=1   ; 0 = no pop-up
trace=0   ; testing: 1 = log every change of the players' turn states
```

The robot keeps robot runs quiet (`sound=0`, `popup=0`; the flash and the log lines stay); `--notify-setting KEY=VALUE` changes that.

## Player list (`player_list*.cpp`, on by default)

**What it is for:** in co-op games with simultaneous turns, everyone wants to know who still plays and who has pressed End Turn.

**What it shows:** a small panel on the adventure map's left edge, between the resource bar and the minimap (the owner's design):
- the day and how many humans are ready ("Jour 34 - 3/4 Prêts", or "Day 34 - 3/4 ready");
- the teams, each with a numbered round button and a thin line (none in a free-for-all);
- one pill per player: the hero's portrait (a little bigger than the pill, on its left end, like the game's round buttons on its bars) in a ring of the player's colour, the player's name, and the turn: **green dot** = playing, **tick** = turn ended, **three blinking dots** = the computer player is moving, **ring** = waiting.

It looks like the game's own interface: the pills are cut from the game's resource bar picture (its metal frame, read from the game's data), round things get the game's thin bronze rims, and the text is white with a dark shadow. If that picture cannot be read, the log says so and the panel uses plain dark capsules.

It only shows the adventure map (not in towns, battles or menus). **F9** hides it or shows it again. When the mouse is over it, it turns almost invisible, so it never hides the map. It never takes a click or a key from the game. Its words follow the game's language (French or English).

**Where the data comes from:** the game world's players (turn order, name, colour, team, selected hero, "my turn now", "pressed End Turn"), the same on every PC; the hero's name and portrait from its game data. Details and addresses: [docs/subsystems/players.md](../../docs/subsystems/players.md). It only reads the game, so it cannot desync a game, and each player can turn it on or off.

**How it draws:** the DLL sees the game's Direct3D 9 device (`src/hook/d3d9_hook.cpp`: the `Direct3DCreate9` import, then entries of the Direct3D objects' vtables; data patches only) and draws just before each frame is shown, with [Dear ImGui](https://github.com/ocornut/imgui) (`src/hook/third_party/imgui`, v1.91.9b, MIT). A device reset (fullscreen switch, alt-tab) is handled. It stays plain Direct3D 9 and Win32, so it should work under Wine/Proton (with their `d3d9.dll`), not checked there yet.

**Checked with the robot** (A1M4, LAN: 2 humans + 1 computer player; screenshots show the panel, see `tools/robot/README.md`):

| Game | What the panel showed | World checksums |
|---|---|---|
| simultaneous turns, teams humans vs AI | both humans "playing", then a tick for each one who ended, then "thinking" for the computer; the same on the client | 501, all equal |
| turns one after another, teams humans vs AI | host playing / client waiting, then the other way round, then the computer | 711, all equal |
| simultaneous turns, free-for-all | no team buttons; "Ordinateur 1" for the nameless computer player | 357, all equal |
| single player, 2560×1080 | "Player 1", "AI 2", "AI 3" (the game's names), one at a time | (one PC) |
| simultaneous turns, teams, 1920×1080 | the same, 1.4 times bigger, as the game's own interface | 267, all equal |
| 8 rows (the client's `player_list_test_rows=8`: 4 teams of 2, the tallest list), 1024×768 and 1920×1080 | the list starts higher and gets a little smaller, and stays above the chat box | 259 and 254, all equal |

**Screen sizes:** the game's interface grows with the screen height (it is laid out for 768 lines), so the list does too: its sizes and its distance to the left edge are fractions of the height, and its fonts are made again at the new size. At 1920×1080 and 2560×1080 it keeps the same size next to the resource bar and the minimap as at 1024×768, and stays clear of the minimap and the chat box.

Not checked by the robot: F9 and the fade (the robot's windows are in the background), fullscreen, and Linux.

**Settings** (`open-homm5.ini`):

```ini
[ui]
player_list=1          ; 0 = no player list (nothing is installed)
player_list_key=F9     ; F1..F12, or none
player_list_top=34     ; where it starts, % of the screen height
player_list_left=2     ; its left inset, % of the screen height
language=auto          ; auto (the game's language), en, fr
trace_players=0        ; testing: 1 = log each change of a player's turn
player_list_test_rows=0  ; testing: show N rows (copies of the real players, teams of two)
```

## Fast AI pathfinding (`fast_paths*.cpp`, on by default)

**What it fixes:** long AI thinking late in a game on big maps (several seconds per AI move).

Late in a game, nearly all of the AI's thinking time goes into searching paths for heroes (see [docs/subsystems/ai-pathfinder.md](../../docs/subsystems/ai-pathfinder.md)). The mod keeps every step of every search and makes the steps cheaper:

- **Remembered checks.** One search can look at the same map tile over a hundred times, and each time it repeats checks that only depend on the tile ("can the hero step there?", "is there something to visit?", "what does a step onto it cost?"). The mod remembers their answers until the search ends, asking the game's own functions the first time.
- **Faster open list.** The list of tiles still to look at is sorted with the same algorithm and the same order as the game's, written out again so that it compares in place instead of copying every tile it looks at. At start-up the mod tests it against the game's own code and keeps the game's if anything differs.
- **Ported search core.** The search loop, the node expansion and the "relax a neighbour" step are rewritten from the game's code, step by step, calling the game's own functions in the same order. Inside it, the list of tiles still to look at is kept in a compact form (same order as the game's) and given back to the game when the search stops.
- **Fast "go ashore?" check.** Half of the remaining time was one check that scans every object of the map floor three times per tile. The mod builds, once per AI step, tables of which object covers which tile (same order rules as the game) and answers from them.

Because every search takes exactly the same steps, **every path stays the same**. In co-op only the host runs the AI, so the mod only matters on the host's PC, and it cannot cause a desync.

One subtlety: the original AI thinks in real-time slices, so how fast it runs can change what it decides, just as the original game decides a bit differently on a faster PC. The robot checks the mod with a virtual clock that removes that effect: then the original and the mod make the same decisions over whole turns.

**Checked** with the robot on four of the owner's saves (`tools/robot/path_check.py`, 4 whole AI turns each, virtual clock): every big path search gave exactly the same result as the game's own, node by node (leaving out only padding and flag bits the game never reads), plus the order of reached tiles and what is left of the open list.

**Measured** (owner's day-50 save, huge map, 4 AI turns, 85 AI moves):

| | AI thinking |
|---|---:|
| without the mod | 93.3 s |
| remembered checks + faster open list + ported search core | 44.0 s |
| + fast "go ashore?" check | 22.7 s |
| + cheaper steps (per-tile records, compact open list, fewer copies) | **16.7 s** (x5.6) |

**Settings** (`open-homm5.ini`):

```ini
[ai]
fast_paths=1         ; 0 = the game's own pathfinding (default 1 since 2026-10-24)
remember_checks=1    ; the parts, one by one (all default 1)
fast_queue=1
port_relax=1         ; the ported search core
port_search=1
port_expand=1
fast_ashore=1        ; the fast "go ashore?" check
ashore_check=0       ; 1 = also ask the game every time and log any difference (slow, for testing)
max_expansions=0     ; EXPERIMENT, CHANGES THE AI'S PATHS: expand a tile at most N times per search (0 = off)
```

`max_expansions` is not part of the exact speed-up: it is a first test of a faster search that may change the AI's paths (see the experiment in [ai-pathfinder.md](../../docs/subsystems/ai-pathfinder.md)). With 16 on the owner's day-50 save, AI thinking went from 16.7 s to 8.3 s; only the AI hero's giant long-range searches changed. On another save the AI then chose differently after a few moves. Off by default; not for real games until it has been tested much more.

## Teams on random maps (`rmg_teams.cpp`; on by default only for recipe maps)

Step 2 of the map generator plan ([docs/plans/map-generator.md](../../docs/plans/map-generator.md)).

- **Where teams live:** in the game setup (`NWorld::CPlayersStartInfo`), one slot per player with its team at +0x1c. A random map puts every player on their own team.
- **How it works:** when the host starts the game (0x007200da → 0x0071feb0), it sends its setup to every PC, and each PC creates its players from it (0x00c9fa80). The mod changes the teams in the host's setup just before that.
- **Results:**
  - Every PC gets the same teams, including friends without our DLL.
  - Only the host's setting counts.
  - Maps with teams of their own keep them.
  - A choice that would leave a single team is not applied.

```ini
[rmg]
teams=            ; empty: the map's teams. humans_vs_ai, pairs, or a list like 1,1,2,2,3,3
lobby_teams=0     ; 1 = the lobby's team arrows pick team numbers on maps without teams
trace_teams=1     ; log the teams each PC starts with
recipe_teams=1    ; maps made from a recipe template get the template's teams
```

**Teams of recipe maps** (`recipe_teams=1`, on by default, host only; the owner's choice, 2026-10-25). A map made from a recipe template (`OH-X1-...`, see [Random map templates](#random-map-templates-rmg_templatescpp-rmg_makercpp-rmg_recipecpp-on-by-default)) knows its teams: with 3 teams of 2, players 1+2, 3+4 and 5+6 start in the zones that belong together. When the host starts such a map, it sets those teams, unless `teams=` says something else or the lobby arrows (`lobby_teams=1`) already put two players together. How: the setup constructor gets the map's description, whose `sRMGProps` (`+0x254`) names the template (`+0x28`); we remember the recipe's teams per setup, as for `lobby_teams`. **Robot test** (LAN, a 2v2 recipe map, 2 humans and 2 computer players): both PCs started with the humans on team 0 and the computer players on team 1, checksums equal; with a lobby arrow pressed (`lobby_teams=1`), the arrows' teams; with `recipe_teams=0`, one team per player.

**Choosing teams in the lobby** (`lobby_teams=1`, host only). On maps without teams of their own, each row's team arrows move that player's team number up or down, instead of moving the player to another colour (the game's arrows). The pieces:
- **Which maps:** the setup constructor (0x00ca0240) tells whether the map has teams. We remember that per setup.
- **The rules:** for those maps, `CanChangeTeam` (vtable cell 0x00e92770) allows every playing slot, and `SetTeam` (cell 0x00e92774) changes the slot's team.
- **The other lobbies:** at 0x0071e67d, a 6-byte jump to a small patch makes the host send a row's team to them also when only the team changed. The game itself never changes a team without the colour.
- **At the start,** the teams are renumbered 0, 1, 2…
- **Clients** need nothing: the host decides and tells them. Lobby choices win over `teams=`.
- **Maps with teams of their own** keep the game's arrows.

**Robot tests** (LAN; the host has the settings, the client has none):
- `teams=humans_vs_ai`: both PCs started with the two humans on team 0 and the computer player on team 1. 352 checksums, all equal.
- `teams=1,2,2`: both PCs started with player 1 alone against players 2 and 3. 466 checksums, all equal.
- `lobby_teams=1`, on A1M4:
  - The host pressed its own team arrow right, and the computer player's left; the client pressed its own right.
  - Both PCs started with the host and the computer player together against the client. 544 checksums, all equal.
  - AI moves were slower (1.7 s), as the AI's ally can now see its heroes walk.
- `lobby_teams=1`, on A1M2 (a map with teams): the arrow moved the host to the other team's open seat, as in the game.

The robot sets these with `run_lan_test.py --host-rmg KEY=VALUE`, and presses a team arrow with the step `"38|arrow|<row> team left|right"`.

**Not done yet:** a random map in a LAN robot game. The robot plays A1M4, which has no teams either.

## Random map templates (`rmg_templates.cpp`, `rmg_maker.cpp`, `rmg_recipe.cpp`, on by default)

Step 1 of the map generator plan ([docs/plans/map-generator.md](../../docs/plans/map-generator.md)), and steps 1 and 2 of the in-game builder plan ([docs/plans/in-game-generator.md](../../docs/plans/in-game-generator.md)).

- **How the game finds templates:** the random map screen asks the game's file system (`NVFS::CWinVFS`) for the files in `RMG/Templates` (List, vtable slot 10), then opens each one (Open, slot 7). The mod sits on those two slots. Nothing on disk changes.
- **Template files:** the `.xdb` files of `open-homm5\templates\` next to the exe (the install package puts the 24 ready ones there) are added to the list and served when the game opens them.
- **Recipe names** (new): a template named `RMG/Templates/OH-X1-<16 characters>.xdb` needs no file at all. Its name holds every choice of the template maker, for example `OH-X1-321110nnxxxxxxxa` = 2v2v2, Normal, best size, underground. When the game opens such a name, the DLL builds the template's bytes from the name with `rmg_maker.cpp`, a C++ copy of the web page's template maker. The format: [tools/rmg/README.md](../../tools/rmg/README.md#recipe-names).
  - **Why:** in multiplayer every PC builds the random map itself from the template's path. With a recipe, every PC with the DLL has the template, and they all get the same bytes.
  - **The list** shows every recipe this PC was asked to open (for example a joined game's), plus those of `recipes=`. In the list, a recipe template's name is the maker's name plus `*`, for example `OH-2v2v2-Lu-Normal*`, so it can't be mixed up with the ready file of the same layout.
  - **Same bytes as the page:** `build.ps1 -Test` checks that `rmg_maker.cpp` writes exactly what `tools/rmg/template_maker.js` writes, on the 24 ready templates and 400 random choices.
  - **Same bytes forever:** what an `X1` name builds never changes (`tools/rmg/x1_fingerprints.txt` records 302 of them; the test fails on any change). And at start, the DLL builds three recorded names itself and serves no recipe if one differs (the log says `self-check FAILED`).
  - A file in `open-homm5\templates\` whose name is a recipe is skipped (the log says so): the name decides.
- **Joining a random map game** (`rmg_join.cpp`, step 3). Each PC rebuilds the host's random map from the template's name, the seed and the options, which the host's game info carries.
  - When this PC reads a host's game info, the game loads the template by its name at once: for a recipe, the Open hook builds it and puts it in the list. Just before the game builds the map, a hook on the connector (`0x00701530`) registers the recipe again from the game info, in case.
  - **The guard:** a PC without the map's template used to crash there, because the game used the missing template without a check (`0x007012b1`). Now the game shows its own "can't make the map" message (`RMG_MAP_FAIL`). This works for any missing template, also with `templates=0`.
  - Players without our DLL still crash on templates they lack: everyone should update together.
- **Teams on recipe maps:** see [Teams on random maps](#teams-on-random-maps-rmg_teamscpp-on-by-default-only-for-recipe-maps) (`recipe_teams=1`).
- **Not done yet:** the builder panel (steps 4 to 7).

```ini
[rmg]
templates=1     ; 0 = off: neither the folder's files nor recipe names
recipes=        ; recipe names to offer in the list, separated by commas (for tests, before the panel)
```

The log says what was served, for example:

```
rmg templates: 24 template(s) from open-homm5\templates: OH-2v2-Lu-Normal.xdb, ...
rmg templates: recipe names (RMG/Templates/OH-X1-....xdb) are built when the game asks for them (maker self-check ok); in the list from recipes=: OH-X1-321110nnxxxxxxxa
rmg templates: OH-X1-321110nnxxxxxxxa.xdb built from its name: "OH-2v2v2-Lu-Normal*", 6 players, 13 zones, Large (176x176, with underground), 23121 bytes
```

**Robot tests (2026-10-25):**
- A recipe map made with the console and no file: built, generated and played.
- In a LAN game, the client rebuilt the host's recipe map, and all world checksums were equal.
- A client without recipe support got the game's message instead of a crash.

Details: [the plan's robot results](../../docs/plans/in-game-generator.md#robot-results-2026-10-25). Not checked yet: the generator screen's template list.

## Original music (`original_music*.cpp`, off by default)

**What it is for:** MMH5.5 brings its own music (new tracks for terrains, the main menu, the credits, battles and hero themes). With this switch on, the game plays the **original Heroes V music** wherever MMH5.5 changed it. Off (the default), you hear MMH5.5's music, exactly as without the add-on.

Nothing on disk changes. The game finds its music through small data files in the `.pak` files (see [docs/subsystems/audio.md](../../docs/subsystems/audio.md)): one "music definition" per track, and map tiles, heroes and a faction table that point at them. When the game reads one of those files from MMH5.5's paks, the mod reads the same file from the original game's `data.pak` and gives the game the original music in it:

- **music definitions and the faction music table**: the original file (main menu, credits, the 5 terrain tracks MMH5.5 replaced, the battle music lists);
- **map tiles and heroes**: only their music line goes back to the original's. Everything else in those files (MMH5.5's hero stats, ...) stays MMH5.5's.

So the original choice is made tile by tile: on flowers you hear the original grass music, on a bog the original water music, on roads the terrain's music (MMH5.5 removed it there), even where MMH5.5 plays the same new track on all of them.

**Where the original game has no music** (heroes and tiles only MMH5.5 has, and hero themes MMH5.5 gave to heroes that had none): no hero theme, no tile music. The game then picks, as it always does, among the other music of that place: the terrain under the hero and the map's own music. MMH5.5's new tracks are never played.

The log (`open-homm5.log`) says what was set back, for example:

```
original music: on; mods: MMH55-Data.pak, MMH55-Index.pak, ... (16951 data files, 3028 of them also in the original game); indexed in 32 ms; ...
original music: [5] MapObjects/Haven/Nicolai.(AdvMapHeroShared).xdb: AdventureMusic AdvMap-Heroes/Nicolai.xdb -> none
original music: [26] Sounds/_(Music)/MainMenu/a2-MainMenu.xdb: MMH5.5 track ..\Music\MMH55\MainMenu_Dynasty_002.ogg -> original ..\Music\a2\Main_Menu.ogg
original music: [40] MapObjects/_(AdvMapTile)/Grass/Flowers.xdb: AdventureMusic AdvMap-Terrain/Flowers.xdb -> AdvMap-Terrain/Grass.xdb
```

**Checked** with the robot (switch off, then on, `log_music=1` lists the tracks that start):

| Where | Off (MMH5.5) | On |
|---|---|---|
| main menu | `MMH55\MainMenu_Dynasty_002.ogg` | `a2\Main_Menu.ogg` |
| credits | `MMH55\MainMenu_Credits_002.ogg` | `Credits\Final-Credits.ogg` |
| map A1M4, hero on a stone road | `MMH55\Adventure_Terrain_SummerPlains_001.ogg` | `AdvMap-Terrain\Terrain-Grass.ogg` |
| map A2M1, hero on grass | `MMH55\Adventure_Terrain_SummerPlains_002.ogg` | `AdvMap-Terrain\Terrain-Grass.ogg` |
| random maps (other starts each time) | `MMH55\Adventure_Terrain_ElvenForest_001.ogg`, ... | `Terrain-Dirt.ogg`, `Terrain-Sand.ogg`, `a2\2ndCampaign_Stronghold.ogg`, ... |

In a LAN game with the switch on for the host and off for the client, the world checksums stayed equal (no desync): music is only sound, so each player can choose.

Only mods in the game's `data\*.pak` files are considered (not `UserMODS\`). It works with any MMH5.5 version: the original files are read when the game starts (~40 ms).

**Settings** (`open-homm5.ini`):

```ini
[audio]
original_music=0   ; 1 = the original Heroes V music where MMH5.5 changed it
log_music=0        ; testing: 1 = log every music file the game starts
log_data_files=0   ; testing: log the first N files the game reads from its data
```
