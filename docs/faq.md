# H5 Together: questions and answers

Short answers to what players ask most. How to install: [install.md](install.md). What it does: [the README](../README.md). If your question is not here, [open an issue](https://github.com/jozrcr/h5-together/issues/new/choose).

**Installing**
[Windows protected your PC](#windows-protected-your-pc) ·
[Antivirus](#my-antivirus-removed-or-blocked-dinput8dll) ·
[Without MMH5.5?](#does-it-work-without-mmh55) ·
[Which game files?](#which-game-files-does-it-work-with) ·
[GOG](#i-have-the-gog-version) ·
[Updating](#how-do-i-update-and-how-do-i-know-a-new-version-is-out) ·
[Removing it](#how-do-i-uninstall-it) ·
[Another dinput8.dll mod](#i-already-use-a-mod-that-is-a-dinput8dll)

**Playing**
[Is it running?](#how-do-i-know-it-is-running) ·
[Settings](#how-do-i-change-settings) ·
[GameRanger](#gameranger-and-the-h5-lobby-app) ·
[Online play over Steam](#can-we-play-over-the-internet-without-gameranger) ·
[A friend's game is not in the list](#online-play-my-friends-game-is-not-in-the-lan-list) ·
["Timeout" when joining](#timeout-may-be-no-more-free-slots-when-joining) ·
[A friend has another version](#a-friend-has-another-version-or-none) ·
[Out of sync](#the-game-says-out-of-sync) ·
[A freeze](#the-game-froze) ·
[Single player](#does-it-work-in-single-player) ·
[Long intro video](#the-intro-video-is-very-long) ·
[Linux and Steam Deck](#linux-and-steam-deck) ·
[Languages](#my-language-is-not-there)

**Problems and logs**
[Where is the log?](#where-is-the-log) ·
["unknown build"](#the-log-says-unknown-build) ·
[Crash reports](#the-game-crashed-where-is-the-report) ·
[Making a bug report](#how-do-i-make-a-bug-report) ·
[MMH5.5's own bugs](#a-bug-of-mmh55-itself) ·
[Privacy](#does-it-send-anything-about-me)

---

## Installing

### "Windows protected your PC"

Normal for a new file from the internet that is not signed. Click **More info**, then **Run anyway** (or **Run** / **Open** in the white "Open File - Security Warning" box). Why it happens, how to avoid it, and how to check the download: [install.md](install.md#windows-protected-your-pc-and-antivirus).

### My antivirus removed or blocked `dinput8.dll`

Some antivirus programs distrust a `dinput8.dll` that changes a game while it runs, because cheats use the same trick. We have not seen it yet, but it can happen. Restore the file from the antivirus's quarantine, add the game's `bin` folder to its exclusions, and install again. Please tell us which antivirus it was and what it said, and report the false alarm to its maker. The steps, step by step: [install.md](install.md#windows-protected-your-pc-and-antivirus). The code is open, so you can read it or build the DLL yourself ([src/hook/README.md](../src/hook/README.md#build)).

### Does it work without MMH5.5?

Yes, since 0.3.5, with the original 2008 program. The installer makes `bin\H5_Original_4GB.exe` (the original game with 4 GB of memory, started with a double-click): with your yes it fetches only the 2008 exe (about 5.4 MB) from MMH5.5's official release on GitHub and checks it, without installing MMH5.5. Without that file, Steam's protected `H5_GameOLD.exe` (after the GameRanger guide) works too, with 2 GB, started through Steam's launch options. In a folder where MMH5.5 is installed, H5 Together hides MMH5.5's files from it, so you play the original game, with its own profile and saves (`Documents\H5 Together\Original game`). The game rules menu (F2) is not there: its rules are all MMH5.5's. More in the [README](../README.md#the-original-game-without-mmh55-new-in-035).

Steam's current `H5_Game.exe` is a 2025 rebuild of the game that the add-on does not know yet: with only that one, the add-on does nothing, and the installer says so.

The add-on does not install MMH5.5. Get it from [MMH5.5's ModDB page](https://www.moddb.com/mods/might-magic-heroes-55). The installer knows **RC19d** and **RC19k**.

### Which game files does it work with?

| What you start | Result |
|---|---|
| `MMH55_Utility_64.exe` (MMH5.5's co-op exe), or a copy of it named `H5_Game.exe` for GameRanger | Works |
| The original 2008 exe (`H5_GameOLD.exe`) | Works (new in 0.3.5): the original game, MMH5.5's files hidden from it |
| `MMH55_64.exe` / `MMH55.exe` (MMH5.5's single-player exe) | Not recognised: nothing changes, the log says "unknown build" |
| Steam's 2025 `H5_Game.exe` (3.1.2.99) | Not recognised yet |

The installer's "Your game" screen tells you which `H5_Game.exe` you have. More in the [README's table](../README.md#what-works-with-what).

### I have the GOG version

The installer looks for it (in GOG's list in the Windows registry, and through the entry that MMH5.5's own installer leaves). We have no GOG copy of Heroes V to try, so we cannot promise it. If the installer does not find your game, type **B** when it asks and choose the game's folder (the one that holds `bin`); or run it with the folder, `install.bat -GameDir "<folder>"`. Then tell us how it went. You need MMH5.5 installed in that copy too.

### How do I update, and how do I know a new version is out?

Install the new version over the old one: your settings and hero looks are kept. Details: [install.md](install.md#updating). To hear about new versions, say **Yes** to the question the first time the game starts with the add-on, or tick **Check for new versions** in Options, H5 Together, Help. Then, at most once a day, the add-on asks GitHub for the number of the latest version and tells you in the main menu when there is a newer one. It is off unless you turn it on. Nothing is ever downloaded or installed by the add-on.

### How do I uninstall it?

Run `uninstall.bat` (Windows) or `bash uninstall.sh` (Linux) from the folder you extracted. It removes only the add-on's own files and asks before it deletes your settings. Or just delete `dinput8.dll` from the game's `bin` folder: the game is then as before. To switch it off for a test without removing anything, create an empty file called `h5-together.nohooks` in the `bin` folder.

### I already use a mod that is a `dinput8.dll`

Only one file can have that name. Rename the other mod's `dinput8.dll` to `dinput8_next.dll`, then install H5 Together. It loads the other mod and passes DirectInput on to it; the log says `chained`. We can only try this with a stand-in mod, so tell us how it goes with yours. Details and how to undo it: [install.md](install.md#another-mod-that-is-also-a-dinput8dll).

## Playing

### How do I know it is running?

Three signs:

- In the main menu, a line **"H5 Together 0.3.4"** (your version) above MMH5.5's "Version" line.
- `h5-together.log` in the game's `bin` folder, whose first line says `H5 Together hook DLL <version> loaded`.
- In a multiplayer waiting room, a line at the bottom: "Every player has H5 Together 0.3.4."

![The main menu with the version line](images/main-menu.png)

### How do I change settings?

In the game: main menu, **Options**, **H5 Together** (or press **F3**), then **Apply**. A name ending in `*` applies after a restart, `**` from the next game. You can also edit `h5-together.ini` in the game's `bin` folder with Notepad. Details: [install.md](install.md#changing-settings).

![The H5 Together page in the Options screen](images/settings-page.png)

### GameRanger and the H5 Lobby app

GameRanger works. It starts `H5_Game.exe`, which, following the GameRanger guide for MMH5.5, is a copy of MMH5.5's co-op exe (`MMH55_Utility_64.exe`). The installer recognises that copy. Everybody in the game installs the same H5 Together version. If Steam updates the game, it may put its own `H5_Game.exe` back (the add-on then logs "unknown build"): copy the co-op exe over it again.

We have not tried the H5 Lobby app. If you do, please tell us.

### Can we play over the internet without GameRanger?

Yes, with your **Steam friends**, from 0.3.0 (a test for now). Every player sets **Options, H5 Together, Online play: Steam** and restarts the game. The host creates a LAN game as usual and clicks **Invite friends...** in the waiting room; the friends accept the invite in Steam, and their game joins by itself (or they pick the host's game in **Multiplayer, LAN**). Everything else is the game's own LAN play. Details: [the README](../README.md#online-play-over-steam-new-a-test).

It needs the game bought on Steam (not GOG), Steam running, and the same H5 Together (0.3.0 or newer) with online play on, for every player (on by default since 0.3.5; without Steam the game uses its own LAN play). While it is on, the LAN list shows only games through Steam: for GameRanger or a real LAN game, set it back to **Off**.

### Online play: my friend's game is not in the LAN list

Look at the lines under the list on the LAN screen: they say whether Steam is on, how many of your friends play with H5 Together, and why a game cannot be joined (another version, for example). Then check:

- you are **Steam friends** with the host (the host's game is for friends only);
- **both** of you set Online play to **Steam** and restarted the game, and Steam is running on both PCs;
- you have the **same H5 Together and the same MMH5.5** (a game made with another one shows a line saying so).

If it still does not show, make a [bug report](#how-do-i-make-a-bug-report) from both PCs.

### "Timeout. May be no more free slots" when joining

Click **OK**, pick the game again and click **Join**. Over Steam, 0.3.0 sends every message again until it arrives, which fixed the one case we saw (a join answer lost on a 4G connection). If it happens again, please make a [bug report](#how-do-i-make-a-bug-report): the log says what was lost.

### A friend has another version, or none

The waiting room's bottom line says it: who has which version, or none. The game still runs, but what needs everyone stays off: the host's game rules, the shorter co-op steps and other players' hero looks. For the best result, everyone installs the same version of H5 Together, and has the same MMH5.5.

![The waiting room: the version line is at the bottom](images/lobby-versions.png)

### The game says "out of sync"

It means the PCs no longer agree on what is happening in the game. In our long test games, with the same files on every PC, it did not happen; if it does, we want to know. The usual causes, to check first:

1. **Not the same MMH5.5** (its version, or its settings): everybody must have the same. The main menu shows MMH5.5's version ("Version: 5.69k" is RC19k).
2. **Not the same H5 Together:** look at the waiting room's bottom line.
3. **Not the same random map templates:** the 24 `OH-` templates must be the same on every PC, so install the same version.
4. **Different game rules:** let the host choose them in the waiting room (**Rules...**, or **F2**), so every PC plays with the host's rules and nobody swaps settings files.

If it still happens, make a bug report on **every PC** (the host's and the player's who saw the message), from the same session, and attach all the zips ([below](#how-do-i-make-a-bug-report)).

### The game froze

Wait **30 seconds** before closing it. If the game stops answering for 20 seconds, the add-on writes in `h5-together.log` where it is stuck, and it needs those seconds. Then make a bug report ([below](#how-do-i-make-a-bug-report)). A long load or a big map can be slow without being frozen.

### Does it work in single player?

Yes, if you start the game with MMH5.5's co-op exe, `MMH55_Utility_64.exe`. The template builder, hero looks, the camera, sound and menu options, the player list and the "your turn" notification then work in single player too, and the AI thinks faster. The parts for co-op (the waiting room line, the host's game rules, the shorter steps) only matter in multiplayer.

MMH5.5's own single-player exe, `MMH55_64.exe`, is not recognised: the add-on stays off there and the game runs as it always did. Note that the co-op exe plays with the original 3.1 AI, not the "3.1j enhanced AI" that `MMH55_64.exe` brings.

### The intro video is very long

That is MMH5.5, not us: RC19k's intro video lasts 2 minutes 50 seconds. Press **Esc** to skip it.

### Linux and Steam Deck

Linux with Steam and Proton works: `bash install.sh` finds the game and prints one line to paste into the game's Steam launch options. The whole walkthrough, with ZeroTier to play without GameRanger, is in [linux.md](linux.md). The mouse wheel fix for Wine and Proton is automatic.

The Steam Deck is a Linux PC that runs the game through Proton, so the same steps should work from its Desktop mode. We have not tried it. Wine, Lutris and Heroic are untested too: the same idea applies (the game must load the `dinput8.dll` of the game folder, which the Steam launch option `WINEDLLOVERRIDES="dinput8=n,b" %command%` does; in another launcher, set the same `WINEDLLOVERRIDES` as an environment variable, and run the installer with `bash install.sh --game-dir <folder>`). If you try, tell us.

### My language is not there

The add-on's own words (its panels and messages) come in English and French, and it follows the game's language. Another language needs a translation file: one short text file, no programming. Steps: [src/mods/texts/TRANSLATING.md](../src/mods/texts/TRANSLATING.md), and send it to us with the "Translation" issue form. Translators are welcome. The game's own texts and MMH5.5's translations are not ours: ask the MMH5.5 team.

## Problems and logs

### Where is the log?

`h5-together.log`, in the game's **`bin`** folder, next to the exe you start. The first line says `H5 Together hook DLL <version> loaded`. The older log, if there is one, is `h5-together.old.log`.

- Windows, Steam: `C:\Program Files (x86)\Steam\steamapps\common\Heroes of Might and Magic 5 Tribes of the East\bin`. Or, in Steam: right-click the game, **Manage**, **Browse local files**, then open `bin`.
- Linux, Steam: `~/.local/share/Steam/steamapps/common/Heroes of Might and Magic 5 Tribes of the East/bin` (Flatpak Steam: `~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/...`).

No log at all means the add-on did not load: the exe you started is not in the folder where `dinput8.dll` is, an antivirus removed the file, or (Linux) the launch option is missing.

The log lists player names and LAN addresses, so look at it before you post it in public.

### The log says "unknown build"

The add-on does not know the exe you started, so it does nothing, on purpose. Start MMH5.5's co-op exe, `MMH55_Utility_64.exe`, or a copy of it named `H5_Game.exe` (see the [table above](#which-game-files-does-it-work-with)). On Linux, the launch option must start that exe ([linux.md](linux.md)).

### The game crashed: where is the report?

If the game crashes, the add-on writes two files next to the log, in `bin`: `h5-together-crash-<date>-<time>.txt` (what crashed, and where) and a small `.dmp` file (part of the game's memory at that moment, which holds player and folder names). The three newest are kept. Nothing is sent anywhere. The next start's log says "THE LAST RUN OF THE GAME CRASHED" and names the report. To send it to us, make a bug report: it adds the newest crash report. To leave the `.dmp` out, open the zip and delete it before you attach the zip (or run `bug-report.bat -NoDump`).

### How do I make a bug report?

Run `bug-report.bat` (Windows) or `bash bug-report.sh` (Linux) from the folder you installed from. It makes `h5-together-bug-report-<date>.zip` on your Desktop (on Linux, in the folder you run it from). It sends nothing: you open a [new bug report](https://github.com/jozrcr/h5-together/issues/new/choose) and attach the zip yourself. It holds the log, your settings, the newest crash report and a list of versions; no saves, no maps. It works even when the game does not start. What is in it: [install.md](install.md#reporting-a-problem).

### A bug of MMH5.5 itself

MMH5.5's scripts, balance, installer and translations are made by the MMH5.5 team, who look after them: tell them, on [their ModDB page](https://www.moddb.com/mods/might-magic-heroes-55) or their Discord. To find out whose bug it is, try once without H5 Together (create an empty `h5-together.nohooks` file in `bin`, or delete `dinput8.dll`): if it still happens, it is MMH5.5's or the game's. If it only happens with H5 Together, it is ours: please report it.

### Does it send anything about me?

No. It sends nothing to us or to anyone else. In a multiplayer game the PCs of the players exchange a few small messages (versions, game rules, hero looks) over the game's own connection. Apart from online play over Steam (below), the only thing that can contact a server is the update check, which is **off** unless you say yes: once a day it asks GitHub for the number of the latest version, and GitHub sees the address it comes from, as any website does. The bug report zip is made on your PC and goes nowhere until you attach it yourself.

**Online play over Steam** is on by default since 0.3.5 (Options, H5 Together, Online play: Off turns it off). When it is on, the game talks to your friends' games through Steam, as any Steam game does: Steam sees the friends-only game you host and the invites you send, and the add-on shows its version number to your Steam friends' add-ons (a small Steam "rich presence" value).
