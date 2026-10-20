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

## Hypothesis H1: why AI turns are slow in co-op

Three facts from the code:

1. The turn driver **stops thinking as soon as it has produced one command** (step 4 above) and returns.
2. The command then goes through the transceiver, in multiplayer **over the network**.
3. The AI plans its next move from the world state, so it has to see its previous command executed first.

If, in multiplayer, a command only executes once it has made its way through the network (the lockstep "everyone has it" point), then **every AI move costs at least one network round trip of waiting**. A typical AI turn has many moves (each hero step, purchase, hire…). Over GameRanger's internet links (tens to hundreds of ms per round trip), and with several AI players, that easily adds up to minutes. In single player the same commands execute immediately, which matches "AI turns are fast alone, slow in co-op".

**Test:** the hook DLL logs `gap=` for every AI step (time the AI waited since its previous step). In single player the gaps should be close to one frame. If H1 is right, in co-op they will be close to the ping between the PCs, once per command. If H1 holds, the fix would be to let the AI run several moves without waiting (apply its own commands locally right away, and stream them to the other PCs). That's option A in CLAUDE.md, and it only touches the AI manager / transceiver boundary.

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

1. **Is H1 right?** Compare the hook's `gap=` values in a single-player game and in a 2-PC LAN/GameRanger game.
2. **Which PC runs the AI in multiplayer?** Run the hook DLL on two PCs in the same game. If only one PC logs `ai-step` lines for a given AI player, only that PC runs it.
3. When exactly does a sent command execute in multiplayer? Read `CMultiPlayerTransceiver` (`0x007c89f0`–`0x007cc1c0`): when queued commands are released for execution, and what it waits for.
4. How is a typical turn's time split between thinking (`think=`) and waiting (`gap=`)?
