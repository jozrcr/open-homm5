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

## Q3: May the AI pathfinding speed-up be on by default? (2026-10-24, status: open)

Context: the `fast_paths` mod (and its ported search core) makes the AI's late-game thinking about 5 to 6 times faster (save_11: 93 s of AI thinking over 4 AI turns down to about 16 s) and gives exactly the same result for every search (checked on four of your saves, node by node, over 4 whole AI turns each). A robot LAN co-op game with it on stayed in sync (12 AI turns, 2,539 world checksums equal on both PCs). But we found that the original AI's decisions depend on how fast it runs: it thinks in real-time slices (`aai_step_limit`, `ai_time_limit`) and some of its code reads the clock. So when it thinks faster, it can sometimes take a different (not worse) branch, exactly as the original game does on a faster PC. With the robot's virtual clock, where thinking takes no game time, the original and the mod make the same decisions over whole turns.

Your rule was: changes to AI decisions are settings, original by default. This one does not change any search, only how fast the game's own code runs, like a faster CPU would.

Options:
1. **(Recommended)** On by default once it has been checked on more saves: it is the same code, faster; `fast_paths=0` stays available.
2. Off by default, like the decision-changing settings.

Answer:

## Q2: How should CI get the 2003 compiler? (2026-10-20, status: open, not urgent)

Context: CLAUDE.md wants CI to re-verify every matched function on each push, which needs `cl.exe` 13.10.3077. Microsoft's license does not let us commit the compiler to a public repo.

Options:
1. **(Recommended)** CI (a GitHub Actions Windows runner) downloads the Toolkit 2003 v1.01 installer from archive.org on each run, checks its SHA-256, and unpacks it with `tools/toolchain/extract_vctoolkit2003.py`. Free and simple, but depends on archive.org being up (we can cache the download between runs).
2. A self-hosted runner on the owner's PC, which already has the compiler. No download, but the PC must be on.
3. Run the verifier only locally, without CI. Simplest, but regressions could slip in.

Answer:

## Q1: Hooking library for the hook DLL (2026-10-20, status: open, needed for session 2)

Context: the hook DLL (`src/hook/`, a `dinput8.dll` proxy) must redirect some game functions to ours. That means overwriting the first bytes of a game function in memory with a jump (a "detour").

Options:
1. **(Recommended)** MinHook (BSD 2-clause license, small, widely used, handles the tricky cases such as relocating overwritten instructions). We would add its source to the repo under `src/hook/third_party/`. This is a download, so it needs your OK.
2. Our own minimal detour code (about 100 lines). No dependency, but we would need to handle each patched function's first instructions by hand. Fine for a few hooks, error-prone for many.

Answer:
