# Inventory tools

Scripts that describe the reference exe: what files exist, which compiler built them, and every function inside.

| Script | What it does |
|---|---|
| `pe_info.py <folder>` | Hash, version, build date, sections and imports of every exe/dll in a folder (table in `docs/reference-binaries.md`). |
| `pe_diff.py <a.exe> <b.exe>` | Lists the byte regions where two same-size builds differ (used to find MMH5.5's patches). |
| `mmh55_patch_sites.py <55u.dll> [--list]` | Lists the exe spots MMH5.5's `55u.dll` patches at run time, and the game functions where they meet an address our hook DLL uses (for MMH5.5 upgrades, see `docs/plans/mmh55-rc19k.md`). Needs no `pefile`. |
| `rich_header.py <exe> [comp_id.txt]` | Decodes the MSVC Rich header: which compiler/linker builds made the exe (see `docs/toolchain.md`). |
| `rtti_scan.py <exe> <out.tsv>` | Finds every RTTI class and vftable directly in the exe bytes, and demangles the class names with Ghidra's demangler. |
| `build_inventory.py <export> <exe>` | Turns the Ghidra dump plus the RTTI scan into `progress/inventory.csv` and `analysis/classes.txt`, then runs `make_progress.py`. |
| `make_progress.py` | Regenerates `progress/progress.json` from committed files only (inventory, `matched.txt`, `equivalent.txt`, `unit.json` files), so CI can run it. |
| `ghidra/DumpFunctions.java` | Ghidra script: writes disassembly and decompiled C of chosen functions to text files, for research and decompiling. |
| `ghidra/SetMsvcCompiler.java` | Ghidra script: marks the program as MSVC-built (see "Why the extra steps"). |
| `ghidra/CreateFunctionsFromList.java` | Ghidra script: creates functions at a list of addresses. |
| `ghidra/ExportInventory.java` | Ghidra script: dumps functions and the strings they reference to TSV files. |
| `ghidra/DemangleNames.java` | Stand-alone Java program: demangles MSVC names with Ghidra's demangler library (used by `rtti_scan.py`). |

All need Python 3 with `pefile` (`python -m pip install --user pefile`). The Ghidra steps need Ghidra 12.1.x and JDK 21. `rtti_scan.py` finds them through `GHIDRA_HOME` and `JAVA_HOME`, defaulting to `C:\tools\ghidra_12.1.4_PUBLIC` and the Temurin 21 path.

## Rebuilding the inventory from scratch

Prerequisite: `ref/H5_Game_31_vanilla.exe` (see `docs/reference-binaries.md` for how to make it and its expected hash). Total time is about 25 minutes, mostly Ghidra.

```powershell
$env:JAVA_HOME = "C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot"
$env:GHIDRA_HEADLESS_MAXMEM = "12G"
$ghidra = "C:\tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat"

# 1. Import + first auto-analysis (~15 min). The pre-script stores "MSVC" in the project.
& $ghidra analysis\ghidra h5_31 -import ref\H5_Game_31_vanilla.exe -overwrite `
    -processor x86:LE:32:default -cspec windows -scriptPath tools\inventory\ghidra `
    -preScript SetMsvcCompiler.java -postScript ExportInventory.java analysis\export

# 2. RTTI scan + first build: lists vftable entries Ghidra did not find
python tools\inventory\rtti_scan.py ref\H5_Game_31_vanilla.exe analysis\export\rtti_scan.tsv
python tools\inventory\build_inventory.py analysis\export ref\H5_Game_31_vanilla.exe

# 3. Second analysis pass (~7 min): add those functions, re-analyze (now with Ghidra's RTTI analyzer), export
Copy-Item analysis\export\vtable_targets_missing.txt $env:TEMP\vt_missing.txt
& $ghidra analysis\ghidra h5_31 -process H5_Game_31_vanilla.exe -scriptPath tools\inventory\ghidra `
    -preScript CreateFunctionsFromList.java $env:TEMP\vt_missing.txt -postScript ExportInventory.java analysis\export

# 4. Final build of the committed files
python tools\inventory\build_inventory.py analysis\export ref\H5_Game_31_vanilla.exe
```

`analysis/export/` (raw dump, contains strings from the game) and the Ghidra project are gitignored. Only the processed files under `progress/` and `analysis/classes.txt` are committed.

## Why the extra steps

Ghidra recognizes MSVC-built exes by their Rich header. The Steam DRM wrapper zeroed that header in our reference exe, so on import Ghidra records the compiler as "unknown" and skips its MSVC-only analyzers, RTTI recovery included. Without RTTI, about 13,700 functions that are reachable only through vftables (virtual methods) are never found.

- Ghidra picks its analyzers when the program is opened, so `SetMsvcCompiler.java` cannot rescue the first pass. It does store the right value (`visualstudio:unknown`) in the project, so the second pass (step 3) runs the RTTI analyzer.
- `rtti_scan.py` reads RTTI straight from the bytes, so class names and vftables do not depend on Ghidra at all.
- `CreateFunctionsFromList.java` adds the vftable-only functions before the second analysis, so Ghidra also follows the calls they make.

## How "origin" is decided

See the docstring of `build_inventory.py`. In short: a region table for the tail of `.text` (C runtime, codecs, DirectX helpers, Ubisoft SDK, exception funclets, global initializers), byte patterns for adjustor thunks, Ghidra Function ID for C runtime functions, and string anchors for Lua. Lua and codec edges are approximate. Lua is probably undercounted: about 110 found, while Lua 5.0 has roughly 400 functions.
