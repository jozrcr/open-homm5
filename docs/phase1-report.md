# Phase 1 report: why co-op AI turns are slow, and a first fix

Written 2026-10-21 (session 2, night). Phase 1's job was to understand the AI turn and the network code well enough to make co-op AI turns fast. Details live in [subsystems/ai-turn.md](subsystems/ai-turn.md), [subsystems/multiplayer-sync.md](subsystems/multiplayer-sync.md), [tools/robot/README.md](../tools/robot/README.md) and [src/mods/README.md](../src/mods/README.md).

## In one paragraph

A test robot now plays the game by itself: it starts two copies of the game on one PC, has one host a LAN game and the other join, gets both players ready, adds a computer player, starts the game, and times every AI move. Two findings came out of that. First, **the game already runs the AI only on the host** and sends its moves to the other PCs. Second, **each AI move waited about 2.5 seconds**, while the AI's actual thinking takes milliseconds. The waiting comes from the game's network link, which was tuned for 2006 modems: it sends at most one small packet every 50 ms, and an idle PC acknowledges what it received only once per second. A small mod in our DLL gives the link modern timings and cuts that to **0.6 s per AI move: AI turns about 4× faster** in the robot's LAN games.

## What the robot can do

`tools/robot/run_ai_test.py` (single player) and `tools/robot/run_lan_test.py` (LAN, two copies of the game) run in the test copy `C:\Games\H5_test`, never the Steam folder. Without anyone touching the PC, the robot:

- skips the intro videos and the "press any key" screens, uses its own profile, keeps the game running at full speed in the background;
- **sees the game**: saves the game's own frames as screenshots (the desktop shows nothing while the game is in the background);
- **presses buttons** on menus and screens, the way the buttons do: main-menu commands, screen commands (found through the game's built-in type information), and "game messages" (names read from the game's UI files);
- **plays a LAN game against itself**: two copies, each with its own profile, log and network ports; the robot relays LAN discovery between them over 127.0.0.1;
- ends the human players' turns, times every AI move, logs the multiplayer lockstep and every network packet when asked;
- **explains hangs**: if the game stops drawing or stops reading input, the log says where in the game's code it is stuck.

## What we found

1. **In single player** the AI thinks ~0.1 s per turn but waits ~0.4 s after each of its 25–30 moves (the game's 200 ms command segments). See [ai-turn.md](subsystems/ai-turn.md).
2. **In a LAN game only the host runs the AI.** The other PCs receive its moves. So option A of the plan ("host-authoritative AI") is what the game already does.
3. **Each AI move waited 1–2.5 s in a LAN game, even on one PC (no real network delay).** The causes, from the traces:
   - A move only runs when every PC has confirmed the game segment it belongs to.
   - The host's link is full during AI turns (the AI's commands are big) but may send only one ~256-byte packet per 50 ms, about 5 KB/s.
   - The idle client acknowledges only once a second, so the host's send window keeps closing.
   - Each PC therefore sees the other lagging, and both slow their game down to 45–90% speed.
4. **The fix is network timings only.** Game commands still run in the same order on every PC.

| Network timings | per AI move | typical AI turn |
|---|---|---|
| Game's own | 2.5 s | 15–36 s |
| Mod (`src/mods/net_tuning.cpp`) | **0.6 s** | **2–7 s** |

The game was built for much slower PCs and networks (2006), so many of its timings have room to spare. That is also why speed hacks "work". The mod changes only the timings that waste time and leaves the game's rules alone.

## Is it safe? (desync check)

A desync is when the PCs of a game stop agreeing on the state of the world. To check, the robot turns on the game's own world checksum (`mp_check_crc`: every PC computes a checksum of the whole world after every 200 ms segment and sends it to the others). It then logs every checksum on both copies, and `tools/robot/lan_report.py` compares them.

- With the mod on, three long LAN games: **4,227, 5,730 and 5,723 checksums, all identical on both copies. No desync.**
- The last one ran a **whole game**: 16 AI turns and three game weeks, until the AI had beaten both (passive) robot players. Along the way:
  - battles resolved by themselves, because the robot ticks "quick combat" on the setup screen;
  - the robot closed the combat results window, which otherwise waits for "OK".
- The game's per-object variant (`mp_check_object_crc`) froze the game, so it isn't used.

## Late game: the AI's own thinking

The robot can load a saved game (`run_ai_test.py --save-file … --load-save …`). On the owner's single-player save `save_11.sav` (huge random map, day 50), one AI turn **thought for 21 seconds**, one move alone for 9 s, against ~0.1 s per turn early in a game. A sampling profiler built into the robot shows where the time goes: **97% is hero pathfinding**. That's a search over the map whose priority queue copies 64-byte entries around on every step. A faster pathfinder that finds exactly the same paths would make late-game AI turns much quicker. It changes nothing for the other PCs, because only the host runs the AI. Details: [ai-turn.md](subsystems/ai-turn.md).

The robot can't fully play such saves yet: when the AI attacks the robot's own hero, the game asks the human to choose "flee or surrender", and the robot can't answer that box yet.

## Tried and not usable yet

Shorter multiplayer segments (100 ms instead of 200, same on both PCs): no desync, and most AI steps got faster, but hero moves became very slow (seconds, growing). Something else assumes 200 ms. See [multiplayer-sync.md](subsystems/multiplayer-sync.md).

## What the owner can do next

- **Fix `main`:** PRs #2–#4 were merged into each other's branches, so `main` still has only phase 0. PR #5 (`test-robot`) contains everything. On GitHub: open PR #5, "Edit" → change the base branch to `main`, then merge. (The app blocked me from doing this myself.)
- **Try the mod in a real game:** copy `build/hook/dinput8.dll` next to the game exe your group uses (all players), play a co-op game with AI players, and compare AI turn times. The DLL logs to `open-homm5.log` next to the exe. To turn the mod off: an `open-homm5.ini` file next to the exe with `[net]` and `tuning=0`.

## Next steps

1. **Test the mod in a real co-op game** (GameRanger, all players with the DLL), then over long games. The robot found no desync in three long LAN games, including one played to the end.
2. **Faster hero pathfinding** for late games: decompile the pathfinder (byte-matched first), then a faster version with identical results as a mod.
3. **The remaining 0.6 s per AI move** is the game's segment pacing. Shorter multiplayer segments need the hidden "× 200" found first, and a check that all players use the same setting.
4. **Robot:** answer the retreat box, so the robot can play through late-game saves, including your group's co-op saves (`sauvegarde_14/15`, a 6-player map).
