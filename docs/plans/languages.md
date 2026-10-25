# One language for the whole game

The owner's request (2026-10-25, [backlog](backlog.md)): the game shows a mix of French and English. Each player should pick French or English, as a setting.

Status: **plan and pilot done, nothing built in the DLL yet.** The next steps need the owner: a decision on distribution ([Q11](../questions.md)) and two downloads to approve.

## Short version

- **Why the mix:** the base game's texts are French (your Steam copy is French). The 3.1 patch's texts and MMH5.5's texts are English, and they replace many of the French ones. Ubisoft also left about 300 texts of its own maps in English inside the French files.
- **Good news:** MMH5.5 already has an official French translation, made by the community and published by the MMH5.5 team. There is one for **RC19d**, the version you have installed. So French mostly means "use their pack". We only translate what it leaves out: about **5,000 words** for co-op, **10,000** with the campaigns. Translating MMH5.5 ourselves would have been about **84,000 words**.
- **English** needs the English base texts. They come from your own Steam copy: Steam's English download for this game (173.5 MB). It can go to a separate folder without touching the game.
- **The switch:** `[lang] texts=game|fr|en` in `open-homm5.ini`, set by each player. The DLL hands the game the chosen text files as it reads them, the way the music switch works. No game file changes. `game` (the default) changes nothing.
- **Co-op:** texts look local to each PC. The game objects keep a text's path, never the text itself. A robot test with one French copy and one English copy will confirm it before release.

## 1. Findings

### 1.1 The layers

Counted by `tools/lang/inventory.py` from your install, read only. Full counts: [../../tools/lang/inventory.md](../../tools/lang/inventory.md).

| Archive | What | Text files | Words | Language |
|---|---|---:|---:|---|
| `texts.pak` | Heroes V + ToE base texts | 15,349 | 159,324 | French (Steam language), plus about 300 Ubisoft map texts left in English |
| `a2p1-texts.pak` | ToE 3.1 patch | 253 | 4,937 | English |
| `MMH55-Texts-EN.pak` | MMH5.5 (RC19d) | 5,805 | 75,213 | English |
| `MMH55-Data.pak`, `-Index`, `-Templates` | MMH5.5, a few interface texts | 70 | 174 | English |
| `UserMODS\*.h5u` | campaigns and single-player maps (MMH5.5 and "All campaigns", partly the same files twice) | about 15,300 | about 120,000 | English |

The game uses **19,268** different text paths. Today 5,620 of them are French, 2,232 English, and 11,416 are names or numbers too short to tell.

**How the game picks a file** ([../subsystems/texts.md](../subsystems/texts.md)): when two archives have the same path, the **newer file inside the zip wins**, whatever the archive's name (`0x009def50`). MMH5.5's 1,970 English files that sit on the same path as a French one are all newer, so they all win. 22 paths have two files with exactly the same date. There the order of the folder listing decides, and Wine may list folders in another order.

### 1.2 The format

- Every text is its own small file in **UTF-16 LE with a byte order mark** (`FF FE`). The mark is required: the game's reader (`0x004c7310`) returns an empty text without it.
- **Tags** must survive translation unchanged. In the English texts to translate: `<br>` (2,927 times), `<color_default>` (786), `<value=...>` (602), `<color=...>` (474), `<body_bright>` (217), `<color_bright>` (103), and a few others.
- **Placeholders** that the game fills in: 215 files have one. Almost all are `<value=name>` (for example "partially resisted `<value=spell>`"), plus one `<hotkey=...>` and one printf-style `%s` (MMH5.5's Witch Hut first visit). Any other `%` is just a percent sign.
- **Length:** French is about **17% longer** than English. Measured on MMH5.5's own French translation, from the file sizes it publishes: median ×1.17, 90% under ×1.36. Labels in fixed-size boxes are the risk.

### 1.3 How the game reads texts

Read from the code, not yet watched in a running game. Details in [../subsystems/texts.md](../subsystems/texts.md).

- A text is read **the first time it is shown**, through `0x004c7430` (383 call sites). It is then kept in a cache until the game closes. So a language change needs a restart.
- The read goes through the game's file system, ending in **`CWinVFS::OpenFile`**, slot 7 of the vtable at `0x00e66500` (cell `0x00e6651c`). Our music, templates and player list mods already wrap that function. So the text switch needs no new kind of hook.
- The game objects store a text's **path**, not the text (a `...FileRef` field is a string). The game rules, the AI and the world never read the words.

### 1.4 Existing translations (web search, nothing downloaded)

**French for MMH5.5: yes, an official one.**

- **On ModDB**, as add-ons of the mod:
  - "MMH5.5: French Translation (RC19i)", <https://www.moddb.com/mods/might-magic-heroes-55/addons/mmh55-french-translation-rc19i>. File `MMH55-Texts-FR.1.zip`, 373.6 KB. Credited to Toleman71, uploaded by dredknight, a member of the MMH5.5 team. Licence field: **"Proprietary"**. You unzip it and put the `.pak` in `data\`.
  - Older versions on ModDB: RC19e (2025-02-13), **RC19d (2024-07-23)**, RC19c, RC19b and RC9c. The RC19b page says it is about 99% translated and that a few sentences are "hardcoded".
  - ModDB blocks our page reader (HTTP 403), so the RC19i details above come from search results.
- **On GitHub**, the source: <https://github.com/Might-Magic-Heroes-5-5/MMH55-Texts-FR>, "French translation for MMH55 and the skillwheel app".
  - The repo has no licence file.
  - Commits: "RC19d release" (2025-02-11), RC19e, skillwheel changes. The RC19i update is still an open pull request (#4, 2025-10-02).
  - Its script builds `MMH55-FR-TEXT.pak`.
  - The English texts have a repo too: <https://github.com/Might-Magic-Heroes-5-5/MMH55-Texts-EN>.
- **Your installed version is RC19d.** `MMH55-Index.pak` says `H55_Version = "19d"`, and the menu text says "Version: 5.69d" (2024-07-13). The RC19d French pack matches it. RC19i is two releases newer than your install. The newest MMH5.5 is **RC19k** (2025-10-13).
- **How complete it is.** Measured from GitHub's file listing (names, sizes and fingerprints only, no file contents) against your English pack:
  - The RC19d French has 5,842 text files.
  - It covers 5,796 of your 5,806 English entries. The 10 missing ones are combat results lines and one rune description.
  - About 61 of its files with real sentences (623 words) are still the English text.
  - It also re-translates 13 of the 3.1 patch's files and 19 base files.
  - It does **not** cover the 70 MMH5.5 texts that sit in `MMH55-Data.pak` and the other MMH5.5 paks.
- **Campaigns:** "MMH5.5: French Dialogues & Subtitles (All)" (`MMH55-Dialogs-Subs-FR.zip`, 2017, "Proprietary"), for `UserMODS`. Not needed for co-op.
- **Other languages:** the team also hosts German, Spanish, Hungarian, Italian, Polish, Russian, Czech and Chinese. Its translation guide asks translators to send the result to them so they can publish it: <https://www.moddb.com/mods/might-magic-heroes-55/tutorials/mod-translation-guide>.
- **French forums** (Archangel Castle and others): no separate French translation found.

**English base texts: from your own Steam.**

- The game's Steam download is split into parts, "depots". The French install has two: `15371` (shared content, 2.07 GB) and `15373` (French, 186.7 MB).
- **`15372` is English, 173.5 MB.** It holds `data\texts.pak` (5.84 MiB), `data\sound.pak` (English voices, 97.75 MiB) and the manuals. Sources: api.steamcmd.net, and SteamDB through search results.
- **Way 1 (recommended):** Steam's console command `download_depot 15370 15372`. You open the console with `steam://open/console` in a browser or Windows' Run box. Steam puts the files in `Steam\steamapps\content\app_15370\depot_15372\`, a separate folder, so the installed game is not touched. Then copy only `texts.pak` into `bin\open-homm5\lang\en\`. This is a community-documented command, not an official one.
- **Way 2 (not recommended):** switch the game's language in Steam (Properties, Language). That rewrites the installed game's language files, voices included, for every program that uses that folder.
- **A bonus:** the English `texts.pak`, paired path by path with the French one, gives the complete official English-French list of names. Ours today (below) is partial.

### 1.5 The glossary

`tools/lang/glossary.py` writes `build/lang/glossary.tsv` (local only, it holds game texts). It pairs English and French names that stand for the same thing:

1. **Same path:** MMH5.5's English file and the base French file on the same path (`.../Spells/.../Name.txt`).
2. **Same object:** where MMH5.5 moved an object's name to a new file, the object's `.xdb` in `data.pak` and in MMH5.5 point at the French and at the English file.
3. **Folder names:** the base game's name files sit in folders named in English (`Righteous_Might/Name.txt`).
4. **`--fr-pack`:** MMH5.5's French pack, once you download it. It gives exact pairs, and these replace the rest.

The result: 1,656 pairs. 442 of them are marked "low" and are not offered to translators: single words that don't look alike, often a path MMH5.5 reused for something else ("Creatures" paired with "Paysans"). In the pilot, 31 hints were offered and 4 were misleading (13%), against about 1 in 3 before that filter. Those were an old name ("Golem d'Obsidienne" for what is now Magnetic Golems), a skill MMH5.5 renamed, and a hero name ("Brand") matched inside a normal word.

### 1.6 Is text local to each PC in co-op?

Probably yes. Not yet tested.

- **For:**
  - The game objects keep a text's path, not its words.
  - Texts are read only for display.
  - Our co-op guard only exchanges the segment length, never settings.
  - The MMH5.5 team ships each language as a separate pack and says nothing about needing the same one to play together.
- **Unknown:**
  - whether the world checksum (`mp_check_crc`) includes any name copied into the world, for example a hero's name;
  - whether Lua scripts compare texts.
- **Test before release:** a robot LAN game with one copy on `texts=fr` and the other on `texts=game`, 20 AI turns, world checksums compared (`tools/robot/lan_report.py`). The robot must wait: the owner is using this PC.

## 2. The recommended design

### Setting

`open-homm5.ini`, per player, never sent to the others:

```ini
[lang]
texts=game   ; game (default, the game as installed), fr, en
```

### Files

All of them sit next to the DLL, installed by the player, never in `data\`:

```
bin\open-homm5\lang\fr\MMH55-Texts-FR.pak     MMH5.5's French pack, as downloaded (any name)
bin\open-homm5\lang\fr\open-homm5-fr.pak      our translations of what it leaves out
bin\open-homm5\lang\en\texts.pak              the English base texts from your Steam (depot 15372)
```

### How it works (`src/mods/languages.cpp`, to write)

1. **When the DLL loads,** it wraps `CWinVFS::OpenFile` (slot 7), like the music mod (`InstallOriginalMusicEarly`). Texts are read from the splash screen on. Several mods can wrap the same slot: `hookpatch::PatchPointer` chains them.
2. **At the first file the game opens,** it reads the list of files in each archive of its language folder. The zip reading is the music mod's code, moved into a shared file, and inflating uses the game's own `zlib1.dll`. It keeps only the `.txt` files. A language pack can never change an `.xdb`, a script or anything else that decides gameplay.
3. **`texts=fr`:** when the game opens a `.txt` path that a pack in `lang\fr` has, the game gets that file instead. Our gap pack wins over MMH5.5's pack only for the paths it lists, and it lists only files missing from that pack.
4. **`texts=en`:** the DLL also reads the list of files in the game's own `data\*.pak`, using the game's date rule. When the file the game would use comes from the base `texts.pak` (French), it hands over the English `texts.pak`'s file instead. MMH5.5's and the 3.1 patch's files are English already and stay.
5. **Safety:** a replacement must start with `FF FE`, or it is skipped (the game would show an empty text). If an archive cannot be read, it is skipped and the log says why. `texts=game` installs nothing.
6. **Log:** the setting, the packs found, their counts, and the MMH5.5 version each pack says it is for (its `UI/MainMenu2/Version.txt`) next to the installed one. For example: "French pack for 5.69d, installed 5.69d".
7. **The player list** already picks its own words from a game text (`red_player.txt`). If this hook goes in first, `language=auto` follows the chosen language without any change.

Not covered: maps made by others (`.h5m`) keep their own texts, the few sentences written inside MMH5.5's scripts or the exe stay as they are, and the voices don't change.

## 3. Effort and cost

### 3.1 The pilot (measured)

41 files that stand for the real work, made with `tools/lang/batch.py make`. I translated them, checked them with `batch.py check`, and packed them as game files (local only, `build/lang-pilot/`):

- 10 MMH5.5 texts the French pack lacks, and 4 it left in English;
- 10 texts of the 3.1 patch;
- 3 English texts of Ubisoft's own multiplayer maps;
- 14 typical MMH5.5 texts: spell, skill, artifact set, building, creature ability, two hero biographies, two script windows full of `<value=...>`, interface labels.

| Measure | Result |
|---|---|
| Words | 955 English, 1,031 French, length ×1.11 |
| Tags and placeholders | **0 errors**. The check does catch them: a planted lost tag, a changed `<value=COST>` and an English line were all reported. |
| Length warnings | 3 short headers (for example "Hot-Seat Options" → "Options du mode Hot Seat"). In all three the old French text on the same path was just as long, so they fit. |
| Names to decide | 8 of 41 files had a name the glossary did not know, or knew in an older form: "Bloodeyed Cyclops", "Town Gate", the "Tunic of the Prophet" set, "Haven" in the Creaturepedia. Two needed a lookup in the base French texts (found: "Golem Magnétique", "Grâce de Rapidité"). MMH5.5's French pack, or the English base texts, would answer most of these. |
| Grammar risk | 1 placeholder whose number we cannot see (`<value=target>` "a partiellement résisté": a single creature or a stack?). |
| Found on the way | Ubisoft left a developer note ("TODO: ...") in a multiplayer map's text; the base game writes `<value=cost>` in lowercase where 3.1 writes `<value=COST>` (the check holds to the English). |

**Tokens.** About 6,000 tokens in per 1,000 English words: the text, its context, glossary hints and instructions. About 4,000 out (the French text), plus 3,000 to 8,000 tokens of thinking. That makes **about 15,000 to 20,000 fresh tokens per 1,000 words**. Each turn of a worker session also re-reads its starting context (about 50,000 tokens, mostly cached).

On the plan's gauge, this session's 5-hour bar went from 32% to 34% during the glossary and pilot work. Other sessions share that bar, so this is an upper bound.

### 3.2 The full jobs

| Job | Words | Batches of ~1,200 words | Sonnet worker sessions | Fresh tokens (approx.) |
|---|---:|---:|---:|---:|
| **French with MMH5.5's pack: co-op gaps** (3.1 patch 2,814; MMH5.5 gaps 773; Ubisoft's English in multiplayer maps 1,105; other 276) | ~5,000 | 4 | 1 | ~100,000 |
| ... plus campaigns and single maps (Ubisoft's English map and dialogue texts, 5,058) | ~10,000 | 8 | 2 | ~200,000 |
| Without any pack: all of MMH5.5 + 3.1 + Ubisoft's English | ~84,000 | 70 | 8 to 10 | ~1.5 million |
| English option | 0 (Steam's English texts) | | | |

The first row is one night batch, with time left. Upgrading MMH5.5 later adds little:

- From the RC19i French to RC19k, MMH5.5's English changed in only 3 files (RC19h to RC19k).
- From your RC19d to RC19k, it changed in at least 142 files. GitHub's comparison stops counting at 300 files, skillwheel files included.

## 4. Steps and night batches

0. **Owner** (approvals):
   - answer [Q11](../questions.md);
   - approve downloading MMH5.5's French pack for RC19d (ModDB add-on, about 0.4 MB, or the GitHub repo at its "RC19d release" commit);
   - approve getting the English depot 15372 (173.5 MB, `download_depot`, outside the game folder);
   - say which MMH5.5 version the group will play (RC19d now; RC19k is out).
1. **Measure** with the real pack (a day session, small):
   - `python tools/lang/inventory.py --fr-pack <pack>` gives the exact gaps and writes `build/lang/gaps.txt`;
   - `glossary.py --fr-pack <pack>` gives exact names.
2. **DLL** (a day session): `src/mods/languages.cpp` as in section 2, with the zip code shared with the music mod. Tests:
   - a robot run on `texts=fr` and one on `texts=en`. The log counts the texts served; screenshots of the main menu, a town and a skill tooltip.
   - the two-copy LAN test of section 1.6.
3. **Night batch "lang-fr-gaps"** (after Q11):
   - **Coordinator** (Opus, short): splits `gaps.txt` by area into batches of about 1,200 words. It decides the new names once, into `build/lang/terms.tsv`: the names neither pack nor glossary has. Then it starts the workers.
   - **Workers** (Sonnet, 1 or 2): for each batch, `batch.py make` → write `fr.txt` → `batch.py check` until 0 errors. Batches are kept in the main checkout's `build/lang/batches/` (ignored by git), never committed.
   - **Coordinator again:** checks every batch, reads 10% of them, merges, and packs everything into `open-homm5-fr.pak` with `batch.py pack` and a zip.
   - **Report** for the owner: counts, warnings left, the names it decided.
4. **Owner test** in game: a French game and an English game, and a co-op game with mixed choices.
5. **Release** per Q11's answer: the DLL feature in the zip, and the text packs the way the owner decided.

## 5. Risks

- **Licence.** MMH5.5's texts and translations are marked "Proprietary", the texts repos have no licence, and the base texts are Ubisoft's. So nothing made from them goes public until the owner decides ([Q11](../questions.md)).
- **Version drift.** A French pack made for another MMH5.5 version describes that version's numbers. The DLL logs both versions. `inventory.py --fr-pack` shows the gaps again after any update.
- **Co-op.** "Text is local" is likely but not proven. The two-copy test must pass first.
- **A missing `FF FE` mark** gives an empty text in game. `batch.py pack` always writes it, and the DLL checks for it.
- **Names.** MMH5.5 reused some old paths for new things, so a few glossary pairs are wrong. Workers check the meaning, and the French pack's exact names replace the guesses.
- **Length.** French is about 17% longer. Short labels in fixed boxes may be cut. The check flags them, and where an old French text exists on the same path, its length is known to fit.
- **Not every word is in a text file.** A few sentences live in MMH5.5's scripts or in the exe, and maps made by others keep their own language.
- **Wine.** On equal dates (22 paths) the folder order decides. The DLL applies its own fixed rule, so both PCs show the same text.

## 6. Questions for the owner

- **Q11** in [../questions.md](../questions.md): where may translations live (public repo and release, or private)?
- The approvals in step 0: the two downloads, and which MMH5.5 version the group plays.
- Does the Linux PC's Steam copy run in English? Its `texts.pak` could then serve, with no download.

## Tools

`tools/lang/`. All of them only read the game folder, and anything that holds game text goes to `build/` (ignored by git).

| File | What |
|---|---|
| `inventory.py` | Counts by layer and language. Writes `tools/lang/inventory.md` (counts only, committed) and `build/lang/inventory.csv`. With `--fr-pack`, it adds coverage and `build/lang/gaps.txt`. |
| `glossary.py` | English-French names, written to `build/lang/glossary.tsv`. |
| `batch.py` | `make` (English text + context for a list of paths), `check` (tags, placeholders, lines, leftover English, length), `pack` (game files: UTF-16 + BOM, CRLF). |
| `h5texts.py` | Shared code: reading the archives, the date rule, the markup, the language guess. |
