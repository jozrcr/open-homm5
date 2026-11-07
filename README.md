# H5 Together

![H5 Together's page in the game's Options screen](docs/images/settings-page.png)

*H5 Together's own page in the game's Options screen. (The test game in this picture is in French, so the game's own buttons are too.)*

An unofficial add-on for **Heroes of Might and Magic V: Tribes of the East** with the **MMH5.5** mod, or the original 2008 game (new in 0.3.5). Free and open source. It is made for a small group of friends who play co-op games against the computer, and anyone is welcome to use it.

## What it does

- **Faster co-op games.** The computer's turns are much quicker, and so are clicks. A rare freeze of the host during computer turns is fixed.
- **Online play with your Steam friends** (new in 0.3.0, a test). Your friends' games show in the game's own LAN list, through Steam: no GameRanger, no VPN. The host invites friends from the waiting room. [How it works](#online-play-over-steam-new-a-test).
- **Settings inside the game.** Main menu, **Options**, **H5 Together** (or press **F3**). No file to edit.
- **Watch your friends while you wait** (new in 0.3.3, polished in 0.3.4). Click an ally's name or eye in the player list: your camera follows theirs, and you can watch their battles live. A player who loses the game can stay and watch (new in 0.3.4).
- **A few extras on top:** a player list on the map, a "your turn" notification, a random map template builder, other looks for your heroes, and a game rules menu for the host.
- **Safe to try.** It never changes the game's own files, and it is easy to remove.
- **Made for** the Steam game with MMH5.5 RC19d or RC19k, on Windows, or on Linux with Proton. **New in 0.3.5:** it also works with the original game without MMH5.5, even from a folder where MMH5.5 is installed ([how](#the-original-game-without-mmh55-new-in-035)).

H5 Together was called **open-homm5** until version 0.2.7. If you have an old version, install the new one over it: your settings and hero looks are kept.

## Install

1. Download the latest `h5-together-<version>.zip` from the [releases page](https://github.com/jozrcr/h5-together/releases/latest).
2. Close the game. **Extract the whole zip** (Windows: right-click it, **Extract All**).
3. **Windows:** double-click `install.bat`. **Linux:** run `bash install.sh` in the folder.

The installer finds your game, shows you what it found ("Your game": the folder, Steam or GOG, the language, which MMH5.5 you have), copies the files in and tells you what to do next. Start the game as you always do. In the main menu, a line **"H5 Together 0.3.4"** now sits above MMH5.5's version line: the add-on is running.

![The main menu with the H5 Together version line](docs/images/main-menu.png)

- Windows may show a warning ("Windows protected your PC", or "the publisher could not be verified"). That is normal for files that are new and unsigned. What to click: [docs/install.md](docs/install.md#windows-protected-your-pc-and-antivirus).
- Linux needs one more step: paste the line the installer prints into the game's Steam launch options. The full walkthrough: [docs/linux.md](docs/linux.md).
- More detail (what gets installed, updating, removing): [docs/install.md](docs/install.md). Questions and problems: [docs/faq.md](docs/faq.md).
- There is no `setup.exe`. The two scripts do the job. A one-click setup may come later.

## Is it safe?

- **Open source.** All the code is in this repository, under the [MIT licence](LICENSE). Anyone can read it.
- **Built on GitHub's machines.** The zip on the releases page is built and tested by GitHub from the code you see here (the recipe is in [.github/workflows/package.yml](.github/workflows/package.yml)), not on a private PC.
- **It never changes the game's own files.** It adds one file, `dinput8.dll`, a settings file and some folders of its own to the game's `bin` folder. The game loads `dinput8.dll` when it starts. MMH5.5's settings are changed only in the PC's memory, never on disk.
- **It sends nothing about you.** It sends nothing to us or to anyone else. In a multiplayer game, the players' PCs exchange a few small messages (versions, game rules, hero looks) over the game's own connection. Apart from online play over Steam (below), the only thing that can contact a server is the optional update check, which is off unless you say yes: once a day it asks GitHub for the number of the latest version. Nothing is downloaded or installed by the add-on.
- **Online play over Steam is on by default** (since 0.3.5; Options, H5 Together, Online play: Off turns it off). When it is on, the game talks to your friends' games through Steam, as any Steam game does: Steam sees the friends-only game you host and the invites you send. The add-on also shows its version number to your Steam friends' add-ons (a small Steam "rich presence" value), so they know you have it.
- **Antivirus scan of this release:** (link to the VirusTotal result: to be added at release.)
- **How to remove it:** run `uninstall.bat` (Windows) or `bash uninstall.sh` (Linux). It removes only the add-on's own files. Or delete `dinput8.dll` from the game's `bin` folder: the game is then as before.

Some antivirus programs distrust a `dinput8.dll` that changes a game while it runs, because cheats work the same way. If yours complains, see the [FAQ](docs/faq.md#my-antivirus-removed-or-blocked-dinput8dll).

## Play with friends: everyone needs the same version

Co-op works best when **every player installs the same version of H5 Together**, and has the same MMH5.5. In the multiplayer waiting room, a line at the bottom tells you whether everyone has the same version, and who does not.

If a player has another version, or none, the game still runs, but the parts that need everyone (the host's game rules, the shorter co-op steps, other players' hero looks) stay off.

![The waiting room, with the Rules button and the version line at the bottom](docs/images/lobby-versions.png)

Over the internet, use what your group uses today (GameRanger, or ZeroTier for a friend on Linux), or the new online play over Steam below.

## Online play over Steam (new, a test)

New in 0.3.0: play over the internet with your **Steam friends**, without GameRanger or a VPN. The add-on carries the game's own LAN play through Steam's network, so your friends' games show in the game's LAN list as if you were in the same house.

1. **Every player:** nothing to do since 0.3.5 (online play is on by default; before: **Options, H5 Together, Online play: Steam**, then restart). Steam must be running, and the game must be the Steam version; without Steam (a GOG copy, Steam closed) the game uses its own LAN play.
2. **The host:** **Multiplayer, LAN, Create**, as usual. In the waiting room, **Invite friends...** opens Steam's invite window (or the add-on's own list of your Steam friends).
3. **The friends:** accept the invite in Steam. The game starts if it is not running, opens the LAN screen and joins the host's game by itself. Or open **Multiplayer, LAN** and join the host's game in the list, like a LAN game.

Good to know:

- **Everyone needs the same H5 Together (0.3.0 or newer) with Online play on Steam,** and the host's Steam friends are the ones who see the game (the game is for friends only).
- **While it is on, the LAN list shows only games through Steam**, not games on your local network. For a real LAN game or GameRanger, set Online play back to **Off**.
- **Steam picks the way:** straight between the PCs when it can, or through Valve's relay servers (on a phone's 4G connection, for example). The LAN screen shows a few lines about Steam: whether it is on, and which friends play with H5 Together.
- **Tested** with two PCs (Windows, and Linux with Proton), on home Wi-Fi and over 4G, and played by three friends over the internet. Tell us how it goes with more.
- **Not for GOG copies:** it needs the game bought on Steam. A group with a GOG player keeps using LAN, ZeroTier or GameRanger.
- It uses Valve's `steam_api.dll`, which comes with the zip (see [Legal](#legal)).

## What works with what

| Game | Status |
|---|---|
| Steam Tribes of the East 3.1 with **MMH5.5 RC19d or RC19k**, started with the co-op exe `MMH55_Utility_64.exe` (or `MMH55_Utility.exe`), also when it is renamed `H5_Game.exe` for GameRanger | **Supported** |
| The original 2008 game (`H5_GameOLD.exe` after the GameRanger guide, or the `H5_Game.exe` that MMH5.5 RC19k leaves) | **Supported** (new in 0.3.5). In a folder with MMH5.5, H5 Together hides MMH5.5 from it ([how](#the-original-game-without-mmh55-new-in-035)) |
| MMH5.5's single-player exe `MMH55_64.exe` / `MMH55.exe` | Not recognised: the add-on stays off and the game runs as it always did |
| Steam's 2025 rebuild of `H5_Game.exe` (3.1.2.99) | Not recognised yet |
| Tribes of the East **without** MMH5.5 | With the 2008 exe, yes (new in 0.3.5). Steam's 2025 rebuild alone: not yet, the installer says so |
| GOG copy | The installer looks for it. Not tested |
| Windows 10 and 11 | Yes |
| Linux with Steam and Proton | Yes, with a launch option ([docs/linux.md](docs/linux.md)) |
| Steam Deck, Wine / Lutris / Heroic, the H5 Lobby app | Not tested |
| Online play over Steam | Steam copies, on Windows and on Linux with Proton. Not GOG |

"Not recognised" is safe: the add-on checks the exe at start and, when it does not know it, only writes "unknown build" in its log and changes nothing.

## Settings

In the game: **Options, H5 Together** (or press **F3**, in the main menu or on the map). The page is part of the game's own Options screen: change a setting, then **Apply**. **Default** puts them all back.

![The end of the H5 Together page: the Help part, and the update check](docs/images/settings-page-end.png)

It has two pages side by side. **On this PC:** interface, camera and visuals, sound, language, random maps. **Co-op games:** the host's options, advanced options, and help. A name that ends in `*` applies after you restart the game, `**` from the next game; the others apply at once. During a multiplayer game, the host's and the advanced settings are read only.

The settings are saved in `h5-together.ini`, in the game's `bin` folder next to the exe. You can also edit that file with Notepad (then restart the game). Every line in it is commented out and shows its default.

The defaults keep the original game: the speed-ups, fixes and panels are on, and anything that changes how the game plays, looks or sounds is off until you turn it on. Your hero look choices are kept in `h5-together-skins.ini`. Every setting is explained in [src/mods/README.md](src/mods/README.md).

## What it does, in detail

### Faster co-op multiplayer

- **Quicker AI turns and clicks.** The game's network timings were made for 2006 modems; the add-on uses modern ones. When every player has the add-on, the PCs also agree on shorter game steps (50 ms instead of 200 ms). In robot tests (two copies of the game on one PC), an AI move went from 2.5 s to about 0.3 s. Over the internet, the ping between the PCs comes on top.
- **Less AI thinking on big maps.** The AI finds exactly the same paths, faster: on a day-50 huge map, 93 s of AI thinking over 4 turns became 17 s.
- **No long autosave freeze** at the start of each round (7 to 10 s down to a fraction of a second when the `Saves` folder holds many multiplayer saves).
- **LAN games over ZeroTier** and other virtual networks show up in the LAN list, so a friend (on Linux too) can join without GameRanger.
- **The waiting room shows versions.** A line at the bottom says whether every player has the same H5 Together, and who has another version or none.
- **Computers play together** (new in 0.3.2, the host's choice, on by default since 0.3.5). The computer players take their turns at the same time instead of one after another: their part of each day about 40% shorter with two or three of them.
- **Undo End Turn** (new in 0.3.2, the host's choice, on by default since 0.3.5). With simultaneous turns, a player who ended the turn too early can take it back while another human still plays: a round button over the hourglass (since 0.3.4).
- **Quick resume after a crash** (new in 0.3.2, on). When a player's game crashes, every PC saves the game as "Reprise" with that player still in it; the player who crashed gets the save over Steam.
- **Each game its own autosaves** (new in 0.3.2). Autosaves are named after their game's map, so a new game no longer overwrites yesterday's.

### A smoother map

- **Big maps zoomed out run about twice as fast** (new in 0.3.2): the add-on gives the game more room for its 3D models, and far-away animations get lighter when the map runs slow (robot, a 6-player map at 2560x1440 zoomed out to 250%: 33 to 70 frames a second).
- **MMH5.5's scripts at their own pace** (new in 0.3.2): with the faster co-op steps, MMH5.5's script loop ran four times as often as intended; it runs at its original pace again.
- **A frame rate limit and the vertical sync** (new in 0.3.2; since 0.3.5 the limit is 180 frames a second by default), in Options, H5 Together.

### Stability

- **A rare freeze of the host during computer turns is fixed.** The computer takes at most one decision per frame, as in the original game.
- **A freeze detector.** If the game stops answering for 20 seconds, `h5-together.log` says where it is stuck. If that happens, wait 30 seconds before closing the game, then make a bug report (see [Reporting a problem](#reporting-a-problem)).
- **Crash reports.** If the game crashes, the add-on writes a short report (`h5-together-crash-<date>-<time>.txt`) and a small `.dmp` file next to the log. They stay on your PC; the bug report script adds them if you want.
- **The log stays small.** Over 5 MB, the old log becomes `h5-together.old.log`.

### Interface

- **Player list** on the adventure map: who is still playing, who has ended the turn, and when the computer is moving. **F9** hides it.

  ![The player list during a computer turn](docs/images/player-list.png)

- **Follow a friend's view** (new in 0.3.3, on). While you wait for your turn, click an ally's name (or eye) in the player list: your camera goes to their hero and follows their camera on the adventure map; click again to stop. A short line after the eye says where they are when not on the map ("in a town", "in a menu"). An eye shows who follows whom. Allies only; each player still sees the map through their own fog.
- **Watch their battle live** (new in 0.3.3, on). While you follow an ally who fights, a small "Watch the battle" button after their eye opens their battle on your screen, also when it has begun. You only watch: a right click shows a creature's card, but the battle gets none of your other clicks, and your PC sends nothing. Leave with "Stop watching" (or Esc) and come back whenever you like.
- **Skill wheel** (new in 0.3.5, the original game). A small wheel button on the hero screen (next to the Look button) shows what the hero's class can learn: each skill's chance, every ability with what it needs first, and what the hero already has (gold) or can take now (light frame). The arrows show the other classes.
- **A sword on an ally's row while they fight** (new in 0.3.5). Click it to follow them and open their battle at once.
- **Stay and watch after a defeat** (new in 0.3.4, on). When you lose the game while your friends play on, closing the results keeps you on the map: follow your team and watch their battles instead of going back to the lobby.

- **Hero details in the waiting room** (new in 0.3.5, on). Hover a starting hero's picture (or hold the right button on it): besides its name and specialty, the tooltip gives its class, its starting skills and what its specialty does.
- **"Your turn" notification** when the game is in the background: the taskbar button flashes, a short sound plays, and a pop-up appears.
- **Hero looks.** Give a hero the 3D figure of another hero of the same faction (the small button on the hero screen, or **F7**). In multiplayer, friends see your looks when everyone has the add-on.

  ![The hero looks panel](docs/images/hero-looks.png)

- **In your language.** The add-on's own words come from one text file per language, in `bin\h5-together\texts\`. English and French come with it, and the add-on follows the game's language. **Translators are welcome**: [src/mods/texts/TRANSLATING.md](src/mods/texts/TRANSLATING.md).
- **French texts for the whole game** (off by default). With MMH5.5, a French game mixes French and English; `[lang] texts=fr` shows French everywhere it can. It needs MMH5.5's French pack and our pack of the remaining texts (shared in the group).
- **New version notice** (off by default). If you say yes (the question on the first start, or in Options, H5 Together), the main menu tells you when a newer version is out.

### Random maps

- **Template builder in the game.** A **Modèles... / Template...** button on the random map screen (or **F11**) opens every choice of our template maker, with a map of the zones. **Use** fills in the game's screen for you.

  ![The template builder](docs/images/template-builder.png)

- **Recipe maps.** A template's name carries its whole setup, so every friend with the add-on rebuilds the same template when joining; there is nothing to share. A friend who lacks a template gets the game's error message instead of a crash.
- **24 ready templates** in the game's list (names start with `OH-`).
- **Teams.** Maps made from a team template get their teams in the lobby. On them, the team arrows move you to **another start of your own team** (new in 0.3.5: an open one; your team stays the same), so teammates can pick their places. The host can also set teams on other random maps (`[rmg] teams=`, `lobby_teams=1`).
- **Start terrain that matches your faction** (on by default since 0.3.5, the host's choice). With `[rmg] faction_terrain=1`, each start zone gets the ground of the faction picked in the lobby; the map is rebuilt for a few seconds at the start. Terrain changes movement costs, which is why it is off.

### Game rules menu

- The **host** chooses MMH5.5's rule switches for each game: governors, town conversion, the town gate, mine guards, the computer's weekly bonus, mixed neutral stacks, prank artifacts, random map extras and more. **Règles... / Rules...** in the multiplayer waiting room, or **F2**. The computer's weekly bonus also applies to a loaded game, from the next week (new in 0.3.4).

  ![The game rules panel](docs/images/game-rules.png)

- Every PC plays with the host's rules, so nobody swaps settings files any more. The others see the rules in the lobby and during the game.
- **Tribes of the East start bonuses:** each player keeps the bonus picked with the lobby arrows (gold, resources, artifact), with ToE's own icons, and the game mode (Default, Savage, Nightmare, Random) gets its own choice.
- **MMH5.5 scripts on random maps:** in multiplayer, MMH5.5's scripts (and so these rules) also run on random maps straight from the game's generator. Off on RC19d and on on RC19k by default, which is what each version does by itself.
- **Skills of each class:** MMH5.5's lock (the default), or **Unlocked**: each class keeps its own skills and can also be offered the others at a small chance, as in the original game.
- It works only when **every player** has the add-on (0.2.7 or later for the last three). Otherwise everyone uses their own MMH5.5 file, as before.

### The original game, without MMH5.5 (new in 0.3.5)

H5 Together also works with the original Tribes of the East 3.1, the 2008 program.

- **The installer makes `bin\H5_Original_4GB.exe`**, the original game that may use **4 GB** of memory, as MMH5.5's own exes do. The original exe can use only 2 GB, and a big map at a high resolution runs out of it (the game then crashes; measured: 1.86 GB of 2 in single player). The 2008 exe without Steam's protection comes from MMH5.5's own files: the installer asks, then fetches **only that file** (about 5.4 MB) from [MMH5.5's official release on GitHub](https://github.com/Might-Magic-Heroes-5-5/MMH55/releases) and checks it is exactly the 2008 exe (its SHA-256). MMH5.5 itself is not installed. Without internet, it takes it from **MMH5.5's installer file** instead (`MMH55_RC19k.exe` in your Downloads folder, next to `install.bat`, or `install.bat -Mmh55Installer <file>`). Nothing of the game's own files changes; uninstalling removes it.
- **Start it with a double-click** (Steam running, for online play): the main menu shows "H5 Together" over "Version : 3.1", without MMH5.5's line. On Linux, the installer prints the Steam launch option for it.
- Without MMH5.5's installer file: Steam's protected copy of the 2008 exe (`bin\H5_GameOLD.exe` in groups that followed the GameRanger guide) works too, with 2 GB only, and only when Steam starts it: put it in the game's Steam launch options (`"<game>\bin\H5_GameOLD.exe" %command%`). A changed copy of it does not start (Steam's protection refuses it). Steam's current `H5_Game.exe` is a 2025 rebuild that H5 Together does not know.
- **From the same folder as MMH5.5.** The 2008 program would read MMH5.5's data files without MMH5.5's program changes, a broken mix. So H5 Together hides MMH5.5's files from it (`data\MMH55-*.pak`, `UserMODs\MMH55-*.h5u`): you get the original game. MMH5.5 still starts as before from its own exe. No file is changed or moved.
- **Its own profile and saves:** a save of one game crashes the other, so the original game keeps its profile and saves in `Documents\H5 Together\Original game`. At its first start it copies your profile's settings there (name, options, keys), never the saves.
- **MMH5.5's HD textures, if you like them:** Options, H5 Together, **MMH5.5's HD textures** (`[video] hd_textures=1`). Only the pictures: terrain, towns, sky. Each player chooses; it does not matter for the others.
- **Endless simultaneous turns** (the host's choice, on by default since 0.3.5): the original game ends simultaneous turns when heroes get close; with **Endless simultaneous turns** (`[coop] endless_turns=1`) they last the whole game, as MMH5.5's PEST does. Every PC needs H5 Together 0.3.5.
- Everything else of H5 Together works the same: faster computer turns, online play over Steam, following and watching your friends, the player list, quick resume, Undo End Turn. The game rules menu (F2) is not there: its rules are all MMH5.5's.
- **In one game, everybody starts the same:** all the original game, or all MMH5.5. Over Steam you only see games of your own kind.
- **Maps:** the original game cannot load maps made for MMH5.5 (with its editor or its random map generator): it crashes on their MMH5.5 objects. Use the game's own maps, or random maps made in the original game.
- **Joining a big random map** takes a while (every PC builds the map itself): H5 Together gives a joining PC up to 300 s (`[net] join_timeout_s`) instead of the game's limits (its join stages' 10 + 20 s, its 20 s silence limit), and the game's values again once the game runs. A Linux PC can now also join a Windows host's builder map over Steam: the add-on used to drop a network message the game read more than 3 s late, and under Proton, after a long map build, that could be the host's one "accepted" answer.
- Measured with the robot (same map, same settings): the computer's turns are as fast as with MMH5.5, and faster on a big map (15 turns in 92 s, against 119 s with MMH5.5's scripts).

### Camera, sound and visuals (off by default)

All of these are rows on the Options page, under **Camera and visuals** and **Sound**.

- **Zoom out farther:** `[camera] zoom_out=150` (adventure map) and `combat_zoom_out=150` (battles); `start_zoom` sets where the camera starts.
- **Black map edges:** `[camera] edge_sky=none` replaces the violet sky MMH5.5 put past some random maps' edges.
- **Quieter volume range** (on by default since 0.3.5): `[audio] volume_curve=wide` makes the sliders' lowest step about 10 times quieter; `game` is the game's own range.
- **Original Heroes V music** instead of MMH5.5's: `[audio] original_music=1`.
- **Another picture behind the main menu**, with its own music: `[menu] scene=toe` (Tribes of the East), `homm5` (the Heroes V one, with MMH5.5) or `random` (drawn at each start).
- **Mouse wheel on Linux:** under Wine / Proton the wheel could zoom in but never out. This one is automatic, on Linux only.

### Works next to another `dinput8.dll` mod

Some mods also come as a `dinput8.dll`. Rename the other one `dinput8_next.dll` before you install; H5 Together then loads it and passes DirectInput on to it. Details: [docs/install.md](docs/install.md#another-mod-that-is-also-a-dinput8dll).

## Reporting a problem

Run `bug-report.bat` (Windows) or `bash bug-report.sh` (Linux) from the folder you installed from. It makes `h5-together-bug-report-<date>.zip` on your Desktop (on Linux, in the folder you run it from): `h5-together.log`, your settings, the newest crash report if there is one, and a short list of versions (the add-on, MMH5.5, your system). No saves. It sends nothing; you attach the zip to a [new bug report](https://github.com/jozrcr/h5-together/issues/new/choose). The log lists player names and LAN addresses, so look at the zip first if that matters. What is in the zip, in detail: [docs/install.md](docs/install.md#reporting-a-problem).

Problems of MMH5.5 itself (its scripts, balance or installer) are best told to [the MMH5.5 team](https://www.moddb.com/mods/might-magic-heroes-55).

## For curious readers

- [docs/faq.md](docs/faq.md): questions and answers (the log, antivirus, GOG, "out of sync", Linux, removing it...).
- [src/mods/README.md](src/mods/README.md): every change the add-on makes, how it works, and how it was tested.
- [src/hook/README.md](src/hook/README.md): the DLL itself, and how to build it.
- [docs/plans/](docs/plans/): plans for each feature, and the [backlog](docs/plans/backlog.md) of ideas.
- [docs/subsystems/](docs/subsystems/): what we found out about how the game works (AI turns, multiplayer sync, saves, random maps, camera, sound...).
- [docs/releases/](docs/releases/): the notes of every release.

## Project status

This is a hobby project, built for a group of friends; others are welcome to use it. Bug reports help.

The project started with a bigger long-term idea: rebuild the original exe function by function (each function checked to compile to the same bytes), and one day grow it into an open Heroes V engine, the way VCMI did for Heroes III. That decompilation is **set aside for now**: the work goes into the add-on. What was done stays in `src/decomp/`.

## Thanks

H5 Together stands on **MMH5.5**, the big Heroes V mod by its team ([ModDB page](https://www.moddb.com/mods/might-magic-heroes-55)). It changes MMH5.5's settings only in memory. Some numbers in our ready random map templates were copied from MMH5.5's own templates. And thanks to the friends who test every build.

## Legal

Our code is under the [MIT licence](LICENSE). The only third-party code in the DLL is Dear ImGui (MIT, with the stb files and the small ProggyClean font it carries), in [src/hook/third_party/imgui/](src/hook/third_party/imgui/); the install zip carries both licences as `LICENSE.txt` and `THIRD-PARTY-NOTICES.txt`.

The release zip also carries **Valve's `steam_api.dll`** (from the Steamworks SDK), for online play over Steam. It is Valve's file, redistributed as Valve's Steamworks SDK agreement allows for software that uses Steam; it is not covered by our licence and is not in this repository. The add-on loads it only when online play over Steam is on. Steam is a trademark of Valve Corporation; this project is not affiliated with or endorsed by Valve.

Apart from that, this repository contains only our own code, notes and tools. It contains **no game files**, so you need your own legal copy of Heroes of Might and Magic V: Tribes of the East (for example from Steam) to use anything here. Heroes of Might and Magic is a trademark of Ubisoft; this is an unofficial fan project, not affiliated with or endorsed by Ubisoft.
