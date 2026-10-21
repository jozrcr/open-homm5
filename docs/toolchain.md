# Toolchain: which compiler built the game

## Answer

The vanilla ToE 3.1 exe (our reference, see [reference-binaries.md](reference-binaries.md)) was built with **Microsoft Visual Studio .NET 2003, original release (RTM), build 3077**:

| Tool | Version |
|---|---|
| C/C++ compiler (`cl.exe`) | **13.10.3077** |
| Linker (`link.exe`) | 7.10.3077 |
| Assembler (`ml.exe`) | 7.10.3077 |
| Resource converter (`cvtres.exe`) | 7.10.3052 |
| C runtime | `msvcr71.dll` (dynamic CRT, i.e. `/MD`) |

**Not** VS .NET 2003 SP1 (build 6030). A different build generates different code and would make byte-matching fail.

## How we know

Microsoft's linker hides a "Rich header" in every exe: a list of every tool (compiler, assembler, linker) and its exact build number, with how many object files each produced. We decode it with `pefile` and the [richprint ID table](https://github.com/dishather/richprint/blob/master/comp_id.txt).

The Steam copy had its Rich header wiped by the Steam DRM wrapper, but three other copies of the same code still have it, and its built-in checksum is valid in each:

| Exe | Built | C++ objects by VS2003 build 3077 | Linker |
|---|---|---:|---|
| `MMH55_Utility.exe` (vanilla 3.1 + MMH5.5 patches) | 2008-06-04 13:58 | 1,209 | 7.10 build 3077 |
| `H5_MapEditor.exe` (Ubisoft-signed, same day) | 2008-06-04 14:22 | 1,770 | 7.10 build 3077 |
| `MMH55.exe` (3.1j enhanced AI) | 2011-10-20 | 1,137 | 7.10 build 3077 |
| `H5_AIProcess_31j.exe` | 2011-10-20 | 28 | 7.10 build 3077 |

Full Rich header of the vanilla build (from `MMH55_Utility.exe`):

| comp.id | Tool | Objects | Probably |
|---|---|---:|---|
| `00600c05` | C++ compiler, VS2003 build 3077 | 1,209 | **the game's own C++ source files** (plus a few static library objects) |
| `005f0c05` | C compiler, VS2003 build 3077 | 54 | C files: CRT startup code, small C libraries |
| `000f0c05` | MASM 7.10 build 3077 | 11 | assembly files (CRT helpers or hand-written math) |
| `005a0c05` | Linker 7.10 build 3077 | 1 | the final link |
| `005c0c05` / `005d0c05` | export / import records, build 3077 | 1 / 10 | |
| `005e0bec` | CVTRES 7.10 build 3052 | 1 | the `.rsrc` resources |
| `005f0fc3` / `00600fc3` / `005d0fc3` | C / C++ / import, build 4035 (Windows Server 2003 SP1 DDK / Platform SDK) | 2 / 2 / 6 | prebuilt SDK libraries |
| `005f0883`, `005d0883`, `005d0813`, `00690813` | build 2179 / 2067 (not in the table) | 1 / 15 / 2 / 3 | prebuilt libraries, probably the DirectX SDK (`d3dx9`, `dxguid`) |
| `000420ff` | VC++ 6.0 SP5 import records | 50 | old import libraries (Windows DLLs) |
| `001923fa` | VS2002 import library | 4 | old import libraries |
| `00010000` / `00000000` | unmarked | 442 / 4 | objects without a Rich ID (very old libraries, import stubs) |

What this tells us beyond the version:

- **About 1,200 C++ source files.** Each one is a "translation unit" in CLAUDE.md terms, and later a GitHub issue. Some belong to statically linked libraries rather than game code.
- **No link-time code generation (`/GL`) and no profile-guided optimization.** Every function is compiled on its own, so matching one function at a time is realistic.

## Compiler flags

### Confirmed by matching (2026-10-20)

`/O2 /G7` ("maximize speed", "tune for Pentium 4"), plus `/GX /GR`. Tested on the first 10 matched functions (`src/decomp/GameTimer`):

| Flags | Functions matching |
|---|---:|
| `/O2` alone, or with `/G6`, `/G5`, `/GB` | 7 of 10 |
| **`/O2 /G7`** | **10 of 10** |
| `/O1 /G7` | 6 of 10 |
| `/Ox /G7` | 1 of 10 (`/Ox` lacks `/Gy`, so functions are not packaged one per section) |

`/G7` shows in the code as `add eax,1` instead of `inc eax`, and as memory operands (`add eax,[ecx+34h]`) instead of separate loads. These small functions cannot tell `/arch:SSE`, `/Oy-` or `/GS` apart. Bigger functions will.

### First clues from the imports

Collected from what the exe imports from `msvcr71.dll`.

| Clue | Flag |
|---|---|
| imports `__security_error_handler` | `/GS` (buffer security checks) on, at least for some files |
| imports `_CxxThrowException`, `__CxxFrameHandler` | C++ exceptions enabled (`/EHsc` or `/GX`) |
| imports `type_info` methods; RTTI class names present | `/GR` (RTTI) on |
| imports `_ftol` | default float-to-int conversion (no `/QIfist`) |
| links `msvcr71.dll` | `/MD` (release, DLL CRT) |
| uses Nival's own STL (`nstl::basic_string`) | few dependencies on Microsoft's STL headers |

Still unknown: optimization level (`/O2` vs `/Ox`, `/Oy`), CPU target (`/G6` / `/G7`), `/arch:SSE`, struct packing. We find these by trial when matching the first functions.

## How to get this exact compiler

We use the free **Microsoft Visual C++ Toolkit 2003, version 1.01** (owner approved the download on 2026-10-20). Microsoft released it in 2004 as a free command-line kit (compiler, linker, C/C++ runtime libraries) and withdrew it in 2006, so it now exists only in archives.

| | |
|---|---|
| Installer | `VCToolkitSetup.exe`, 32,948,392 bytes |
| SHA-256 | `c040f6ffbc2259426dd523b7561575fe1704b58bc37092fbfe27b64aefc449f6` |
| SHA-1 | `50dc6cca1503f08a41d56816f3f1abcaf347b0bd` (identical in two independent archive.org uploads: `vc-toolkit-2003-setup` from 2023 and `20260623_202606` from 2026) |
| Signature | Authenticode, valid: "Microsoft Corporation", timestamped by VeriSign; product "Microsoft Visual C++ Toolkit 2003" 1.01.0000 |
| Unpacked to | `C:\tools\vctoolkit2003` (outside the repo) |
| How | `python tools/toolchain/extract_vctoolkit2003.py VCToolkitSetup.exe C:\tools\vctoolkit2003`. The 2004 installer does not run on modern Windows, so the script unpacks the files without running it (details in the script). |

Verified: `cl.exe` prints `Version 13.10.3077 for 80x86`, and an object file it compiles carries `@comp.id` = `00600c05`, the same ID as the game's 1,209 C++ objects in the Rich header.

| File | SHA-256 |
|---|---|
| `bin/cl.exe` | `2ecf86a3edfd3deae498e08298e210e984537ce9e11759930561e43f40bd2515` |
| `bin/c1.dll` | `11f452af93f8e686b36b348763b33d32ae2766f1b0c360a447053bf44ec50fcc` |
| `bin/c1xx.dll` | `353f3d5dcd050b876e1c044b99c4bf47958d255f395a4919f406a19921be4dfa` |
| `bin/c2.dll` | `bcd28f39b1798b07989c30a1df471cf87fc20e3802d130901a08f83800e6b81e` |
| `bin/link.exe` | `0d5f9712d0da843d787bf4ce4e678abc6b737940996706b2c86101eac9df3e3c` |
| `bin/mspdb71.dll` | `85ebd7dcbe1b9fa756f23e5f811d7dfeccdac0de14271f2c18affbf918d8cb79` |
| `bin/msobj71.dll` | `19ce191c44e783e70c762ef13c1e918fc07745c8f64fcfc7b217e73ef667c168` |

Watch out:

- **Toolkit version 1.00** (installer 32,952,488 bytes, SHA-1 `956c81c3...11f4`, archive.org item `microsoft-visual-c-toolkit-2003`) contains **build 13.10.3052**, not 3077. Wrong for us, even though it is the more widely mirrored copy.
- The Toolkit has no MFC/ATL, no Windows SDK import libraries except `kernel32.lib`, and no IDE. For compiling single functions to compare against the exe we need none of these.
- Not usable either: VS .NET 2003 SP1 (13.10.6030), the Windows Server 2003 SP1 DDK compiler (13.10.4035), or anything newer. Different code generation.
- The old compiler is 32-bit, so it should also run under Wine on the owner's Linux PC (not tested yet).

## Modern compilers (for the hook DLL)

The hook DLL does not need to byte-match anything, so it can use a modern compiler. The owner's Windows PC already has three. Step 1 missed them, because `vswhere` (Visual Studio's locator) does not list these installs:

| Install | MSVC toolset |
|---|---|
| `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` (17.5) | 14.35.32215 |
| `C:\Program Files\Microsoft Visual Studio\2022\Community` (17.3) | 14.33.31629 |
| `C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools` (16.11) | 14.29.30133 |

Windows SDK 10.0.22000.0 is installed. A test 32-bit DLL builds fine with the 2022 Build Tools (`vcvarsall.bat x86`).

## Other builds, for reference

- **2025 Ubisoft rebuild** (current `bin\H5_Game.exe`): linker 14.29 (Visual Studio 2019 v16.11 toolset), runtime `vcruntime140` / `msvcp140`. Its Rich header was wiped by the Steam wrapper. Not our target.
- **MMH5.5 DLLs** (`55.dll`, `55u.dll`, `55e.dll`): linker 14.29, built 2024. Not our target.
