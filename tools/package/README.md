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

The scripts and templates are in `tools/package/files/`. Everything is in one top folder, `open-homm5-<version>/`, so that unzipping on Linux does not spread the files around. Line endings are set when the zip is written: CRLF for `.bat`, `.ps1`, `.txt` and `.ini` (Notepad, cmd.exe), LF for `.sh`, which also get the executable bit. In the repo, `.gitattributes` keeps `.bat` as CRLF and `.sh` as LF.

## What the scripts do

- Windows (`install.ps1`, Windows PowerShell 5.1, French Windows included):
  - finds the game: Steam's folder from `HKCU:\Software\Valve\Steam`, the libraries from `steamapps\libraryfolders.vdf`, then `steamapps\common\Heroes of Might and Magic 5 Tribes of the East`, whose `bin` must hold `MMH55_Utility_64.exe` or `H5_Game.exe`. With none or several found it asks (a number, or a pasted folder: the game folder or its `bin`);
  - copies `dinput8.dll`; refuses (exit code 3) if the game folder already has a `dinput8.dll` that is not ours;
  - copies `open-homm5.ini` only if there is none;
  - looks at the network profiles and explains how to make a Public ZeroTier network Private. It changes no Windows setting and no firewall rule;
  - ends with "Press Enter to close".
- Linux (`install.sh`): the same, looking for Steam in `~/.steam/steam`, `~/.local/share/Steam` and the Flatpak folder. It prints the Steam launch option `WINEDLLOVERRIDES="dinput8=n,b" %command%`, and which exe to start. It does not edit Steam's files.
- "Ours" means the file contains the text `open-homm5 hook DLL` (a string in `src/hook/dllmain.cpp`). `make_package.py` refuses to pack a DLL without it.
- Uninstall removes our `dinput8.dll`, `open-homm5.log`, and `open-homm5.ini` after asking. Nothing else.

Exit codes of the scripts: 0 done, 3 refused (not our `dinput8.dll`), 4 game not found or cancelled, 5 copy/delete failed, 6 unexpected error, 7 package incomplete (for example `install.bat` started from inside the zip without extracting it).

Test options (also handy for odd setups): `-GameDir <folder>`, `-SteamDir <folder>`, `-NoPause` on Windows; `--game-dir <folder>`, `--steam-dir <folder>` on Linux (which otherwise reads `$HOME`). `install.ps1 -TestProfiles "name|category;..."` fakes the network profiles, to test the ZeroTier hint.

## Rules for editing the scripts

- **Plain ASCII only** in `files/*` (`make_package.py` checks). Windows PowerShell 5.1 reads a script without a byte-order mark as ANSI, so an accent would be garbled on a French Windows.
- The code that finds the game is written twice, in the install and the uninstall script, between the lines `# ---- shared with uninstall...` and `# ---- end of the shared block ----`. Edit both copies; `make_package.py` stops if they differ.
- Keep the messages plain and short: the readers are not programmers.

## Test it

```
python tools\package\test_scripts.py --bash "C:\Program Files\Git\bin\bash.exe"
```

It builds the zip, unzips it, makes **fake** Steam and game folders in a temp folder (empty files named like the game's exes), and runs the scripts against them: fresh install, re-install over our DLL, a foreign `dinput8.dll` (refused), an existing ini (kept), uninstall, several or no games found, and more. It never touches a real game folder. On Linux, leave out `--bash`; on Windows without Git Bash the Linux part is skipped.

## Built by GitHub

`.github/workflows/package.yml` runs on GitHub's Windows machines, which are free for public repositories.

**On every push to `main` and every pull request**, it:
1. builds the DLL and runs its tests;
2. builds the zip (version `0.0.0-dev.<run number>`);
3. runs `test_scripts.py`.

The zip is then a download on the run's page (Actions tab).

**On a version tag** (`git tag v0.1.0`, then `git push origin v0.1.0`), it does the same with that version and creates a **draft** release with the zip attached. The owner edits the notes and publishes it; the workflow never publishes anything itself.

