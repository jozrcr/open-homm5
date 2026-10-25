# Text inventory (counts only)

Made by `python tools/lang/inventory.py` on 2026-10-25 from the owner's install (read only). No game text is in this file. Plan: [../../docs/plans/languages.md](../../docs/plans/languages.md).

Words are counted without the markup tags. The language is a guess from common words and accents; "neutral" means names, numbers or too short to tell (a name like a hero's is often the same in both).

## 1. The layers

| archive | folder | kind | text files | words | French | English | neutral | dates |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| a2p1-texts.pak | data | toe31 | 253 | 4937 | 1 | 127 | 125 | 2007-2008 |
| MMH55-Data.pak | data | mmh55 | 68 | 159 | 0 | 5 | 63 | 2017-2021 |
| MMH55-Index.pak | data | mmh55 | 1 | 2 | 0 | 0 | 1 | 2023-2023 |
| MMH55-Templates.pak | data | mmh55 | 1 | 13 | 0 | 0 | 1 | 2017-2017 |
| MMH55-Texts-EN.pak | data | mmh55 | 5805 | 75213 | 1 | 1847 | 3957 | 2007-2024 |
| texts.pak | data | base | 15349 | 159324 | 7098 | 301 | 7950 | 2006-2007 |
| All_campaigns.data.h5u | usermods | usermods | 1608 | 6177 | 0 | 1 | 1607 | 2006-2007 |
| All_campaigns.texts_en.h5u | usermods | usermods | 5072 | 43222 | 0 | 1526 | 3546 | 2006-2007 |
| MMH55-ARMGMaps-Demo.h5u | usermods | usermods | 575 | 7276 | 0 | 288 | 287 | 2019-2022 |
| MMH55-Cam-Maps.h5u | usermods | usermods | 1608 | 6177 | 0 | 1 | 1607 | 2015-2015 |
| MMH55-Cam-Texts-EN.h5u | usermods | usermods | 5072 | 43222 | 0 | 1526 | 3546 | 2015-2015 |
| MMH55-SPMaps-Dialogs-EN.h5u | usermods | usermods | 971 | 4569 | 0 | 154 | 817 | 2015-2015 |
| MMH55-SPMaps-Texts-EN.h5u | usermods | usermods | 423 | 9796 | 0 | 240 | 183 | 2015-2015 |

## 2. What the player sees today (data\*.pak, newest date wins)

- **19268** different text paths.
- The file used comes from: base 13261, mmh55 5821, toe31 186.
- Its language: French 5620 files (110508 words), English 2232 files (79050 words), neutral 11416 files (16870 words).
- Paths where two archives have the same date (the folder order decides, which may differ on Linux): 22.

## 3. Which English files replace a French one

| layer | text files | same path as a base file | ... newer than it | new paths | used by the game (not replaced again) |
|---|---:|---:|---:|---:|---:|
| toe31 (a2p1-texts.pak) | 253 | 168 | 168 | 85 | 186 |
| mmh55 (MMH55-Data.pak, MMH55-Index.pak, MMH55-Templates.pak, MMH55-Texts-EN.pak) | 5821 | 1970 | 1970 | 3851 | 5821 |

The ToE 3.1 files that are not used are replaced again by MMH5.5's.

## 4. For French: the English the player sees

The files the game uses that are English: every file of an English layer (ToE 3.1, MMH5.5) that has a word and does not read as French, and the base files that read as English. "Sentences" read as English; "short" are names and labels too short for the guess ("Peasant", "Spell power"): most need French too, and the names must match the game's French names (glossary). "Replaces" = a French base file exists on the same path (an older text to start from).

| layer | sentences: files | words | short: files | words | files that replace a French one | all words |
|---|---:|---:|---:|---:|---:|---:|
| base | 300 | 6439 | 0 | 0 | 300 | 6439 |
| mmh55 | 1852 | 70150 | 1807 | 4175 | 1930 | 74325 |
| toe31 | 80 | 2461 | 91 | 480 | 116 | 2941 |
| **total** | 2232 | 79050 | 1898 | 4655 | 2346 | **83705** |

Biggest areas (all layers):

| layer | area | files | words |
|---|---:|---:|---:|
| mmh55 | text/game/heroes | 662 | 29262 |
| mmh55 | text/game/skills | 554 | 11088 |
| mmh55 | text/game/artifacts | 416 | 6682 |
| mmh55 | text/game/townbuildings | 235 | 6303 |
| mmh55 | text/game/creatures | 202 | 4546 |
| base | maps/scenario | 169 | 4395 |
| mmh55 | text/game/scripts | 360 | 4324 |
| mmh55 | text/game/spells | 159 | 3180 |
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
| mmh55 | text/interface/risedead | 9 | 184 |
| mmh55 | text/game/combatstrings | 65 | 166 |
| mmh55 | ui/doc/skillwheel | 84 | 164 |

## 5. For English: the French the player sees

**5620** files, **110508** words, all from `MMH55-Texts-EN.pak, texts.pak`: an English `texts.pak` of the same game version replaces them path for path. Biggest areas:

| area | files | words |
|---|---:|---:|
| maps/scenario | 748 | 21337 |
| text/game/towns | 296 | 13618 |
| maps/multiplayer | 339 | 13509 |
| text/game/creatures | 486 | 10649 |
| text/game/rumours | 115 | 6283 |
| text/game/heroes | 124 | 5682 |
| maps/singlemissions | 149 | 4505 |
| text/game/townbuildings | 236 | 3064 |
| text/game/buildings | 139 | 2369 |
| text/game/dwellings | 160 | 1830 |
| dialogscenes/a2single | 79 | 1758 |
| text/game/spells | 210 | 1752 |
| dialogscenes/a2c1 | 93 | 1641 |
| dialogscenes/a2c2 | 90 | 1599 |
| dialogscenes/a2c3 | 91 | 1515 |

## 6. The format

- Encodings (all layers): utf-16le+bom 36804, other 2.
- Line breaks: no final line break 36702, one line 36088, CRLF 718, ends with a line break 104.
- In the English files to translate (4130): 215 have a placeholder (`<value=...>`, `<hotkey=...>`, printf `%s`), 1 of them printf-style.

Tags in the English files to translate (each must come out unchanged):

| tag | times | files |
|---|---:|---:|
| `<br>` | 2927 | 968 |
| `<color_default>` | 786 | 630 |
| `<value=...>` | 602 | 202 |
| `<color=...>` | 474 | 364 |
| `<credits_body>` | 253 | 1 |
| `<body_bright>` | 217 | 211 |
| `<color_bright>` | 103 | 103 |
| `<credits_h2>` | 62 | 1 |
| `<font size=...>` | 50 | 50 |
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
| `<textamount>` | 1 | 1 |
| `<texttype>` | 1 | 1 |
| `<push>` | 1 | 1 |

## 7. Length

Same path, old French base text vs the MMH5.5/3.1 English text that replaced it (1114 pairs, 20+ characters). MMH5.5 often rewrote and lengthened the text, so this is not a translation ratio: French/English length median 0.88, 90% under 1.44. 38 of the pairs are short (40 characters or less), the labels where a fixed-size box can cut the text. For a real translation ratio see docs/plans/languages.md (MMH5.5's own French: about 1.17).

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

