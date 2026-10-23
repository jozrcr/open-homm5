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

With those left out, every run matched every other run on all 33 searches. A faster version is checked the same way: `path_report.py <plain run> <mod run>` (these bytes are left out by default; `--strict` compares everything). It must match the original on every other byte of every reached node, search after search.

## The fast paths mod

`src/mods/fast_paths.cpp` (`[ai] fast_paths=1`, off by default while it is being checked) keeps every step of the search and makes the steps cheaper:

- **Remembered checks:** four checks of the expansion that read only the tile at `node->position` (go ashore `0x008649f0`, step onto `0x009d1410`, step onto dry land `0x009d1510`, visitable `0x00c072d0`) are answered from memory after their first call in a search. In the giant search, 99% of the calls were answered from memory.
- **Faster open list:** the two heap functions are replaced by a port of the same algorithm and of `operator<` `0x00c08fd0`, which compares in place instead of copying both nodes first.

Results (2026-10-23). AI thinking is the sum of the `think=` times of the AI steps, measured without the path trace (which costs time itself); "identical" is checked with the trace on, by `path_report.py`:

| | game | mod |
|---|---:|---:|
| save_11 (day 50, huge map): AI thinking in 12 AI steps | 22.4 s | 11.6 s (x1.93) |
| save_11: longest AI step (one giant search) | 9.4 s | 5.1 s |
| save_11: 9 one-day searches (`0x008724eb`) per step | 1.11 s | 0.53 s |
| save_11: big searches with identical results | | 33 of 33 |
| save_10: path searches in 5 AI turns | 24.1 s | 13.6 s (x1.76) |
| save_10: big searches with identical results | | 162 of 162 |
