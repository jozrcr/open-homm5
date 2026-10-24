# Worker brief (template)

The coordinator ([coordinator.md](coordinator.md)) sends this to each worker, with the `{...}` placeholders filled in from `python tools/decomp/next.py --assign`. Everything between the two lines is the prompt.

---

You are decompilation worker {WORKER} of the open-homm5 project (Heroes of Might and Magic V: Tribes of the East). Several workers decompile disjoint groups of functions in parallel; a coordinator merges and checks your work afterwards.

## Read first (in your worktree)
- `CLAUDE.md`: its hard rules apply.
- `docs/decomp/matching-notes.md`: **what makes this compiler produce the original bytes.** Most of your time goes into this; read it all.
- `src/decomp/README.md` (units, unit.json) and `tools/verify/README.md` (what "match" means).
- A finished unit to copy the style from, for example `src/decomp/HeroPathfinderQueue/`.

## Your job
Decompile these functions of the vanilla 3.1 exe ({AREA} area) into C++ that the original compiler turns into **exactly the original bytes**:

{ADDRESSES}

Put them in a new unit folder `src/decomp/{UNIT}/` with a `.cpp`, a `unit.json` and an `attempts.txt`. You may rename the part before the `_` once you know the class (keep the `_<address>` suffix, so no other worker can pick the same folder). If the functions turn out to come from two source files (the flags that match change, see the notes), use a second folder with `_2` at the end.

## Setup (once)
1. Base your branch on the current batch: `git fetch origin`, then `git checkout -B {WORKER_BRANCH} origin/{BATCH_BRANCH}`.
2. The verifier needs the reference exe in `ref\`, which is not in git. Create a directory junction with PowerShell (not `cmd mklink`, which is blocked):
   `New-Item -ItemType Junction -Path ref -Target C:\Users\joz84\Documents\claude_projects\homm_decomp\ref`
   Read only: never write there.
3. The compiler is installed at `C:\tools\vctoolkit2003`, and Python has `pefile`.

## Inputs
- **Ghidra's disassembly and pseudo-C** of each function is at `C:\Users\joz84\Documents\claude_projects\homm_decomp\analysis\export\dump\<address>.txt` (read only). Do not run Ghidra or `tools/inventory/dump.ps1`. If you need a function that isn't dumped, say so in your report.
- Notes on the subsystems: `docs/subsystems/`. Class names: `analysis/classes.txt`.

## The loop, per function
1. **Pause switch:** if `C:\Users\joz84\Documents\claude_projects\homm_decomp\PAUSE` exists, commit what you have and stop at once.
2. Write the C++ with a `// FUNCTION: 0x<address>` comment above it. Declare the structs and classes it uses with members at the right offsets (local declarations in your .cpp are fine). For calls into code outside your unit: declare the callee and append its address and mangled name to `progress/symbols.txt`, at the end, under a line `# --- {UNIT} ---`.
3. Add it to `unit.json`; `python tools/verify/verify.py --list {UNIT}` shows the mangled names.
4. Run `python tools/verify/verify.py {UNIT}`. Iterate on the C++ only: up to about 8 compiles per function, more only when you are clearly getting closer.
5. **Matched:** `git commit` the unit with a one-line message.
6. **Held (rare):** you checked that the bytes are identical, but the verifier still says otherwise because of a gap in the verifier itself. Take it out of `unit.json`, keep the code, add a line to `progress/held.txt` (`<address>  {UNIT}  <why>`), commit, and describe the gap in your report. Loop alignment and jump tables are not gaps any more (see the notes).
7. **Not matched:** take it out of `unit.json`, keep the draft in `drafts.cpp` (not in `sources`). Write the details in the unit's `attempts.txt`, and add one line to `progress/attempts.txt`: `<address>  {DATE}  {UNIT}: <reason in a few words>`. Commit, and move on.

## Never
- Never run `verify.py --record`: the coordinator does it after merging.
- Never edit anything under `tools/` or `.github/`. CI hashes the verifier.
- Forbidden in decomp source (CLAUDE.md): inline assembly, naked functions, embedded bytes, object patching, `#pragma` tricks. `tools/decomp/check_rules.py` checks this, and you can run it yourself.
- Never commit game files or the dump files, and never touch the game folder or `C:\Games\H5_test`.
- Keep files LF. In Python, write with `newline="\n"`.

## Shell notes
- In worktrees, chained commands (`&&`, several heredoc steps) may be refused: run plain separate commands.
- Helper scripts go in a subfolder of the scratchpad named after you (for example `scratchpad\{WORKER}`), not in the repo. The scratchpad is shared with the other workers, who may overwrite files at its top level.
- `cl /FAcs` listings (in your scratchpad) are the best way to see what differs.

## Time box
- Run `date` at the start, and again before each new function.
- Stop when every function has been tried, or **75 minutes after your start time**, whichever comes first. Don't guess the time: in the first night run, workers stopped after about 25 minutes, believing they had used 75.
- Commit everything on your branch; do not push and do not open a PR.

## Report back (short)
- your branch name and unit folder(s);
- matched functions (count and list), held ones, and failed ones with the reason for each;
- your elapsed time;
- struct layouts or class facts worth a shared header (offsets, vtable slots), compiler or flag lessons that should go into `matching-notes.md`, and any tool problems.

---
