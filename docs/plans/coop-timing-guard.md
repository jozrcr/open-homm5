# Plan: faster co-op AI moves with a "same settings" guard

Status: proposal for the owner (2026-10-24). Nothing here is built yet.

## In short

- **What we would get:** AI moves in co-op about **2 to 3 times faster** (each AI move waits ~0.75 s today, down to ~0.25 s), on top of the AI pathfinding speed-up.
- **Why it needs a guard:** the speed-ups change the game's timing. In co-op every PC runs the same game world in lockstep, so **every PC must use the same timing**, or the game desyncs.
- **The guard:** at the start of each game, our DLLs say "hello" to each other. Only if every human player has the DLL with the same settings do they all switch, at the same moment. Otherwise nobody switches and the game runs exactly as today.
- **For players:** everyone installs the same DLL version. If someone forgets, nothing breaks; the game just keeps its original speed, and the log says why.

## The two speed-ups

| | Today | With it | Notes |
|---|---:|---:|---|
| **Shorter segments** (200 → 100 ms) | 0.60 s per AI move | 0.49 s | Measured on 22 Oct, no desync. Human orders also get through a bit faster. No visible change. |
| **AI hero walks finish at once**, as in single player | ~0.75 s per AI hero move | ~0.42 s (~0.25 s with 100 ms segments) | Estimate; to be measured (step 1). **Visible change:** an AI hero a human can see would jump instead of walking. |

Why AI moves wait at all (details in [multiplayer-sync.md](../subsystems/multiplayer-sync.md#what-an-ai-hero-move-waits-for-in-co-op-2026-10-24)):
- the game only moves forward in "segments" of 200 ms that every PC has confirmed;
- every AI move waits 2 segments;
- in co-op, a hero move also waits while the hero walks in game time, 1 or 2 more segments;
- single player finishes AI walks at once.

## Why every PC must agree

In co-op each PC runs the whole game itself. Only the players' orders travel over the network, and each order runs at the same segment everywhere. Segment length and walk duration decide *when* things happen in the game world. If one PC used 100 ms segments and another 200 ms, their worlds would drift apart (a desync).

## How the guard works

1. **Hello.** At the start of each game, every PC's DLL sends one small message to the others: its version and the co-op timing settings it wants.
   - It travels in the game's own world-checksum message, with check number −1. The game ignores that number: we read the code at `0x007c9410`.
   - A PC without our DLL receives it and does nothing.
2. **In time for everyone.** The hellos go out in the first second of the game. The game's network delivers each PC's messages in order, before that PC's later "segment done" reports. A PC cannot pass segment 50 (10 s into the game) without those reports, so by then every PC with the DLL has seen the same hellos.
3. **Decision, the same everywhere.** At segment 50 each DLL applies the same rule: "every human player sent a hello with the same version and settings".
   - If yes, all switch at that segment.
   - If no, nobody switches. A PC without the DLL never switches either, so everyone stays on the original timing.
4. **Telling the players.** A line in `open-homm5.log`, for example "co-op timing: ON (3 players agree)" or "OFF (player 2 has no DLL)". If it's easy, also a chat line.

## What players need

- The same DLL version on every PC, and the same `[coop]` settings (the defaults are fine).
- That's all. A missing or different DLL turns the speed-ups off for that game, without a desync.

Settings (`open-homm5.ini`):

```ini
[coop]
fast_segments=1      ; 100 ms segments when every player agrees (default: your call, see the questions)
instant_ai_walks=0   ; AI heroes finish walking at once, as in single player (off by default: it looks different)
```

## Steps

Each step ends with something we can test.

1. **Research** (about 1 session):
   - find how single player finishes AI walks at once;
   - prototype "instant AI walks" as a robot experiment on both copies, and measure it (with world checksums);
   - prove the message-order assumption with a robot test: log when each hello arrives, compared with the sender's segment reports.
2. **The guard** (1–2 sessions), `src/mods/coop_guard.cpp`: hello, decision, log. Robot tests with two copies, world checksums equal in every case:
   - same settings → ON on both;
   - different settings → OFF on both;
   - one copy pretending to have no DLL → OFF on both.
3. **Shorter segments behind the guard** (about 1 session). Today's experiment changes the length from the very start. A switch at segment 50 needs the game-time formula to stay continuous, so the two places that compute it get a hook. Robot: 20+ AI turns, checksums.
4. **Instant AI walks behind the guard** (1–2 sessions, depends on step 1).
5. **Real games:** you and your friend (Windows + Linux), then 3 players on GameRanger.

## Risks

- **Message order.** If the network did not keep a PC's messages in order, PCs could decide differently, which means a desync. Step 1 tests this before anything is built on it. Fallback: decide later, or add an explicit "I agree" round.
- **Loaded multiplayer saves, players dropping out:** the hello runs once per game session; to be tested in step 2.
- **Players' DLL versions:** the hello carries a protocol version, so different versions simply mean OFF.
- **Instant walks may not be possible cleanly** (step 1 will tell). Shorter segments alone still give ~20%.

## Questions for you

1. When every player has the DLL, should **shorter segments** be on by default? Recommended: yes. Nothing looks different; the game just reacts faster.
2. **Instant AI walks:** off by default because it looks different, OK? If possible, should it apply only to AI heroes that no human can see? Recommended: yes to both.
3. Go ahead with step 1?

## Technical notes (for future sessions)

- **Carrier:** `NWorld::CCrcTestCmd`:
  - vtable `0x00e43db0`, 0x2c bytes: check number at `+0x0c`, checksum at `+0x10`, a vector at `+0x20` (per-object checksums) that can carry our payload.
  - Sent like the game's own (`0x007c98e0`, around `0x007c9cd0`): handled locally by `0x007c9410(player, cmd)`, then sent through `transceiver+0x34`, slot 7 (`+0x1c`). That is the same network object as world commands and segment reports.
  - Received in the message pump `0x007ca7c0` → `0x007c9410` (call at `0x007ca8d1`). The robot's checksum trace already hooks both call sites.
- **`0x007c9410` with check −1:**
  - The catch-up `base + 200·check + 30000` becomes `base + 29800`, below the current segment time, so nothing happens. A large negative number would wrap around (unsigned) and push the clock forward: never use one.
  - With `mp_check_crc` off it then returns.
  - With it on, the compare branch needs `check > 0`, so it skips; the "store" branch is not taken.
- **Switch point:** this PC's own step (transceiver slot 4, `0x007ca990`) for segment 50.
- **Segment length:**
  - the three immediates `0x007c8b20`, `0x007ca9df`, `0x007cc0d4`;
  - the game-time formula `base + 200·n + 30000` at `0x007c9a48`–`0x007c9a5c` (segment runner) and `0x007c9456`–`0x007c946d` (checksum catch-up). After a switch at segment S it must become `base + 30000 + 200·S + N·(n − S)`.
- **Walk wait:** the AI manager's busy check `0x0098e1c0`: the movers' "moving" flag (slot `0xc`) and end time (slot `0x10`), see multiplayer-sync.md.
