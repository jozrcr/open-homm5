# Trying the DLL in a real game

This is for the owner and friends. It installs our hook DLL, which carries the **LAN link tuning** mod (faster co-op AI turns, see [src/mods/README.md](../src/mods/README.md)). Everything here is something **you** do; the DLL only adds files and never changes the game's own files.

## Easy install (package)

The simplest way is the package, a zip called `open-homm5-<version>.zip`. It holds the DLL, a settings file, an installer and an uninstaller for Windows, the same for Linux, and a short `README.txt`. It is built with `python tools/package/make_package.py` ([tools/package/README.md](../tools/package/README.md)); there is no download page yet, so ask the owner for the zip. If you use it, skip "What you need" and "Install" below.

1. Close the game. Extract the whole zip (right-click it, **Extract All**).
2. **Windows:** double-click `install.bat`. It finds the game through Steam, or asks you for the folder. It copies `dinput8.dll` and `open-homm5.ini` into the game's `bin` folder and tells you what it did. It never overwrites a `dinput8.dll` that is not ours, and never overwrites your `open-homm5.ini`. It also warns you if a ZeroTier network is set to Public in Windows and tells you how to make it Private (it changes no Windows setting itself).
   **Linux (Steam / Proton):** run `bash install.sh` in the extracted folder. It does the same, then prints the one launch option to paste into Steam. [linux.md](linux.md) has the full walkthrough.
3. Start the game. A file `open-homm5.log` appears in the `bin` folder: the add-on is running.
4. To remove it: `uninstall.bat` (Windows) or `bash uninstall.sh` (Linux). Only our own files are removed; it asks before deleting your `open-homm5.ini`.

The manual steps below do the same by hand.

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
