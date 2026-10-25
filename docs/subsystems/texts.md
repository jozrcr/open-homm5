# Texts: how the game finds and reads them

Vanilla ToE 3.1 addresses (also `MMH55_Utility.exe`). Found 2026-10-25 by reading Ghidra's output (`tools/inventory/dump.ps1`, `xrefs.ps1`); not yet checked in a running game. The plan that uses this is [../plans/languages.md](../plans/languages.md).

## Where texts are

- Each text is one small `.txt` file: one name, one description, one button label. Paths such as `Text/Game/Spells/Combat/Haste/Name.txt`.
- The files sit in zip archives: `data\*.pak`, and `UserMODs\*.h5u` (mods, campaigns, single maps). `texts.pak` is the base game in the language of the Steam install (French on the owner's PC), `a2p1-texts.pak` holds the 3.1 patch's texts, MMH5.5 adds `MMH55-Texts-EN.pak` and a few texts in `MMH55-Data.pak`, `MMH55-Index.pak` and `MMH55-Templates.pak`. Counts: [../../tools/lang/inventory.md](../../tools/lang/inventory.md).
- The game objects (`.xdb` files) do not hold the text, only its path: `<NameFileRef href="Name.txt"/>`. In the type tables a `...FileRef` field is a string (for example the random map template's `DescriptionFileRef`, registered in `0x0049f110` as a 12-byte field of kind 5).

## Which archive wins

`NVFS::CWinVFS` indexes every archive of a folder (`0x009e08d0`: `\*.pak`, `\*.h5m`, `\*.h5p`, `\*.h5c`; `0x009e01b0` for each match, `0x009df790` for each file in it). When a path is already known, `0x009def50` decides:

- the sources have different priorities: the higher priority wins;
- same priority (all of `data\`), or no priority (`-1`, the `UserMODs` archives added by `0x00c8bd00`): the entry with the **newer date inside the zip** wins (the DOS date and time in the zip's directory, `+0xc` of the entry). On equal dates the entry met first stays.

So MMH5.5's English files replace the French ones because they are newer (2007 to 2024 against 2006 to 2007), not because of their names. The 3.1 patch's files are newer than `texts.pak`, and MMH5.5's replace 67 of them again. 22 paths have equal dates in two archives; there the folder's listing order decides, which can differ under Wine.

## Reading one text

| Address | What |
|---|---|
| `0x004c7430` | **Get a text by path.** 383 call sites. Makes a relative path absolute (`0x004c7910`), looks it up in a cache (`0x004c77c0`), and returns the cached wide string. On a miss it reads the file (`0x004c73b0`) and stores the result in the cache, an empty string if the read failed. A flag at `0x00fd3f18` skips the cache (not yet traced who sets it). |
| `0x004c73b0` | Opens the file through the game's file system (`0x00513260` returns the global `NVFS::IVFS` at `0x00fd63c4`, a `CCombinerVFS`; it asks `CWinVFS`, slot 7 `0x009e0020`, the function our mods wrap), then reads it. |
| `0x004c7310` | Reads a stream as text: the first two bytes must be `FF FE` (the UTF-16 little-endian byte order mark), the rest is copied as UTF-16. **Without the mark it returns nothing**, and the game caches an empty text. |

What this means:

- Every text is read **the first time it is needed**, then kept for the rest of the session. A change of language needs a restart, unless the cache is emptied.
- All of them go through `CWinVFS::OpenFile` (slot 7), the same function `original_music.cpp`, `rmg_templates.cpp` and `player_list_textures.cpp` wrap. A mod there sees every text read and can hand the game another file.
- A replacement text must start with `FF FE`.

## Format

From the inventory of the owner's install (MMH5.5 RC19d):

- UTF-16 LE with BOM: every file but 2. Lines end in CRLF; almost no file ends with a line break.
- Markup tags, which must come out of a translation unchanged: `<br>`, `<color=...>`, `<color_default>`, styles (`<body_bright>`, `<h1>`, `<center>`, ...), `<font ...>`.
- Placeholders the game fills in: `<value=name>` (for example `<value=target>`, `<value=COST>`), `<hotkey=...>`, and one printf-style `%s` (MMH5.5's `Text/Game/Buildings/WitchHut/FirstVisit.txt`). Any other `%` is a percent sign.

## Not texts

Some words are elsewhere and are not covered by text packs: map files (`.h5m`) carry their own texts, MMH5.5's Lua scripts may contain a few sentences written in the script, and the exe has some strings of its own.

## Our mod: French texts (`src/mods/languages*.cpp`)

`[lang] texts=fr` wraps slot 7 (`0x00e6651c`). The slot is changed only if it still holds `0x009e0020` and the code there is the 3.1 thunk `E9 CB FE FF FF`. When the game opens a `.txt` path found in `bin\open-homm5\lang\fr\*.pak`, the mod hands back a new `CMemoryStream` (`0x004034d0`, sized with `0x008c7610`) holding that file, starting with `FF FE`. The game deletes the stream as usual. The hook goes in at DLL load, before the first text is read. Use in [../../src/mods/README.md](../../src/mods/README.md), plan in [../plans/languages.md](../plans/languages.md).
