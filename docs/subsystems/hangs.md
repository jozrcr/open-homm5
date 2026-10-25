# Freezes ("not responding") and the hang watchdog

Status: one real freeze, **cause not known yet** (2026-10-25). The DLL now has a watchdog that writes where the game is stuck the next time (`src/hook/watchdog.cpp`). This page has what the log of that freeze shows, the suspects, and how to read a watchdog report.

## The freeze of 2026-10-25

A 3-human co-op game with release 0.2.3, the owner's PC hosting (`MMH55_Utility_64.exe`). Only the host froze: its window went "not responding", one core at 100% for minutes, until it was killed. In co-op only the host computes the AI turns and sends their commands (`ai-turn.md`), so a loop in the AI, or in our code running on the host during AI turns, freezes only the host.

What the host's log (`open-homm5.log`, thread `[28588]`) shows:

- **When:** day 15, the third AI player's turn (`ai=1FBCDB00`), 7.6 s into it, about 2.5 minutes after the day-15 autosave.
- **The last line**, `3219141 ai-step ... stage 1->13 calls=24 cmds=9 ... gap=0.1ms types=00e0fbd8,00e0db8c x7`: the AI left `MOVE_HERO` (stage 1) for `STEP_REARRANGE_TOWN_HEROES` (13) and sent the threat info plus 7 `CSetPlayerMoneyCmd` (one per resource: a market trade) and one more command (the log shows 8 of the 9). The step before it, `1->1 cmds=4` (a hero move plus `CUpdateHeroRoleLinkTownCmd`), returned **in the same frame** (gap 0.1 ms).
- **Then nothing:** no next AI step (normally `13->0` with `CEndOfTurn`), and the lockstep summary due 10 s after the previous one (about 3222016) never came. The lockstep pump runs at the start of each adventure controller update (`0x006083d9`), so the main thread stopped going round its frame somewhere between 3219141 and 3222016.
- **The game was in the background:** from 3181 s it ran at 13-18 frames/s, the rate it shows every time the owner switched away from the full-screen game in this session (e.g. 548-648 s, 2811-2981 s).
- **The AI was thinking slowly:** that AI's `MOVE_HERO` steps took 110-150 ms each on day 15 (25 ms on day 2). That grew over the days (164 ms on day 12) and the replay below shows the same, so it is the map, not the freeze.
- **The replay passed:** the owner reloaded the day-15 autosave (session `[30836]`). The same AI made the same moves (`1->1 cmds=4` after a ~2 s wait, then `1->13 cmds=9` with the same money commands), but 207 ms apart instead of in the same frame, and then `13->0` with `CEndOfTurn`. So the freeze depends on timing, not only on the game's state.
- No Windows hang report: the process was killed (Task Manager), not closed through the "not responding" dialog.

### Something new in 0.2.x: several AI decisions in one frame

The AI manager (`0x006082f0`, the adventure controller's frame update) runs this each frame:

```
pump (slot 3: take in the other PCs' reports, run the complete segments)
pacing (slot 5)
while (transceiver->slot6(&t))     // 0x007c8b10: is this PC's segment timer past its last closed segment?
    transceiver->slot4()           // 0x007ca990: close one segment (+0x2c += segment length), report it
    if (an AI player has the turn && !world->slotA4(GetTime(), player))   // 0x0098e1c0: "busy at the shown time?"
        ai->slot0(&commands, flag) // the turn driver; then each command is sent (0x00604360)
    transceiver->slot1(...)
```

With the game's timing (200 ms segments, the segment timer moving at most 150 ms per frame: `0x00475da4`), that loop closes at most one segment per frame, so the AI decides at most once per frame. Our co-op timing (50 ms segments, `catch_up_ms=600`) lets it close up to 12 per frame, and the turn driver is called in each, as long as the "busy" check passes. That check looks at what is shown (`GetTime()` = timer − margin), and a command that was just sent has not run yet, so it passes: **the AI takes its next decision before its last one has happened in the world.** Counted from the owner's logs (AI steps that started less than 1 ms after the previous one returned):

| Session | Build, timing | AI steps | Same frame | Longest run |
|---|---|---:|---:|---:|
| `[2528]` | Oct 24 17:29, 100 ms segments, game's catch-up and margin | 247 | 0 | - |
| `[20356]` | 0.1.1, 100 ms segments, game's catch-up and margin | 335 | 0 | - |
| `[28588]` (froze) | 0.2.3, 50 ms, catch-up 600, margin 50 | 809 | 141 (17%) | 6 |
| `[30836]` (replay) | 0.2.3, same | 190 | 26 (14%) | 3 |

It cannot desync (only the host runs the AI; the others apply its commands), and it is part of why the robot measured faster AI moves with 0.2.0. But the game itself never lets its AI decide on a world without its last moves, and the freeze came right after such a pair.

## Suspects, most likely first

| # | Suspect | Where | For | Against |
|---|---|---|---|---|
| 1 | **The game's own AI code**, running on a world that has not applied the AI's last commands (several decisions per frame, above). The next call after `1->13` runs `STEP_REARRANGE_TOWN_HEROES` (`0x00868ca0`), `STEP_ORDER_CARAVANS` (`0x0086a350`, which plans caravan paths), `LAST_WAIT`, `RETURN_TURN`: an endless loop in one of them, or in what they call. | turn driver `0x0086dd40`, stage table `0x0086e14c` | Fits every fact: on the host only, inside an AI turn, timing-dependent (the replay without the same-frame pair passed), new since 0.2.x | Same-frame steps happened 141 times, also into stage 13 (17 times), without a freeze; the stage 13 function itself is a short bounded loop over the towns |
| 2 | **Our pathfinding port** (`src/mods/fast_paths*.cpp`) inside that AI step: the AI's path searches run our `PortedSearch` loop (`while` the open list is not empty) and helpers | `fast_paths_search.cpp`, game's `0x00c08890` | An endless search would look exactly like this (100% CPU, inside the AI step, no more lines); its inputs are new (a world mid-update, caravans) | Not changed since Oct 24; checked against the game's own search on many saves (`path_check.py`, same paths); its loops only push improved nodes, like the game's |
| 3 | **Direct3D device lost / reset** with the player list's objects: the game was full screen and in the background; coming back resets the device, and a reset fails while any `D3DPOOL_DEFAULT` object lives, which some games retry in a loop | `d3d9_hook.cpp`, `player_list.cpp` | New in 0.2.3; the timing fits a return to the game | Our reset callback releases ImGui's objects first; the textures are `D3DPOOL_MANAGED`; three earlier resets in that session worked; no "reset" log line came |
| 4 | **The lockstep drain and trace** (`lockstep.cpp`) | pump hook on `0x007ca7c0` | Runs every frame on the host | Its loops are bounded (at most 2 × players extra pump calls; the trace's loops by the report counts, now also capped, see below) |
| 5 | **File system hooks** (templates, original music, player list pictures) | CWinVFS slot 7 `0x009e0020`, slot 10 | Run on every file the game opens (a town upgrade opens model files) | All their loops end (the music XML scan always moves on, the mask test is for `*.xdb`); none calls the file system again from inside the hook; the player list reads files on the main thread only |
| 6 | **Clock and network thread** (`timeBeginPeriod(1)`, `net_sleep_ms=1`) | `0x0047cc05`, `FineClock()` | New since 0.2.2 | A finer clock makes waits shorter, it does not make loops endless; the game's segment timer step (`0x00475d80`) caps each frame's move; the network thread sleeps every round; a lock waited for uses no CPU |
| 7 | notify, autosave, teams | `notify.cpp`, `autosave.cpp`, `rmg_teams.cpp` | Run on the host | notify only reads the game per frame (its pop-up has its own thread); autosave only inside saves; teams only in the lobby |

**What was reviewed (static, 2026-10-25):** every new code path that runs on the host's main thread per frame or per command: `lockstep.cpp` (pump drain, trace, send hook, the patched margin, catch-up and pacing immediates), `notify.cpp`, `player_list*.cpp`, `d3d9_hook.cpp`, `autosave.cpp`, `rmg_templates.cpp`, `rmg_teams.cpp`, `original_music*.cpp`, `net_tuning.cpp`, `coop_guard.cpp`. No unbounded loop was found in them. Hook chains: a vtable or call site hooked twice by the same function would call itself (a crash, not a freeze); every install is guarded against running twice.

**Fixed on the way:** the lockstep trace (`TraceLook`) walks from the report counts it saw last to the game's current ones; a count that went back or jumped by more than its 256-segment ring now restarts the trace instead of looping (and indexing) by that difference.

**Not changed (uncertain, for the owner and the next session):** limiting the AI to one decision per frame, or making it wait until the segment with its last commands has run (suspect 1). It would cost some AI speed. The watchdog's next report says whether the freeze is in the AI.

## The hang watchdog

On by default (`[debug] hang_watchdog=1`). How it works is in `src/hook/README.md` and at the top of `watchdog.cpp`; in short: progress = the main thread taking its window messages (the exe's `PeekMessageA` / `GetMessageA`); after `hang_seconds` (20) without, it looks 20 times in 2 s (each look suspends the main thread for well under a millisecond) and writes a report. Once per freeze; it never stops or changes anything.

### Reading a report

```
watchdog: the game's main thread (28588) has not taken its window messages for 20 s ...
watchdog: the main thread used 1990 ms of CPU in those 2016 ms: busy (it is running: an endless or very long loop)
watchdog: EIP by place (of 20 samples): pathfinder search loop (0x00c08890) 14; dinput8.dll (open-homm5)+0x5300..53ff 6
watchdog: EIP 12/20 at MMH55_Utility_64.exe+0x8089a (0x00c0889a, pathfinder search loop+0xa) ...
watchdog: stack 20/20 at 0019f2a4: MMH55_Utility_64.exe+0x46e0a5 (0x0086e0a5, AI turn driver+0x365)
...
watchdog: AI: INSIDE the game's AI turn driver for 23512 ms (ai=1FBCDB00, called at stage 13 STEP_REARRANGE_TOWN_HEROES, its call 25)
watchdog: AI: last steps: ...
watchdog: lockstep: not inside the message pump; the last pump 23490 ms ago ...
watchdog: d3d9: not inside Present; the last frame was shown 31000 ms ago (..., its Present said 88760868: device lost ...)
watchdog: the busiest threads in those 2016 ms (thread, CPU time): 28588 (main) 1990 ms, ...
watchdog: our DLL is at 6f340000 (link time stamp 6ade25e8): ...
```
(an invented example)

- **busy or waiting:** ~2000 ms of CPU = a loop (like the owner's freeze); ~0 = blocked on a lock or a wait (a deadlock); then EIP is in `ntdll` and the stack shows who waits.
- **EIP by place:** a loop's instructions together. Exe addresses are the vanilla 3.1 exe's (the same in `MMH55_Utility`), named when they are in a function our docs know (`kKnown` in `watchdog.cpp`).
- **stack:** values on the stack that point just after a call instruction, found in at least half of the looks, innermost first. Some are stale (left over from earlier calls), but the callers of the loop are there. Our DLL's offsets: `python tools\hang\symbolize.py <open-homm5.log>` names them with `build\hook\dinput8.map` of the same build (check the link time stamp; CI keeps each package build's map as the `open-homm5-<version>-map` artifact) and the exe's with Ghidra's `analysis/export/functions.tsv`.
- **AI / lockstep / d3d9:** our own record of where the main thread was in our hooks: inside the AI turn driver (and since when, at which stage), inside the multiplayer pump, inside Present or a device reset; the last 20 AI steps.
- **"takes its messages again, after N s":** the game came back by itself: a long load or a long step, not a hang.

### Testing it

- `build.ps1 -Test` runs `test/watchdog_test.cpp`: a thread freezes on purpose, once busy and once blocked.
- In the game: `[debug] hang_test=1`, then F12 with the game in front: it spins 25 s in `HangTestSpin` (the report must show it, and the game must go on afterwards).
- Robot: `python tools\robot\run_ai_test.py --turns 2 --debug-setting hang_test_at=60` (the freeze starts once, 60 s after the game first took its messages).
