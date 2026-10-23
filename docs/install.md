# Trying the DLL in a real game

This is for the owner and friends. It installs our hook DLL, which carries the **LAN link tuning** mod (faster co-op AI turns, see [src/mods/README.md](../src/mods/README.md)). Everything here is something **you** do; the DLL only adds files and never changes the game's own files.

## What you need

- The game with MMH5.5, as you play it over GameRanger (the co-op exe is `MMH55_Utility_64.exe`).
- `dinput8.dll` from this repo: build it with `powershell -File src\hook\build.ps1` (it lands in `build\hook\`), or ask for a copy.
- **Every player** of the co-op game should install it. Each PC only speeds up what it sends.

## Install

1. Close the game.
2. Copy `dinput8.dll` into the game's `bin` folder, the folder that holds `MMH55_Utility_64.exe`.
   On Steam that's usually `C:\Program Files (x86)\Steam\steamapps\common\Heroes of Might and Magic 5 Tribes of the East\bin`.
3. Start the game as usual (GameRanger, or the exe directly).

The DLL writes `open-homm5.log` in the same `bin` folder. After starting the game, the log should contain lines like:

```
game build: ToE 3.1 + MMH5.5 patches (MMH55_Utility)
net tuning: idle links send every 50 ms (game: 1000)
net tuning: links may send every 10 ms (game: 50)
net tuning: idle PCs batch up to 0 segment reports (game: net_batched_segments, 3)
```

During AI turns it adds one `ai-step` line per AI move, which is how we measure how long the AI waits.

## What to try and report

Play a co-op game with AI players, as usual. Useful to report back:

- Did AI turns feel faster than before? (Rough seconds per AI turn is great.)
- Any "out of sync" messages, crashes, or players seeing different things?
- Each player's `open-homm5.log` (zipped) from that game.

For a fair comparison, play once with the mod off (below) and once with it on.

## Turn the mod off (keep the DLL)

Create a text file `open-homm5.ini` in the same `bin` folder with:

```ini
[net]
tuning=0
```

All players should use the same setting. The game then uses its original network timings; the DLL still logs AI steps.

## Uninstall

Delete `dinput8.dll` (and `open-homm5.ini` / `open-homm5.log` if present) from the `bin` folder. Nothing else was changed.

## Linux (Wine / Proton)

Wine prefers its own `dinput8.dll`. To make it load ours, set a DLL override for `dinput8` to "native, builtin": `WINEDLLOVERRIDES="dinput8=n,b"` in the launch options (Steam: game properties, launch options, `WINEDLLOVERRIDES="dinput8=n,b" %command%`). Not tested yet.

The full walkthrough for a Linux player joining a Windows host (Proton, starting the MMH5.5 exe from Steam, ZeroTier instead of GameRanger) is in [linux.md](linux.md).
