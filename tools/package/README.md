# Package: the easy install

Builds `open-homm5-<version>.zip`, the file a friend downloads to install the hook DLL (see [docs/install.md](../../docs/install.md), "Easy install"). It stays local: this tool does not upload or publish anything.

## Build it

```
powershell -File src\hook\build.ps1            # builds build\hook\dinput8.dll (done for you if it is missing)
python tools\package\make_package.py            # -> build\package\open-homm5-0.1.0-dev.zip
python tools\package\make_package.py --version 0.1.0
python tools\package\make_package.py --rebuild  # build the DLL again first
```

`build\` is not committed. The script lists the zip's contents and its SHA-256 at the end.

## What is in the zip

| File | What it is |
|---|---|
| `dinput8.dll` | the hook DLL, built from `src/hook` and `src/mods` |
| `open-homm5.ini` | the settings, every one commented out and shown at its default (the DLL's built-in defaults apply until a line is un-commented) |
| `install.bat`, `install.ps1` | Windows installer (the `.bat` only starts the `.ps1`) |
| `uninstall.bat`, `uninstall.ps1` | Windows uninstaller |
| `install.sh`, `uninstall.sh` | Linux (Steam / Proton) installer and uninstaller |
| `README.txt` | ten lines for a friend; `@VERSION@` is filled in |
| `open-homm5/templates/OH-*.xdb` | the ready random map templates (see below) |

The scripts and text files are in `tools/package/files/`. Everything is in one top folder, `open-homm5-<version>/`, so that unzipping on Linux does not spread the files around. Line endings are set when the zip is written: CRLF for `.bat`, `.ps1`, `.txt` and `.ini` (Notepad, cmd.exe), LF for `.sh`, which also get the executable bit. In the repo, `.gitattributes` keeps `.bat` as CRLF and `.sh` as LF.

## The random map templates

`make_package.py` packs every `src/mods/rmg/templates/*.xdb` as `open-homm5/templates/<name>.xdb` (inside the top folder, the same layout as in the game's `bin` folder). Those files are written by `node tools/rmg/make_templates.js`; the list of ready templates is in [../rmg/README.md](../rmg/README.md).

- **Byte for byte:** the `.xdb` files are UTF-8 with a BOM and CRLF, and they are not converted (`.gitattributes` has `*.xdb -text`). The script stops if a file lost its BOM or got LF line endings.
- **Names:** `OH-<letters, digits, . or ->.xdb` only. The scripts rely on that.
- **Every player of a multiplayer game needs the same template files** (each PC builds the map from the template). So a name that has shipped must keep building the same file; add a new entry for a changed template, and everyone installs the same version.
- `test_template_maker.js` fails if the files in `src/mods/rmg/templates` are not what `make_templates.js` builds now, so the package never carries a stale file.

## What the scripts do

- Windows (`install.ps1`, Windows PowerShell 5.1, French Windows included):
  - finds the game: Steam's folder from `HKCU:\Software\Valve\Steam`, the libraries from `steamapps\libraryfolders.vdf`, then `steamapps\common\Heroes of Might and Magic 5 Tribes of the East`, whose `bin` must hold `MMH55_Utility_64.exe` or `H5_Game.exe`. With none or several found it asks (a number, or a pasted folder: the game folder or its `bin`);
  - copies `dinput8.dll`; refuses (exit code 3) if the game folder already has a `dinput8.dll` that is not ours;
  - copies `open-homm5.ini` only if there is none;
  - copies the templates into `bin\open-homm5\templates\` (making the folder). It overwrites its own `OH-*.xdb` files there (by name) and touches no other file, so a player's own templates stay. The summary says how many templates were installed (and how many replaced older copies of ours, and how many other files it left alone);
  - looks at the network profiles and explains how to make a Public ZeroTier network Private. It changes no Windows setting and no firewall rule;
  - ends with "Press Enter to close".
- Linux (`install.sh`): the same, looking for Steam in `~/.steam/steam`, `~/.local/share/Steam` and the Flatpak folder. It prints the Steam launch option `WINEDLLOVERRIDES="dinput8=n,b" %command%`, and which exe to start. It does not edit Steam's files.
- "Ours" means the file contains the text `open-homm5 hook DLL` (a string in `src/hook/dllmain.cpp`). `make_package.py` refuses to pack a DLL without it.
- The Linux installer also reuses an existing `open-homm5\templates` folder and existing template files under another upper/lower case spelling (Wine reads them either way), so it never makes a second copy.
- Uninstall removes our `dinput8.dll`, `open-homm5.log`, and `open-homm5.ini` after asking. It also removes the templates: only the `OH-*.xdb` names that are in the package it runs from, never another file. If the `templates` folder is then empty it is removed, and then `open-homm5` too if it is empty. Nothing else.
  - A template that an older version of the package installed, and a newer one no longer has, is not in the newer package's list: uninstalling from the newer package leaves it. This is on purpose (the alternative is deleting every `OH-*.xdb`, which would also delete a player's own template made with the template maker page).

Exit codes of the scripts: 0 done, 3 refused (not our `dinput8.dll`), 4 game not found or cancelled, 5 copy/delete failed, 6 unexpected error, 7 package incomplete (for example `install.bat` started from inside the zip without extracting it, or a package without its `open-homm5\templates` folder).

Test options (also handy for odd setups): `-GameDir <folder>`, `-SteamDir <folder>`, `-NoPause` on Windows; `--game-dir <folder>`, `--steam-dir <folder>` on Linux (which otherwise reads `$HOME`). `install.ps1 -TestProfiles "name|category;..."` fakes the network profiles, to test the ZeroTier hint.

## Rules for editing the scripts

- **Plain ASCII only** in `files/*` (`make_package.py` checks). Windows PowerShell 5.1 reads a script without a byte-order mark as ANSI, so an accent would be garbled on a French Windows.
- The code that finds the game is written twice, in the install and the uninstall script, between the lines `# ---- shared with uninstall...` and `# ---- end of the shared block ----`. Edit both copies; `make_package.py` stops if they differ.
- Keep the messages plain and short: the readers are not programmers.

## Test it

```
python tools\package\test_scripts.py --bash "C:\Program Files\Git\bin\bash.exe"
```

It builds the zip, unzips it, makes **fake** Steam and game folders in a temp folder (empty files named like the game's exes), and runs the scripts against them: fresh install, re-install over our DLL, a foreign `dinput8.dll` (refused), an existing ini (kept), uninstall, several or no games found, and more. For the templates it checks that the zip holds every source file byte for byte, then (Windows and Linux scripts alike) that:
- an install puts exactly our templates in `bin/open-homm5/templates`, and the summary counts them;
- a player's own files there (`my-own.xdb`, `notes.txt`, even an `OH-mine.xdb` that is not in the package) are kept on install and on uninstall;
- a re-install overwrites our changed file and brings back a missing one;
- an uninstall removes only our names, then the empty folders, but not an `open-homm5` folder that holds something else;
- a package without its `templates` folder is refused (exit 7);
- on Linux, a folder and a file that exist under another spelling (`Open-Homm5/Templates`) are reused.

It never touches a real game folder. On Linux, leave out `--bash`; on Windows without Git Bash the Linux part is skipped.

## Built by GitHub

`.github/workflows/package.yml` runs on GitHub's Windows machines, which are free for public repositories.

**On every push to `main` and every pull request**, it:
1. builds the DLL and runs its tests;
2. builds the zip (version `0.0.0-dev.<run number>`);
3. runs `test_scripts.py`.

The zip is then a download on the run's page (Actions tab).

**On a version tag** (`git tag v0.1.0`, then `git push origin v0.1.0`), it does the same with that version and creates a **draft** release with the zip attached. The owner edits the notes and publishes it; the workflow never publishes anything itself.

## Making a release

1. Set the version in `src/hook/version.h` (`OPEN_HOMM5_VERSION`). The DLL writes it on the first line of `open-homm5.log`, and the package takes it from there.
2. Write the release notes in `docs/releases/<version>.md`, in plain words for players.
3. Merge to `main`.
4. Tag that commit `v<version>` and push the tag (`git tag v0.1.0`, then `git push origin v0.1.0`). GitHub then builds and tests everything, and creates a **draft** release with the zip and the notes.
5. The owner reads the draft on the repo's Releases page and publishes it.

`make_package.py` refuses a release version that is not the one in `version.h`, and a DLL built from another version. Test builds use a version with a `-`, like `0.1.0-test2`.

