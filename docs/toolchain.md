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

## Compiler flags (first clues)

Collected from what the exe imports from `msvcr71.dll`. To be refined when we match the first functions.

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

**Not downloaded yet**, per the phase 0 plan. Options, best first:

1. **Microsoft Visual C++ Toolkit 2003** (free). Microsoft released it in 2004 as a free command-line kit containing the same optimizing compiler, **`cl.exe` 13.10.3077**, plus the linker and the C/C++ runtime libraries. Microsoft withdrew it in 2006, so it now exists only in archives (for example archive.org, installer `VCToolkitSetup.exe`, about 32 MB). We would verify `cl.exe` reports `Version 13.10.3077 for 80x86` and record the file's SHA-256 here. Drawbacks: no MFC/ATL and no IDE. We need neither for compiling single functions.
2. **Visual Studio .NET 2003 Professional (RTM)** from a paid Visual Studio Subscription (formerly MSDN). Same compiler with the full IDE. Costs money: owner decision.
3. Not usable: VS .NET 2003 SP1 (13.10.6030), the Windows Server 2003 SP1 DDK compiler (13.10.4035), or anything newer. Different code generation.

The old compiler is 32-bit and runs on Windows 11 and under Wine, so it also works on the owner's Linux PC.

## Other builds, for reference

- **2025 Ubisoft rebuild** (current `bin\H5_Game.exe`): linker 14.29 (Visual Studio 2019 v16.11 toolset), runtime `vcruntime140` / `msvcp140`. Its Rich header was wiped by the Steam wrapper. Not our target.
- **MMH5.5 DLLs** (`55.dll`, `55u.dll`, `55e.dll`): linker 14.29, built 2024. Not our target.
