# Playing co-op from Linux

A step-by-step guide for a player on Linux who joins a co-op game hosted on Windows. You end up with Heroes V: Tribes of the East + MMH5.5 running under Steam's Proton, our DLL with the faster co-op AI mod, and a ZeroTier network that makes the two PCs look like they are on the same LAN. GameRanger is not needed.

**Fixed in the DLL (2026-10-24), and it works: the owner (Windows) and a Linux friend played over ZeroTier the same day, using the install package.** History of the problem:
- The first try (23 Oct) connected the two PCs, but the host's game never showed up in the LAN list. Windows and Linux sent the game's "any games here?" message only out of the normal network card, not into ZeroTier ([details](subsystems/multiplayer-sync.md#over-the-internet-with-zerotier-2026-10-23-failed)).
- Our DLL now also sends that message into every network, ZeroTier included (`src/mods/lan_discovery.cpp`).
- **Both players need the new DLL**, and the Windows host's ZeroTier network must be **Private** (step 3 of the host's part).

**Status:** the mod is tested on Windows (two copies of the game on one PC, no desync in long games). The Linux and ZeroTier parts follow the standard steps but nobody has tried them with this game yet. If something doesn't work, the [troubleshooting](#troubleshooting) table says what to check, and [what to send back](#what-to-send-back) lists what helps us fix it.

**Words used here:** the *host* is the Windows player who creates the game. *You* are the Linux player who joins. Only the host's PC runs the AI, so the faster PC should host.

## What you need

- Heroes of Might and Magic V: Tribes of the East in your own Steam library.
- From the host:
  - a zip of the host's whole game folder (about 4 GB). It holds MMH5.5, the host's MMH5.5 settings and our `dinput8.dll`;
  - the ZeroTier **network ID** (16 characters, like `8056c2e21c000001`).
- About 30 minutes. Some steps use a terminal and `sudo`.

Both PCs must run exactly the same game files: the same MMH5.5 version and the same MMH5.5 settings. Copying the host's folder is the easy way to get that. A different version usually fails to join or gets out of sync mid-game.

## Part A: the game on Proton

### 1. Install the game with Proton

1. Install Tribes of the East in Steam.
2. Right-click the game → **Properties** → **Compatibility** → tick **Force the use of a specific Steam Play compatibility tool** → pick **Proton Experimental** (or the newest numbered Proton).
3. Start the game once, until the main menu, then quit. This makes Proton create the game's Windows environment.

### 2. Put the host's files in your game folder

1. Right-click the game → **Manage** → **Browse local files**. This opens the game folder, usually
   `~/.local/share/Steam/steamapps/common/Heroes of Might and Magic 5 Tribes of the East`
   (Flatpak Steam: `~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/...`).
2. Extract the host's zip somewhere else first.
3. Copy everything **inside** its `Heroes of Might and Magic 5 Tribes of the East` folder into the folder Steam opened. Choose **Replace** / **Overwrite all**.
4. Check that the `bin` folder now holds `MMH55_Utility_64.exe` and `dinput8.dll`.

To install or update **only our DLL** later (a new open-homm5 version), use the package instead: extract `open-homm5-<version>.zip` and run `bash install.sh` in its folder. It finds the game, copies `dinput8.dll` into `bin`, and prints the launch option of step 3.

From now on, never click **Verify integrity of game files** in Steam: it would undo MMH5.5. If Steam ever updates the game, copy the host's files again.

### 3. Set the launch options

Steam starts `bin/H5_Game.exe`, the plain game. We want it to start MMH5.5's co-op exe, `MMH55_Utility_64.exe`, and to load our `dinput8.dll` instead of Wine's own.

Right-click the game → **Properties** → **General** → **Launch options**, and paste this line exactly:

```
WINEDLLOVERRIDES="dinput8=n,b" bash -c 'exec "${@/H5_Game.exe/MMH55_Utility_64.exe}"' -- %command%
```

- `WINEDLLOVERRIDES="dinput8=n,b"` tells Wine to use the `dinput8.dll` in the game folder (n = native) before its own (b = builtin).
- `bash -c 'exec "${@/H5_Game.exe/MMH55_Utility_64.exe}"' -- %command%` takes the command Steam would run and swaps `H5_Game.exe` for `MMH55_Utility_64.exe`.

**If that doesn't work** (the log in step 4 says `unknown build`), use the file swap instead, which is what the GameRanger setup guide does:

1. In `bin`, rename `H5_Game.exe` to `H5_Game_steam.exe` (keep it as a backup).
2. Copy `MMH55_Utility_64.exe` and rename the copy to `H5_Game.exe`.
3. Change the launch options to just `WINEDLLOVERRIDES="dinput8=n,b" %command%`.

### 4. Check that the mod is on

Start the game from Steam, wait for the main menu, and quit. Then open `bin/open-homm5.log` in a text editor. It should contain:

```
game build: ToE 3.1 + MMH5.5 patches (MMH55_Utility)
hooked AI turn driver (CPlayerAI vftable slot 0 at 00e4c8bc)
net tuning: idle links send every 50 ms (game: 1000)
net tuning: links may send every 10 ms (game: 50)
net tuning: idle PCs batch up to 0 segment reports (game: net_batched_segments, 3)
```

- **No log file:** Wine didn't load our DLL. Check the `WINEDLLOVERRIDES` part of the launch options, quotes included.
- **`unknown build`:** the game started through the plain `H5_Game.exe`. Use the file swap in step 3.

Before the first co-op game, play a few turns of a single-player skirmish to check the game runs well under Proton.

## Part B: connect to the host with ZeroTier

ZeroTier is a free virtual network: once both PCs join the host's network, the two games see each other as if they were on the same LAN, which is all their multiplayer needs. It finds games with a broadcast on UDP port 8889 and plays on UDP port 8888.

### 5. Install ZeroTier

The official install script works on most distributions (Ubuntu, Mint, Debian, Fedora, …):

```bash
curl -s https://install.zerotier.com | sudo bash
```

On Arch or Manjaro use the package instead, then start the service:

```bash
sudo pacman -S zerotier-one
```

```bash
sudo systemctl enable --now zerotier-one
```

On a Steam Deck or another SteamOS-style system the install is different. Tell the host before going further.

### 6. Join the host's network

Replace `<network-id>` with the ID the host sent you:

```bash
sudo zerotier-cli join <network-id>
```

Then tell the host to approve your PC (in their ZeroTier web console, the **Auth** box next to your device). After that, this command should show the network with status `OK` and an address:

```bash
sudo zerotier-cli listnetworks
```

`ACCESS_DENIED` means the host hasn't approved you yet. `REQUESTING_CONFIGURATION` for more than a minute usually means a typo in the ID.

### 7. Open the game's ports in your firewall

Only if a firewall is on. Ubuntu and Mint usually have `ufw`, off by default; Fedora has `firewalld`, on by default.

`ufw` (check with `sudo ufw status`; if it says `inactive`, skip this):

```bash
sudo ufw allow 8888:8889/udp
```

`firewalld`:

```bash
sudo firewall-cmd --permanent --add-port=8888-8889/udp
```

```bash
sudo firewall-cmd --reload
```

## Part C: play

### 8. Join the game

1. Start ZeroTier first (it starts by itself after a reboot), then the game.
2. The host creates the game: **Multiplayer** → **LAN** → **Create**, picks the map and creates it.
3. You: **Multiplayer** → **LAN**. The host's game appears in the list. Select it and click **Join**.
4. In the waiting room, click the arrow next to your name until it says you are **ready**. The host starts the game.

All players should keep the mod on (the default). Each PC speeds up only what it sends.

## Troubleshooting

| What you see | What to check |
|---|---|
| The game doesn't start or crashes at start | Remove the launch options and try again. If the plain game starts, the problem is our line: send the log. If it still fails, try another Proton version in **Compatibility**. |
| No `bin/open-homm5.log` | Wine is not loading our DLL: check the `WINEDLLOVERRIDES` part of the launch options. |
| The log says `unknown build` | The game didn't start through `MMH55_Utility_64.exe`: use the file swap in step 3. |
| The host's game is not in the LAN list | `sudo zerotier-cli listnetworks` must say `OK` on your side; in the host's ZeroTier console both PCs must be ticked **Auth** and show as online. Check your firewall (step 7) and that the host marked the ZeroTier network as **Private** in Windows. Start the game only after ZeroTier says `OK`. |
| You can join but the game says it's out of sync, or players see different things | Both PCs must have the same files: copy the host's folder again. Send both logs. |
| It worked before, then stopped after a Steam update | Steam replaced some files: copy the host's files again (step 2). |

If the list stays empty with the new DLL on both PCs:
- Look in your `open-homm5.log` for the line `lan: game queries also go to ...`. It must list the ZeroTier network (`... (zt...)`).
- If it does and the list is still empty, add the host's ZeroTier address to `open-homm5.ini`. The DLL then also asks the host directly:

```ini
[net]
lan_peers=10.21.174.28
```

- Then send both logs (see below). The lines `lan: game found at ...` and `lan: join request to ...` show how far it got.

## What to send back

- `bin/open-homm5.log` from your PC, and the host's `bin\open-homm5.log`, from the same session.
- Your Linux distribution and Proton version.
- The output of `sudo zerotier-cli listnetworks`.
- A screenshot of anything strange, and roughly how long AI turns took.

## Turning things off

- Back to the plain game: clear the launch options (and undo the file swap if you used it: delete the copied `H5_Game.exe`, rename `H5_Game_steam.exe` back).
- Remove the mod: delete `bin/dinput8.dll`.
- Leave ZeroTier: `sudo zerotier-cli leave <network-id>`.

## For the host (Windows)

1. Zip the whole game folder `C:\Program Files (x86)\Steam\steamapps\common\Heroes of Might and Magic 5 Tribes of the East` (with `bin\dinput8.dll` in it) and send it. Use the Steam folder, not a robot test copy.
2. Install ZeroTier from zerotier.com, sign in at my.zerotier.com and click **Create A Network**. Keep it private. Send the network ID.
3. Join the network from the ZeroTier tray icon (**Join New Network**). When Windows asks, allow your PC to be found on this network (that makes it a **Private** network). If it doesn't ask: Settings → Network & internet → the ZeroTier network → **Private**.
4. On the network's page at my.zerotier.com, under **Members**, tick **Auth** for your PC and your friend's.
5. The first time you host, allow `MMH55_Utility_64.exe` through the Windows firewall when asked.
6. Host from **Multiplayer** → **LAN** → **Create**, as you would over GameRanger.
