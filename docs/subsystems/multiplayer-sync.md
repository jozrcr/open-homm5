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

Network detail seen on one PC: the game binds UDP 8889 (`net_lan_match_maker_port`, game discovery and then the game link) and 8888 (`net_game_port`), and broadcasts to 255.255.255.255 and to the subnet of each network adapter. See `tools/robot/README.md` for how two copies share one PC.

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
