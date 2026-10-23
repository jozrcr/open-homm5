// Shared by the parts of the fast paths mod (fast_paths*.cpp). Not for other code.
//
// The ported pathfinder functions must do exactly what the game's do, in the
// same order (including every call into the game), so that every search gives
// the same result: see docs/subsystems/ai-pathfinder.md and fast_paths.cpp.
#pragma once
#include <windows.h>

namespace mods {
namespace fastpaths {

// A search node (64 bytes), as in the pathfinder's grids and open list.
//   d[0]  position x | y << 10 | floor << 20
//   d[1]  flags (0x40 boat layer, 0x200, 0x7f8000 counter, 0x800000 over the limit, ...)
//   d[2]  movement spent, d[3] tie-breaker, d[4] movement left, d[5] previous position,
//   d[6]  set by some checks, d[8..11] cost 1, d[12..15] cost 2 (see Cost)
struct Node {
	DWORD d[16];
};

// A cost as the game stores it at node+0x20 and node+0x30. `pad` is never read.
struct Cost {
	int a;
	DWORD pad;
	__int64 b;
};

// The pathfinder object's fields used here are read through these offsets.
inline DWORD &At(BYTE *pf, DWORD offset)
{
	return *reinterpret_cast<DWORD *>(pf + offset);
}

// Remembered checks (fast_paths.cpp). Each behaves exactly like the game's
// function of the same purpose; the answer may come from memory.
bool GoAshore(void *world, void *player, const DWORD *node, char flag);  // 0x008649f0
bool CanEnter(BYTE *pf, const DWORD *node);                              // 0x009d1410
bool CanEnterDry(BYTE *pf, const DWORD *node);                           // 0x009d1510
bool Visitable(BYTE *pf, const DWORD *node);                             // 0x00c072d0

// Relax a neighbour (0x00c09d90): the game's function, or the port when it is on.
void Relax(BYTE *pf, Node *from, Node *grid, Node *candidate, int step, Cost cost, const DWORD *extra, DWORD flag);
// The port itself (fast_paths_relax.cpp).
void PortedRelax(BYTE *pf, Node *from, Node *grid, Node *candidate, int step, Cost cost, const DWORD *extra, DWORD flag);

// The open list's _Adjust_heap (the port in fast_paths.cpp, same order as the game's).
void AdjustHeap(Node *first, int hole, int bottom, const Node &val);
// The object pool's "something stands here?" (slot 1), remembered per search.
bool StandsHere(void *pool, DWORD *out, const Node *node);
// Run the expansion through the search loop's call site 0x00c08b2d (the game's,
// the port, or a robot trace on top).
void RunExpand(BYTE *pf, Node *node, int count, const int *dirs, int remaining, char flag, DWORD *out);
// The ported search loop (fast_paths_search.cpp), 0x00c08890.
void PortedSearch(BYTE *pf, int limit);

// Robot-only timing of game helpers called by the ported functions (the path
// trace turns it on): Measure(slot, fn) runs fn and, when measuring, adds its
// time and one call to the slot. Slots are named in fast_paths.cpp.
enum HelperSlot { kHelper9d19a0, kHelper9d1700, kHelperTerrain, kHelper9d1c40, kHelper9d1310, kHelperShips,
                  kHelperPool, kHelperValuation, kHelperSlots };
bool Measuring();
void AddHelperTime(int slot, LONGLONG ticks);
template <class Fn>
auto Measure(int slot, Fn fn) -> decltype(fn())
{
	if (!Measuring())
		return fn();
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	auto value = fn();
	QueryPerformanceCounter(&t1);
	AddHelperTime(slot, t1.QuadPart - t0.QuadPart);
	return value;
}

// The exact, faster go-ashore check (fast_paths_ashore.cpp, written by a
// parallel agent): returns what 0x008649f0 __fastcall(world, player, node,
// flag) returns. Its tables of "which object covers which tile" are built once
// per world floor and kept until FastGoAshoreInvalidate(): call that whenever
// the world may have changed (at the start and end of every AI turn-driver
// call, and before every search outside one). FastGoAshoreInit allocates the
// tables (6 MB) and reads [ai] ashore_check (1 = also ask the game every time,
// return the game's answer and log every difference); false = out of memory,
// do not use FastGoAshore.
bool FastGoAshoreInit();
void FastGoAshoreInvalidate();
bool FastGoAshore(void *world, void *player, const DWORD *node, char flag);

// The ported expansion of 0x00c07f80 (fast_paths_expand.cpp): same arguments as
// the game's __thiscall(pf, node, count, dirs, remaining, flag, out), ret 0x18.
void Expand(BYTE *pf, Node *node, int count, const int *dirs, int remaining, char flag, DWORD *out);

}  // namespace fastpaths
}  // namespace mods
