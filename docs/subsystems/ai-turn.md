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

## Who creates the AI

`CPlayerAI` objects are created by the factory `0x0086d720`, called from `0x00608180`. That function goes through a list of players (at `this+0x4c`), checks several virtual "is this player …" methods and the global `0x00fd9e81` (a console variable), and creates or removes AI objects. It is reached from code under `NUI::CAdventureScreen` (`0x0059d5f0` → `0x0058d630` → `0x00608180`).

So the AI objects are managed from the **adventure-map screen code**, which runs on every PC that shows the map. This hints, but does not prove, that **every PC in a multiplayer game creates and runs the AI** (lockstep). It also fits a known fact: MMH5.5's faster "enhanced AI" (3.1j) cannot be used for LAN games. If only the host ran the AI and sent its commands, a different AI on the host would not matter.

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

1. **Does every PC run the AI in multiplayer, or only the host?** Plan: the hook DLL logs every call of the turn driver (`0x0086dd40`) with player id, time and stack trace, on two PCs in a LAN game.
2. Where do the AI's commands go, straight into the local world or through the multiplayer transceiver? See [multiplayer-sync.md](multiplayer-sync.md).
3. How much of a typical turn is spent where? The game's own timing logs (if `adventure_ai_timing` enables them) or our hook will tell.
4. Is the AI deterministic? It must be if every PC runs it. Then the random-number generator (`log_random_calls`) and the pathfinder must behave the same on every PC.
