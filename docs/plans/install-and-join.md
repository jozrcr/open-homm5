# Plan: easy install and easy multiplayer joining

Status: proposal for the owner (2026-10-24).

## Why

- **Installing today** takes too many steps for friends: build the DLL or get a copy, find the game's `bin` folder, copy it there, and on Linux add a Steam launch option.
- **Joining today:**
  - Works through GameRanger (Windows).
  - Over ZeroTier, which is what a Linux friend can use, the game list stayed empty (test of 23 Oct, [details](../subsystems/multiplayer-sync.md#over-the-internet-with-zerotier-2026-10-23-failed)).

## Part A: easy install

Goal: a friend downloads one zip, double-clicks one file, and plays.

- **The package** `open-homm5-<version>.zip`:
  - `dinput8.dll`;
  - `open-homm5.ini` with every setting at its default, commented;
  - `install.bat` (Windows) and `install.sh` (Linux), with matching uninstall scripts;
  - a 10-line `README.txt`.
- **`install.bat`:**
  - finds the game through Steam (Steam's registry key and its library folders), or asks for the folder;
  - copies the DLL next to `MMH55_Utility_64.exe` and says what it did;
  - never touches the game's own files, and refuses to overwrite a `dinput8.dll` that isn't ours.
- **`install.sh` (Linux, Steam/Proton):** finds the Steam library, copies the DLL, and prints the one launch option to paste: `WINEDLLOVERRIDES="dinput8=n,b" %command%`. Steam's own config files are too fragile to edit from a script.
- **Version:**
  - the DLL writes its version on the first line of `open-homm5.log`;
  - later, a small "open-homm5 0.x" in a corner of the main menu, so everyone can check they have the same one (the co-op guard needs that anyway).
- **Builds:**
  - GitHub builds the package by itself for every version tag (a Windows build machine has the compiler);
  - **you** publish each release with one click;
  - the friends then download it from the repo's Releases page.

## Part B: easy joining

**B1 (now, small): LAN games over ZeroTier, with no settings.**
- The cause: the game's "who is hosting?" broadcast only goes out of the main network card, never into ZeroTier.
- The fix, in our DLL:
  - also send that broadcast to **every network adapter**, ZeroTier's included;
  - optionally also to addresses listed in a setting (`[net] lan_peers=10.21.174.12`), for VPNs that carry no broadcasts at all (Tailscale, for example).
- **Windows:** the ZeroTier network must be "Private" (Windows marked it "Public", and the game's firewall rule allows only Private networks). The install script can check this and explain how to change it.
- **Tests:** robot LAN games, to make sure nothing else breaks; then a real try with your friend (you both need the new DLL).

**B2 (later): join by IP.** The `lan_peers` setting already does it: the host's game then shows up in the joiner's LAN list.

**B3 (phase 6, long term): built-in online play.**
- The goal: no GameRanger, no ZeroTier; pick "Online" in the game and see your friends' games.
- The pieces: a small lobby server, plus a library that connects PCs through home routers (GameNetworkingSockets).

## Proposed order

1. **B1** (about 1 session), then a try with your friend.
2. **Part A** (about 1 session), built by a parallel agent while I do B1.
3. **Co-op timing, step 1** (as agreed in Q5).
4. **Release v0.1** (phase 4 of the project): network tuning, fast AI pathfinding, LAN over VPNs, easy install. Then the co-op timing guard in v0.2.

## Questions for you

1. OK with this order?
2. OK to set up the GitHub build of the package? It only builds; you publish.
3. Firewall on Windows: should `install.bat` offer to add a firewall rule for the game? That needs admin rights, and Windows asks you first. Or should it only explain how to make the ZeroTier network Private? Recommended: explain only. It's one setting, and the script then changes nothing in Windows.
