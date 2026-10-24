# The decompilation worker

Instructions for the scheduled sessions that decompile the game unattended (plan: [../plans/decomp-pipeline.md](../plans/decomp-pipeline.md)). Read CLAUDE.md first: its hard rules apply to every line here. The conventions for units and files are in [src/decomp/README.md](../../src/decomp/README.md).

## Where

- **Worker folder:** `C:\Users\joz84\Documents\claude_projects\homm_decomp_worker`, a git worktree of the repo, used only by the worker. Never work in the main checkout (`...\homm_decomp`): the owner's interactive sessions use it.
- **Links:** `ref\` and `analysis\` in the worker folder point at the main checkout's folders (directory junctions), for the reference exe and the Ghidra project. Read them, never write to them.
- **First run:** if the worker folder does not exist, create it from the main checkout:
  1. `git worktree add ..\homm_decomp_worker origin/main`;
  2. `mklink /J ref ..\homm_decomp\ref` (from inside the worker folder);
  3. the same for `analysis`.

## Start of a run

1. **Budget:** call `mcp__ccd_session_mgmt__get_usage`. **Stop at once** (do nothing else) if the weekly limit is at 90% or more, or the 5-hour limit at 85% or more. The owner allowed up to about 90% of the plan (Q7); the rest is for their own chats.
2. **One worker at a time:** if `build\decomp\worker.lock` exists and is less than 3 hours old, another run is busy: stop. Otherwise write the current time into it, and delete it when you stop.
3. **Branch:**
   - `git fetch origin`.
   - If a worker PR is open (`gh pr list --author @me --search "decomp batch" --state open`), continue on its branch.
   - Else start `decomp/batch-<yyyy-mm-dd>` from `origin/main`.
   - Never push to `main`, never merge a PR: the owner merges.

## The loop

Repeat until a stop condition (below):

1. `python tools/decomp/next.py -n 10`: the next functions, in the owner's priority order.
2. **Dump** them in one go (Ghidra takes a minute to start): `powershell -File tools\inventory\dump.ps1 <addresses...>`. That writes `analysis\export\dump\<address>.txt`, with the disassembly and Ghidra's pseudo-C.
3. **For each function:**
   1. Pick its unit, an original source file: the same folder as its neighbours by address if they are done, else a new folder named after its class or area. Note in the file header that the name is a guess.
   2. Write the C++, with a `// FUNCTION: 0x<address>` comment, declaring the classes and members it uses at the right offsets.
   3. Add it to `unit.json`. Get the mangled name with `python tools/verify/verify.py --list <Unit>`.
   4. Run `python tools/verify/verify.py <Unit>`.
   5. Iterate on the source only, up to about 6 compiles.
   6. **MATCH:** run `python tools/verify/verify.py --record` (all units), then `git commit` the unit and `progress/` with a one-line message.
   7. **No match:** keep the draft out of `unit.json`, add a line to `progress/attempts.txt` (`<address>  <yyyy-mm-dd>  <reason in a few words>`), and commit that. After 3 failed attempts across runs, `next.py` stops offering the function. Only after real, varied attempts may a function go to `progress/equivalent.txt`, with a written reason (CLAUDE.md).
   8. Calls into code not yet decompiled: declare the callee and add its address to `progress/symbols.txt`.
4. **Every 20 matched functions,** and at the end of a run:
   1. `python tools/inventory/make_progress.py`;
   2. push the branch;
   3. open or update the PR **"decomp batch <date>"** with a short plain-language summary: what matched, what failed and why, the units touched, and `python tools/decomp/next.py --status`.

## Stop conditions

- The budget check fails (check again every 10 functions).
- The time window is over: stop by **07:00** on a night run, or by the end time given in the task.
- 3 errors in a row that are not about one function (the compiler, Ghidra or git not working). Write them in the PR and stop.

At the end: delete the lock, push, and update the PR.

## Never

- Never edit `tools/verify/`. CI hashes it.
- No inline assembly, naked functions, embedded bytes, object patching or `#pragma` tricks aimed at the checker.
- Never commit game files: no exe, dll, pak, data, maps, saves, Ghidra databases, or the `analysis\export\dump\` files.
- Never change the game folder or `C:\Games\H5_test`. The worker does not run the game.
- Don't "improve" the original code: keep its bugs.
