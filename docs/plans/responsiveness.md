# Responsiveness in multiplayer: plan

Status: **first fixes built and robot-tested (2026-10-25); four quick wins added the same day (pacing slope, per-command trace, margin floor 50, finer clock).**

## Why clicks feel slow

The owner (2026-10-25, a 4-player game over ZeroTier, all links direct): "all our actions seem a little unresponsive (delay after click)", and "the experience feels close to vanilla". Their log shows our co-op timing ON (100 ms segments, all 4 PCs agree). It also shows AI moves of 0.90 s (median), against 0.49 s in the 2-player game the day before.

How a click becomes a move, from the code (static reading by a research agent; details in [../subsystems/multiplayer-sync.md](../subsystems/multiplayer-sync.md) and the decompiled transceiver in `src/decomp/MultiPlayerTransceiver_007c8960/`):

1. **The click joins the open segment.** The command goes into this PC's currently open segment k, with no extra steps ahead (transceiver slot 0, 0x007c9810), and is sent to the other PCs at once (the network object's slot 0x1c). Its game time is that segment's, T_k = base + 30000 + S·k (S = the segment length: 200 ms in the game, 50 with our co-op timing). This PC closes the segment when its timer passes T_k, so on average S/2 after the click.
2. **The segment runs** on every PC once **every** PC has reported it done.
   - The test is 0x007c8c80, and the reports travel directly between all PCs, not through the host.
   - So everyone waits for the PC that is last: its distance, its frame rate, or a freeze.
3. **The move is shown 400 ms late on purpose.** The screen and the AI follow a "now" that is the timer **minus 400 ms** (slot 8, 0x007c8b00), so that a segment's results are surely everywhere before they are shown.
   - Our shorter segments never touched this margin, which is why clicks still feel like vanilla.
   - The AI's "is my hero still walking" check uses the same "now", so every AI move pays it too.
4. **One report per frame.** Each frame, the message pump (slot 3, 0x007ca7c0) stops after the first "segment done" report.
   - With 4 PCs and 100 ms segments, that is 30 reports a second.
   - A PC below about 30 frames a second falls behind, and the game's pacing (0x007c8dd0) then slows everyone down.
5. **Freezes are never made up.** The segment timer moves at most 150 ms per frame (0x00475d80), so a PC whose frame froze for longer stays behind for good.
6. **The pacing brakes too hard with short segments.** A PC ahead of the slowest one runs its timer at max(1 − 0.05·lead, 0), lead in segments, at most 20 (0x007c8dd0). Made for 200 ms segments: at 50 ms, 200 ms of lead already brakes by 20%, and 1 s stops the PC.
7. **The network thread is slow to wake.** It rests `Sleep(10)` between rounds (0x0047cbd0), but without `timeBeginPeriod` (the game never calls it) Windows wakes it only every 15.6 ms.

So, in short: a click is shown after about S/2 (the segment closes) + max(the wait for the last report, the margin) + a frame or two.

## What is built (`src/mods/lockstep.cpp`, see [../../src/mods/README.md](../../src/mods/README.md))

| Setting | Default | What it does |
|---|---|---|
| `drain_reports=1` | **on** | Each frame takes in every report that has arrived. Safe: the same work in the same order, only sooner. |
| `trace_lockstep=1` | **on** | A log line every 10 s: the wait after this PC's own report (average, p80, longest), who was last, frames per second, the spread. Then a real game tells us who everyone waits for. A second line times this PC's own commands per kind (send to run, and to being shown) and suggests a margin: the p80 of the waits. |
| `view_delay_ms=100` | **100 while co-op timing is ON** (0.2.0; 150 before), else the game's 400 | The margin of point 3, 50 to 400. The owner's choice (Q10): everyone is on fiber, and 250 felt too slow. |
| `catch_up_ms=600` | **600 while co-op timing is ON** (0.2.0), else the game's 150 | Point 5: a PC catches up after a freeze. |
| `scale_pacing=1` | **on while co-op timing is ON**, else the game's slope | Point 6: the slope becomes 0.05 × S / 200 (0.0125 at 50 ms) and the lead's cap 20 × 200 / S (80), so the pacing is the game's per millisecond of lead, and a PC stops 4 s ahead as in the game. The `fmul dword ptr [0x00e12f18]` at 0x007c8ea2 reads our own float instead; the cap is the `cmp ecx, 0x14` at 0x007c8e7f and the `mov eax, 0x14` at 0x007c8ef3. (The other `fmul`, 0x007c8e64, is for a PC that only watches and counts in 200 ms units: left alone.) |
| `net_sleep_ms=2` | **2** (the game: 10) | Point 7: the network thread's `push 0x0a` (0x0047cc05) becomes 2. With the drain or the trace on, the DLL also calls `timeBeginPeriod(1)` once, so that sleeps really are short. |

**Robot results** (2 copies on one PC, about 45 AI moves each; the AI decides differently every run, so the numbers are noisy):

| Setting | AI moves | AI hero moves | Checksums |
|---|---|---|---|
| Plain | 0.50 s | 0.61 s | 439, all equal |
| Drain + `view_delay_ms=200` | 0.44 s | 0.46 s | 439, all equal |
| Drain + `view_delay_ms=250` | 0.42 s | 0.57 s | 440, all equal |
| Drain + `catch_up_ms=600` | 0.45 s | 0.56 s | 458, all equal |
| 0.2.0 defaults: `segment_ms=50`, view 100, catch-up 600 | 0.32 s | 0.30 s | 825, all equal |
| **Quick wins** (pacing slope, finer clock, `net_sleep_ms=2`), two runs | **0.27 s, 0.25 s** | **0.20 s, 0.26 s** | 895 and 788, all equal |
| Quick wins but `scale_pacing=0` | 0.35 s | 0.39 s | 825, all equal |
| Quick wins and `view_delay_ms=60` | 0.27 s | 0.23 s | 891, all equal |

(Quick wins: 5 AI turns, `run_lan_test.py --seconds 330 --turns 5`, about 50 AI moves each.)

**What the per-command trace showed** (host, quick wins, 10-second summaries with the AI moving):
- An AI hero move (`CSetHeroAITargetCmd` + `CSetWalkPathCmd` + `CStartMoveHeroCmd`, sent together) ran 70 to 160 ms after it was sent on average (longest 240) in the first run, and would be shown 150 to 170 ms after; in the second run 110 to 290 ms (longest 450), shown 165 to 290. Without the pacing slope: 110 to 290 ms (longest 550), shown 180 to 300.
- With the pacing slope the host made 17 to 20 segments a second (20 = real time); without it 15 to 18.
- The suggested margin (p80 of the waits) was 200 to 300 ms in these robot games, more than the 100 we use: the client copy, on the same PC, reports last almost every time. So the margin rarely decides when a move is shown here; the waits do. A real game's log will tell whether that is the robot's setup.

**What the trace showed:**
- Even on one PC, the host waits **130 to 240 ms on average** (up to 700) after its own report, and the client is the last reporter almost every time. The two copies' timers run 2 to 4 segments apart.
- Both copies have frames that freeze for about 390 ms (during AI turns).
- The drain took in about 90 extra reports per 10 s: reports often arrive two at a time.

## Autosave (2026-10-25, `src/mods/autosave.cpp`)

The owner: the autosave at the start of each round freezes everyone for a long time. Details in [../subsystems/save.md](../subsystems/save.md).

- **Not N saves per round.** The game sends one autosave request per round, from the PC of the first living human; every PC then saves once. (Robot, 2 PCs, normal and simultaneous turns.) So no "only the host asks" setting was built: it could lose the autosave when the first human is not the host.
- **The freeze is the saves folder.** To pick the name and the file, the game reads the header of every multiplayer save in the profile's `Saves` folder: 4 times on the PC that asks, twice more on every PC. Each read of a big-map save costs ~85 ms (it rebuilds that game's hero pool). With the owner's 17 old multiplayer saves copied into the robot's profile: **~7 s for the request, then ~3.3 s for each PC's save**, every round. With 3 small autosaves only: 0.1 s and 0.15 s.
- **Fixed:** `fast_save_list` skips that hero pool step during these reads only (nothing in a save uses it). Same folder: **0.07–0.08 s for the request, 0.11–0.13 s per save**, about 0.3 s frozen per round instead of ~10 s (the first autosave of a session: ~1 s).
- **Writing is the smaller part.** On the robot's small map, zlib takes 0.1 s per save. On the owner's biggest save (106 MB before compression), the game's zlib takes 1.75 s at its level 6 and 0.60 s at level 1.

| Setting | Default | What it does |
|---|---|---|
| `fast_save_list=1` | **on** | The folder reads during autosaves skip each save's hero pool step. |
| `trace_autosave=1` | **on** | A log line per request and per save (each phase, zlib time and sizes, header reads), and how long each PC closed no segment. |
| `compression=1` | **level 1** (the game: 6) | Saves compress 3x faster, files ~25% bigger, same format. |
| `autosave_every=1` | every day (as the game) | Autosave only on days 1, 1+N, 1+2N..., decided from the game's day, the same on every PC. |

World checksums stayed equal in every robot run. **Next:** read the trace of the owner's next real game (the freeze per PC on big maps, where the world's own writing takes longer).

## Next

1. **Q10, decided: 100 ms by default with co-op timing ON** (0.2.0). Tune it from the trace of the next real game: each summary now ends with "suggested view_delay_ms" (the p80 of the waits). If it stays well under 100, go lower (the floor is now 50).
2. **Read the trace from the owner's next real game** (it is on by default). It shows the real wait and who is last, so we can see whether 4 PCs are held back by the network, by frame rates or by freezes. The per-command line shows how long a click really takes, per kind of command.
3. **Then, depending on that trace:**
   - **Adaptive margin:** the view delay follows the measured wait (the p80 the trace already computes), changed slowly so the clock never jumps.
   - **Catch-up:** if freezes dominate.
   - **Closing a segment early** when a command is issued: saves about S/2 (25 ms at 50 ms segments), only if the trace justifies it.
   - Done: the pacing slope (point 6) and the finer clock (point 7).
4. Each needs a robot run with checksums. A 3- or 4-copy robot test would help; today's robot runs 2.

**Open doubts about the quick wins:**
- With `scale_pacing`, a PC that is really slower (not just seen late) lets the others run further ahead before they wait for it: with a PC at 90% speed, the leaders settle 400 ms ahead instead of 100 ms, and their own clicks wait that long. The game made the same trade with its 200 ms segments; the robot's spread stayed at 0 to 5 segments either way. If a real game's trace shows large spreads, try `scale_pacing=0`.
- The slope alone (0.0125 with the cap left at 20) would never stop a leading PC (speed at least 0.75), so it could run ahead without limit while another PC is frozen. That is why the cap is raised with it.
- `timeBeginPeriod(1)` makes Windows wake threads more often for the whole game, single player too. It costs a little power, nothing else seen (frames per second unchanged in the robot).
