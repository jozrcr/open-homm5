# Open questions for the owner

When a rule in CLAUDE.md blocks the work, or a decision belongs to the owner, it goes here instead of being worked around. Newest first. Mark answered questions with the answer and date, and keep them for history.

Format:

```
## Q<n>: short title (date, status: open | answered)
Context: what we were doing and why it matters.
Options: what the choices are, with a recommendation.
Answer: (owner fills in)
```

---

## Q10: Show moves sooner after a click by default? (2026-10-25, status: answered)

Context: the plan is [plans/responsiveness.md](plans/responsiveness.md). In multiplayer, the game shows every move 400 ms after its step, on purpose, as a safety margin (`view_delay_ms`). With our 100 ms steps the real wait is usually 100 to 250 ms. A lower margin makes moves start sooner, for your clicks and for the AI's moves. In robot tests, AI hero moves dropped from 0.61 to 0.46 s with 200 ms, with no desync. The risk: when a step arrives later than the margin, that move starts with a small jump (the hero is already a few steps along). It never changes the game itself, only when this PC shows it, and each PC can choose.

Options:
1. **(Recommended)** Default `view_delay_ms=250`, after one real game with the trace on confirms that your waits stay under it. The next game's log will show this.
2. Default 200: faster, more jumps on slow links.
3. Keep the game's 400. Each player can still lower it in `open-homm5.ini`.

Answer (2026-10-25): yes, and lower: everyone has a good PC and fiber, so 250 ms still feels delayed. Default **150 ms while co-op timing is ON** (the game's 400 otherwise); tune it with the trace of the next game.

## Q9: A better random map generator: which route, and which maps first? (2026-10-25, status: answered)

Context: the owner wants team maps and much more control over random maps (neutral zones, loot, personalization). The plan is [plans/map-generator.md](plans/map-generator.md), with the findings in [subsystems/rmg.md](subsystems/rmg.md). The decompilation is paused after the 2026-10-25 night batch.

Questions:
1. **Route:** the template maker plus ready templates now, then teams on random maps through the DLL, then the generator's code? **(Recommended.)** Or go straight to decompiling the generator?
2. **Which maps first?** For example: 3 allied humans vs 3 AI on XL, 2 vs 2, or more neutral zones in free-for-all.
3. **Template maker:** is a separate page that writes the template file OK for a start (later inside the game)? **(Recommended.)**
4. **Teams:** is one setting in `open-homm5.ini` that every player sets the same OK, for example `teams=humans_vs_ai`? Or do they have to be chosen in the lobby?

Answer (2026-10-25):
1. **A + B now, C later**, as recommended.
2. **The first maps:**
   - **2v2v2** with neutral zones, in two variants: no extra towns, and extra towns in the neutral zones;
   - **2 vs 2**;
   - **more neutral zones**;
   - **bigger and richer maps**.
3. A separate page is fine.
4. **In the lobby**, like on normal maps. So the first task of step B is to find out how the lobby handles teams on random maps.

## Q8: May the verifier learn about jump tables and loop alignment? (2026-10-24, status: open)
## Q8: May the verifier learn about jump tables and loop alignment? (2026-10-24, status: answered)

Context: in the pilot, 4 functions came out byte-identical to the exe, but the verifier still says "differs". It has two blind spots:
- **Size:** it takes each function's size from Ghidra, and Ghidra doesn't count the filler bytes a loop jumps over (3 or 6 bytes), so it says "obj 73 bytes, exe 70" for a perfect copy.
- **Jump tables:** the compiler puts a `switch`'s jump table at the end of the function, and the verifier doesn't understand table entries that point back into the same function (it even crashed on one).

The verifier is protected (CLAUDE.md: never edit `tools/verify/` to make a function pass; CI checks its hash), and the safety check rightly stopped me from changing it together with its hash. So it's your call. The proposed change (applied after the answer below, in `tools/verify/verify.py`) keeps comparing every byte, and changes two rules:
- **Size:** a function must be at least Ghidra's count **and** end exactly where the exe's function ends (only the linker's `0xCC` filler or the next function after it).
- **Jump-table entries** must point to the same place inside the function in the exe.

Tested locally, without committing:
- **The 4 held functions:** all match.
- **The 67 already matched:** they still match.
- **Wrong code is still rejected:**
  - a jump-table entry pointing at the wrong case;
  - a function cut short by 4 bytes;
  - a function cut back to Ghidra's 70 bytes;
  - one changed byte;
  - a check at the wrong address.

Options:
1. **(Recommended)** Apply the change. I commit it with the new hash in `.github/workflows/decomp.yml`, in its own PR for you to review and merge. Then the 4 held functions are added. About 1 game function in 18 is affected: by a scan of the exe, 2,359 have loop filler and 383 a jump table, out of 48,609. Without the fix, none of them can ever count as matched.
2. Fix only the sizes, in the inventory (`tools/inventory/`, not protected), and leave the jump tables for later. Then the 3 loop-alignment functions (and the 2,359 like them) can match without touching the verifier; the 383 functions with a jump table stay held.
3. Leave it as is: such functions stay held.

Answer (2026-10-24): yes, apply it (option 1). The owner trusts Claude's technical review and tests what they can.

## Q7: The continuous decompilation: where, budget, merging, order (2026-10-24, status: answered)

Context: the plan is in [plans/decomp-pipeline.md](plans/decomp-pipeline.md). AI agents decompile the game function by function, each one checked byte-for-byte, in batch PRs, on a schedule.

Options (details in the plan):
1. Where: this PC, on a schedule (recommended: nights and when you are not playing), or your Linux PC.
2. Budget: how much of your Max plan it may use (e.g. overnight only).
3. Merging: you merge one batch PR once or twice a day (recommended at first), or decomp-only PRs that pass GitHub's check merge by themselves (a branch-rule change on your side).
4. Q2: the 2003 compiler on GitHub from archive.org (recommended).
5. Order: the code our mods touch first (recommended), or plain address order.

Answer (2026-10-24):
1. This PC, overnight and while the owner is at work.
2. Up to about 90% of the subscription; the worker reads the plan usage and stops before that.
3. Batch PRs merged by the owner at first; Claude will guide the owner through the branch-rule change for auto-merge later.
4. Yes, the compiler from archive.org (see Q2).
5. The code our mods touch first.

## Q6: Easy install and easy joining: order, GitHub builds, firewall (2026-10-24, status: answered)

Context: the plan is in [plans/install-and-join.md](plans/install-and-join.md):
- a zip with one install script for Windows and one for Linux;
- LAN games over ZeroTier through our DLL (the "who is hosting?" broadcast also goes to every network adapter);
- later, built-in online play.

Options:
1. Order: LAN over VPNs first, then the package (done by a parallel agent), then co-op timing step 1. **Recommended.**
2. GitHub builds the package for each version tag, and you publish each release. **Recommended: yes.**
3. Firewall: the install script only explains how to make the ZeroTier network Private (recommended), or it offers to add a firewall rule (needs admin; Windows asks you).

Answer (2026-10-24):
1. The order is fine (done: LAN over VPNs in PR #12, the package in PR #13).
2. Yes, GitHub builds. They are free for public repositories; the owner's Actions limit is for private ones. Added to PR #13: `.github/workflows/package.yml`.
3. The firewall: the script only explains.

## Q5: Faster co-op AI moves behind a "same settings" guard: defaults, and go ahead? (2026-10-24, status: answered)

Context: the plan is in [plans/coop-timing-guard.md](plans/coop-timing-guard.md). Our DLLs check at the start of each game that every player has them with the same settings. Only then do they all switch to shorter segments and, optionally, AI hero walks that finish at once. Expected: AI moves in co-op 2–3 times faster.

Options:
1. Shorter segments: on by default when every player agrees? **Recommended: yes.** Nothing looks different.
2. Instant AI walks: off by default, since AI heroes then jump instead of walking. If possible, should it apply only to AI heroes no human can see? **Recommended: yes to both.**
3. Go ahead with step 1 of the plan (research and a message-order test)? **Recommended: yes.**

Answer (2026-10-24):
1. Shorter segments: yes, on by default.
2. Instant AI walks: **on** by default, but an AI hero that a human player can see walks normally.
3. Go ahead.

The owner also asked for an easy install and much easier multiplayer joining: see [plans/install-and-join.md](plans/install-and-join.md).

## Q4: "Option 2" (faster AI paths that may differ): go on with the re-expansion cap? (2026-10-24, status: answered)

Context: you asked for a faster search order as a setting, with the original by default. A first test is in (`[ai] max_expansions=16`, off by default). It halves the remaining AI thinking on your day-50 save (16.7 s to 8.3 s over 4 AI turns; the game alone takes 93 s). Only the AI hero's giant long-range searches change. On that save and on save_9 the AI then made the same moves; on save_10 it took another branch after a few moves (36 moves instead of 41 in 4 turns). Whether it plays worse is not known yet. See "Experiment: a cap on re-expansions" in docs/subsystems/ai-pathfinder.md.

Options:
1. **(Recommended)** Keep testing it as an off-by-default setting. Next: AI-vs-AI games with and without the cap, to see whether the AI plays worse. Later it becomes an option you can switch in-game.
2. Drop it and keep only the exact speed-up.

Answer (2026-10-24): yes, keep testing it (option 1).

## Q3: May the AI pathfinding speed-up be on by default? (2026-10-24, status: answered)

Context: the `fast_paths` mod (and its ported search core) makes the AI's late-game thinking about 5 to 6 times faster (save_11: 93 s of AI thinking over 4 AI turns down to about 16 s) and gives exactly the same result for every search (checked on four of your saves, node by node, over 4 whole AI turns each). A robot LAN co-op game with it on stayed in sync (12 AI turns, 2,539 world checksums equal on both PCs). But we found that the original AI's decisions depend on how fast it runs: it thinks in real-time slices (`aai_step_limit`, `ai_time_limit`) and some of its code reads the clock. So when it thinks faster, it can sometimes take a different (not worse) branch, exactly as the original game does on a faster PC. With the robot's virtual clock, where thinking takes no game time, the original and the mod make the same decisions over whole turns.

Your rule was: changes to AI decisions are settings, original by default. This one does not change any search, only how fast the game's own code runs, like a faster CPU would.

Options:
1. **(Recommended)** On by default once it has been checked on more saves: it is the same code, faster; `fast_paths=0` stays available.
2. Off by default, like the decision-changing settings.

Answer (2026-10-24): yes, on by default (option 1).

## Q2: How should CI get the 2003 compiler? (2026-10-20, status: answered)

Context: CLAUDE.md wants CI to re-verify every matched function on each push, which needs `cl.exe` 13.10.3077. Microsoft's license does not let us commit the compiler to a public repo.

Options:
1. **(Recommended)** CI (a GitHub Actions Windows runner) downloads the Toolkit 2003 v1.01 installer from archive.org on each run, checks its SHA-256, and unpacks it with `tools/toolchain/extract_vctoolkit2003.py`. Free and simple, but depends on archive.org being up (we can cache the download between runs).
2. A self-hosted runner on the owner's PC, which already has the compiler. No download, but the PC must be on.
3. Run the verifier only locally, without CI. Simplest, but regressions could slip in.

Answer (2026-10-24, with Q7): option 1, download from archive.org with the SHA-256 check.

## Q1: Hooking library for the hook DLL (2026-10-20, status: open, needed for session 2)

Context: the hook DLL (`src/hook/`, a `dinput8.dll` proxy) must redirect some game functions to ours. That means overwriting the first bytes of a game function in memory with a jump (a "detour").

Options:
1. **(Recommended)** MinHook (BSD 2-clause license, small, widely used, handles the tricky cases such as relocating overwritten instructions). We would add its source to the repo under `src/hook/third_party/`. This is a download, so it needs your OK.
2. Our own minimal detour code (about 100 lines). No dependency, but we would need to handle each patched function's first instructions by hand. Fine for a few hooks, error-prone for many.

Answer:
