# Responsiveness in multiplayer: plan

Status: **first fixes built and robot-tested (2026-10-25); one question for the owner (Q10).**

## Why clicks feel slow

The owner (2026-10-25, a 4-player game over ZeroTier, all links direct): "all our actions seem a little unresponsive (delay after click)", and "the experience feels close to vanilla". Their log shows our co-op timing ON (100 ms segments, all 4 PCs agree). It also shows AI moves of 0.90 s (median), against 0.49 s in the 2-player game the day before.

How a click becomes a move, from the code (static reading by a research agent; details in [../subsystems/multiplayer-sync.md](../subsystems/multiplayer-sync.md) and the decompiled transceiver in `src/decomp/MultiPlayerTransceiver_007c8960/`):

1. **The click joins the open segment.** The command goes into this PC's currently open segment, with no extra steps ahead (transceiver slot 0, 0x007c9810). It is not sent until this PC closes that segment, which happens on its timer every 100 ms (200 in the original game).
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

## What is built (`src/mods/lockstep.cpp`, see [../../src/mods/README.md](../../src/mods/README.md))

| Setting | Default | What it does |
|---|---|---|
| `drain_reports=1` | **on** | Each frame takes in every report that has arrived. Safe: the same work in the same order, only sooner. |
| `trace_lockstep=1` | **on** | A log line every 10 s: the wait after this PC's own report, who was last, frames per second, the spread. Then a real game tells us who everyone waits for. |
| `view_delay_ms=100` | **100 while co-op timing is ON** (0.2.0; 150 before), else the game's 400 | The margin of point 3. The owner's choice (Q10): everyone is on fiber, and 250 felt too slow. |
| `catch_up_ms=600` | **600 while co-op timing is ON** (0.2.0), else the game's 150 | Point 5: a PC catches up after a freeze. |

**Robot results** (2 copies on one PC, about 45 AI moves each; the AI decides differently every run, so the numbers are noisy):

| Setting | AI moves | AI hero moves | Checksums |
|---|---|---|---|
| Plain | 0.50 s | 0.61 s | 439, all equal |
| Drain + `view_delay_ms=200` | 0.44 s | 0.46 s | 439, all equal |
| Drain + `view_delay_ms=250` | 0.42 s | 0.57 s | 440, all equal |
| Drain + `catch_up_ms=600` | 0.45 s | 0.56 s | 458, all equal |
| **0.2.0 defaults:** `segment_ms=50`, view 100, catch-up 600 | **0.32 s** | **0.30 s** | 825, all equal |

**What the trace showed:**
- Even on one PC, the host waits **130 to 240 ms on average** (up to 700) after its own report, and the client is the last reporter almost every time. The two copies' timers run 2 to 4 segments apart.
- Both copies have frames that freeze for about 390 ms (during AI turns).
- The drain took in about 90 extra reports per 10 s: reports often arrive two at a time.

## Next

1. **Q10, decided: 150 ms by default with co-op timing ON** (moves start 250 ms sooner). Tune it from the trace of the next real game: if the waits stay well under 150, go lower. To make lower margins safe, shrink the drift between the PCs' step timers (catch-up, pacing).
2. **Read the trace from the owner's next real game** (it is on by default). It shows the real wait and who is last, so we can see whether 4 PCs are held back by the network, by frame rates or by freezes.
3. **Then, depending on that trace:**
   - **Adaptive margin:** the view delay follows the measured wait, changed slowly so the clock never jumps.
   - **Catch-up:** if freezes dominate.
   - **Pacing slope:** if the spread stays high (the game slows the leading PC by 5% per segment of lead, a rule tuned for 200 ms segments).
   - **Closing a segment early** when a command is issued: small gain, only if the trace justifies it.
4. Each needs a robot run with checksums. A 3- or 4-copy robot test would help; today's robot runs 2.
