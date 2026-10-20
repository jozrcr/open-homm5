# Reference binaries

What is in the game's `bin\` folder (Steam copy, as installed on the owner's Windows PC on 2026-10-20), what each file is, and which one we treat as "the original game".

All files were **copied** (never moved or modified) into `ref/`, which is gitignored. Regenerate the table with:

```
python tools/inventory/pe_info.py ref/          # this table
python tools/inventory/pe_info.py ref/ --json   # full details: sections, imports, version strings
```

## Short version

There are **four different builds of the game exe** in `bin\`, not one:

| Build | Files | Built | Notes |
|---|---|---|---|
| **Vanilla ToE 3.1** (v3.1.2.98) | `H5_GameOLD.exe` (Steam DRM wrapped), `MMH55_Utility.exe` / `_64` (unwrapped, lightly patched by MMH5.5) | 2008-06-04 | **The reference** (see below). Built with Visual Studio .NET 2003 (linker 7.10). This is what co-op games run. |
| **3.1j "enhanced AI"** (Quantomas AI) | `MMH55.exe` / `_64` + `H5_AIProcess_31j.exe` | 2011-10-20 | A later build of the game with a rewritten AI that runs in a separate process. Single player only (no LAN). |
| **2025 Ubisoft rebuild** (v3.1.2.99) | `H5_Game.exe` (Steam DRM wrapped) | 2025-06-05 | Same game recompiled with Visual Studio 2019 (linker 14.29). Arrived with a Steam update in July 2025. |
| Duel Mode 2025 rebuild | `binDM\H5_Game.exe` (not copied, outside `bin\`) | 2025 | Separate "Duel Mode" build; not relevant for now. |

The `_64` variants are **not 64-bit programs**. Each differs from its non-`_64` twin by exactly one byte: the "large address aware" flag, which lets a 32-bit program use up to 4 GB of RAM on 64-bit Windows.

## All files

| File | Size | SHA-256 | Machine | File version | PE timestamp (UTC) |
|---|---:|---|---|---|---|
| 55.dll | 642,560 | `4727c14f3163d5e0082b5a94dbad0156b3e162cb6b3e413010c9eb00ba325fa2` | x86 | - | 2024-05-14 20:38:58 |
| 55e.dll | 498,176 | `0577a8b43e224ba6ab4beb38d7a71f010cf1be6e166d9b1b743da9080802ee71` | x86 | - | 2024-05-07 20:49:23 |
| 55u.dll | 661,504 | `a6de006f9492f1e8af6eae19619b5a345536c443a5f2e4bb5e1e26947988085b` | x86 | - | 2024-05-14 20:32:12 |
| dbghelp.dll | 724,944 | `bcccce7bbce0d68b8bcb35d6921c239b1c0a5faa783b21a5daf63457d67d9a38` | x86 | 6.0.0017.0 (DbgBuild.020528-1721) | 2002-05-31 23:54:47 |
| fmod.dll | 168,400 | `9e3e6dae77a548aa16e68c5713b914b8b4a831e2f51fae5fa70d748fb04bffab` | x86 | 3.75 | 2005-12-28 04:44:30 |
| granny2.dll | 406,536 | `b40c1636c298ba0af1bfda8cc6db38c73924a2f85397976e40cf2fc2365aae00` | x86 | 2.5.0.5 | 2004-11-12 03:35:09 |
| H5_AIProcess_31j.exe | 473,088 | `da1b34b1d2e16cf0969607e4086580f5c338a2f69e6ad830ddd77abdc0a61038` | x86 | - | 2011-10-20 19:36:19 |
| H5_Game.exe | 14,531,720 | `07b44a30dba8d9039ffa21b00fc9cecb2a77fa9db003ca15383314220b442d25` | x86 | 3.1 | 2025-06-05 14:06:45 |
| H5_GameOLD.exe | 13,433,344 | `11690919cf726a8afd60e44ea186d1af4c7daafa720693473385767c57bd2146` | x86 | 3.1 | 2008-06-04 13:58:55 |
| H5_MapEditor.exe | 17,554,024 | `6d868b71f7f9faed1d4416eeaa3f899a99593b8680706202f481be1d6ac7d0b1` | x86 | - | 2008-06-04 14:22:01 |
| ICSharpCode.SharpZipLib.dll | 196,608 | `70d58f268a1a3fee3e88c77da7bd3b5130d0f63cf0dc4ce3aad16b0f9fbe8bb1` | x86 | 0.86.0.518 | 2010-05-25 02:55:20 |
| libcurl.dll | 243,152 | `c1e037d3a0c112b1743d3da891e7f3edfb60cc4d878d0d364c68f474320ef744` | x86 | - | 2005-05-28 09:10:04 |
| MFC71.dll | 1,066,448 | `470d040ef5f4eb1fad93c931892c26e151a14d83d6f2a6bb743a085a8a15048c` | x86 | 7.10.3077.0 | 2003-03-19 05:19:57 |
| MFC71ENU.DLL | 62,928 | `aff2b738afee0a27615c4cb87e5c2cdc882ae013a670e67e61fa71c37de24758` | x86 | 7.10.3077.0 | 2003-03-19 04:44:36 |
| MMH55.exe | 13,089,792 | `d1a0e6173883704b7b6a309ff37d7bbcc806e159c13a74580dd6924b23b77baf` | x86 | 3.1j enhanced AI | 2011-10-20 19:28:39 |
| MMH55_64.exe | 13,089,792 | `20210e51e39e61d0d0b40ed04321f3d01715b52abbe2b2fc2ed78f0453a55fac` | x86 | 3.1j enhanced AI | 2011-10-20 19:28:39 |
| MMH55_Editor.exe | 17,547,776 | `082c44474c261acead4dc92583105dd72f5efc6f55c6e68938b58af860ed64bb` | x86 | - | 2008-06-04 14:22:01 |
| MMH55_Editor_64.exe | 17,547,776 | `eda6eb2e76372952e165f3ba2febd5f59d484477c959d3479b67081e90c9bb70` | x86 | - | 2008-06-04 14:22:01 |
| MMH55_Mapmixer.exe | 12,985,537 | `de3e67c24bf4125019f55702de1154dd3aec31bac83e0039790e6dd108b2fe9b` | x64 | - | 2024-05-25 15:52:49 |
| MMH55_Utility.exe | 13,089,792 | `7c01933eacc8138261b8a82d24ccb29e4a9ebd4e47656fe62befd7888a87fd69` | x86 | 3.1 | 2008-06-04 13:58:55 |
| MMH55_Utility_64.exe | 13,089,792 | `3d2e26d352002f62c9f09b12d58bd0a5e1ede8be702b1ad3a6ce8722629fcf6c` | x86 | 3.1 | 2008-06-04 13:58:55 |
| msvcp71.dll | 505,296 | `1930572185b08f55cf597a756f56b9b63287d37e35a5ffb2286fa03f7cae2e60` | x86 | 7.10.3077.0 | 2003-03-19 04:14:51 |
| msvcr71.dll | 353,744 | `926c7cc31a5ac46fd09c09f481d9c2657c442dc8f778c9048e34e0d47e5ab7d2` | x86 | 7.10.3052.4 | 2003-02-21 12:42:20 |
| UbiStats.dll | 169,424 | `db2db8795afe2baa45d9d849d10166396de018b8eb468e212936221b4f28453e` | x86 | 2, 5, 0, 1 | 2006-03-22 16:59:59 |
| um.dll | 169,576 | `1956c00b371d22a3e1a644394ff3e7159b6ec36d660d5ffa36628fcf63fd0fc6` | x86 | 2, 5, 0, 3 | 2006-09-15 20:59:04 |
| zlib1.dll | 65,488 | `7304163d3d7ddd48de8a4d7672ed38cf77c027b5cc16ea02bde6af0ff88131d5` | x86 | 1.2.3 | 2005-07-20 09:48:10 |
| zlibwapi.dll | 78,288 | `83ba676fc2789dc752ca55cdc67e17d241a301d0548204b6b84dd246efd2a33b` | x86 | 1.2.2 | 2004-10-07 10:50:49 |

"PE timestamp" is the build date the linker wrote into the file, not the file's date on disk.

## Details

### Vanilla ToE 3.1 (2008-06-04, v3.1.2.98)

- **`H5_GameOLD.exe`** is the original Steam `H5_Game.exe`, renamed as the GameRanger setup guide asks. Its original filename (in the version info) is `H5_Game.exe`.
- It is wrapped in **SteamStub**, Steam's DRM layer: an extra `.bind` section holds the program's real entry point, and the code section (`.text`) is encrypted (its entropy is 8.0, which means it looks like random data). Ghidra cannot read the code until the wrapper is removed.
- **`MMH55_Utility.exe`** is the same build with the wrapper already removed. Compared section by section with `H5_GameOLD.exe`, `.data`, `.tls`, `.rsrc` and `.reloc` are identical. `.rdata` differs in only 55 bytes:
  - the import `um.dll` is renamed to `55u.dll`. MMH5.5 loads its own DLL in place of Ubisoft's `um.dll`. This is the same "proxy DLL" trick our hook track plans to use.
  - a game-object path `Cartographer` is changed to `RMGDimensionDoor`.
  - We cannot yet tell what MMH5.5 changed in the code (`.text`), because the vanilla code is still encrypted. See "Next step" below.
- Built with linker 7.10 = **Visual Studio .NET 2003**, which matches the bundled `msvcr71.dll` / `msvcp71.dll` / `MFC71.dll`. Step 4 confirms the exact compiler from the Rich header.

### 3.1j "enhanced AI" (2011-10-20)

- `MMH55.exe` reports version "3.1j enhanced AI". It is a **different, later build** of the game, not a patched 3.1: section sizes differ, and it was compiled 8 minutes before `H5_AIProcess_31j.exe`.
- This is the **Quantomas AI**, a community AI overhaul for ToE that MMH5.5 bundles. Text inside the exe says it was written "by a professional AI programmer working with the C++ sources" and "received Ubisoft's approval", and that "turn processing is much faster".
- The AI runs in a **separate process**, `H5_AIProcess_31j.exe`. The game talks to it through shared memory (`H5_AI_PROCESS_FILE_MAPPING_`) and two signals (`H5_AI_PROCESSING_REQUIRED`, `H5_AI_PROCESSING_COMPLETE`).
- **It does not support LAN games**, and therefore not GameRanger co-op ([ModDB MMH5.5 multiplayer guide](https://www.moddb.com/tutorials/mmh55-preventing-issues-in-multiplayer)).

### 2025 Ubisoft rebuild (2025-06-05, v3.1.2.99)

- The current `bin\H5_Game.exe` is an **official rebuild of ToE 3.1** compiled with Visual Studio 2019 (linker 14.29, modern `vcruntime140` runtime). It is also SteamStub-wrapped, and the files are dated July 2025, so a Steam update delivered it.
- This means the Steam update replaced whatever `H5_Game.exe` was there before (per the GameRanger guide, an MMH5.5 exe copy).
- It may be useful later as a second view of the same source code, but MMH5.5 does not use it.

### Which exe MMH5.5 uses

| Mode | Exe | Source |
|---|---|---|
| Single player, campaigns, skirmish | `MMH55_64.exe` (3.1j enhanced AI) | [ModDB guide](https://www.moddb.com/tutorials/mmh55-preventing-issues-in-multiplayer) |
| LAN / multiplayer (and therefore GameRanger co-op) | `MMH55_Utility_64.exe` (vanilla 3.1 + `55u.dll`) | [ModDB guide](https://www.moddb.com/tutorials/mmh55-preventing-issues-in-multiplayer) |
| Map editor | `MMH55_Editor_64.exe` (vanilla editor + `55e.dll`) | file imports |

All players in a multiplayer game must use the same exe.

### Exported names (useful anchors)

Every game exe exports about 11 functions with full C++ names, all starting with `SFLB` (probably markers from the StarForce copy-protection tooling), for example `NWorld::CPlayerAI::SFLB_InitDay`. They give us real namespace and class names (`NWorld`, `NUI`, `NUbi`, `NLAN`, `NDb`, `NRandom`, `nstl`) and a few known function addresses. The 3.1j build adds `NWorld::StrategicAI::SFLB_InitDay`.

## The reference exe (confirmed by the owner, 2026-10-20)

**Vanilla ToE 3.1, build 2008-06-04 (v3.1.2.98), unwrapped from Steam DRM:**

| | |
|---|---|
| File | `ref/H5_Game_31_vanilla.exe` |
| SHA-256 | `94309157dc65498203c1ee0c357a30ae8788184b91d73bcd5eb5c3159aa81375` |
| Size | 13,089,792 bytes |
| Made from | `H5_GameOLD.exe` (SHA-256 `11690919cf726a8afd60e44ea186d1af4c7daafa720693473385767c57bd2146`) |
| Made with | Steamless v3.1.0.5 by atom0s (zip SHA-256 `e3e2d22e098ff3fb359b2876aa2bed9596f0501e6ff588cbffae90a76d2dc4f5`), default options. It detected SteamStub variant 2.1. |
| Image base | `0x400000` |

Why this one:

1. It is what CLAUDE.md asks for ("vanilla ToE 3.1").
2. It is the code our co-op games run. The owner confirmed GameRanger launches `MMH55_Utility_64.exe`, which is this build plus a few small patches (below). Speeding up this AI is the Phase 1–2 target.
3. Its compiler (VS .NET 2003) is old but well known.

To recreate it on another machine: copy `bin\H5_GameOLD.exe` (check the hash above), then run `Steamless.CLI.exe <copy>`. The output must match the SHA-256 above.

The unwrapped file has no DOS stub or Rich header (bytes `0x40`–`0x127` are zero). The Steam wrapper removed them. This does not affect any function bytes.

### What MMH5.5 changed (vanilla vs `MMH55_Utility.exe`)

`python tools/inventory/pe_diff.py ref/H5_Game_31_vanilla.exe ref/MMH55_Utility.exe`: 526 bytes differ in 16 regions. **Every function is at the same address in both files**, so addresses from the reference work directly in the co-op exe.

| Where | Address | Bytes | What (first guess) |
|---|---|---:|---|
| header | `0x40` | 232 | DOS stub and Rich header restored (Steam had zeroed them) |
| .text | `0x81eeb7` | 1 | `je` → `jmp`: a check is always skipped |
| .text | `0x847891` | 17 | constant `0x161` → `0x179` |
| .text | `0x84c9c0` | 1 | constant `0x61` → `0x79` |
| .text | `0x84ca7f` | 2 | jump into new code placed in padding at `0x84cad8` |
| .text | `0x84cad8` | 7 | new code in former padding (`push 0xC4; jmp`) |
| .text | `0x90760a` | 60 | float constants changed (e.g. 1.0 → 1.4) |
| .text | `0x920cb2` | 2 | a call/jump target changed |
| .text | `0xae29e0` | 1 | constant `0x09` → `0x4a` |
| .text | `0xba77a9` | 189 | a function body rewritten in place |
| .text | `0xba79d4` | 18 | its jump table updated |
| .text | `0xd11f3c` | 4 | jump into new code at `0xd122e1` |
| .text | `0xd122e1` | 15 | new code in former padding (calls a function, then jumps back) |
| .rdata | `0xe577dc`, `0xe57818` | 51 | object path `Cartographer` → `RMGDimensionDoor` |
| .rdata | `0xf1ce22` | 7 | import `um.dll` → `55u.dll` (MMH5.5's own DLL) |

What each patch does in gameplay terms is not known yet. That comes once Ghidra has named the functions around them.

The Rich header that MMH5.5 restored likely comes from the original retail (non-Steam) 3.1 exe. Step 4 reads the compiler version from it and cross-checks it against `H5_MapEditor.exe`, built the same day.
