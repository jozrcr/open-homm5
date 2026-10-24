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

// --- remembered answers per tile (fast_paths.cpp) -------------------------------

// One bit per check in a tile's record.
enum Check : WORD {
	kCheckGoAshore = 1,
	kCheckGoAshoreFlag = 2,
	kCheckCanEnter = 4,       // 0x009d1410
	kCheckCanEnterDry = 8,    // 0x009d1510
	kCheckVisitable = 16,     // 0x00c072d0
	kCheckStandsHere = 32,    // the object pool's slot 1 (search loop)
	kCheckLiveObject = 64,    // the object pool's slot 2 finds a live object (0x009d19a0's first test)
	kCheckDanger = 128,       // the danger map's value (NWorld::CDangerPool slot 0, 0x00c27500), in Tile::danger
	kCheckWorld = 256,        // the world tile's kinds, in Tile::kind8 and Tile::kind10
	kCheckStepCost = 512      // the step cost on land (0x00ca9a70, kind 0), in Tile::cost and Tile::diagonal
};

// Remembered answers, one record per tile (x and y below 512, 2 floors; other
// positions are not remembered). A record belongs to the current search when
// its generation is g_generation, so a new search needs no clearing.
// Neighbouring tiles sit next to each other, which keeps the lookups in the
// CPU cache.
struct Tile {
	DWORD generation;
	WORD known;         // checks answered in this search
	WORD value;         // their answers
	DWORD stands_here;  // what the object pool wrote when something stands here
	int danger;         // the danger map's value at this tile
	WORD cost;          // step cost onto this tile, straight
	WORD diagonal;      // and diagonal, when the movement left allows it (see StepCost)
	BYTE kind8;         // the world tile's kind at +8 (2 = water, 5 = blocked)
	BYTE kind10;        // and its other kind at +0x10
	WORD unused;
};
const DWORD kTileBits = 9;
const DWORD kTileFloors = 2;
const DWORD kTiles = kTileFloors << (2 * kTileBits);
extern Tile *g_tiles;  // nullptr when remember_checks=0
extern DWORD g_generation;
extern bool g_count_hits;  // robot statistics

// The record of the tile at `position` for this search, or nullptr (then every
// check goes to the game).
inline Tile *TileFor(DWORD position)
{
	const DWORD x = position & 0x3ff, y = (position >> 10) & 0x3ff, floor = position >> 20;
	if (!g_tiles || x >> kTileBits || y >> kTileBits || floor >= kTileFloors)
		return nullptr;
	Tile *t = &g_tiles[(floor << (2 * kTileBits)) | (y << kTileBits) | x];
	if (t->generation != g_generation) {
		t->generation = g_generation;
		t->known = 0;
		t->value = 0;
	}
	return t;
}

// Asks the game (and remembers the answer in t): kCheckCanEnter,
// kCheckCanEnterDry, kCheckVisitable or kCheckLiveObject.
bool CheckMiss(WORD check, BYTE *pf, const DWORD *node, Tile *t);
void CountHit(WORD check);

// A remembered check for the tile record t (from TileFor(node[0])).
inline bool Remembered(Tile *t, WORD check, BYTE *pf, const DWORD *node)
{
	if (t && (t->known & check)) {
		if (g_count_hits)
			CountHit(check);
		return (t->value & check) != 0;
	}
	return CheckMiss(check, pf, node, t);
}

// The ported expansion calls this once per node: a different pathfinder than
// the last one starts a new generation of remembered answers.
void NotePathfinder(BYTE *pf);

// Remembered checks (fast_paths.cpp). Each behaves exactly like the game's
// function of the same purpose; the answer may come from memory.
bool GoAshore(void *world, void *player, const DWORD *node, char flag);  // 0x008649f0
bool CanEnter(BYTE *pf, const DWORD *node);                              // 0x009d1410
bool CanEnterDry(BYTE *pf, const DWORD *node);                           // 0x009d1510
bool Visitable(BYTE *pf, const DWORD *node);                             // 0x00c072d0

// Relax a neighbour (0x00c09d90): the game's function, or the port when it is on.
// memo: the candidate tile's record (TileFor(candidate->d[0])) when the caller
// has it, else nullptr.
void Relax(BYTE *pf, Node *from, Node *grid, Node *candidate, int step, Cost cost, const DWORD *extra, DWORD flag,
           Tile *memo = nullptr);
// The port itself (fast_paths_relax.cpp).
void PortedRelax(BYTE *pf, Node *from, Node *grid, Node *candidate, int step, Cost cost, const DWORD *extra, DWORD flag,
                 Tile *memo = nullptr);

// The open list's _Adjust_heap (the port in fast_paths.cpp, same order as the game's).
void AdjustHeap(Node *first, int hole, int bottom, const Node &val);
// The object pool's "something stands here?" (slot 1), remembered per search.
bool StandsHere(void *pool, DWORD *out, const Node *node);
// Run the expansion through the search loop's call site 0x00c08b2d (the game's,
// the port, or a robot trace on top).
void RunExpand(BYTE *pf, Node *node, int count, const int *dirs, int remaining, char flag, DWORD *out);
// The ported search loop (fast_paths_search.cpp), 0x00c08890.
void PortedSearch(BYTE *pf, int limit);
// While a ported search of pf runs, its open list lives in the search: relax
// pushes there (true), otherwise into the game's vector (false).
bool PushToActiveSearch(BYTE *pf, const Node &n);

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
