# Checklist: a co-op test game with 3 or more players

This is the next big test of open-homm5 (phase 3 of the project): **three or more human players finish a whole game against the AI, with no crash and no desync.**

## Before the game

1. **Everyone** installs the same open-homm5 version: from the zip, `install.bat` on Windows or `bash install.sh` on Linux.
2. Everyone starts the game once and checks the first line of `open-homm5.log` in the game's `bin` folder. It must say the same version for all, for example `open-homm5 hook DLL 0.1.0 loaded`.
3. **How you connect:**
   - **GameRanger:** as usual.
   - **ZeroTier:** everyone joins the host's network. On Windows, the ZeroTier network must be **Private** (the installer tells you if it isn't). Start ZeroTier before the game.

## The game

- 3 or more humans, plus at least one AI player. A medium or large map is best, so that AI turns get long.
- Play as long as you can, ideally to the end.
- Write down (a phone note is enough):
  - who hosted;
  - roughly how long AI turns felt;
  - **anything odd:** an "out of sync" message, a crash, a player seeing something different from the others, a turn that never ends.

## After the game

Everyone sends their `open-homm5.log` (from the game's `bin` folder). Logs keep every game since the install, so send them before deleting anything.

## What we look at in the logs

- **The guard:** `co-op timing: ON from segment 50 ... (all N PCs agree)` on every PC. If it says OFF, it also says why: for example a player without the add-on, or one with other settings.
- **AI move timings:** the `ai-step` lines in the host's log.
- **Crashes:** a game that stops without `process exiting` at the end.
