# Text inventory (counts only)

Made by `python tools/lang/inventory.py` on 2026-10-25 from the game copy `C:\Games\H5_test_k` (read only). No game text is in this file. Plan: [../../docs/plans/languages.md](../../docs/plans/languages.md).

Words are counted without the markup tags. The language is a guess from common words and accents; "neutral" means names, numbers or too short to tell (a name like a hero's is often the same in both).

## 1. The layers

| archive | folder | kind | text files | words | French | English | neutral | dates |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| a2p1-texts.pak | data | toe31 | 253 | 4937 | 1 | 127 | 125 | 2007-2008 |
| MMH55-Data.pak | data | mmh55 | 68 | 159 | 0 | 5 | 63 | 2025-2025 |
| MMH55-Index.pak | data | mmh55 | 1 | 2 | 0 | 0 | 1 | 2025-2025 |
| MMH55-Templates.pak | data | mmh55 | 1 | 13 | 0 | 0 | 1 | 2025-2025 |
| MMH55-Texts-EN.pak | data | mmh55 | 5839 | 75484 | 1 | 1862 | 3976 | 2025-2025 |
| texts.pak | data | base | 15349 | 159324 | 7098 | 301 | 7950 | 2006-2007 |
| All_campaigns.data.h5u | usermods | usermods | 1608 | 6177 | 0 | 1 | 1607 | 2006-2007 |
| All_campaigns.texts_en.h5u | usermods | usermods | 5072 | 43222 | 0 | 1526 | 3546 | 2006-2007 |
| MMH55-ARMGMaps-Demo.h5u | usermods | usermods | 575 | 7276 | 0 | 288 | 287 | 2019-2022 |
| MMH55-Cam-Maps.h5u | usermods | usermods | 1608 | 6177 | 0 | 1 | 1607 | 2015-2015 |
| MMH55-Cam-Texts-EN.h5u | usermods | usermods | 5072 | 43222 | 0 | 1526 | 3546 | 2015-2015 |
| MMH55-SPMaps-Dialogs-EN.h5u | usermods | usermods | 971 | 4569 | 0 | 154 | 817 | 2015-2015 |
| MMH55-SPMaps-Texts-EN.h5u | usermods | usermods | 423 | 9796 | 0 | 240 | 183 | 2015-2015 |

## 2. What the player sees today (data\*.pak, newest date wins)

- **19273** different text paths.
- The file used comes from: base 13232, mmh55 5855, toe31 186.
- Its language: French 5606 files (110310 words), English 2247 files (79298 words), neutral 11420 files (16866 words).
- Paths where two archives have the same date (the folder order decides, which may differ on Linux): 0.

## 3. Which English files replace a French one

| layer | text files | same path as a base file | ... newer than it | new paths | used by the game (not replaced again) |
|---|---:|---:|---:|---:|---:|
| toe31 (a2p1-texts.pak) | 253 | 168 | 168 | 85 | 186 |
| mmh55 (MMH55-Data.pak, MMH55-Index.pak, MMH55-Templates.pak, MMH55-Texts-EN.pak) | 5855 | 1999 | 1999 | 3856 | 5855 |

The ToE 3.1 files that are not used are replaced again by MMH5.5's.

## 4. For French: the English the player sees

The files the game uses that are English: every file of an English layer (ToE 3.1, MMH5.5) that has a word and does not read as French, and the base files that read as English. "Sentences" read as English; "short" are names and labels too short for the guess ("Peasant", "Spell power"): most need French too, and the names must match the game's French names (glossary). "Replaces" = a French base file exists on the same path (an older text to start from).

| layer | sentences: files | words | short: files | words | files that replace a French one | all words |
|---|---:|---:|---:|---:|---:|---:|
| base | 300 | 6439 | 0 | 0 | 300 | 6439 |
| mmh55 | 1867 | 70398 | 1824 | 4198 | 1957 | 74596 |
| toe31 | 80 | 2461 | 91 | 480 | 116 | 2941 |
| **total** | 2247 | 79298 | 1915 | 4678 | 2373 | **83976** |

Biggest areas (all layers):

| layer | area | files | words |
|---|---:|---:|---:|
| mmh55 | text/game/heroes | 662 | 29266 |
| mmh55 | text/game/skills | 554 | 11072 |
| mmh55 | text/game/artifacts | 416 | 6686 |
| mmh55 | text/game/townbuildings | 235 | 6303 |
| mmh55 | text/game/creatures | 207 | 4620 |
| base | maps/scenario | 169 | 4395 |
| mmh55 | text/game/scripts | 360 | 4324 |
| mmh55 | text/game/spells | 160 | 3189 |
| mmh55 | text/game/buildings | 204 | 2432 |
| mmh55 | ui/doc/creaturepedia | 277 | 2113 |
| base | maps/multiplayer | 73 | 1105 |
| toe31 | maps/scenario | 31 | 788 |
| toe31 | ui/credits | 1 | 621 |
| mmh55 | gamemechanics/rpgstats/artifactsets | 27 | 389 |
| mmh55 | text/game/towns | 38 | 371 |
| mmh55 | ui/mpwait/playerslist | 8 | 342 |
| toe31 | maps/multiplayer | 17 | 336 |
| base | dialogscenes/a2c1 | 16 | 318 |
| mmh55 | ui/customgamecreate | 17 | 308 |
| toe31 | text/game/creatures | 8 | 295 |
| mmh55 | text/weektexts | 20 | 257 |
| mmh55 | text/game/dwellings | 24 | 205 |
| mmh55 | text/game/combatstrings | 78 | 186 |
| mmh55 | text/interface/risedead | 9 | 184 |
| mmh55 | ui/consts/gameoptions | 13 | 175 |

## 5. For English: the French the player sees

**5606** files, **110310** words, all from `MMH55-Texts-EN.pak, texts.pak`: an English `texts.pak` of the same game version replaces them path for path. Biggest areas:

| area | files | words |
|---|---:|---:|
| maps/scenario | 748 | 21337 |
| text/game/towns | 296 | 13618 |
| maps/multiplayer | 339 | 13509 |
| text/game/creatures | 483 | 10582 |
| text/game/rumours | 115 | 6283 |
| text/game/heroes | 124 | 5682 |
| maps/singlemissions | 149 | 4505 |
| text/game/townbuildings | 236 | 3064 |
| text/game/buildings | 139 | 2369 |
| text/game/dwellings | 160 | 1830 |
| dialogscenes/a2single | 79 | 1758 |
| text/game/spells | 209 | 1744 |
| dialogscenes/a2c1 | 93 | 1641 |
| dialogscenes/a2c2 | 90 | 1599 |
| dialogscenes/a2c3 | 91 | 1515 |

## 6. The format

- Encodings (all layers): utf-16le+bom 36838, other 2.
- Line breaks: no final line break 36734, one line 36119, CRLF 721, ends with a line break 106.
- In the English files to translate (4162): 229 have a placeholder (`<value=...>`, `<hotkey=...>`, printf `%s`), 1 of them printf-style.

Tags in the English files to translate (each must come out unchanged):

| tag | times | files |
|---|---:|---:|
| `<br>` | 2931 | 971 |
| `<color_default>` | 798 | 642 |
| `<value=...>` | 621 | 216 |
| `<color=...>` | 474 | 364 |
| `<credits_body>` | 253 | 1 |
| `<body_bright>` | 219 | 213 |
| `<color_bright>` | 103 | 103 |
| `<credits_h2>` | 62 | 1 |
| `<font size=...>` | 51 | 51 |
| `<credits_h1>` | 13 | 1 |
| `<center>` | 12 | 12 |
| `<hotkey=...>` | 12 | 12 |
| `<body>` | 11 | 9 |
| `<h1>` | 10 | 10 |
| `<value_name>` | 9 | 9 |
| `<left>` | 7 | 7 |
| `<value_value>` | 7 | 7 |
| `<h4>` | 6 | 5 |
| `<h3_text>` | 3 | 2 |
| `<h2>` | 3 | 3 |
| `<color_negative>` | 2 | 2 |
| `<h3_block>` | 2 | 2 |
| `<tooltip_default>` | 2 | 1 |
| `<color_title>` | 2 | 2 |
| `<justify>` | 1 | 1 |
| `<h3_bright>` | 1 | 1 |
| `<combat_flying_signs>` | 1 | 1 |
| `<damage>` | 1 | 1 |
| `<textamount>` | 1 | 1 |
| `<texttype>` | 1 | 1 |

## 7. Length

Same path, old French base text vs the MMH5.5/3.1 English text that replaced it (1125 pairs, 20+ characters). MMH5.5 often rewrote and lengthened the text, so this is not a translation ratio: French/English length median 0.88, 90% under 1.44. 40 of the pairs are short (40 characters or less), the labels where a fixed-size box can cut the text. For a real translation ratio see docs/plans/languages.md (MMH5.5's own French: about 1.17).

## 8. UserMODS (campaigns, single maps)

Not needed for co-op on multiplayer or random maps. Their archives compete with data\ by date too.

| archive | text files | words | French | English | neutral | newer than data\ |
|---|---:|---:|---:|---:|---:|---:|
| All_campaigns.data.h5u | 1608 | 6177 | 0 | 1 | 1607 | 0 |
| All_campaigns.texts_en.h5u | 5072 | 43222 | 0 | 1526 | 3546 | 0 |
| MMH55-ARMGMaps-Demo.h5u | 575 | 7276 | 0 | 288 | 287 | 0 |
| MMH55-Cam-Maps.h5u | 1608 | 6177 | 0 | 1 | 1607 | 0 |
| MMH55-Cam-Texts-EN.h5u | 5072 | 43222 | 0 | 1526 | 3546 | 0 |
| MMH55-SPMaps-Dialogs-EN.h5u | 971 | 4569 | 0 | 154 | 817 | 0 |
| MMH55-SPMaps-Texts-EN.h5u | 423 | 9796 | 0 | 240 | 183 | 14 |

## 9. The French pack MMH55-Texts-FR-RC19i.pak

- 5875 text files; it has 3677 of the 4162 English files the player sees; 271 of those are still the English text. Left to translate: 756 files, 12072 words (list: gaps.txt in the --detail folder).

