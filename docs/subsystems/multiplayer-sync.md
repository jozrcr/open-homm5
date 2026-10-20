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
