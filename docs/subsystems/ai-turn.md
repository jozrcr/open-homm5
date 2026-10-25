# How an AI turn runs (adventure map)

Status: **first static reading** (2026-10-20), from Ghidra's decompiled code of the reference exe. Nothing here has been checked in a running game yet. Addresses are in the vanilla 3.1 exe and identical in `MMH55_Utility(_64).exe`. Names in `code font` are real (from RTTI or exports); names like "turn driver" are ours.

## The pieces

| Thing | Address | What we know |
|---|---|---|
| `NWorld::IPlayerAI` | vftable `0x00e4c750` (4 slots) | Interface the rest of the game uses to talk to a player's AI. |
| `NWorld::CPlayerAI` | vftables `0x00e4c8bc` (main, 4 slots) and `0x00e4c89c` (second base at +316), object size `0x148` bytes | The adventure-map AI of one player. Implements `IPlayerAI`. |
| Factory | `0x0086d720` | `new CPlayerAI` (`0x148` bytes), calls the constructor at `0x0086cb30`. |
| Constructors | `0x0086c9d0`, `0x0086cb30` | Write the vftable pointers. |
| Destructors | `0x00870190`, `0x008704b0` | |
| **Turn driver**: `IPlayerAI` slot 0 | `0x0086dd40` | Called again and again while it is the AI's turn. Each call does a bit of thinking, then returns (see below). |
| `CPlayerAI::InitDay` | `0x00867440` | One of the 11 named exports (`SFLB_InitDay`). First stage of a turn. Logs "We not't call AdvAI for some days" when the AI was skipped. |
| Stage names | `0x00866550` | Returns the name of a stage, for logs. |
| End-of-turn logger | `0x00868eb0` | Prints "end turn: %i steps, %i.%i seconds". |
| Begin-of-day timings | `0x0086d750` | Prints "begin day", "Clear pathfinders: %d ms", "Solve puzzlemap: %d ms", "CHeck threats: %d ms". |
| Console variables | registered at `0x0086eb60` | `ai_time_limit` → global `0x00f440d0` (milliseconds), `aai_step_limit` → global `0x00f440d4` (milliseconds). |

## The turn driver (`0x0086dd40`)

Signature: `__thiscall`, **two** stack arguments (every exit is `ret 8`; Ghidra's decompiler gets them muddled), as pushed by the only caller (see next section):

1. a pointer to a **command list** the caller owns: a vector `{begin, end, capacity}` of 4-byte entries (probably smart pointers to command objects). The driver stores the pointer at `this+0x130` and clears the list when a new step starts. The commands the AI produces end up in it, **so the caller receives the AI's moves as data**.
2. a flag (a bool; the caller computes it from two globals, see below).

The AI does not think in one go. The game calls the turn driver repeatedly, and each call works like this:

1. Checks a few conditions first (one uses globals `0x00fe8304`, `0x00fe8305`, `0x00fe8308`, probably debug settings such as "skip this player's AI").
2. **Hard time limit:** if the turn started more than `ai_time_limit` ms ago (it stores the start time with `GetTickCount()` at `this+0x12c`), it logs "time limit (%d seconds)" and ends the turn.
3. Otherwise it runs **stages** in a loop: `switch (this->stage)` (stage at `this+0x128`) with at least 15 cases (0–14), each a function: case 0 = `InitDay` + begin-of-day work, then hero movement, choices, towns, hiring and so on. The stage names (`INIT_DAY`, `MOVE_HERO`, `COLLECT_PASSABLE_OBJECTS`, `BUILD_HERO_CHOICES`, `ESTIMATE_CHOICES`, `EXECUTE_BEST_CHOICE`, `MANAGE_TOWNS`, `HIRE_HEROES`, …) come from the table at `0x00866550`.
4. After each stage it **stops** if either
   - more than `aai_step_limit` ms have passed since this call started, or
   - the AI has produced at least one **command** (a list at `this+0x130` is not empty).
5. Before returning it logs `"%s: %u ms, %i cmds"` (stage name, time, number of commands), and increments a call counter at `this+0x10`.

What this means for us:

- **The AI's output is a stream of world commands.** It plans a bit, emits a command (move a hero, buy creatures, …), returns, and the command is executed through the normal command system before the AI continues. That fits option A in CLAUDE.md ("host computes the AI and broadcasts its moves"), since commands are already the unit that travels over the network.
- **The AI works in time slices.** `aai_step_limit` bounds how long one call may think, and the game stays responsive between calls. `ai_time_limit` bounds the whole turn.
- **The owner's profile sets `ai_time_limit = 180000`** (`profiles/default_profile/user_a2.cfg`), i.e. up to 3 minutes per AI player per turn. With several AI players, waiting times add up.

## Who calls it, and where the AI's commands go

The only caller is the **AI manager update** at `0x006082f0` (called from `0x00608930`, which is called from `0x00597d70`). For the player whose turn it is:

1. `0x00608180` returns that player's AI object, creating it if needed (factory `0x0086d720`).
2. It prepares an empty command list on its stack and computes the flag: `flag = g_0x00fd9e78 && ai->Player()->Id() != g_0x00fd9e7c` (two globals, probably console variables such as "AI plays for player N").
3. `ai->slot0(&list, flag)` at `0x00608732`: **the turn driver**.
4. For **every command in the list**, it calls `0x00604360`.

`0x00604360` is a generic **"send command"** method with 16 callers, many of them from the adventure-map UI (the code behind a human player's clicks). It stores the command as "last command" (at `this+0x20`) and calls slot 0 of the object at `this+0x1c`, which is the world's **transceiver** (see [multiplayer-sync.md](multiplayer-sync.md)):

- in single player (`CSinglePlayerTransceiver`, slot 0 = `0x007c80a0`) the command is just queued for execution (`0x0079edf0`);
- in multiplayer (`CMultiPlayerTransceiver`, slot 0 = `0x007c9810`) it is queued **and sent over the network** to the other players (through the object at `this+0x34`).

So **the AI's moves travel exactly like a human player's moves.**

## Who creates the AI

`CPlayerAI` objects are created by the factory `0x0086d720`, called from `0x00608180`. That function goes through a list of players (at `this+0x4c`), checks several virtual "is this player …" methods and the global `0x00fd9e81` (a console variable), and creates or removes AI objects. It is reached from code under `NUI::CAdventureScreen` (`0x0059d5f0` → `0x0058d630` → `0x00608180`).

So the AI is managed from the **adventure-map screen code**, the same layer that turns a human's clicks into commands. Because the AI's commands are sent to the other players like a human's, it seems likely that **only one PC runs a given AI player and the others just apply its commands**. That would mean vanilla already works roughly like option A. This contradicts the earlier guess that every PC runs the AI; the in-game trace will decide. (Why MMH5.5's 3.1j AI cannot do LAN remains open. Its separate AI process may simply not be wired into the multiplayer code.)

## Measured (2026-10-21, robot, single player)

First real measurements, made by the robot (`tools/robot/run_ai_test.py`) with the hook DLL's AI trace on map A1M4 (2 AI players, hard difficulty, MMH55_Utility_64):

| Setting | AI steps per turn | Commands per turn | Think time per turn | Wait between steps (median) |
|---|---:|---:|---:|---:|
| default (`adventure_speed_ai = 2`) | 28–32 | 72–88 | ~110 ms | **401 ms** |
| `adventure_speed_ai = 1` | 26–28 | 61–69 | ~90 ms | **401 ms** |
| `adventure_speed_ai = 10` | 25–27 | 65–70 | ~78 ms | **403 ms** |
| `AI_SEGMENT_DURATION = 10` | 25–26 | 63–65 | ~92 ms | **400 ms** |

- **The AI is not slow at thinking.** A whole AI turn needs about 0.1 s of computation.
- **It waits about 400 ms after every step**, whatever the animation speed. Over a turn that is ~12 s of waiting for ~0.1 s of work, i.e. **over 99% of the AI turn is waiting**, even in single player.
- 400 ms is two of the 200 ms segments seen in the transceiver code, so the wait is probably the time for the AI's commands to go through the command queue (one segment to send, one to execute) before the AI is called again. Not confirmed yet.

This shifts H1: the per-step wait exists even without a network; in multiplayer the network and `net_batched_segments` delays come on top of it.

### What sets the 400 ms rhythm (2026-10-21)

Traced with the robot (`--trace-segments`, and the command types each AI step sends):

- **The AI manager calls the AI once per command segment.** `0x006082f0` runs a loop each frame: `while (transceiver->slot6(&segment)) { transceiver->slot4(); …the current AI player's turn driver… ; transceiver->slot1(…) }`. In single player, slot 6 (`0x007c7ea0`) says a segment is due when game time has passed the transceiver's time, and slot 4 (`0x007c7b20`) runs the queued commands and adds **200 ms** to the transceiver's time. The value 200 is an immediate operand at `0x007c7ba9` and `0x007c7ecf`.
- Game time runs at real time, but advances in steps of ~47 ms (three ticks of the 15.6 ms Windows clock), so a segment cannot be shorter than that.
- **Each AI step waits two segments** before the AI is called again (its commands run in the next segment; the AI only resumes after the world has changed).
- **Hero moves**, which are most AI steps (`CSetHeroAITargetCmd` + `CSetWalkPathCmd` + `CStartMoveHeroCmd`), additionally take **~200 ms of game time** to play out, whatever `adventure_speed_ai` is.

Robot experiment `--segment-ms 50` (memory patch of the two immediates, robot mode only), 6 AI turns on A1M4:

| Wait before the AI's next step, by what the step sent | 200 ms segments (game default) | 50 ms segments |
|---|---:|---:|
| hero move (27 steps) | 401 ms | 199 ms |
| buy creatures + threats info (5) | 392 ms | 109 ms |
| puzzle map + demand/threats info (4) | 399 ms | 89 ms |
| hero roles / town swaps (5) | ~400 ms | 49–92 ms |
| **whole run (6 AI turns)** | **49 s** | **34 s** |

The game kept playing normally with 50 ms segments (AI built, recruited, ended turns; no console errors). Not tested yet: battles, multiplayer, long games.

Also tried, **no effect** on the rhythm: `adventure_speed_ai` 1–10, the `AI_SEGMENT_DURATION` variable (default 50; read at start-up by `0x0092b250`), and the `game_speed <n>` console command (handler `0x00401440`, calls method `0x60` of the game timer interface).

Side finding: the robot game was running "inactive" (40 ms sleep per frame) until robot mode held the active flags `0x00f7a207`/`0x00f7a208` at 1. The main loop (`0x00c3b490`) sets them from queued `WM_ACTIVATEAPP` messages (`0x00c3b3c0`), not from the window procedure.

**Next:** (1) find what makes a hero move take ~200 ms of game time; (2) measure the same in a 2-PC LAN game, where each segment also needs every PC's confirmation; (3) design a real mod (`src/mods/`) for shorter segments, which in multiplayer every PC must run identically.

## Hypothesis H1: why AI turns are slow in co-op

Three facts from the code:

1. The turn driver **stops thinking as soon as it has produced one command** (step 4 above) and returns.
2. The command then goes through the transceiver, in multiplayer **over the network**.
3. The AI plans its next move from the world state, so it has to see its previous command executed first.

If, in multiplayer, a command only executes once it has made its way through the network (the lockstep "everyone has it" point), then **every AI move costs at least one network round trip of waiting**. A typical AI turn has many moves (each hero step, purchase, hire…). Over GameRanger's internet links (tens to hundreds of ms per round trip), and with several AI players, that easily adds up to minutes. In single player the same commands execute immediately, which matches "AI turns are fast alone, slow in co-op".

Making it worse: idle PCs report finished segments only every `net_batched_segments` (default 3) segments, see [multiplayer-sync.md](multiplayer-sync.md). So each AI move may wait several segments, not just one round trip. Setting it to 1 on every PC is a no-code experiment.

**Test:** the hook DLL logs `gap=` for every AI step (time the AI waited since its previous step). In single player the gaps should be close to one frame. If H1 is right, in co-op they will be close to the ping between the PCs, once per command. If H1 holds, the fix would be to let the AI run several moves without waiting (apply its own commands locally right away, and stream them to the other PCs). That's option A in CLAUDE.md, and it only touches the AI manager / transceiver boundary.

## Measured in a LAN game (2026-10-21, robot, 2 copies on one PC)

`tools/robot/run_lan_test.py` plays a LAN game between two copies of the game on one PC: map A1M4 (Trèfle), 2 humans (both played by the robot, which only ends their turns) and 1 computer player. Results:

- **Only the host runs the AI.** While both copies are connected, only the host logs `ai-step` lines; the client logs none. (When the host closes, the client starts running the AI itself.) So the game already computes AI turns on one PC and sends the resulting commands to the others, much like option A in CLAUDE.md.
- **Each AI step waits 0.9–2.5 s** for its commands (median 1.27 s, hero moves 1.56 s), against 0.4 s in single player. The AI's own thinking is tiny: 0.23 s in total for 57 steps. A 7–13-step AI turn took 15–36 s.
- **`net_batched_segments = 1` changes nothing** (checked in memory: both copies had 1).
- The wait comes from the multiplayer pipe (see [multiplayer-sync.md](multiplayer-sync.md), "Measured"): a command sent by the AI joins the host's current segment, which runs only when the other PC has reported a later segment. Those reports arrive seconds late, because the LAN link's send queue is backed up.

So **H1 is right in spirit**: every AI step waits for the network. But even on one PC (no real network delay) the wait is ~1.3 s per step, because of how the game's LAN link sends data. Over the Internet (GameRanger) the ping comes on top.

## Late game: the AI's thinking is mostly pathfinding (2026-10-22)

Measured with the robot on the owner's single-player save `save_11.sav` (MMH5.5 random map "4p-cps-solo", huge, day 50, loaded with `run_ai_test.py --load-save`): in one AI turn of 12 steps the AI **thought for 21 s** (one step alone 9 s) and waited 9 s. Early-game turns think ~0.1 s. So on big, late maps the AI's own computation becomes the main cost, on top of the per-step waiting.

The robot's AI profiler (`--profile-ai`, `src/hook/robot_profile.cpp`; `tools/robot/profile_report.py`) samples the main thread every ~2 ms while the AI thinks. 8,550 samples (17 s) of that turn:

| Function | On the stack | Notes |
|---|---:|---|
| `0x00c08be0` | 95% | path query entry; called from 12 places in the AI (`0x0086c010`, `0x0086cd10`, `0x008675f0`, …) |
| `0x00c08890` | 97% | the search loop |
| `0x00c095b0`, `0x00c09500` (self 13.5% + 10%) | | **binary heap** push/pop (`(i-1)/2` sift-up) of **64-byte nodes copied by value** (16 dwords per move); comparator `0x00c08fd0` |
| `0x00c07f80` (self 8%) | 69% | node expansion (2,306 bytes) |

Neighbouring classes: `NWorld::CHeroTraceTile`, `NWorld::CHeroesFilter`, `NWorld::CDangerPool`. So this is the **hero pathfinder** (a Dijkstra/A*-like search over the adventure map), run again and again by the AI. A pathfinder that returns exactly the same paths, faster (nodes by index instead of 64-byte copies, reuse between queries), would be a pure speed-up. In multiplayer only the host runs the AI and its moves carry their paths, so this changes nothing for the other PCs. Candidate for phase 5 (and a good first decompilation target: the functions are small and self-contained).

The rest of the late-game run was blocked by dialogs for the robot's own human player (a battle's retreat box, which needs Flee or Surrender chosen). Benchmarks on such saves need those handled, or the human player switched to AI.

## Whose turn it is (2026-10-25, robot)

Found for the "your turn" notification (`src/mods/notify.cpp`), checked in robot LAN games with its trace (`[notify] trace=1`).

**Each player has a turn state.** `NWorld::CPlayer` objects are reached through their `IPlayer` part (at `CPlayer +0x1c`, vftable `0x00e1a09c`); the world's player list (`CWorld +0x24`, a vector `{begin, end, capacity}` of `IPlayer *`, from slot `0x64`) and the adventure controller hold those pointers. Fields of the `IPlayer` part:

| Field | Read by | What |
|---|---|---|
| `+0x74` | slot `0x184` = `0x00500d60` (`[ecx+0x74] == 0`) | 0 = human. Slot `0x190` (`0x004fb8c0`) is the same unless the global `0x00fd5c9c` is set (not identified yet). |
| `+0x78` | slot `0x2c` = `0x00720e90` | player number, 1..8 |
| `+0x7c` | slot `0x6c` = `0x00c61a70` | status: 1 = in the game; 4 = an empty slot of the map |
| `+0x634` | slot `0x44` = `0x00500c10`; set by slot `0x48` = `0x00500c20` (stores, then marks the player changed with `0x0050c020`) | **turn state: 0 = playing its turn now, 1 = waiting** |

Nothing else in the exe writes `+0x634` (byte scan): every change goes through slot `0x48`. The game itself finds "the player whose turn it is" from these: `0x0098d480(players, human, alive)` returns the one living player (human or AI, as asked) whose state is 0, or none when there are several. The adventure controller's frame update (`0x006082f0`) uses it to pick the AI player to run, and `0x00607d50` (called when the world's `+0x12c`, read by slot `0xac` and probably a turn number, or the day changes) to follow the human whose turn it is.

**This PC's player** is the adventure controller's `+0x3c` (`NUI::CAdventureController`, vftable `0x00e29a2c`; its world is `+0x18`). In a hotseat game `0x00607d50` switches it to the human whose turn it is.

What the trace showed (A1M4, 2 humans + 1 AI; players 4..8 are the map's empty slots, status 4):

```
day 2  1h:1/0 2h:1/1 3ai:1/1     player 1 plays
day 2  1h:1/1 2h:1/0 3ai:1/1     player 1 ended: player 2 plays
day 2  1h:1/1 2h:1/1 3ai:1/0     the AI plays
day 3  1h:1/0 2h:1/1 3ai:1/1     a new day
```

With simultaneous turns, both humans are at 0 together (`1h:1/0 2h:1/0 3ai:1/1`), each goes to 1 when it ends its turn, then the AI plays. The states are part of the game world (set by world commands), so both PCs logged the same changes at the same time.

## Related settings (console variables)

From strings in the exe; most addresses of the backing globals are still unknown.

| Variable | Notes |
|---|---|
| `ai_time_limit` | ms per AI turn (global `0x00f440d0`). Profile default 180000. |
| `aai_step_limit` | ms per turn-driver call (global `0x00f440d4`). |
| `aai_proceed` | Also read in the turn driver (`0x0086dd40`). |
| `adventure_ai_timing` | Probably turns on the timing logs. Registered at `0x00609910`. |
| `self_adventure_ai`, `aai_play_for_humans` | Console commands at `0x00590830`: let the AI play for human players. Useful for AI-vs-AI test games (phase 5). |
| `aai_testmap`, `show_ai_fog`, `show_ai_geometry`, `town_ai_log` | Debug aids. |
| `combat_ai_lookahead`, `combat_ai_retreat_value`, `combat_ai_use_magic_for_human` | Combat AI. |

## Open questions (for phase 1)

1. ~~Is H1 right?~~ Yes, measured above: ~1.3 s per AI step in a LAN game on one PC, against 0.4 s alone.
2. ~~Which PC runs the AI in multiplayer?~~ The host (measured above).
3. When exactly does a sent command execute in multiplayer? Read `CMultiPlayerTransceiver` (`0x007c89f0`–`0x007cc1c0`): when queued commands are released for execution, and what it waits for.
4. How is a typical turn's time split between thinking (`think=`) and waiting (`gap=`)?
