# One language for the whole game

The owner's request (2026-10-25, [backlog](backlog.md)): the game shows a mix of French and English. Each player should pick French or English, as a setting.

Status (2026-10-25, evening):
- **The French switch is built** (`src/mods/languages*.cpp`, `[lang] texts=fr`). Its unit tests pass. It has **not run in the game yet**: the robot waits until the PC is free (section 4).
- **What MMH5.5's pack leaves out, outside MMH5.5's own texts, is translated:** the 3.1 patch's texts and Ubisoft's English in the multiplayer maps. It is in a private pack (section 3.3).
- **Waiting:** MMH5.5's own gaps wait for RC19k, and the campaigns come later.

## The owner's decisions (2026-10-25)

- **Q11:** "private + ModDB" ([../questions.md](../questions.md)). Nothing translated goes in the public repo or the release. Each player gets MMH5.5's French pack from ModDB. Our gap translations are shared within the group and offered to the MMH5.5 team.
- **French only.** No English option for now: `en` is designed (section 2) but not built, and the Steam English depot is not fetched.
- **Download approved:** the RC19d French pack (done, section 1.4).
- **The group moves to RC19k** (section 1.7).

## Short version

- **Why the mix:** the base game's texts are French (your Steam copy is French). The 3.1 patch's texts and MMH5.5's texts are English, and they replace many of the French ones. Ubisoft also left about 300 texts of its own maps in English inside the French files.
- **Good news:** MMH5.5 already has an official French translation, made by the community and published by the MMH5.5 team. So French mostly means "use their pack".
  - We only translate what it leaves out: about **3,800 words** for co-op, **10,000** with the campaigns (measured with the real pack; the first estimate said 5,000 because it counted the 3.1 patch's campaign texts as co-op). Translating MMH5.5 ourselves would have been about **84,000 words**.
  - Of those 3,800 co-op words, the **~3,000 that do not depend on MMH5.5's version are done**. The ~800 MMH5.5 ones wait for RC19k. About 50 stay as they are on purpose: the product name and internal keys.
- **The switch:** `[lang] texts=game|fr` in `open-homm5.ini`, set by each player. The DLL hands the game the French text files as it reads them, the way the music switch works. No game file changes. `game` (the default) changes nothing.
- **Co-op:** texts look local to each PC. The game objects keep a text's path, never the text itself. A robot LAN test with only the host on `texts=fr` will confirm it before anyone relies on it.

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
- **Measured again with the downloaded RC19d pack** (`inventory.py --fr-pack`; the full counts are in [../../tools/lang/inventory.md](../../tools/lang/inventory.md), section 9):
  - it has 3,646 of the 4,130 English files the player sees, and 62 of those are still the English text;
  - left to translate: 546 files, 10,030 words.
  The estimate from the GitHub listing (10,026 words) was right.
- **The download** (approved by the owner, 2026-10-25):
  - **Source:** the GitHub repository at the commit "RC19d release", <https://github.com/Might-Magic-Heroes-5-5/MMH55-Texts-FR/tree/f1c4ff2c9cde4eb354b9767df8a1b52ce8be30fe>. Commit `f1c4ff2c9cde4eb354b9767df8a1b52ce8be30fe` by dredknight, 2025-02-11. The repo has no tags or releases.
  - **Fetched** as GitHub's zip of that commit, <https://codeload.github.com/Might-Magic-Heroes-5-5/MMH55-Texts-FR/zip/f1c4ff2c9cde4eb354b9767df8a1b52ce8be30fe>. Size 2,706,978 bytes, SHA-256 `946c316ff2c9cc2ffd71bb6ca595aa7436683b36352789c5b0924f245fdd3c4e`. GitHub may build that zip differently later; the commit id is what identifies the content.
  - **Contents:** 5,842 `.txt` files under `mod/` (UTF-8 with BOM, LF), one `.xdb`, two PowerShell scripts and a README. Nothing in it was run; it was read with `python -I`.
  - **Made into a pack** by `tools/lang/build_pack.py`, which does what the repo's `prep_game_package.ps1` does (UTF-16 LE + BOM, CRLF): `MMH55-Texts-FR-RC19d.pak`, 5,842 texts, 1,518,445 bytes, SHA-256 `457576d9817119a792b6eb0163f0535be9809041bef7e041db423740a9a2f546`.
  - **Kept locally** in `build/lang/fr-rc19d/`, in this worktree and in the main checkout. It is ignored by git and never committed.
  - **For the players:** each player downloads the ModDB add-on for their own version instead. Its `.pak` is the same kind of file, and the DLL accepts either.
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
- **Test before anyone relies on it:** a robot LAN game with only the host on `texts=fr` and the client on `texts=game`, 20 AI turns, world checksums compared (`tools/robot/lan_report.py`). The robot must wait until the PC is free (section 4).

### 1.7 Versions: the group moves to RC19k

The French pack must match the MMH5.5 version the players run. A pack for an older version describes that version's numbers, and has nothing for texts added later. From the GitHub file listings (names and fingerprints, no contents, nothing downloaded):

| MMH5.5 English texts | Files | Changed since the previous one |
|---|---:|---:|
| RC19d (installed today) | 5,805 | |
| RC19e | 5,820 | 111 added or changed |
| RC19f | 5,838 | 27 |
| RC19h | 5,838 | 3 (1 removed) |
| **RC19k** (latest, 2025-10-15) | 5,839 | 3 |

French packs that exist:
- **RC19i**, the closest to RC19k. On ModDB: <https://www.moddb.com/mods/might-magic-heroes-55/addons/mmh55-french-translation-rc19i>, `MMH55-Texts-FR.1.zip`, 373.6 KB, licence field "Proprietary". On GitHub it is the `RC19i` branch, open pull request #4 (2025-10-02), head commit `c33031457c3cf46c3a258377707600a46dbcb01a`, 5,875 text files (downloaded since: see below).
- **RC19e** on ModDB (2025-02-13) and the GitHub `main` branch (RC19e plus skillwheel texts).
- **No RC19k French pack** was found on GitHub (branches: `main`, `RC19i`) or in the search results. A ModDB comment asks whether one is coming.

**The RC19i French against the RC19k English:**
- 11 English files are missing from it (538 bytes of text: combat results lines, one rune, one spell effect);
- 5 French files are probably out of date: their English changed after RC19d, and their French did not;
- about 450 files are byte for byte the English: names and numbers, but also real texts (counted below, from the contents).
- So with RC19i, the MMH5.5 gaps for RC19k stay about the size they are today: some hundreds of words.

**The RC19i download** (approved by the owner, 2026-10-25):
- **Source:** the GitHub branch `RC19i`, head commit `c33031457c3cf46c3a258377707600a46dbcb01a` (dredknight, 2025-10-02), <https://github.com/Might-Magic-Heroes-5-5/MMH55-Texts-FR/tree/RC19i>.
- **Zip:** codeload, 2,780,197 bytes, SHA-256 `d6657a0b6e5b9817e70dd8089fbae156f91e06346827c439df59a4c2f39a35bd`. It was put in a new folder, `build/lang/fr-rc19i/`, and read with `python -I`; nothing in it was run.
- **Pack:** `build_pack.py` made `MMH55-Texts-FR-RC19i.pak` from it: 5,875 texts, 1,522,461 bytes, SHA-256 `e8df219cd5118366c5acbc2dda2667e9116782d8ec0a3cc7a97e87a1e1c8118b`.
- **The ModDB file** (`MMH55-Texts-FR.1.zip`, 382,568 bytes, MD5 `9f445f66f2779ee1978a300deb714102`) was not fetched: ModDB refuses our requests. Whether it holds exactly the branch's texts is not checked.

**Measured with the RC19i contents against RC19k's English.** The installed English is RC19d's. For a file whose English did not change from RC19d to RC19k (by the GitHub fingerprints), the installed text is RC19k's:
- **11 files missing.** Two game texts: the Rune of Battle Rage description, and the new Master of Ice spellbook prediction. And the 9 combat results texts, which the French pack keeps under the older folder `UI/CombatReplayScreen/CombatResults/` while the English moved them to `UI/CombatResults/` ([mmh55-rc19k.md](mmh55-rc19k.md), PR #42).
- **5 files probably out of date:** of the 141 English files that changed after RC19d, these 5 were not changed in the French since RC19d.
- **271 files (3,637 words) still English** among the unchanged ones. **210 of them are Creaturepedia texts that RC19d's French pack had in French:** the RC19i branch has the English back. Since their English did not change, our gap pack can take their French from RC19d.
- **So with RC19k,** the MMH5.5 gaps are measured against RC19i: about 11 files, plus whatever is still English (about 60 files once the 210 Creaturepedia texts come back from RC19d), plus the 5 out of date.

**The switch works with whichever pack sits in `lang\fr`:**
- **Any version:** RC19d, RC19i, or a later one. The log names each pack and its version text.
- **A `.zip` is read too.** If it only wraps a `.pak` (the ModDB download), the log says to unzip it.
- **Nothing in `data\` needs removing.** The official install puts the `.pak` in `data\` in place of `MMH55-Texts-EN.pak`, because the newer English file would win there. Our switch serves the French whatever the dates in `data\`.

**Next on versions:**
1. When the RC19k test copy exists, run `inventory.py --fr-pack` with the RC19i pack against it.
2. Translate the MMH5.5 gaps that remain, and take the 210 Creaturepedia texts from RC19d (held today: 773 words for RC19d).
3. Check the gaps again whenever MMH5.5 or its French pack changes. The DLL's "version text" log line shows when the pack and the game differ.

The 3.1 patch's texts and Ubisoft's map texts do not depend on MMH5.5's version, except where a later MMH5.5 starts replacing one of them. `inventory.py` shows that too.

## 2. The design (French built, English not)

### Setting

`open-homm5.ini`, per player, never sent to the others:

```ini
[lang]
texts=game     ; game (default, the game as installed), fr; en: logged "not available yet"
log_texts=0    ; testing: log the first N texts taken from the packs
```

### Files

All of them sit next to the DLL, installed by the player, never in `data\`:

```
bin\open-homm5\lang\fr\MMH55-Texts-FR.pak     MMH5.5's French pack, as downloaded (any name ending in .pak)
bin\open-homm5\lang\fr\open-homm5-fr.pak      our translations of what it leaves out (private)
bin\open-homm5\lang\en\texts.pak              (not built) the English base texts from a player's Steam
```

### How it works (`src/mods/languages.cpp`, built; user notes in [../../src/mods/README.md](../../src/mods/README.md))

1. **When the DLL loads,** it wraps `CWinVFS::OpenFile` (slot 7), from `hooks::InstallEarly`, like the music mod. If the exe is still Steam-encrypted then, it goes in later from `InstallAll`, and the log says it was late. Several mods wrap the same slot; `hookpatch::PatchPointer` chains them.
   - **Before the patch,** the slot must still hold `0x009e0020`, and the code there must be the 3.1 thunk `E9 CB FE FF FF`. The thunk is the same in the vanilla exe and in `MMH55_Utility(_64).exe`.
2. **At the first text the game opens,** it reads the zip directory of each `.pak` in `lang\fr`, in name order (case ignored, the same on Wine). It keeps only the `.txt` files: a language pack can never change an `.xdb`, a script or anything else that decides gameplay. The newest file of a path wins; on equal dates the pack that sorts last wins. Inflating uses the game's own `zlib1.dll`.
3. **`texts=fr`:** when the game opens a `.txt` path that one of these packs has, the game gets a new memory stream with that file. Every other file goes on to the game unchanged.
4. **Safety:** the stream always starts with `FF FE`. A pack's UTF-8 text is converted to UTF-16 with CRLF line ends; UTF-16 big endian and broken UTF-8 are refused. A file that cannot be read falls back to the game's own (logged, at most 20 lines). `texts=game` installs nothing.
5. **Log:** the setting, the packs and their counts, and the version line. When the main menu reads `UI/MainMenu2/Version.txt`, the log puts the pack's version text next to the game's, with a warning if they differ. `log_texts=N` lists the first N texts given; counts follow at 100, 1,000 and every 5,000.
6. **The player list** (`language=auto`) uses French words when `texts=fr`.
7. **`texts=en` (designed, not built):** the DLL would also read the directory of the game's own `data\*.pak` and apply the game's date rule. Where the file the game would use comes from the base `texts.pak` (French), it would hand over the English `texts.pak`'s file instead.

Tested so far: the byte work (paths, encodings, zip directories) in `src/hook/test/languages_test.cpp`; `build.ps1 -Test` and `tools/package/test_scripts.py` (104 of 104) pass. **Not run in the game yet.**

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
| Length warnings | 3 short headers (the hot-seat screens' titles, a Creaturepedia header). In all three the old French text on the same path was just as long, so they fit. |
| Names to decide | 8 of 41 files had a name the glossary did not know, or knew in an older form: "Bloodeyed Cyclops", "Town Gate", the "Tunic of the Prophet" set, "Haven" in the Creaturepedia. Two needed a lookup in the base French texts (found: "Golem Magnétique", "Grâce de Rapidité"). MMH5.5's French pack, or the English base texts, would answer most of these. |
| Grammar risk | 1 placeholder whose number we cannot see (`<value=target>` "a partiellement résisté": a single creature or a stack?). |
| Found on the way | Ubisoft left a developer note ("TODO: ...") in a multiplayer map's text; the base game writes `<value=cost>` in lowercase where 3.1 writes `<value=COST>` (the check holds to the English). |

**Tokens.** About 6,000 tokens in per 1,000 English words: the text, its context, glossary hints and instructions. About 4,000 out (the French text), plus 3,000 to 8,000 tokens of thinking. That makes **about 15,000 to 20,000 fresh tokens per 1,000 words**. Each turn of a worker session also re-reads its starting context (about 50,000 tokens, mostly cached).

On the plan's gauge, this session's 5-hour bar went from 32% to 34% during the glossary and pilot work. Other sessions share that bar, so this is an upper bound.

### 3.2 The full jobs

Measured with the real RC19d pack:

| Job | Words | Status |
|---|---:|---|
| 3.1 patch texts used in co-op (random map screen, combat log, creatures, credits, multiplayer maps) | 1,796 | **done** |
| Ubisoft's English in its multiplayer maps, and a few base texts | 1,160 | **done** |
| Product names and internal keys (`OBJECTIVE_KIND_...`), left as they are on purpose | ~50 | kept |
| MMH5.5 texts its French pack lacks (RC19d: 86 files) | 773 | **held for RC19k** (section 1.7) |
| Campaigns and single maps (Ubisoft's English, 3.1 campaign texts) | 6,263 | later |
| Without any pack: all of MMH5.5 + 3.1 + Ubisoft's English | ~84,000 | not needed |
| English option | 0 (Steam's English texts) | not built (owner: French only) |

### 3.3 The gap translation (done 2026-10-25)

Done by me (Opus) in this session, in 3 batches with `batch.py`. The glossary was rebuilt with `--fr-pack`, so the names come from MMH5.5's own French: 2,602 pairs, 1,720 of them exact.

| Batch | Texts | English words | French words | `check` |
|---|---:|---:|---:|---|
| `b1-maps-base`: Ubisoft's English in the multiplayer maps LS2MC2, LS2MC6, LSMC1 | 73 | 1,105 | 1,095 | 0 errors, 1 length warning (a briefing line) |
| `b2-toe31`: the 3.1 patch (random map screen and its settings, combat log, creature abilities, towns, week texts, multiplayer maps A2M2 to A2M11) | 106 | 1,175 | 1,339 | 0 errors, 14 length warnings |
| `b3-credits-ui`: the 3.1 credits (headings only, names kept), two ability texts, a tooltip | 4 | 676 | 713 | 0 errors |
| **Total** | **183** | **2,956** | **3,147** | **0 tag or placeholder errors** |

Notes:
- **The length warnings** are on the combat log (a scrolling list), on map goals and names, and on short labels. Where the old French text on the same path was as long, it fits (a map goal, the quicksave name). One label was shortened (the random map screen's faction label). The rest needs a look in game.
- **Two of the 3.1 patch's multiplayer maps (A2M3, A2M6) have their story and goals in Russian** in `a2p1-texts.pak`: Nival left them untranslated. They are translated from the Russian.
- **Kept as they are:**
  - files named `...Internal...` (`VS`, `I`, `Random`) and the `OBJECTIVE_KIND_...` keys, which the game may compare;
  - the product name;
  - `ORIGINAL_GAME_NOT_FOUND.txt`, already French (the language guess took it for English).
- **Reused:** the old official French text on the same path, where it says what the new English says (Shagnakh, week of the Angel, the Rage message, map names).
- **Cost:** about 3,000 words in about 20 minutes of this session, with reading, lookups and checks. That fits the estimate of section 3.1.

### 3.4 The private files (never in git)

Kept in `build/lang/` of this worktree and copied to the main checkout's `build/lang/`, so they survive the worktree's cleanup:

| Path | What |
|---|---|
| `build/lang/private-fr/open-homm5-fr.pak` | **Our gap pack**: 183 texts, 51,754 bytes, SHA-256 `d02b4d784844a2e5810fb86f7a87c777dcb220578cde1c1a8ddceceb2acb5b28` |
| `build/lang/private-fr/LISEZMOI-fr.txt` | The install note for the group, in French |
| `build/lang/gap-batches/b1..b3/` | The batches: `en.txt`, `context.txt`, `fr.txt` (the source of the pack) |
| `build/lang/fr-rc19d/` | The RC19d snapshot from GitHub and the pack made from it (for tests) |

**Install, in short** (the note has the details):
1. Put MMH5.5's French `.pak`, from the ModDB add-on for the player's version, unzipped, in `bin\open-homm5\lang\fr\`.
2. Put `open-homm5-fr.pak` there too.
3. Set `texts=fr` under `[lang]` in `open-homm5.ini`.
4. Restart the game.

Never put either pack in `data\`.

## 4. Steps and night batches

Done:
- ~~0. Owner's answers~~ (Q11, French only, the RC19d download, the move to RC19k).
- ~~1. Measure with the real pack~~ (section 1.4).
- ~~2. The DLL switch~~, `texts=fr` (section 2). Unit tests pass.
- ~~3a. The gaps that do not depend on MMH5.5~~ (section 3.3).

Next:

2b. **Robot checks, when the coordinator says the PC is free** (the test copy, `C:\Games\H5_test`, never the Steam folder):
   - put the RC19d pack (`build/lang/fr-rc19d/MMH55-Texts-FR-RC19d.pak`) and `open-homm5-fr.pak` into the test copy's `bin\open-homm5\lang\fr\`, with `texts=fr` and `log_texts=200` in the robot's ini;
   - **French texts in the menus:** the log has `languages: texts=fr: 6024 French texts ...`, the version line, and served texts from `UI/...`. Screenshots of the main menu and of the random map screen (the 3.1 texts: the faction and player labels, the map sizes);
   - **French on the adventure map:** served texts from `Text/Game/...` (heroes, creatures, buildings); a screenshot of a hero or creature tooltip;
   - **the 3.1 texts:** the random map screen and its settings, a combat log line (`Text/CombatLog/...`) after a fight;
   - **the player list:** the log says `player list: French words, as the game's texts`;
   - **a LAN run with only the host on `texts=fr`** (client `texts=game`): about 20 AI turns. The world checksums must stay equal on both copies (`tools/robot/lan_report.py`);
   - **nothing breaks with the folder empty or missing:** the log says so, and the game keeps its own texts.
3b. **MMH5.5's gaps for RC19k** (needs the owner's OK to download the RC19i French pack, section 1.7): measure against the installed RC19k, translate what remains (one short session), then add it to `open-homm5-fr.pak`.
3c. **Campaigns** (6,263 words), whenever the group wants them: the same steps.
3. **Night batch "lang-fr-gaps"** (the method, for 3b, 3c or a bigger job):
   - **Coordinator** (Opus, short): splits `gaps.txt` by area into batches of about 1,200 words. It decides the new names once, into `build/lang/terms.tsv`: the names neither pack nor glossary has. Then it starts the workers.
   - **Workers** (Sonnet, 1 or 2): for each batch, `batch.py make` → write `fr.txt` → `batch.py check` until 0 errors. Batches are kept in the main checkout's `build/lang/batches/` (ignored by git), never committed.
   - **Coordinator again:** checks every batch, reads 10% of them, merges, and packs everything into `open-homm5-fr.pak` with `batch.py pak`.
   - **Report** for the owner: counts, warnings left, the names it decided.
4. **Owner test** in game: a French game, and a co-op game where only some players use `texts=fr`.
5. **Release** per Q11: the DLL feature, the ini setting and the docs in the zip. The text packs go only through the private channel, and our gap translations are offered to the MMH5.5 team (a pull request to `MMH55-Texts-FR`, or a message to dredknight). That needs the owner's OK, since it is posting on the owner's behalf.

## 5. Risks

- **Licence.** MMH5.5's texts and translations are marked "Proprietary", the texts repos have no licence, and the base texts are Ubisoft's. So, by the owner's choice ([Q11](../questions.md)), nothing made from them goes public: the packs stay private.
- **Version drift.** A French pack made for another MMH5.5 version describes that version's numbers. The group moves to RC19k, and no RC19k French pack exists yet (section 1.7). The DLL logs both versions. `inventory.py --fr-pack` shows the gaps again after any update.
- **Co-op.** "Text is local" is likely but not proven. The two-copy test must pass first.
- **A missing `FF FE` mark** gives an empty text in game. `batch.py pack` always writes it, and the DLL checks for it.
- **Names.** MMH5.5 reused some old paths for new things, so a few glossary pairs are wrong. Workers check the meaning, and the French pack's exact names replace the guesses.
- **Length.** French is about 17% longer. Short labels in fixed boxes may be cut. The check flags them, and where an old French text exists on the same path, its length is known to fit.
- **Not every word is in a text file.** A few sentences live in MMH5.5's scripts or in the exe, and maps made by others keep their own language.
- **Wine.** On equal dates (22 paths) the folder order decides. The DLL applies its own fixed rule, so both PCs show the same text.

## 6. Questions for the owner

- ~~Q11~~: answered (private + ModDB).
- **May we download the RC19i French pack** (ModDB, 373.6 KB, or GitHub's `RC19i` branch) to measure the gaps for RC19k? (Section 1.7.)
- **Later:** may we offer our gap translations to the MMH5.5 team (a pull request or a message on the owner's behalf)?

## Tools

`tools/lang/`. All of them only read the game folder, and anything that holds game text goes to `build/` (ignored by git).

| File | What |
|---|---|
| `inventory.py` | Counts by layer and language. Writes `tools/lang/inventory.md` (counts only, committed) and `build/lang/inventory.csv`. With `--fr-pack`, it adds coverage and `build/lang/gaps.txt`. |
| `glossary.py` | English-French names, written to `build/lang/glossary.tsv`. |
| `batch.py` | `make` (English text + context for a list of paths), `check` (tags, placeholders, lines, leftover English, length), `pack` (game files: UTF-16 + BOM, CRLF), `pak` (several batches into one `.pak`, dated now so it wins). |
| `build_pack.py` | A pack from a GitHub snapshot of one of MMH5.5's text repos (UTF-8 under `mod/` → UTF-16 + BOM, CRLF), as their `prep_game_package.ps1` does, without running it. |
| `h5texts.py` | Shared code: reading the archives, the date rule, the markup, the language guess. |
