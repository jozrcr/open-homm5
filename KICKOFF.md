# Phase 0 kickoff

Paste this into Claude Code, opened in the project folder:

> Read CLAUDE.md and KICKOFF.md, then run phase 0 step by step. Stop at each "Owner check" and wait for me.

Goal of this session: a working repo, a full inventory of the reference exe, the compiler identified, and a first report. The hook DLL comes in session 2.

## Step 1: Check the environment

- Confirm `git`, `gh` (logged in: `gh auth status`), Python 3, and a JDK 21+ (needed by Ghidra).
- List what is missing and how you would install it.

**Owner check:** approve installs (expected: Ghidra, Python `pefile`, possibly a JDK and Visual Studio Build Tools).

## Step 2: Create the repo

- Ask the owner for the repo name. Default: `homm5-reborn`, public, MIT license.
- Create it with `gh repo create`, scaffold the layout from CLAUDE.md, and write `.gitignore` before the first commit. It must block: `ref/`, `analysis/**/*.gpr`, `analysis/**/*.rep/`, `*.exe`, `*.dll`, `*.pak`, `*.h5m`, `*.h5u`, saves, `*.i64`, `*.idb`.
- First commit: CLAUDE.md, KICKOFF.md, README (2 paragraphs: goal + "bring your own copy of the game"), `.gitignore`, empty folders with `.gitkeep`.

## Step 3: Collect reference binaries

- From the game's `bin\`, copy every `.exe` and `.dll` into `ref/` (never modify the original folder).
- For each file record in `docs/reference-binaries.md`: name, size, SHA-256, PE machine type (x86 or x64), file version, timestamp.
- Work out which exe is the true vanilla Tribes of the East 3.1 binary (beware of a renamed `H5_Game.exe`, see CLAUDE.md). Note which exe MMH5.5 uses for single player and which for LAN.

**Owner check:** confirm the reference exe.

## Step 4: Identify the compiler

- Parse the reference exe's Rich header (Python `pefile`) and map the product IDs to the MSVC version and update level.
- Record it in `docs/toolchain.md`, with how to obtain that exact compiler. Do not download it yet.

## Step 5: Inventory every function

- Run Ghidra headless analysis on the reference exe (project stored in `analysis/`, gitignored).
- Export `progress/inventory.csv`: address, size, Ghidra name, and a guessed origin (game, CRT, DirectX, other library) using Ghidra's Function ID and string references.
- Recover MSVC RTTI class names and vtables; export `analysis/classes.txt` (class names, vtable addresses).
- Generate the first `progress/progress.json` (everything at 0 matched).

## Step 6: First map of the game

In `docs/phase0-report.md`, in plain language:

- Totals: functions, library functions, game functions, classes found via RTTI.
- Best guesses for where the AI turn logic and the network code live (class names, strings, imports such as Winsock).
- Anything surprising (packers, obfuscation, a separate AI process or DLL).
- Recommended next steps for session 2: hook DLL skeleton (which proxy DLL to use) and the first byte-match oracle.

Commit, push, open a PR, and summarize it for the owner.

**Owner check:** review the report and the PR.
