# The hero pathfinder

On big maps late in the game, the AI spends almost all of its thinking time searching paths for heroes (see [ai-turn.md](ai-turn.md#late-game-the-ais-thinking-is-mostly-pathfinding-2026-10-22)). This page describes that code and what we measured, as a basis for a faster version that returns exactly the same results.

Addresses are in the vanilla 3.1 exe and in `MMH55_Utility(_64).exe`, which has the same code.

## The pieces

| Address | What | Notes |
|---|---|---|
| `0x00c076d0` | build a **settings block** | `__thiscall(int mode, ?)`; mode 0–5 sets a dozen flags (which obstacles count, …). 13 callers. |
| `0x00c07920` | **prepare** the pathfinder | Clears the two "reached" mark grids only every 255 searches: each search gets a new mark value (byte at `this+0xd8`), so "reached in this search" = mark equals the current value. Resizes the open list. 12 callers. |
| `0x00c08be0` | **query**: search from a start | `__thiscall(const DWORD *start, const Settings *s)`, `ret 8`. Copies the settings to `this+0x20` (`0x00c09c20`), stores the start at `this+0x8c`, puts the start node (and extra start nodes from the vector at `this+0x98`) in the open list, runs the search. 16 call sites. |
| `0x00c08890` | **search** loop | `__thiscall(int limit)`, `ret 4`. Pops the best node, skips it if the next one is the same tile, runs checks (a callback on the object at `this+4`, which is the world's `NWorld::CMobileObjectsPool`), expands it, stops at the target (`this+0x3c`) or when the open list is empty (then sets the byte at `this+0x90`). |
| `0x00c07f80` | **expand** a node | `__thiscall`, 6 arguments, `ret 0x18`; 2,306 bytes. Tries the 8 directions (table `0x00f757c8`). Called once per popped node. |
| `0x00c09d90` | **relax** a neighbour | `ret 0x28`. Computes the neighbour's new cost and replaces the neighbour's node when the new one is better (`0x00c070c0` / `0x009d1210`), or equal with a smaller tie-breaker (`node[3]`). Then pushes a copy on the open list (`0x00c099d0`). A valuation callback (object at `this+0xc`, slot 0) can make a tile's cost **negative** (a gain). Over the limit `this+0x40`, it sets the byte at `this+0x91` instead. |
| `0x00c08fd0` | open-list order | `__fastcall(const Node &a, const Node &b)`, the `operator<` of the open list. Compares `+0x20` (or the min/max of `+0x20`/`+0x30` when flag bit 23 of `+4` is set), then the 64-bit value at `+0x28` (plus `+0x38` with that flag), then `+0x0c`. |
| `0x00c099d0`, `0x00c09500`, `0x00c095b0` | open list | A `std::priority_queue` of 64-byte nodes (vector at `this+0x10`..`this+0x18`): `push`, and the compiler library's `_Push_heap` and `_Adjust_heap` (VC++ 2003 Dinkumware STL). Library code, not decompiled; their exact algorithm matters for the order (see below). |

The free functions here take their first two arguments in `ECX`/`EDX` (`__fastcall`), even the STL templates. The game was probably compiled with `/Gr` (fastcall by default). To be confirmed when matching such a function.

### Data

- **Positions** are packed in one `DWORD`: `x | y << 10 | floor << 20`. `0x00ffffff` = none.
- **Grids** (one per layer; layer 1 is used when node flag `0x40` is set, probably "on a boat"): vectors of 16-byte floor descriptors `{data, rows, width, height}`; `rows[y]` points to the row. Nodes: `this+0xa8` (layer 0) and `this+0xb4` (layer 1), 64 bytes per tile. "Reached" marks: `this+0xc0` and `this+0xcc`, 1 byte per tile.
- **Node** (64 bytes): `+0` position, `+4` flags (bit 6 layer, bit 9, bits 15–22 a counter used in layer 1, bit 23 "over the limit"), `+8` movement spent, `+0x0c` tie-breaker, `+0x14` previous position, `+0x20`..`+0x3c` cost fields (several 64-bit values).
- The AI's own pathfinder is a global object at `0x00fe8618`. Other users have their own instances (for example a stack object for the `0x00cebb22` searches).

## Measured (2026-10-23, robot `--trace-paths`, owner's save_11, day 50, huge map)

`src/hook/robot_paths.cpp` patches every call site and logs, per AI step, the searches by call site with their time and the number of expanded nodes; for big searches also the number of distinct tiles and a fingerprint of the result.

Of ~23 s of AI thinking in 12 AI steps, **~100% was path searching**:

| Call site (caller) | Per step | What it looks like |
|---|---|---|
| `0x0086c27c` (`0x0086c010`) | 1 search, up to **9.6 s** | The AI's hero, limit 32,000 (several days), whole map. One search expanded **5.2 million nodes on 35,773 tiles** (145 times per tile). |
| `0x008724eb` (`0x00872330`) | 9 searches, **1.1 s**, every other step | 9 heroes (probably all players' heroes), limit ~3,100–3,520 (one day of movement). **Identical results every time** (same node counts), as those heroes did not move. 10 µs per node, 5 times slower per node than the big search. |
| `0x0086cf95` (`0x0086cd10`) | 8 searches, 0.7 s, at the start of the turn | Same heroes, limit 11,200–12,880. |
| `0x00cebb22` (`0x00ceb710`) | 1 search, 0.13–0.39 s | Its own pathfinder object, with a target. |
| `0x00a798b3`, `0x00867851`, … | up to 185 small searches | ~0 ms each. |

### Why the big search is slow

It is not a textbook shortest-path search. Because the valuation callback can subtract gains (treasures, …) from a tile's cost, a tile can keep getting better after it was expanded, and every improvement pushes it on the open list again. Each pop then expands the tile's **current** node. Only ~1–2% of re-expansions repeated identical node data (`same=`). So the extra work is genuine (each re-expansion propagates an improvement), and simply skipping re-expansions would change results.

The final result depends on the exact order of operations, so a faster version that must give **the same paths** has to keep that order: same pops, same comparisons, same tie-breaks. It can only make each step cheaper:

- **Open list:** 64-byte nodes are copied on every heap move (`_Adjust_heap` copies 16 dwords per level). An index heap that runs the same algorithm (Dinkumware's: move the hole down to the bottom along the larger child, then sift the value up) over snapshots stored once per push gives the same order with 4-byte moves. The heap functions alone were 24% of the samples.
- **Expansion:** lookups of objects on tiles (`NWorld::CMobileObjectsPool`, `0x0045d7c0` / `0x0045dbe0`, and virtual methods of map objects) are repeated each time a tile is expanded again (145 times in the big search). Caching what depends only on the tile, per search, would keep the results while cutting that work. Which parts are pure is to be read from the decompiled expansion.
- **Across steps:** the nine one-day searches of `0x008724eb` return the same result step after step. Reusing them while nothing they depend on changed would save ~1.1 s per AI move, but needs a reliable "nothing changed" signal (`NWorld::CRMVersionTracker` looks related). Riskier; later.

### Checking a faster version

The robot logs a fingerprint of each big search's result (`result=`, a hash of every node reached, both layers), and one per node field and per flag bit (`fields=`, `bits=`). `tools/robot/path_report.py` compares runs search by search.

**Determinism (2026-10-23):** runs of the unchanged game on save_11 ran the same 33 big searches in the same order, with the same node counts and the same number of tiles reached. Their results differed only in bytes the game never reads:

- **fields 9 and 13** (`+0x24`, `+0x34`): the node holds two cost values `{int a; /* 4 bytes padding */ __int64 b;}` at `+0x20` and `+0x30`. The padding is filled from uninitialized locals (`local_4c` in the query and the expansion, `uStack_70` in relax). Every function that compares or adds costs (`0x00c08fd0`, `0x00c070c0`, `0x009d1210`, `0x009d1250`) reads only `a` and `b`.
- **flag bits 10–14** (`0x7c00` of `+4`): never initialized for the start node and copied along. Only the debug dump in the query (`"V=%d,P=%d,B=%d,D=%d,..."`, when the byte at `0x010c5889` is set) prints bits 11–14.
- **field 7** (`+0x1c`): also never initialized in the start node (the query builds it on the stack and only sets other fields), copied to every node from there. It differed once, in one search on save_10, with the mod (the mod's extra stack frames change what is left on the stack). No read of it was found in the search, expansion, relax or comparison code; to be confirmed when these functions are decompiled.

Not every save is reproducible. On `sauvegarde_14`, two runs of the plain game already differed in the very first search (same start, 14,505 vs 14,737 nodes), and from the third search on the AI searched for different heroes. Something differs before the AI starts thinking (cause unknown; a time-based random seed is a guess). Such saves cannot be used to check a mod; `tools/robot/path_check.py` runs the plain game twice per save and skips them. Reproducible so far: save_1, save_9, save_10, save_11.

**The AI's decisions depend on how fast it runs (2026-10-24).** With all three ported functions on (below), save_10 did the same 1,083 searches as the plain game, small ones included, with identical results, reached lists and open lists, and then the AI took a different branch. The AI works in real-time slices (`aai_step_limit` per call, `ai_time_limit` per turn, `0x0086dd40`), about 20 places in the AI's code read the clock (`GetTickCount`, `clock()`; e.g. `0x00c42530` throttles some update to every 100 ms), and game time keeps running while the AI thinks. So when the searches get faster, the AI can fit more into a slice and decide differently, just as the original game decides differently on a faster PC. A plain game run versus a mod run can therefore only be compared over whole turns when the game's clock does not depend on real time.

**The robot's virtual clock** (`src/hook/robot_clock.cpp`, `run_ai_test.py --virtual-clock`) does that: the game's main thread sees a clock that advances 16 ms per frame and by the time it sleeps, never while code runs, and a fixed date. AI thinking then takes no game time at all. With it, save_10 plain versus all three ports: **1,481 of 1,481 searches identical over 5 whole AI turns**. Use `--virtual-clock` for every check of a speed-up.

With those left out, every run matched every other run on all 33 searches. A faster version is checked the same way: `path_report.py <plain run> <mod run>` (these bytes are left out by default; `--strict` compares everything). It must match the original on every other byte of every reached node, search after search.

## The fast paths mod

`src/mods/fast_paths.cpp` (`[ai] fast_paths=1`, off by default while it is being checked) keeps every step of the search and makes the steps cheaper:

- **Remembered checks:** four checks of the expansion that read only the tile at `node->position` (go ashore `0x008649f0`, step onto `0x009d1410`, step onto dry land `0x009d1510`, visitable `0x00c072d0`) are answered from memory after their first call in a search. In the giant search, 99% of the calls were answered from memory.
- **Faster open list:** the two heap functions are replaced by a port of the same algorithm and of `operator<` `0x00c08fd0`, which compares in place instead of copying both nodes first.
- **Ported search core** (each with its own switch, off by default while being checked): `port_relax` (`fast_paths_relax.cpp`, `0x00c09d90`), `port_search` (`fast_paths_search.cpp`, the loop `0x00c08890`, with the object pool's per-pop "something stands here" answer remembered per tile), `port_expand` (`fast_paths_expand.cpp`, `0x00c07f80`, written by a parallel agent from the disassembly). They follow the game's code step by step, calling the same game functions in the same order; they are the base for further speed-ups now that the whole loop is our code. Checked: each alone matched save_11 (33/33) and save_10 (162/162); all three together matched save_11 (33/33) and, with the virtual clock, save_10 over 5 whole turns (1,481/1,481 searches of every size).

Batch check 2026-10-24 (`path_check.py`, 4 AI turns per save, `--quick-combat --virtual-clock 50`, all three ports on; each save first reproduced by a second plain run):

| Save | Big searches identical | AI thinking (with trace), plain → ports |
|---|---|---|
| save_11 (day 50, huge map, 82 AI steps) | 301 / 301 | 96.3 s → 47.5 s |
| save_10 | 152 / 152 | 18.3 s → 9.7 s |
| save_9 | 107 / 107 | 7.4 s → 3.6 s |
| save_1 (early game) | 10 / 10 | 0.5 s → 0.4 s |

### The go-ashore check (2026-10-24)

With the ports on, half of the remaining AI thinking on save_11 was the go-ashore check `0x008649f0` (25 s of 49 s), called ~5 million times in 4 AI turns for boat-layer nodes, half of them first calls for a tile in a search. Each call runs three scans over **every object of the floor** (`0x00cea350` object standing on the tile, `0x00cea010` object with an active cell there, `0x00ceab70` object whose zone of control covers it; ~10 µs per call). Remembering answers across searches did not help (each AI step has one boat-heavy search, and the world changes between steps).

`fast_paths_ashore.cpp` (`[ai] fast_ashore=1`, written by a parallel agent from the disassembly) builds, once per AI step and floor, three tables "which object covers this tile" in one pass over the floor's object list, keeping the game's order rules (first match for two scans, last match for `0x00cea010`), and answers each call with lookups plus the cheap tile tests. Self-check mode (`ashore_check=1`, both answers compared on every call): no difference in ~2.4 million calls. Referee: identical over 4 whole turns on save_11 (301/301) and save_10 (152/152).

AI thinking on save_11, 4 AI turns (85 AI steps), no trace, `--quick-combat --virtual-clock 50`:

| | AI thinking |
|---|---:|
| game | 93.3 s |
| ported search core | 44.0 s |
| + fast go-ashore check | **22.7 s** (x4.1) |

Next costs (robot timers, `--trace-paths`): the visit check `0x009d1700` (4 million calls, ~1.3 µs each; it asks the AI's evaluator at `pf+0x94` and writes into the candidate node, so it is not a simple remembered answer), then the search machinery itself.

### Cheaper steps (2026-10-24)

After the go-ashore check, most of the time was our own ported code, mostly waiting for memory: the game's grids are big (64 bytes per tile and layer for the nodes, 36 bytes per tile for the world map), and each expansion touches 8 neighbours in 3 rows. The exact speed-ups since then:

- **Per-tile records inline.** The remembered answers sit in one record per tile (`Tile`, 24 bytes, a direct array, no hashing), looked up once per neighbour and passed on to relax. A neighbour without a live object skips `0x009d19a0` (which then only returns false).
- **The world tile's kinds and the step cost are remembered per tile and search.** The step cost `0x00ca9a70` changes nothing and reads only the tile's first two dwords, the search's settings (`pf+0x20`) and its arguments. For kinds 0–2 it computes a straight cost, then for a diagonal step the straight cost × √2 rounded down, and returns that unless the movement left is between the two. So two calls to the game's own function per tile (movement left 0 and `INT_MAX`) give every later answer exactly, with no float code of ours. Kinds 1 and 2 do not depend on the tile (100 and 141); kind 3 is another function and always goes to the game.
- **The open list inside the ported search** is a heap of 32-byte entries: the node's sort key (both readings of `operator<` `0x00c08fd0`, computed once), its position and the two flag bits the loop reads. The full node copies wait in a pool, only needed to give the list back to the game when the search stops (pushed in array order with the game's own push, so the game's vector ends up identical). Same Dinkumware algorithm and comparisons, so the same pop order, ties included. Pushes from the game's own relax (`0x00c0a1e7`, reached through `0x009d22f0` → `0x009d1ce0` / `0x009d2070` for extra neighbours such as teleports) are redirected into the active list; a first version missed them and the referee caught it from the first search.
- **Relax copies only on success.** The game's relax works on a 64-byte copy of the candidate; the port keeps the two fields it changes apart and copies only when the candidate wins, which most do not.

**Co-op check:** a robot LAN game (`run_lan_test.py --fast-paths on`, every part on, map A1M4, 12 AI turns) stayed in sync: 2,539 world checksums, all equal on host and client.

**Not worth it: skipping repeated expansions.** The trace's `same=` counts expansions of a node identical to the last one expanded at the same tile and layer: 3.5% on save_11 (1.0 of 29.2 million). Skipping them could be made exact (the relax order is monotone), except where a neighbour was refused at the search's limit, which a later repeat can still accept. Too little to gain for the risk.

**Where the time goes now** (save_11, trace): the three giant searches of `0x0086c27c` (the AI's own hero, limit 32,000; 3.3, 5.2 and 1.4 million expansions) are over half of the remaining search time, then `0x00cebb22`. They are slow because of the label-correcting search itself (each tile is improved ~145 times), which an exact version must keep.

AI thinking on save_11, 4 AI turns (85 AI steps), no trace, `--quick-combat --virtual-clock 50`, every stage checked identical (the full four-save batch, 4 AI turns each: save_11 301/301, save_10 152/152, save_9 107/107, save_1 10/10 big searches):

| | AI thinking |
|---|---:|
| game | 93.3 s |
| ported search core | 44.0 s |
| + fast go-ashore check | 22.7 s |
| + inline per-tile records, live-object skip | 18.0 s |
| + open list inside the search (index heap) | 17.4 s |
| + step cost and world kinds per tile, compact heap entries, relax copies only on success | **16.6–16.7 s** (x5.6) |

The last two changes (heap entries holding the position, relax copying only on success) gave no measurable gain on their own (runs vary by ±0.2 s). **Tried, slower:** software prefetch of the 8 neighbours' grid nodes, marks and records at the start of an expansion, and of the heap's next level (17.1–17.4 s): the data is mostly in the CPU caches already, and what is left is the search's own work. So this exact approach is close to its floor; the next big gains need either a different search order (a setting, see the owner's "option 2") or less waiting between AI moves (the 400 ms per step, [ai-turn.md](ai-turn.md#what-sets-the-400-ms-rhythm-2026-10-21)).

### Experiment: a cap on re-expansions (2026-10-24, NOT exact, off by default)

The owner's "option 2" is a faster search order that may change the AI's paths, as a setting with the original by default. A first measurement: `[ai] max_expansions=N` (with the ported search loop) expands a land tile at most N times per search; later pops of that tile are skipped. A normal search expands a tile 1–5 times, so the cap only bites in the label-correcting giant searches.

save_11, 4 AI turns, virtual clock; "identical" against the plain game's trace run, `path_report.py`:

| Cap | AI thinking (no trace) | Big searches identical |
|---|---:|---:|
| none (exact mod) | 16.7 s | 301 / 301 |
| 16 | **8.3 s** | 290 / 301 |
| 4 | 7.6 s | 288 / 301 |
| 1 | 7.2 s | 0 / 301 |

With a cap of 16, the 11 searches that differ are the AI hero's long-range searches (`0x0086c27c`, and one `0x0086c3e4`, limit 32,000). They drop from 0.4–5.2 million expansions to 150–280 thousand (traced: 36.8 s → 2.0 s). Their results differ: tiles reached within the limit change by up to 20%. **All other searches were identical, in the same order with the same starts**, so in these 4 turns the AI apparently made the same moves.

Other saves, cap 16, traced:
- save_9: 104 of 107 searches identical, the same 49 AI moves.
- save_10: the AI took another branch after search #20 (36 AI moves instead of 41 in 4 turns, 76 of 152 searches identical).

So the cap **does change decisions sometimes**, as expected of option 2; whether the AI plays worse, the same or better is not known yet. Next: AI-vs-AI games with and without the cap (the phase 5 test).

Results (2026-10-23). AI thinking is the sum of the `think=` times of the AI steps, measured without the path trace (which costs time itself); "identical" is checked with the trace on, by `path_report.py`:

| | game | mod |
|---|---:|---:|
| save_11 (day 50, huge map): AI thinking in 12 AI steps | 22.4 s | 11.6 s (x1.93) |
| save_11: longest AI step (one giant search) | 9.4 s | 5.1 s |
| save_11: 9 one-day searches (`0x008724eb`) per step | 1.11 s | 0.53 s |
| save_11: big searches with identical results | | 33 of 33 |
| save_10: path searches in 5 AI turns | 24.1 s | 13.6 s (x1.76) |
| save_10: big searches with identical results | | 162 of 162 |
