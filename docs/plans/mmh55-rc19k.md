# Plan: upgrading MMH5.5 from RC19d to RC19k

Status: **research only** (2026-10-25). Nothing was downloaded, installed or run. The game folder was only read. The downloads in [section 8](#8-downloads-to-approve) need the owner's OK.

## Short version

- **RC19k is the current MMH5.5.** It is an installer, `MMH55_RC19k.exe`, 865.7 MB, on ModDB since 13 Oct 2025. The main menu shows "Version: 5.69k".
- **It does not install over RC19d.** The installer stops if it finds an older MMH5.5. You must run RC19d's uninstaller first. A full game reinstall is not needed.
- **The uninstall deletes your edited `MMH55-Settings.pak`.** Back it up first, then make the same 4 changes in RC19k's new file. Everyone in the group must use that one file.
- **The big unknown for us:** did MMH5.5 change the co-op exe (`MMH55_Utility_64.exe`) or `55u.dll`?
  - The RC19e notes speak of "many technical updates to the executable".
  - `55u.dll` almost certainly changed in RC19k: it is the part that reads the Stats paks, and RC19k reads 9 new Stats pak slots.
  - If the exe changed in the wrong place, our DLL says `game build: unknown` and turns all its mods off. The game then still runs normally, just without our mods.
  - Only the download will tell. A test copy comes first ([section 5](#5-test-plan)).
- **French:** the official French pack exists only for **RC19i**. About 11 of ~6,000 text files are missing compared with RC19k.
- **Disk:** C: has 170 GB free. A second test copy needs about 5 GB.

## 1. RC19k: what, where, how it installs

| | |
|---|---|
| Release | Notes posted 20 Sep 2025, later edited to cover RC19f to RC19k ([release notes](https://www.moddb.com/mods/might-magic-heroes-55/news/mmh55-release-notes-rc19f)). RC19f to RC19j were only short-lived updates: ModDB lists no download for them. |
| Official download | ModDB, [Might & Magic: Heroes 5.5 (RC19k)](https://www.moddb.com/mods/might-magic-heroes-55/downloads/might-magic-heroes-55-rc19k): file `MMH55_RC19k.exe`, added 13 Oct 2025, 865.7 MB (907,749,563 bytes), MD5 `61c45d4f0df5c73771007bd507ba895e`. |
| Second copy | GitHub, [MMH55 release "RC19k for lobby"](https://github.com/Might-Magic-Heroes-5-5/MMH55/releases/tag/RC19k): `manual_setup_RC19k.zip`, 820,613,073 bytes, uploaded 19 Oct 2026, "used by lobby app". It is a plain zip, with no installer. It may not be byte for byte the ModDB build. |
| Community | Discord (linked from ModDB). The [H5 Lobby app](https://www.moddb.com/tutorials/mmh55-preventing-issues-in-multiplayer) is a fan-run online lobby, "heir of the ubi.com servers". It installs MMH5.5 updates by itself. It is interesting for our phase 6. |
| Version shown | Main menu "Version: 5.69k" ([EN texts](https://github.com/Might-Magic-Heroes-5-5/MMH55-Texts-EN/pull/6)). `scripts/H55-Version.lua` in `MMH55-Index.pak` holds `H55_Version` (today `"19d"`). |

**How the installer works.** It is built from MMH5.5's [Packager](https://github.com/Might-Magic-Heroes-5-5/Packager) (`main.iss`, `config/variables.iss`).
- It is an Inno Setup installer, stored without compression, so the download is about the installed size.
- It finds the Steam folder by itself (new since RC19e).
- It copies its files into the game folder. Judging by [MMH5.5's list of a full install](https://github.com/Might-Magic-Heroes-5-5/MMH55/blob/master/debug/directory_map.txt) and RC19d's uninstaller, these are:
  - `bin\`: `MMH55.exe`, `MMH55_64.exe`, `MMH55_Utility.exe`, `MMH55_Utility_64.exe`, `MMH55_Editor(_64).exe`, `MMH55_Mapmixer.exe`, `H5_AIProcess_31j.exe`, `55.dll`, `55e.dll`, `55u.dll`, `ICSharpCode.SharpZipLib.dll`;
  - `data\`: the 10 `MMH55-*.pak` files;
  - `music\MMH55\` (about 32 `.ogg` files), 8 `UserMODs\MMH55-*.h5u`, `video\intro.ogg` and `outro.ogg`;
  - the ARMG manual, plus `unins000.exe` / `unins000.dat`.
- It does **not** replace `H5_Game.exe` or `um.dll`, and it never touches our `dinput8.dll`, `open-homm5.ini` or `bin\open-homm5\`.
- It adds desktop and Start menu shortcuts and an "uninstall" entry in Windows.

**Upgrade, not a clean reinstall.**
- The installer **aborts** with "Another version … is already installed … Please uninstall the existing version" when any of these exists: `bin\H5_AIProcess_31j.exe`, `bin\MMH55_64.exe`, `data\MMH55-Stats.pak`.
- MMH5.5's [upgrade guide](https://www.moddb.com/mods/might-magic-heroes-55/tutorials/mmh55-installation-upgrade) gives the steps:
  1. Run the old uninstaller. For RC19d and older that is `Uninstal.exe` in the game folder; from RC19e on it is `unins000.exe`.
  2. Install fresh.
  3. Start new games: saves from RC19d do not get the new changes.
- Known trap (the author, in the RC19k comments): if the game was running, or the folder was moved, the uninstaller "completes" but removes nothing. The installer then still refuses.
- Our check: the RC19d uninstaller (`Uninstal.exe`, registered in Windows as "Might & Magic Heroes 5.5") carries its own file list. It is in the [appendix](#appendix-files-rc19d-installed). It **includes `data\MMH55-Settings.pak`**.

## 2. What changed from RC19d to RC19k

Sources:
- [RC19e notes](https://www.moddb.com/mods/might-magic-heroes-55/news/mmh55-release-notes-rc19e) (9 Feb 2025);
- [RC19f–k notes](https://www.moddb.com/mods/might-magic-heroes-55/news/mmh55-release-notes-rc19f);
- the text changes ([EN texts](https://github.com/Might-Magic-Heroes-5-5/MMH55-Texts-EN/pulls?q=is%3Apr+is%3Aclosed)).

Only what touches us is listed. Most of the notes are balance changes (skills, creatures, spells).

| Area | RC19e (Feb 2025) | RC19f–k (Sep–Oct 2025) |
|---|---|---|
| **Exes and DLLs** (our byte checks) | "Many technical updates to the executable".<br>For the single-player exe (`MMH55(_64)`): widescreen fixes.<br>For **both exes**:<ul><li>fullscreen + borderless now means a window at monitor size;</li><li>"always active" is forced on and its option removed;</li><li>a crash when loading a combat replay with the creature ATB option in the Utility exes is fixed;</li><li>"crashes in long games due to memory overflow" are fixed.</li></ul>The installer now finds the game folder. | Fixes for "issues with Steam update of the game".<br>The "always active" option is back.<br>NCF works again in the editor exes.<br>"9 more `MMH55-Stats*.pak` slots usable by the engine" (Stats1 to Stats9; the highest number wins): this is `55u.dll` code. |
| **RMG templates, towns, objects** | ARMG:<ul><li>the LoopAlt template changed;</li><li>map gold piles give 2×;</li><li>common resources are more likely (22 → 36%);</li><li>a dimension door snatch fix.</li></ul> | The tier 4–7 military posts are split into **32 new dwelling objects** (one per tier and faction, same models).<br>New military post guards.<br>Witch Temple / Arcane Tower / Mage Vault no longer give level 5 spells.<br>The "100-gold treasure prizes" are removed.<br>**Map script activation is now the default.** Before, random maps needed `MMH55_Mapmixer.exe` to turn the scripts on.<br>The map editor shows all MMH5.5 objects. |
| **Music** | none listed | none listed |
| **PEST / simultaneous turns** | none listed | none listed |
| **Multiplayer** | the crash fixes above | "We have also made amends to the multiplayer experience and hope it will bring less lag" (no details).<br>The map script default also applies to LAN maps.<br>**No desync fix is listed.** |
| **AI** | Combat AI fixes, mostly for the single-player AI (QAI). | Many combat AI fixes, several for the original ToE AI as well (ballista ammo, melee units guarding shooters, war machines as targets, Deflect Missile, Counterspell).<br>Nothing for the adventure-map AI that co-op uses (the original ToE AI in `MMH55_Utility`). |
| **Texts** | many descriptions | "5.69k"; 3 text files in RC19k itself |

## 3. What our DLL depends on (static reading, today's RC19d files)

**How the DLL recognises the co-op exe** (`src/hook/game_build.cpp`). All of these must match, or it logs `game build: unknown` and installs nothing:
- PE date 2008-06-04 (`0x48469f9f`);
- image size `0xdd9000`;
- the AI vftable entry at `0x00e4c8bc`;
- 12 code bytes of the AI turn driver at `0x0086dd40`;
- MMH5.5's `jmp` (`0xEB`) at `0x0081eeb7`.

After that, each hook checks its own call site or table slot before it changes anything.

**MMH5.5 patches the game in two ways.** Both are now explained by MMH5.5's [own patch files](https://github.com/Might-Magic-Heroes-5-5/MMH55/tree/master/assembly/patches/production):

1. **Fixed bytes in the exe** (the 12 spots in [docs/reference-binaries.md](../reference-binaries.md)). These are the patches MMH5.5 could not move into its DLL (`migration_not_possible/`):
   - `0x00ba77a9`/`0x00ba79d4`: RMG creature strength;
   - `0x0090760a`: RMG upgrade-point multipliers. This answers an open question in [rmg.md](../subsystems/rmg.md).
   - `0x00920cb2` + the `Cartographer` string: RMG dimension doors;
   - `0x00847891`, `0x0084c9c0`, `0x0084ca7f`, `0x0084cad8`: more artifacts (97 → 121);
   - `0x00ae29e0`: hero classes 9 → 74;
   - the 32/64-bit flag.
2. **At run time, by `55u.dll`.** New finding: `55u.dll` keeps tables of **202 patched spots** in the exe. It applies them on start-up, by the settings in `MMH55Stats.xml` (in `MMH55-Stats.pak`). Among them:
   - all **8 PEST v1.6 spots** (["Permanent and Endless Simultaneous Turns"](https://github.com/Might-Magic-Heroes-5-5/MMH55/blob/master/assembly/patches/production/migrated/pest.yml), switched by `<Pest enabled="true"/>`; the host ticks it in the lobby);
   - **9 spots in the step-cost function `0x00ca9a70`** (MMH5.5's movement costs, `<Movement>`, `DiagonalMovementFactor` 1.41);
   - the battle camera (`0x007d3650`).

   The XML says: *"Players in multiplayer must use the exact same MMH55Stats file to prevent desyncs!"*

**Where today's runtime patches meet our code.** The new tool `tools/inventory/mmh55_patch_sites.py` reads `55u.dll` and the repo, and lists every game function that holds both a `55u.dll` spot and an address our source uses. Today it finds 2:
- **`0x00ca9a70`, the step cost** (fast paths).
  - Our port calls the game's function, so MMH5.5's costs are kept.
  - But it rewrites one small rule of that function, which cost a diagonal step uses.
  - `path_check.py` proved the result equal under RC19d. A new movement rule in RC19k could break that equality.
- **`0x00998060`, the "next turn" function.** Our source only names it in a comment (player list). Nothing to do.

The camera branch also calls `0x007d3650`, which `55u.dll` patches. That is on purpose (see `docs/subsystems/camera.md` on that branch).

**Which copy of a data file wins.** Among the paks in `data\`, the copy with the **newest date inside the zip** wins (see `docs/subsystems/audio.md` on the camera branch). That matters for the settings pak and the French pak below.

## 4. Risk to each of our mods

"Probably works" assumes the exe is still recognised (first row).

| Part | What it relies on | What RC19k may change | Verdict | How we check |
|---|---|---|---|---|
| **Exe recognition** (`game build:` line) | The fingerprint above | RC19e changed "the executable". A new section, or bytes changed at our fingerprint spots, gives `unknown`: every mod off, game normal. | **Re-check first.** If it fails: one session to add the new fingerprint after `pe_diff.py` shows every function still at the same address. | DLL log; `pe_info.py`, `pe_diff.py` old vs new exe |
| AI turn hook, ai-step trace, net tuning, lockstep drain and pacing, co-op guard, autosave | Vanilla 3.1 code at fixed addresses, each site checked before patching | Only if the exe or `55u.dll` now patches these spots (none today) | Probably works | DLL log lines; `mmh55_patch_sites.py`; LAN robot with checksums |
| LAN discovery | The import table (`WSOCK32`) | Nothing | Works | LAN robot |
| **Fast paths** | Ported pathfinder; the step cost `0x00ca9a70` (patched by `55u.dll`) and our copy of its diagonal rule | New movement rules, or new `55u.dll` spots inside the pathfinder | **Re-check.** AI-only and host-only, so it cannot desync, but it could make the AI's paths differ from MMH5.5's intent. | `mmh55_patch_sites.py`; `path_check.py` |
| PEST / simultaneous turns (MMH5.5's; our player list, notify and robot use it) | 8 spots in `55u.dll`, the XML switch, the lobby checkbox `simultaneus` | Nothing announced | Probably works | LAN robot `--simultaneous` |
| Player list overlay | Direct3D 9 overlay, the window size | RC19e window modes (fullscreen + borderless now gives a window at monitor size) | **Re-check by eye** | Robot screenshots, fullscreen and windowed |
| Notify | Window focus, turn states | The "always active" option is back | Probably works | LAN robot `--notify-setting` |
| Original music | Reads `data.pak` itself; works on any MMH5.5 file that mentions music | New music references | Probably works | `--audio-setting log_music=1`, with and without `original_music=1` |
| RMG templates in the list (`rmg_templates.cpp`), RMG teams (`rmg_teams.cpp`) | The exe's template list code; our `OH-*` files | `55u.dll`'s ARMG hooks changed. With map scripts on by default, MMH5.5's own "humans vs AI" team script (`H55_TeamHumansvsAI`) may now also run on random maps. | **Re-check** | Generate an `OH-` map; LAN robot `--host-rmg teams=humans_vs_ai` |
| Template maker recipes (`tools/rmg`) | Numbers copied from RC19d's templates; dwelling counts by tier | Template retunes; the new per-faction dwellings | Probably works (our templates are frozen on purpose) | Diff `MMH55-Templates.pak` RC19d vs RC19k; one generated map |
| `map_towns.py` | `map.xdb` XML (`AdvMapTown`) | Nothing expected | Works | Run on a generated map |
| Camera (branch) | Calls `0x007d3650` | New combat camera limits | Probably works | `mmh55_patch_sites.py`; a camera run |
| Robot (test tool) | Menu and lobby clicks, the game's "app active" flags | UI files in `MMH55-Frame.pak`, always active | Re-check (the first run shows it) | The runs below |
| Decomp track | The vanilla exe only | Nothing | Not affected | — |
| Plans: in-game generator, one language | `UI/UIGameRoot` in `MMH55-Frame.pak`; `MMH55-Texts-EN.pak` | UI and text files changed | Re-read before that work | Diff the paks |

## 5. Test plan

Everything runs on a **second test copy**. The Steam folder and `C:\Games\H5_test` (today's RC19d robot copy) stay as they are, so we can compare.

**0. Optional early answer (needs approval, about 1 MB).**
- A zip ends with a table of contents that holds each file's size and CRC32 (a checksum). Reading only the last ~1 MB of `manual_setup_RC19k.zip` from GitHub (an HTTP "range" request) tells us which files changed against our RC19d files (we compute their CRC32 locally), before any big download.
- Caveat: this zip may not be the exact ModDB build.

**1. Disk space.**
- C: has **170.2 GB free**; D: has 411.3 GB.
- The game folder is 3.86 GB, and `C:\Games\H5_test` is 3.93 GB.
- New copy (about 4 GB) + installer (0.9 GB) + room for logs: about **5 GB**. No problem.

**2. Make the copy.** Copy `C:\Games\H5_test` (it already has the robot's `robot_home*` profiles) to `C:\Games\H5_test_k`:
```
robocopy C:\Games\H5_test C:\Games\H5_test_k /E
```

**3. Remove RC19d from the copy by hand.**
- **Do not run `Uninstal.exe` in the copy.** It was installed for the Steam folder, and Windows' entry points there. It is not certain that it would act only on the copy.
- Delete the files in the [appendix](#appendix-files-rc19d-installed).
- Move `data\MMH55-Settings - Copie.pak` out of `data\`.
- Delete `data\index.bin` and `data\RMGTemp\` (MMH5.5's [multiplayer guide](https://www.moddb.com/tutorials/mmh55-preventing-issues-in-multiplayer) calls them leftover files that can desync games).

**4. Install RC19k into the copy.**
- Check the MD5 of `MMH55_RC19k.exe`, then run it.
- **Change the target folder to `C:\Games\H5_test_k`**: it proposes the Steam folder. If you forget, it stops anyway, because RC19d is there.
- It adds shortcuts and an uninstall entry that point at the copy. That is harmless; the real upgrade later replaces the entry.

**5. Settings.** Claude does this from the files:
- The copy's new `MMH55-Settings.pak` holds RC19k's defaults.
- Compare three versions:
  - RC19d's default (the "Copie" pak, dated 14 Jul 2024, the RC19d release day);
  - the owner's edit;
  - RC19k's default.
- Then make the 4 changes in RC19k's file (table in [section 7](#7-upgrade-steps-for-the-group)), and pack it again as a zip with `scripts/H55-Settings.lua`.
- Record its SHA-256: this becomes **the group's file**.

**6. Static checks** (Claude; no game run). Compare RC19d against RC19k:
- hashes and `python tools/inventory/pe_info.py` of the new exes and DLLs;
- `python tools/inventory/pe_diff.py <RC19d MMH55_Utility.exe> <RC19k MMH55_Utility.exe>`: same size? same functions? our fingerprint bytes?
- `python tools/inventory/mmh55_patch_sites.py <RC19k 55u.dll>`: compare with today's 202 spots and 2 overlaps;
- `MMH55Stats.xml` (PEST, Movement, camera), `H55-Settings.lua`, `MMH55-Templates.pak`, `UI/UIGameRoot` in `MMH55-Frame.pak`;
- `H55_Version = "19k"`.

**7. The DLL's start-up log.** `python tools\robot\run_ai_test.py --game C:\Games\H5_test_k --turns 1`. Then `bin\open-homm5.log` must show:
```
game build: ToE 3.1 + MMH5.5 patches (MMH55_Utility)
hooked AI turn driver (CPlayerAI vftable slot 0 at 00e4c8bc)
net tuning: ... / fast paths: ... on / lan: ... / co-op timing: guard on ...
```
If it says `unknown build`, stop: the DLL needs the new fingerprint first.

**8. The robot suite** (each with `--game C:\Games\H5_test_k`; run the same on `C:\Games\H5_test` for the RC19d baseline):

| Test | Command (short) | Compare |
|---|---|---|
| AI test | `run_ai_test.py --turns 8` (map A1M4) | ai-step seconds per AI move, AI turn length, no crash |
| Fast paths | `path_check.py` on 1–2 saves made in RC19k (RC19d saves may not load) | "every search equal" |
| LAN + checksums | `run_lan_test.py --seconds 400 --turns 6`, then `lan_report.py --last 2` | world checksums all equal, co-op timing ON, per-move times |
| LAN, simultaneous turns | `run_lan_test.py --seconds 240 --turns 4 --simultaneous --ui-setting trace_players=1 --shot-every 6` | PEST still on; player list states and screenshots |
| RMG templates | the console `rmg RMG/Templates/OH-2v2v2-Lu-Normal.xdb ...` step from `tools/rmg/README.md`, then `map_towns.py` | map builds, towns per zone; our templates in the list; `--host-rmg teams=humans_vs_ai` teams |
| Music | `run_ai_test.py --turns 1 --quick-combat --audio-setting log_music=1`, then the same with `original_music=1` | MMH5.5 tracks vs original tracks |
| Notify | `run_lan_test.py --seconds 300 --turns 4 --notify-setting sound=1 --notify-setting popup=1 --host-step "16|option|simultaneus 0"` | one "your turn" per new turn on each PC |
| Autosave | `run_lan_test.py --seconds 400 --turns 6 --autosave` | save times, checksums |
| Window modes | Owner: the player list in fullscreen, windowed, borderless | overlay size and clicks |

**What to compare, in one list:**
- the `game build:` line and every hook line;
- seconds per AI move and per AI turn (RC19k with map scripts on may be slower);
- checksums and desyncs;
- path_check equality;
- template list and teams;
- music log;
- notify lines;
- player list states;
- crashes.

The results go into this doc, and a robot report into `build/robot/`.

## 6. French texts

- **ModDB:** [MMH5.5: French Translation (RC19i)](https://www.moddb.com/mods/might-magic-heroes-55/addons/mmh55-french-translation-rc19i).
  - File `MMH55-Texts-FR.1.zip`, 373.6 KB (382,568 bytes), MD5 `9f445f66f2779ee1978a300deb714102`, 2 Oct 2025, by Toleman71.
  - A comment from 28 Dec 2025, "Une version RC19K prévue ?", has no answer.
  - Hungarian, Russian, Italian and Polish already have RC19k packs ([addons](https://www.moddb.com/mods/might-magic-heroes-55/addons)). French is behind.
- **GitHub** [MMH55-Texts-FR](https://github.com/Might-Magic-Heroes-5-5/MMH55-Texts-FR):
  - branches `main` and `RC19i`; no tags, no releases;
  - [PR #4 "RC19i release"](https://github.com/Might-Magic-Heroes-5-5/MMH55-Texts-FR/pull/4) is still open, not merged;
  - nothing for RC19j or RC19k.
- **How far behind.** Compared with the English RC19k texts:
  - 2 game texts are missing (the Rune of Battle Rage description, and the new Master of Ice spellbook prediction);
  - the Master of Ice skill text is the older one;
  - 9 combat result texts sit under an older folder name (`UI/CombatReplayScreen/CombatResults/`; English moved them to `UI/CombatResults/`);
  - the version line says 5.69i.

  That is about 11 of ~6,000 files: small, but those texts would show empty or wrong.
- **How it installs:** the `.pak` goes in `data\` **in place of** `MMH55-Texts-EN.pak`, which must be removed. The English RC19k file is newer, so it would win (a ModDB user found the same).
- This also concerns the backlog item "one language for the whole game": with this pack, a French Steam copy becomes French almost everywhere.

## 7. Upgrade steps for the group

**Rules for everyone** (MMH5.5's [multiplayer guide](https://www.moddb.com/tutorials/mmh55-preventing-issues-in-multiplayer) and our own experience):
- Everyone upgrades **on the same day**.
- After the upgrade, the `data\`, `Maps\` and `UserMODs\` folders are the same on every PC: same MMH5.5 version, **the same `MMH55-Settings.pak` file** (compare its SHA-256), no extra paks.
- Everyone plays with the same exe (`MMH55_Utility_64.exe`) and the same version of our DLL.

**Your settings.** Your `MMH55-Settings.pak` will be deleted by the uninstaller. The 4 changes you made in RC19d (found by comparing the two paks in your `data\`):

| Setting | MMH5.5 default (RC19d) | Yours |
|---|---|---|
| `H55_AICheatMode` | 4 | **0** |
| `H55_AmountExtraTownsDwellings` | 1 | **5** |
| `H55_NegativeArtifacts` | 1 | **0** |
| `H55_RPGPotions` | 0 | **1** |

Don't copy the old pak back as it is: RC19k may have added or changed settings. Use the RC19k file with these 4 changes (test plan step 5).

Your current edited file has SHA-256 `3C986E034D7D1156F17BD2367040517F9A88902F732D7E89656CC12FC3C7E7DE`.

**Steps, once the test copy passes.** The owner goes first; then each friend does the same:

1. Close the game and GameRanger. Finish or drop running RC19d games: start new games in RC19k.
2. Back up to a folder outside the game folder:
   - `data\MMH55-Settings.pak`;
   - `bin\open-homm5.ini`;
   - any of your own maps in `Maps\`.
3. Move `data\MMH55-Settings - Copie.pak` out of `data\` (owner only).
4. Run `Uninstal.exe` in the game folder. Then check that these 3 files are gone: `bin\MMH55_64.exe`, `bin\H5_AIProcess_31j.exe`, `data\MMH55-Stats.pak`. If not, delete the files in the appendix by hand.
5. Delete `data\index.bin` and the `data\RMGTemp` folder.
6. Run `MMH55_RC19k.exe` and keep the proposed Steam folder.
7. Put the group's RC19k `MMH55-Settings.pak` (from the owner) into `data\`, replacing the new one.
8. Start `MMH55_Utility_64.exe` once:
   - the main menu shows "5.69k";
   - `bin\open-homm5.log` says `game build: ToE 3.1 + MMH5.5 patches (MMH55_Utility)`. If it says `unknown`, wait for the new open-homm5 DLL.
9. If you made a copy of the exe named `H5_Game.exe` (the GameRanger guide's trick, or the Linux fallback), **make that copy again** from the new `MMH55_Utility_64.exe`. Otherwise you run the old exe with the new data.
10. **Linux friend:** delete the files in the appendix from the Linux game folder, then copy the host's new folder as in [linux.md](../linux.md).
11. Play a short co-op game with AI before a long one.

**Optional idea** (needs the owner's OK, not built): two things our DLL could do.
- Log MMH5.5's version (`H55_Version`) at start-up and compare it in the co-op hello, so a version mismatch shows before the game starts.
- Apply the group's 4 rule settings itself, through the same file-reading hook as the music mod. Upgrades would then not lose them. That is a gameplay change, so it would be a setting, off by default.

## 8. Downloads to approve

| # | What | From | Size | Format | For |
|---|---|---|---|---|---|
| 0 (optional) | The table of contents of `manual_setup_RC19k.zip` only (HTTP range read) | [GitHub release](https://github.com/Might-Magic-Heroes-5-5/MMH55/releases/download/RC19k/manual_setup_RC19k.zip) | about 1 MB | bytes | Knowing early which files changed |
| 1 | `MMH55_RC19k.exe` | [ModDB](https://www.moddb.com/mods/might-magic-heroes-55/downloads/might-magic-heroes-55-rc19k) ("Download Now", then a mirror) | 865.7 MB (907,749,563 bytes), MD5 `61c45d4f0df5c73771007bd507ba895e` | Inno Setup installer | The test copy, then everyone |
| 2 (optional) | `MMH55-Texts-FR.1.zip` (French, RC19i) | [ModDB](https://www.moddb.com/mods/might-magic-heroes-55/addons/mmh55-french-translation-rc19i) | 373.6 KB (382,568 bytes), MD5 `9f445f66f2779ee1978a300deb714102` | zip with one `.pak` | Trying French |
| alt | `manual_setup_RC19k.zip` (whole) | GitHub (link above) | 820,613,073 bytes | zip, no installer | Only if the ModDB file fails |

ModDB downloads go through a page with a button, so the owner downloads them in his own browser. Claude checks the MD5 afterwards.

## Appendix: files RC19d installed

From the file list inside RC19d's `Uninstal.exe`; it matches what is in the game folder. All paths are inside the game folder:
- `bin\`: `55.dll`, `55e.dll`, `55u.dll`, `H5_AIProcess_31j.exe`, `ICSharpCode.SharpZipLib.dll`, `MMH55.exe`, `MMH55_64.exe`, `MMH55_Editor.exe`, `MMH55_Editor_64.exe`, `MMH55_Mapmixer.exe`, `MMH55_Utility.exe`, `MMH55_Utility_64.exe`;
- `data\`: `MMH55-Creaturepedia.pak`, `MMH55-Data.pak`, `MMH55-Frame.pak`, `MMH55-HDTex.pak`, `MMH55-Index.pak`, `MMH55-Settings.pak`, `MMH55-Skillwheel.pak`, `MMH55-Stats.pak`, `MMH55-Templates.pak`, `MMH55-Texts-EN.pak`;
- `music\MMH55\` (all its `.ogg` files, 32 today);
- `UserMODs\`: `MMH55-ARMGMaps-Demo.h5u`, `MMH55-Cam-Cutscenes.h5u`, `MMH55-Cam-Dialogs-EN.h5u`, `MMH55-Cam-Maps.h5u`, `MMH55-Cam-Texts-EN.h5u`, `MMH55-SPMaps-Dialogs-EN.h5u`, `MMH55-SPMaps-Texts-EN.h5u`, `MMH55-SPMaps.h5u`;
- `video\intro.ogg`, `video\outro.ogg` (RC19k puts them back);
- `ARMG Manual V2.7.pdf`, `Uninstal.exe`.

Not MMH5.5's, keep them: `bin\dinput8.dll`, `bin\open-homm5.ini`, `bin\open-homm5.log`, `bin\open-homm5\`, and the `UserMODS\All_campaigns.*` files.
