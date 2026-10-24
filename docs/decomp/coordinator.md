# The decompilation coordinator

Instructions for the scheduled sessions that run the decompilation unattended (plan: [../plans/decomp-pipeline.md](../plans/decomp-pipeline.md)). Read CLAUDE.md first: its hard rules apply to every line here.

**How it works:** the coordinator, this session, gives each of 3 Sonnet workers its own group of about 20 functions. It waits for their reports, merges their branches into the day's batch branch, checks everything, records the matches and updates the batch PR. That is one **run**, about 75–90 minutes. A session does as many runs as the budget and the time window allow. The pilot of 2026-10-24 measured one run at 54 matches, about 4% of the weekly limit and 30% of the 5-hour limit.

## Where

- **Coordinator folder:** `C:\Users\joz84\Documents\claude_projects\homm_decomp_worker`, a git worktree used only by these runs. Do all git work there, with absolute paths (`git -C <folder> ...`). Never commit, merge or switch branches in the main checkout (`...\homm_decomp`): the owner's sessions use it.
- **Links:** in the coordinator folder, `ref\` and `analysis\` are directory junctions to the main checkout's folders (the reference exe, the Ghidra project and its dumps).
- **First run:** if the coordinator folder does not exist, create it:
  1. `git -C C:\Users\joz84\Documents\claude_projects\homm_decomp worktree add --detach ..\homm_decomp_worker origin/main`;
  2. `New-Item -ItemType Junction -Path <coordinator folder>\ref -Target C:\Users\joz84\Documents\claude_projects\homm_decomp\ref`;
  3. the same for `analysis`.
- **Workers** run in their own worktrees, which the Agent tool creates (`isolation: "worktree"`).

## Start of a session

1. **Pause:** if `C:\Users\joz84\Documents\claude_projects\homm_decomp\PAUSE` exists, stop at once and do nothing else (the owner needs the PC).
2. **Budget:** see "The budget rule" below. If it says no, stop.
3. **One coordinator at a time:** if `<coordinator folder>\build\decomp\coordinator.lock` exists and is less than 4 hours old, another session is busy: stop. Otherwise write the current time into it. Delete it when you stop, whatever the reason.
4. **Branch:**
   - `git -C <coordinator folder> fetch origin`.
   - If a batch PR is open (`gh pr list --state open --search "decomp batch in:title"`), check out its branch and merge `origin/main` into it.
   - Otherwise create `decomp/batch-<yyyy-mm-dd>` from `origin/main`.
   - Never push to `main`, and never merge a PR: the owner merges.
5. **The base is sound:** `python tools/verify/verify.py` must end with "N of N functions match", and `python tools/decomp/check_rules.py` with "0 problem(s)". If not, write what is wrong in the PR (or a new issue) and stop.

## The budget rule

Call `mcp__ccd_session_mgmt__get_usage`. From the "Weekly · all models" window, take `percentUsed` (W) and `resetsAt`. From the "5-hour limit" window, take `percentUsed` (F).

- **Days into the week:** D = 7 − (days from now until the weekly `resetsAt`), for example 2.5.
- **This week's allowance so far:** A = min(90, 10 + 11.5 × D). It reaches 90% at the end of the week. This spreads the work over the week, so the owner always has room for their own chats (Q7: up to about 90% of the plan in total).
- **Start a run only if** W + 5 ≤ A **and** F ≤ 60. A run costs about 4% of the week and 30% of the 5-hour window.

For example, at D = 2.5 the allowance is A = 38.75, so a run may start while W ≤ 33.

Check again before every run.

## One run

1. **Pick the work:** `python tools/decomp/next.py --assign 3`. That gives 3 groups, each with a unit folder name and its addresses. Groups never share a folder.
2. **Dump** all the addresses in one go (Ghidra takes a minute to start): `powershell -File <coordinator folder>\tools\inventory\dump.ps1 <all addresses>`. It writes `analysis\export\dump\<address>.txt`, which the workers read.
3. **Push the batch branch**, so the workers can start from it.
4. **Start the workers,** all 3 at once in one message. Use the Agent tool with:
   - `subagent_type: "general-purpose"`, `model: "sonnet"`, `isolation: "worktree"`;
   - the prompt from [worker-brief.md](worker-brief.md), the part between the two lines;
   - the placeholders filled in: `{WORKER}` W1–W3, `{UNIT}`, `{ADDRESSES}`, `{AREA}`, `{BATCH_BRANCH}`, `{WORKER_BRANCH}` = `decomp/w<n>-<yyyy-mm-dd>-<run>`, `{DATE}`.

   Then wait for their reports; you are notified when each one finishes, so don't poll. A worker that hasn't reported after 2 hours: stop it (TaskStop) and use what it committed.
5. **Merge** each worker's branch into the batch branch: `git merge --no-edit <branch>`.
   - The shared lists (`progress/symbols.txt`, `attempts.txt`, `held.txt`) merge by union (`.gitattributes`), so they don't conflict.
   - Any other conflict: `git merge --abort`, leave that worker out of this batch, and say so in the PR.
6. **Check** the merged result:
   1. `git diff origin/main --stat -- tools .github CLAUDE.md` must show nothing. Workers may not touch these. If one did, take its changes out (`git checkout origin/main -- <file>`) and report it.
   2. `python tools/decomp/check_rules.py` must report 0 problems. For a flagged unit, take its functions out of `unit.json`, and report it. Don't try to judge the code yourself.
   3. `python tools/verify/verify.py` must match everything listed in the `unit.json` files. Any function that doesn't: take it out of `unit.json`, and add a line to `progress/attempts.txt` (`<address>  <date>  <unit>: did not match after merging`).
   4. `progress/symbols.txt`: no name may appear twice with different addresses (a quick script). Identical duplicate lines are harmless.
7. **Record:** `python tools/verify/verify.py --record`, then `python tools/inventory/make_progress.py`. Commit (`Decomp: <n> functions match (<units>)`) and push.
8. **Update the PR** "decomp batch <yyyy-mm-dd>" (open it the first time; template below).
9. **Clean up:** remove each merged worker's worktree and branch (`git worktree remove --force <path>`, `git branch -D <branch>`).

## Between runs, and the end

- Before each new run: check the PAUSE file, then the budget rule, then the time.
- **Time:** start a run only if it can end (about 90 minutes) before the window closes. The windows are 06:15 for the night session and 17:15 for the workday session.
- **Stop** after 3 errors in a row that are not about one function (compiler, Ghidra, git or GitHub not working). Write them in the PR.
- **At the end:** delete the lock, push, and update the PR with the session's totals and `python tools/decomp/next.py --status`.

## The batch PR

Title: `decomp batch <yyyy-mm-dd>`. Body, in plain language:

- **Table:** one row per unit, with matched / held / failed counts.
- **Totals:** this PR, and the whole project (`next.py --status`).
- **Runs:** start and end times, and usage before and after each run.
- **Anything unusual:** a check that flagged something, a worker that conflicted or timed out, tool problems.
- **Lessons:** what the workers reported as compiler lessons. Add the new ones to `docs/decomp/matching-notes.md` in the same PR.
- How it was verified (`verify.py`, `check_rules.py`), and that there is nothing to test in the game.

## Never

- Never edit `tools/verify/`. CI hashes it.
- No inline assembly, naked functions, embedded bytes, object patching or `#pragma` tricks aimed at the checker.
- Never commit game files: no exe, dll, pak, data, maps, saves, Ghidra databases, or the `analysis\export\dump\` files.
- Never change the game folder or `C:\Games\H5_test`. These runs do not start the game.
- Don't "improve" the original code: keep its bugs.
- Never merge a PR, push to `main`, or change branch rules.

## Left for Opus (the architect, in the owner's sessions)

- `python tools/decomp/next.py --big`: functions over 1 KB, and the ones a worker failed.
- `progress/held.txt`: functions waiting for the verifier fix (Q8).
- Merging unit folders that turn out to be one source file. Moving functions between folders is fine as long as the verifier still matches them.
- Shared headers for classes several units use (the pilot's reports list layouts in each unit's `.cpp`).
