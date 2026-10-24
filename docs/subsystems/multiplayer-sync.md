# Multiplayer: commands, transceivers, sync checks

Status: **first static reading** (2026-10-20), not yet checked in a running game. Addresses are in the vanilla 3.1 exe (same in `MMH55_Utility(_64).exe`).

## Big picture (working hypothesis)

Heroes V looks like a **lockstep** game:

- Every change to the game world is a **command object** (238 classes named `NWorld::C…Cmd`: move hero, end turn, AI decisions, …).
- Commands go through a **transceiver** (`NWorld::ITransceiver`). In single player it just executes them. In multiplayer it sends them to the other PCs, and every PC executes the same commands in the same order.
- Each PC runs the full simulation. A **world checksum (CRC)** compared between PCs detects when they drift apart ("desync").

The phase 1 gate ("first divergence pinned in a 2-client test") and the choice between option A and option B in CLAUDE.md both depend on confirming this.

## Transceivers: `NWorld::ITransceiver`

| Class | vftables | Functions found | Role (guess from name and code) |
|---|---|---|---|
| `NWorld::ITransceiver` | `0x00e43b14` (16 slots), `0x00e43af0` (+8) | interface | Where commands go. |
| `NWorld::CSinglePlayerTransceiver` | `0x00e43c54`, `0x00e43c30` | `0x007c7950`–`0x007c87c0` | Executes commands locally. |
| `NWorld::CMultiPlayerTransceiver` | `0x00e43f1c`, `0x00e43ef8` | `0x007c89f0`–`0x007cc1c0` | Exchanges commands with the other players; checks CRCs. |
| `NWorld::CReplayTransceiver` | `0x00e43bec`, `0x00e43bc8` | `0x00475d20`, `0x007c7xxx` | Plays back recorded commands (replays). |
| `NWorld::CSimpleTempTransceiver` | `0x00e43d2c`, `0x00e43d0c` | | Unknown. |

**Slot 0 is "send a command from this PC".** Callers reach it through the generic send-command method `0x00604360` (16 callers: the adventure-map UI and the AI manager, see [ai-turn.md](ai-turn.md)).

- `CSinglePlayerTransceiver` slot 0 (`0x007c80a0`): queue the command for execution (`0x0079edf0`).
- `CMultiPlayerTransceiver` slot 0 (`0x007c9810`): if this PC has a player (`this+0x54 >= 0`), compute the command's size (`0x007c9270`, the `calculate_cmd_sizes` code), queue it locally (`0x0079edf0`) and **send it** through the object at `this+0x34` (slot `0x1c`). Without a local player it checks a bit mask of players (`this+0x58`) instead.

Related strings in this area: `calculate_cmd_sizes` (`0x007c9270`), `multiplayer_game_running` (`0x007c9ec0`, `0x007ca3e0`), "Drop player " (`0x007c8d10`).

## Lockstep segments: when a command actually runs

The multiplayer transceiver cuts time into **segments** and only lets the world advance through a segment once **every player** has reported it. (Static reading, 2026-10-20.)

| What | Where | What it does |
|---|---|---|
| Message pump | `CMultiPlayerTransceiver` slot 3, `0x007ca7c0` | Reads every message received from the network (`this+0x34` slot `0x24`). Type 0 = "player X finished segment N" (sets a bit in `this+0xd4` and that player's segment counter in the array at `this+0x3c`), type 1 = player dropped (`0x007c8d10`), type 2 = data: a world command from another player (`NWorld::CWorldCmd`), a `CCrcTestCmd` (desync check, `0x007c9410`) or a chat message (`NMP::CChatCommand`). |
| Segment records | `0x007c9d80` | Appends a record to the segment list at `this+0x38`: segment number, one command list per player, and a bit mask of players. |
| Run complete segments | `0x007c98e0` | Takes the oldest segment record and, **only if every player has reported it** (`0x007c8c80`), collects all players' commands for it and executes them. |
| Own step | slot 4, `0x007ca990` | Advances this PC's segment and announces it (`0x007c9d80`). |
| Pacing | slot 5, `0x007c8dd0` | Compares this PC's segment with the slowest player's (capped at 20 segments ahead) and computes a speed factor; the formula uses 200 per segment. |
| Single player | `CSinglePlayerTransceiver` slot 4, `0x007c7b20` | Executes queued commands right away and adds 200 to a time counter per step. No waiting. |

So a segment is probably **200 ms of game time**, and in multiplayer **a command can only run once all PCs have confirmed its segment**. This is the mechanism behind hypothesis H1 in [ai-turn.md](ai-turn.md).

### `net_batched_segments`: idle PCs report late

`net_batched_segments` (global `0x00f27398`, **default 3**, registered at `0x004039a0`) is read only by `0x00402440`, which runs once per segment on each PC:

- if this PC has **no commands** in its recent segments, it **holds the "segment done" reports back** and sends them in batches, up to `net_batched_segments` segments at a time;
- as soon as a segment contains a command, everything is sent at once.

During an AI turn the human players are idle, so their PCs report only every 3 segments. The PC running the AI must wait for those reports before its AI's commands can run. If H1 holds, **each AI move may wait up to ~3 segments (≈600 ms) plus the network delay**, not just one round trip.

**Cheap experiment (no code):** on a test copy of the game, on **every** PC, add `setvar net_batched_segments = 1` to the profile (`profiles/default_profile/user_a2.cfg`, or the user's own profile folder). Compare AI turn times in a 2–3 player co-op game with and without it. The hook DLL's `gap=` values show the effect per move.

(`net_timeout`, the other variable registered there, is global `0x00f27394`, default 20 seconds.)

Not related, despite the names: `SegmentLength` / `SkipSegmentsNumber` in the data files belong to projectile animations (`GameMechanics/Shot/...`). The network constants `MaxLatency`, `TimeToAllowDropByLag`, `TimeToStartLagByNoSegmentData` and `AI_SEGMENT_DURATION` exist as field names in the exe but no data file sets them, so they keep built-in defaults (not looked up yet).

## Measured (2026-10-21, robot LAN game, 2 copies on one PC)

With `trace_mp=1` the DLL logs every own step (`mp-step`) and every command sent (`mp-send`) of `CMultiPlayerTransceiver`, and `net_log=1` logs the packets (`src/hook/robot_trace.cpp`, `robot_net.cpp`). Fields used: `+0x28` timer (game time at `+0x14`), `+0x2c` this PC's segment time, `+0x38` segment records (record+8 = segment number), `+0x3c` each player's last reported segment, `+0x54` this PC's player, `0x00f3fa5c` the speed factor set by the pacing.

What happens during an AI turn (host = player 0, client = player 1):

1. **Each PC sees the other one lagging.** The host sees the client 4–9 segments behind; at the same moment the client sees the host stuck 2–13 segments behind (for example the client saw the host at segment 120 for 3 s while the host was at 122–133).
2. **So both slow down.** The pacing (slot 5) slows a PC that is ahead of the slowest player: speed factor 0.45–0.9 on both, i.e. a 200 ms segment takes 260–470 ms of real time.
3. **AI commands wait for the slow reports.** A command sent in host segment N runs only when the client's report for a segment after N arrives: 1.8–2.5 s later in the traces.

Why the reports are late: **the LAN link is throughput-starved.** All game traffic between the copies goes over UDP through `NNet::CLinksManager` (send `0x00484d30` = slot 9, receive `0x00484ea0` = slot 10; network thread `0x0047cbd0` ticks every 10 ms). The host sends full 273-byte data packets every ~50 ms, but only ~8 per burst; then it waits for an acknowledgement, and the client acknowledges about **once per second**. So the host can push only about 8 packets (~2 KB) per second, its send queue backs up, and the packets that do go out carry data that is seconds old. (The client sends little: 18–39-byte packets, about once per second.)

### The LAN link (reliable UDP)

Found by tracing which game code calls `sendto` (`robot_net.cpp` logs the stack of the first data packets):

| What | Where | Notes |
|---|---|---|
| Network thread | `0x0047cbd0` | `Sleep(10)`, then the driver's tick (slot 25, `0x0047e970`): queued outgoing messages, received packets, then every link's update. |
| Per-link update | `0x0047e050` | A link sends only when its send timer (link `+0x3e8e4`) has run out, then resets it to **0.05 s** (`0x0047e54d`). It sends if (a) it has data and room in its window, or (b) more than **1.0 s** passed since its last send (`0x008fdf20`, the keepalive). So at most one packet (~256 bytes of data) per 50 ms per link, and **acknowledgements only ride on data or on the 1-second keepalive**. |
| Reliable channel | channel at link `+0x48`: `+0x38` window (float, min 2), `+0x3c` packets in flight, `+0x40` seconds since last send; update `0x008fe430` | Round-trip estimate with a minimum timeout of 0.1 s; packets older than the timeout count as lost (window −0.5 each, min 2) and are sent again. |
| Batching of idle reports | `0x00402440` | Reads `net_batched_segments` once (`mov edx, [0x00f27398]` at `0x0040247d`). |

During an AI turn the host has a lot to send (AI commands such as `CSetPlayerAIThreatsInfoCmd` are big): its link runs at the cap, 19 full packets per second (5187 bytes/s), for 5–7 s at a time, and everything else (segment reports, the next AI move) queues behind. The client, idle, acknowledges once per second, so the host's window keeps closing.

### The fix: modern link timings (`src/mods/net_tuning.cpp`)

Robot-mode experiments, then the mod (same robot setup, seconds per AI move):

| Change | per AI move | client behind the host | speed factor |
|---|---|---|---|
| none (game) | 2.51 | 4–9 segments | 0.45–0.9 |
| send interval 10 ms | 2.22 | 5 | 0.79 |
| keepalive 50 ms | ~1.6 | 4 | 0.79 |
| keepalive 50 + send interval 10 | 0.90 | 2 | 0.94 |
| keepalive 50 + send interval 10 + `net_batched_segments` 0 | **0.58** | **1** | **1.00** |

The mod (`src/mods/README.md`) applies the last line by default. The remaining ~0.6 s per AI move is the game's own pacing: each AI step waits for its commands' segment (200 ms) plus one more, as in single player (0.4 s), plus the round trip.

### Segment length in multiplayer (not changed yet)

Shorter segments would cut the remaining wait further (in single player, 50 ms segments took a 6-turn run from 49 s to 34 s, see [ai-turn.md](ai-turn.md)). In multiplayer every PC must use the same length, or the lockstep breaks. The 200 ms appears in `CMultiPlayerTransceiver` as:

| Where | Instruction | Role |
|---|---|---|
| `0x007c8b1f` (slot 6, segment due) | `add eax, 200` (immediate at `0x007c8b20`) | time of the next segment |
| `0x007ca9dc` (slot 4, own step) | `add dword ptr [esi+0x2c], 200` (immediate at `0x007ca9df`) | advance this PC's segment time |
| `0x007cc0d0` (slot 12) | `add eax, 200` (immediate at `0x007cc0d4`) | next segment time, for other code |
| `0x007c9456`–`0x007c946d` (checksum handler) | two `lea`s computing `200·check + 30000` | catch-up: a PC whose segment time is 30 s behind the checks jumps forward |

A mod would have to change all four the same way on every PC, and make sure players with different settings cannot join the same game (for example through the version shown in the LAN game list).

There is one more place: **the world time of each segment.** The segment runner (`0x007c98e0`, at `0x007c9a48`–`0x007c9a5c`) computes `base + 200·n + 30000` with the same `lea` chain as the checksum handler and passes it to `0x007cc680`; the transceiver's own clock starts at `base + 30000` (`0x007ca024`). Changing only the three immediates (first try) made the world clock run twice as fast as the segments: other AI steps got faster, but hero moves waited about as long as the time since the turn began (6.5 s, 12 s, 25 s…).

The robot experiment (`--mp-segment-ms 100|50`, `robot_trace.cpp`) now changes the three immediates and both `lea` chains (scale ×8 → ×4 or ×2, displacement 150 → 3,900 or 11,400, so the formula stays `base + N·n + 30000`). Results, with the link tuning mod on, 8 AI turns each:

| Segment | per AI move | hero move | town swap | world checksums |
|---|---|---|---|---|
| 200 ms (game) | 0.60 s | 0.61 s | 0.41 s | all equal |
| 100 ms | 0.49 s | 0.55 s | 0.20 s | 5,179, all equal |
| 50 ms | 0.44 s | 0.55 s | 0.14 s | 10,251, all equal |

Hero moves (most AI steps) stay at ~0.55 s, so shorter segments buy ~20% more. That floor is not the animation: the setup screen's animation speed at maximum (robot `slider|animationspeed 3`) and `adventure_speed_ai = 10` left hero moves at 0.64 s and 0.57 s. In single player with 50 ms segments a hero move waited only ~0.2 s, so this is still to be found in the multiplayer path. A trace (`--trace-mp`, 50 ms segments) shows ~9 segments (~450 ms of game time) between sending a hero move and the AI's next step, with no other commands in between: the AI waits for its hero to finish walking. Possible later mod: let AI heroes move instantly when no human can see them (a gameplay-level change, so a separate decision). As a mod this needs a guard so that players with different settings cannot play together (a mismatch would break the lockstep), so it stays an experiment for now.

Network detail seen on one PC: the game binds UDP 8889 (`net_lan_match_maker_port`, game discovery and then the game link) and 8888 (`net_game_port`). See `tools/robot/README.md` for how two copies share one PC.

### What an AI hero move waits for in co-op (2026-10-24)

Robot LAN game on A1M4 (link tuning on, `mp_check_crc` on), 8 AI turns: the wait before the AI's next step, after a hero move (`CSetHeroAITargetCmd` + `CSetWalkPathCmd` + `CStartMoveHeroCmd`), falls into **two tight groups: ~620 ms and ~860 ms, i.e. 3 or 4 segments** of 200 ms. It does not spread with the path length. A town swap waits 2 segments, as every move does in single player. So in co-op a hero move costs 1–2 segments more, ~350–400 ms of game time (with 50 ms segments, ~9 segments in all).

The AI manager (`0x006082f0`) calls the turn driver only when the busy check `world->slot 0xa4(time, player)` is false (`0x0098e1c0`, called at `0x0060868e`; `time` = the transceiver's game time from its slot 8, the player pushed before that call; `ret 8`). It is true if:
- the player's slot `0x8c` gives a live object; or
- one of the player's movers (the list at world `+0x118` for player type 2, else `+0x5c`) says "moving" (slot `0xc`) or has an end time (slot `0x10`) at or after `time`.

The robot's `--trace-ai-busy` logs each change of its answer:
- **co-op:** after each AI hero move it turns true ~400 ms later and false again ~220–690 ms after that, and the AI moves at once;
- **single player:** it never turns true; AI hero walks are done within the segment.

`--ai-busy-ignore-end` (host only: asks with time `0xffffffff`, so end times no longer count) changed nothing: the "moving" flag stays on for as long. So **in co-op an AI hero move waits for the hero to really finish walking in game time**.

Ruled out, robot experiments (both copies patched the same way; world checksums equal in all runs):

| Experiment | What it changes | AI hero move wait |
|---|---|---|
| none | | 840 ms (median) |
| `--ai-walk-speed-scale 4` / `16` | the walk speed `0x008da900` returns for AI heroes (in co-op from the shared game settings `+0x24`; it is 1.0), at its caller `0x008e021a` in the movement update | 840 / 646 ms (noise) |
| `--walk-step-ms 50` / `100` | the four 200 ms constants of the movement update `0x008dffd0` (`0x008dfffc`, `0x008e01ad`, `0x008e023c`, `0x008e039d`) | 857 / 856 ms |

So the walk speed only sets the animation, and those 200s are not the wait either.

**AI walks are already instant when no human can see them, in co-op too** (`--trace-moves`, 2026-10-24):
- `NWorld::CStartMoveHeroCmd` (vtable `0x00e0d778`) executes at slot 7, `0x00426d50`. It calls the hero's start-move (`NWorld::CAdvMapHero`, vtable `0x00e54274`, slot `0x64` = `0x008e5480`), which is the movement update `0x008dffd0`.
- That update asks `0x008dae10` whether to schedule every step at once: 1 = instant. It answers 0 for heroes of a player whose slot `0x184` is true (probably "human"), or when the game settings byte `+0x1c` is set. Otherwise it answers 1, unless one of the path's tiles is visible (object at `mover+0xac`, slot `0x6c`).
- In the robot LAN game every AI hero move was instant (1), as in single player.

So the extra segment or two of co-op hero moves is not the walk itself. It is still to be found (probably the lockstep's one-segment delay before the move runs, plus the instant walk's +200 ms end time).

**So the remaining co-op wait needs every PC to agree:** the lockstep's segment length (a town swap waits 2 segments, ~420 ms; a hero move 3–4).

**Any co-op timing change needs a guard.** Shorter segments and faster AI walks both change game time, which runs in lockstep on every PC. So all PCs must use the same value, and a PC without our DLL must keep everyone on the original timing. Only the host runs the AI, but that does not help here.

### How LAN games are found (robot socket logs, 50 runs)

1. A player on the LAN screen **broadcasts a 5-byte query** (first byte `02`) to `net_lan_match_maker_port`, about once a second.
2. A PC hosting a game **answers by unicast** to the sender with an 826-byte game description (first byte `03`).
3. Joining: the joiner sends 9-byte packets (first byte `07`) to **each** of the host's addresses (the host's game description seems to carry its address list), on `net_game_port`, and the host answers from the address that worked.

The query goes to only two addresses: `255.255.255.255` and the **classful** broadcast of one local address. On the owner's PC the game picked the WSL adapter `172.28.192.1/20` and sent to `172.28.255.255` (its class B /16), never to `192.168.1.255` of the real Ethernet card. In 50 runs no other broadcast address appeared.

### Over the internet with ZeroTier (2026-10-23, failed)

Owner on Windows, a friend on Linux (Proton), both approved in one ZeroTier network (`10.21.174.0/24`). Unicast worked: ping between the two PCs took 20 ms. **The game list stayed empty**, for these reasons:

- Windows sends `255.255.255.255` only through the interface with the lowest metric (the Ethernet card, metric 25, against ZeroTier's 35), so the query never enters ZeroTier. Linux does the same with its default-route interface.
- The classful broadcast doesn't help either: a ZeroTier address `10.21.174.x` gives `10.255.255.255`, which is outside the ZeroTier route `10.21.174.0/24`, so it also leaves through the normal internet connection.
- Windows marked the ZeroTier network **Public**, and the game's firewall rules allow only Private networks, so the host's answer or a join would be blocked too.

Untested workarounds: on Windows, make the ZeroTier network Private and give it the lowest metric (`Set-NetIPInterface -InterfaceMetric 1`); on Linux, `ip route add 255.255.255.255/32 dev zt…`. The robust fix, planned for the networking phase: a DLL setting that also sends the discovery query by unicast to listed peer addresses (the robot's broadcast relay in `src/hook/robot_net.cpp` already does this for two copies on one PC). GameRanger works because it carries the broadcasts itself.

## Desync detection

| What | Where | Notes |
|---|---|---|
| `mp_check_crc` | global `0x00fe4d18` (registered at `0x007cb230`) | Turns on the world CRC check. |
| `mp_check_object_crc` | global `0x00fe4d19` | Turns on a per-object CRC check. |
| `mp_observer_mode_for_player2` | global `0x00fe4d1a` | Unknown, maybe lets player 2 watch. |
| CRC check / desync handler | `0x007c9410` (a `CMultiPlayerTransceiver` method, called from `0x007c98e0` and `0x007ca7c0`) | Only runs when `mp_check_crc` or `mp_check_object_crc` is on, so **by default the game may not check at all**. Compares the CRC of the current step with the other side's. On a difference it reports "Multiplayer get out of sync". With the object check on, it lists **every world object whose CRC differs** (`"%d. %s"`: index and name). It can save the world as `zero_%1d_%d.sav` (reference point) and `async_%1d_%d.sav` (at the desync; global `0x00f3fa60` enables that). |
| `NWorld::CCrcTestCmd` | `0x007cab20`–`0x007cae80` | A command that carries a CRC, presumably sent between PCs for the check above. |
| Replay check | `0x007c7be0` (`CReplayTransceiver`) | "game and replay are divergenting at this point", "world CRC:", "CRC from reply:". The same idea applied to replays. |
| Random-number logging | `log_random_calls` (`0x00b26200`), `log_game_calcs` (`0x00c1f6a0`) | Logging each random call on two PCs and comparing the logs is the classic way to find the very first divergence. |

**For phase 1:** turning on `mp_check_crc` + `mp_check_object_crc` (+ `log_random_calls`) in a 2-PC LAN game should show whether and where the game desyncs, using the game's own tools. That costs nothing to try (a console or config change on a test copy of the game).

**Tried with the robot (2026-10-21):**

- With `mp_check_crc` on, **every PC computes a world checksum after every segment** (`0x007c98e0`: world `[transceiver+0x20]` slot 4, the checksum; slot 5 the per-object list) and sends it as a `CCrcTestCmd` (check number at `+0x0c`, checksum at `+0x10`). Without the flag, no checksums are computed or sent. At check 0 each PC also saves the world as `zero_<player>_0.sav` (5 MB) in its profile folder.
- **`mp_check_object_crc` freezes the game**: with it, the AI made one move and then nothing for minutes (frames kept coming), probably the per-object checksums of the whole world every segment. Don't use it for long games.
- `mp_check_crc` alone costs nothing visible (AI timings unchanged). The robot logs every checksum on both copies (`trace_crc=1`, `robot_trace.cpp`: the two calls into `0x007c9410` are redirected to a logger) and `tools/robot/lan_report.py` compares them: **all equal = no desync**. First result, with the link tuning mod on: 303 checks, all equal.

## Network driver: `NNet`

| Class | Where | Notes |
|---|---|---|
| `NNet::IDriver` / `NNet::CNetDriver` | vftable `0x00e131e4` (38 slots), code `0x0047c460`–`0x004f8c10` | Socket layer. Uses `wsock32.dll`: UDP (`sendto`/`recvfrom`, broadcast to `255.255.255.255` at `0x0047c8e0`) and TCP (`connect`/`send`/`recv`). "Invalid client %d" at `0x0047d500`. |
| `NNet::ILinksManager` / `NNet::CLinksManager` | vftable `0x00e133c8` (13 slots), `0x00484a30`+ | Connections to the other players ("links"). |
| Settings | `net_game_port` (`0x00402a80`…), `net_timeout`, `net_batched_segments` (`0x004039a0`), `net_load_timeout`, `net_multiplayer_init_timeout`, `net_lan_match_maker_port` (`0x0040bf30`) | |
| `CMultiplayerCommand` | `0x00403780`+ | Base of the multiplayer-layer commands. |

## LAN game setup: `NUbi::NLAN`, `NMP`

- LAN discovery and joining: `NUbi::NLAN::SDriverIPNetworkBroadcast`, `SGameInfoPacket`, `SEnterGameRequestPacket` / `SEnterGameAllowedPacket` / `SEnterGameDeniedPacket`, `SLeaveGamePacket`, `SGameChatPacket`. This is what GameRanger emulates (a virtual LAN).
- Game start and loading: `NMP::CHostHandler` (`0x008213b0`), `NMP::CMPLoad`, `CMPStart`, `CMPLoadHostHandler`, `CMPLoadClientHandler`, `CMPSaveWorldCmd`, `CLoadMPSaveCmd`.
- `NUbi::…` lobby/login classes are for ubi.com, which no longer exists.

## Open questions (for phase 1)

1. Confirm lockstep: does every PC execute every command? Which PC runs each AI player? (See [ai-turn.md](ai-turn.md), hypothesis H1.)
2. When does a queued command actually execute in multiplayer, and what does it wait for? (Slot 0 sends; find where received commands are released.) This is where a faster AI turn would plug in.
3. Does the game desync in our 3+ player co-op games, and where? Use `mp_check_object_crc`.
4. What do turn-time-limit settings (`NUI::CTurnTimeLimitNotify`, `CTurnTimeLimitForcedEOT`) do in co-op?
