# Mods: our improvements to the game

Everything here changes how the original game behaves, on purpose. It is kept apart from the decompiled code (`src/decomp/`), which must stay byte-for-byte faithful. The mods are built into the hook DLL (`src/hook/`, `dinput8.dll`) and switched on and off with a settings file, `open-homm5.ini`, next to the game exe.

## LAN link tuning (`net_tuning.cpp`)

**What it fixes:** slow AI turns in co-op (LAN and GameRanger games).

In a multiplayer game only the host computes the AI, and each AI move has to reach the other PCs before the next one can start. The game's network link was tuned for 2006 modems. It sends at most one small packet every 50 ms (about 5 KB/s), and a PC with nothing to send acknowledges only once per second. During AI turns the host's link is full, every PC thinks the others are lagging and slows itself down, and each AI move waits 1 to 2.5 seconds.

The mod changes three network timings on the PC that runs it:

| Setting | Game | Mod |
|---|---|---|
| `keepalive_ms`: how often an idle link sends (and so acknowledges) | 1000 | 50 |
| `send_interval_ms`: how often a link may send a packet | 50 | 10 |
| `batched_segments`: how many "segment done" reports an idle PC holds back | 3 | 0 |

Game commands still run in the same order on every PC; only the network gets faster. **Every player should run it**: each PC only speeds up what it sends.

**Measured** with the robot (two copies of the game on one PC, 2 human players plus 1 computer player, map Trèfle), seconds per AI move:

| | per AI move | typical AI turn |
|---|---|---|
| Game's timings | 2.5 s | 15–36 s |
| With the mod | 0.6 s | 2–7 s |

(Each change alone helps less: send interval only 2.2 s, keepalive only 1.6 s, both 0.9 s, all three 0.6 s.) Over the Internet the ping between the PCs comes on top.

**Settings** (`open-homm5.ini` next to the game exe; without the file the mod is on with the values above):

```ini
[net]
tuning=1             ; 0 = the game's original timings
keepalive_ms=50
send_interval_ms=10
batched_segments=0
```

The DLL's log (`open-homm5.log`) says what it changed, for example `net tuning: links may send every 10 ms (game: 50)`.
