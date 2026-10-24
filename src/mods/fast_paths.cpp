// Mod: faster AI pathfinding, with exactly the same results.
//
// Late in a game on a big map the AI thinks for seconds per move, nearly all of
// it in its hero pathfinder (docs/subsystems/ai-pathfinder.md). The mod speeds
// up two parts of it without changing a single step of the search, so every
// path and every AI decision stays the same; it is only faster. In co-op only
// the host runs the AI, so the other PCs are not affected either way.
//
// 1. Remembered checks (remember_checks=1). One search can expand the same tile
//    over a hundred times, because the cost of a tile keeps improving (gains
//    along the way make it more than a shortest-path search), and each time the
//    expansion 0x00c07f80 repeats checks that depend only on the tile. Their
//    answers are remembered until the search ends. A new search starts at every
//    call of the search loop 0x00c08890 (from 0x00c08e13, 0x00ce4c26,
//    0x00cea542, 0x00cebb82). The checks, all called from the expansion:
//      0x008649f0  "can the hero go ashore here?" (nodes on the boat layer), at
//                  0x00c08000. __fastcall(world = pathfinder+0x70, player =
//                  pathfinder+0x38, const Node *node, bool flag), ret 8.
//      0x009d1410  "can the hero step onto this tile?", at 0x00c082da;
//                  __thiscall(pathfinder, const Node *node), ret 4. Terrain, the
//                  tile's object, and what the object pool (pathfinder+4) holds
//                  at node->position.
//      0x009d1510  the same and "not water", at 0x00c0830a and 0x00c08349.
//      0x00c072d0  "is there something to visit here?", at 0x00c08076,
//                  0x00c084e3, 0x00c085bc, 0x00c087be. Object pool and tile.
//    Each one reads only node->position from the node, plus the world and
//    pathfinder fields that are fixed during a search. The answers are kept in
//    one small record per tile (maps up to 512 x 512, 2 floors).
//
// 2. Faster open list (fast_queue=1). The search's open list is a
//    std::priority_queue of 64-byte nodes, and the compiler library's heap code
//    copies every node it compares or moves (16 dwords, "rep movsd"). This mod
//    puts in the same two heap functions, written out again: the very same
//    algorithm (VC++ 2003 Dinkumware _Push_heap / _Adjust_heap) and the very
//    same order (operator< 0x00c08fd0, ported below), so the nodes come out in
//    exactly the same order; it only compares in place and copies faster.
//      0x00c09500  _Push_heap(first, hole, top, Node val, pred), __fastcall,
//                  ret 0x48; called by the queue's push 0x00c099d0 at 0x00c09a2f.
//      0x00c095b0  _Adjust_heap(first, hole, bottom, Node val, pred), same
//                  convention; called by the search loop's pop at 0x00c0894e.
//
// Checked with the robot (tools/robot/path_report.py): the result of every big
// search, node by node, is the same as the game's own. When it starts, the mod
// also runs the ported open list against the game's code on random nodes and
// keeps the game's if anything differs.
//
// Settings: open-homm5.ini [ai] fast_paths=1 (default 0 until it is checked on
// more saves); remember_checks and fast_queue (default 1) turn the parts off.
#include "fast_paths.h"

#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "../hook/log.h"
#include "../hook/patch.h"
#include "fast_paths_internal.h"

namespace mods {
namespace {

const DWORD kSearch = 0x00c08890;
const DWORD kSearchCalls[] = {0x00c08e13, 0x00ce4c26, 0x00cea542, 0x00cebb82};
const DWORD kGoAshore = 0x008649f0;
const DWORD kGoAshoreCall = 0x00c08000;
const DWORD kCanEnter = 0x009d1410;
const DWORD kCanEnterCall = 0x00c082da;
const DWORD kCanEnterDry = 0x009d1510;
const DWORD kCanEnterDryCalls[] = {0x00c0830a, 0x00c08349};
const DWORD kVisitable = 0x00c072d0;
const DWORD kVisitableCalls[] = {0x00c08076, 0x00c084e3, 0x00c085bc, 0x00c087be};
const DWORD kPushHeap = 0x00c09500;
const DWORD kPushHeapCall = 0x00c09a2f;
const DWORD kAdjustHeap = 0x00c095b0;
const DWORD kAdjustHeapCall = 0x00c0894e;
const DWORD kRelax = 0x00c09d90;
const DWORD kRelaxCalls[] = {0x00c080aa, 0x00c086fc, 0x00c0885b,  // in the expansion
                             0x00c08ce9, 0x00c08dee};             // in the query (start nodes)

using fastpaths::Cost;
using fastpaths::Node;

// A cost as pushed on the stack for a call (4 dwords; Cost holds an __int64).
struct CostArg {
	DWORD a, pad, low, high;
};
typedef void(__fastcall *RelaxFn)(BYTE *pf, void *edx, Node *from, Node *grid, Node *candidate, int step, CostArg cost,
                                  const DWORD *extra, DWORD flag);
bool g_port_relax = false;
bool g_port_search = false;
const DWORD kExpand = 0x00c07f80;
const DWORD kExpandCall = 0x00c08b2d;  // the search loop's call of the expansion

Cost ToCost(const CostArg &c)
{
	Cost r;
	r.a = static_cast<int>(c.a);
	r.pad = c.pad;
	r.b = static_cast<__int64>(c.low | static_cast<unsigned __int64>(c.high) << 32);
	return r;
}

CostArg ToArg(const Cost &c)
{
	const unsigned __int64 b = static_cast<unsigned __int64>(c.b);
	return {static_cast<DWORD>(c.a), c.pad, static_cast<DWORD>(b), static_cast<DWORD>(b >> 32)};
}

bool g_port_expand = false;

// The search loop's call of the expansion, when port_expand=1.
void __fastcall ExpandSiteHook(BYTE *pf, void * /*edx*/, Node *node, int count, const int *dirs, int remaining,
                               DWORD flag, DWORD *out)
{
	fastpaths::Expand(pf, node, count, dirs, remaining, static_cast<char>(flag), out);
}

// The game's calls of relax, when port_relax=1.
void __fastcall RelaxHook(BYTE *pf, void * /*edx*/, Node *from, Node *grid, Node *candidate, int step, CostArg cost,
                          const DWORD *extra, DWORD flag)
{
	fastpaths::PortedRelax(pf, from, grid, candidate, step, ToCost(cost), extra, flag);
}

typedef void(__fastcall *SearchFn)(void *self, void *edx, int limit);
typedef bool(__fastcall *GoAshoreFn)(void *world, void *player, const DWORD *node, char flag);
typedef bool(__fastcall *NodeCheckFn)(void *pathfinder, void *edx, const DWORD *node);

// --- remembered checks --------------------------------------------------------

// (the record types and the inline lookup are in fast_paths_internal.h)
using fastpaths::Tile;
using fastpaths::g_tiles;
using fastpaths::g_generation;
using fastpaths::kTileBits;
using fastpaths::kTileFloors;
using fastpaths::kTiles;
using fastpaths::kCheckGoAshore;
using fastpaths::kCheckGoAshoreFlag;
using fastpaths::kCheckCanEnter;
using fastpaths::kCheckCanEnterDry;
using fastpaths::kCheckVisitable;
using fastpaths::kCheckStandsHere;
using fastpaths::kCheckLiveObject;
const void *g_world = nullptr;
const void *g_player = nullptr;
const void *g_pathfinder = nullptr;
SearchFn g_search = nullptr;
GoAshoreFn g_go_ashore = nullptr;
NodeCheckFn g_can_enter = nullptr;
NodeCheckFn g_can_enter_dry = nullptr;
NodeCheckFn g_visitable = nullptr;
LONGLONG g_hits = 0;
LONGLONG g_misses = 0;
LONGLONG g_check_hits[8] = {};    // per check (bit number), for the robot's logs
LONGLONG g_check_misses[8] = {};
LONGLONG g_ashore_miss_ticks = 0;  // time in the game's go-ashore check (misses), for the robot's logs
LONGLONG g_miss_ticks[8] = {};      // the same per check, when g_measure (robot path trace) is on
bool g_measure = false;

// Runs a game check and, when measuring, adds its time to the check's misses.
template <class Fn>
bool Timed(int bit, Fn fn)
{
	if (!g_measure)
		return fn();
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	const bool value = fn();
	QueryPerformanceCounter(&t1);
	g_miss_ticks[bit & 7] += t1.QuadPart - t0.QuadPart;
	return value;
}

void NewSearch()
{
	++g_generation;
}

// The go-ashore answers live longer: for a whole AI thinking step (one call of
// the AI's turn driver), across all of its searches. The check looks at the
// map's objects (0x00ceab70 walks every object of the floor for one whose zone
// of control covers the tile), the AI player and the tile; none of these
// change while the AI thinks, because its moves are only queued as commands
// and run after the call. Outside AI thinking they last one search, like the
// other checks. The AI alternates searches for different players' heroes, so
// there is one memory per (world, player), for the 4 most recent pairs.
struct AshoreTile {
	DWORD generation;
	BYTE known;  // bit 0: flag 0 answered, bit 1: flag 1 answered
	BYTE value;
};
struct AshoreMemory {
	const void *world;
	const void *player;
	DWORD last_used;
	AshoreTile *tiles;
};
const int kAshoreMemories = 4;
AshoreMemory g_ashore_memories[kAshoreMemories];
AshoreMemory *g_ashore = nullptr;  // the one for the current (world, player)
DWORD g_ashore_generation = 1;     // all memories older than this are void
DWORD g_ashore_use = 0;
bool g_in_ai_step = false;
bool g_fast_ashore = false;  // [ai] fast_ashore: answer misses from fast_paths_ashore.cpp's tables

void NewAshoreGeneration()
{
	++g_ashore_generation;
	if (g_fast_ashore)
		fastpaths::FastGoAshoreInvalidate();  // the world may have changed
}

// The memory for (world, player): found, or the least recently used one reused.
AshoreMemory *AshoreMemoryFor(const void *world, const void *player)
{
	AshoreMemory *pick = &g_ashore_memories[0];
	for (AshoreMemory &m : g_ashore_memories) {
		if (m.world == world && m.player == player && m.tiles) {
			pick = &m;
			break;
		}
		if (m.last_used < pick->last_used)
			pick = &m;
	}
	if (pick->world != world || pick->player != player) {
		pick->world = world;
		pick->player = player;
		if (pick->tiles)
			for (DWORD i = 0; i < kTiles; ++i)  // forget the other pair's answers
				pick->tiles[i].generation = 0;
	}
	pick->last_used = ++g_ashore_use;
	return pick;
}

void __fastcall SearchHook(void *self, void *edx, int limit)
{
	if (!g_in_ai_step)
		NewAshoreGeneration();
	NewSearch();
	if (g_port_search)
		fastpaths::PortedSearch(static_cast<BYTE *>(self), limit);
	else
		g_search(self, edx, limit);
	NewSearch();
}

Tile *TileAt(DWORD position)
{
	return fastpaths::TileFor(position);
}

// The remembered answer of `check` at `position`, or -1.
int Recall(Tile *t, WORD check)
{
	unsigned long bit = 0;
	_BitScanForward(&bit, check);
	if (t && (t->known & check)) {
		++g_hits;
		++g_check_hits[bit & 7];
		return (t->value & check) != 0;
	}
	++g_misses;
	++g_check_misses[bit & 7];
	return -1;
}

bool Store(Tile *t, WORD check, bool value)
{
	if (t) {
		t->known |= check;
		if (value)
			t->value |= check;
	}
	return value;
}

bool __fastcall GoAshoreHook(void *world, void *player, const DWORD *node, char flag)
{
	if (world != g_world || player != g_player || !g_ashore) {
		g_world = world;
		g_player = player;
		g_ashore = AshoreMemoryFor(world, player);
	}
	const WORD check = flag ? kCheckGoAshoreFlag : kCheckGoAshore;
	const DWORD position = node[0];
	const DWORD x = position & 0x3ff, y = (position >> 10) & 0x3ff, floor = position >> 20;
	AshoreTile *t = nullptr;
	if (g_ashore->tiles && !(x >> kTileBits) && !(y >> kTileBits) && floor < kTileFloors) {
		t = &g_ashore->tiles[(floor << (2 * kTileBits)) | (y << kTileBits) | x];
		if (t->generation != g_ashore_generation) {
			t->generation = g_ashore_generation;
			t->known = t->value = 0;
		}
	}
	const BYTE bit = flag ? 2 : 1;
	unsigned long index = 0;
	_BitScanForward(&index, check);
	if (t && (t->known & bit)) {
		++g_hits;
		++g_check_hits[index];
		return (t->value & bit) != 0;
	}
	++g_misses;
	++g_check_misses[index];
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	const bool value = g_fast_ashore ? fastpaths::FastGoAshore(world, player, node, flag)
	                                 : g_go_ashore(world, player, node, flag);
	QueryPerformanceCounter(&t1);
	g_ashore_miss_ticks += t1.QuadPart - t0.QuadPart;
	if (t) {
		t->known |= bit;
		if (value)
			t->value |= bit;
	}
	return value;
}

bool Remembered(NodeCheckFn check_fn, WORD check, void *pathfinder, void *edx, const DWORD *node)
{
	if (pathfinder != g_pathfinder) {
		g_pathfinder = pathfinder;
		NewSearch();
	}
	Tile *t = TileAt(node[0]);
	const int known = Recall(t, check);
	if (known >= 0)
		return known != 0;
	unsigned long bit = 0;
	_BitScanForward(&bit, check);
	return Store(t, check, Timed(static_cast<int>(bit), [&] { return check_fn(pathfinder, edx, node); }));
}

bool __fastcall CanEnterHook(void *pathfinder, void *edx, const DWORD *node)
{
	return Remembered(g_can_enter, kCheckCanEnter, pathfinder, edx, node);
}

bool __fastcall CanEnterDryHook(void *pathfinder, void *edx, const DWORD *node)
{
	return Remembered(g_can_enter_dry, kCheckCanEnterDry, pathfinder, edx, node);
}

bool __fastcall VisitableHook(void *pathfinder, void *edx, const DWORD *node)
{
	return Remembered(g_visitable, kCheckVisitable, pathfinder, edx, node);
}

// --- faster open list ---------------------------------------------------------

// The open list's order, ported from 0x00c08fd0 (__fastcall(const Node &a,
// const Node &b): "a < b", so the heap's top is the greatest). Bit 23 of a's
// flags decides for both nodes whether the second cost pair counts.
int Primary(const DWORD *n, bool flag)
{
	int p = static_cast<int>(n[8]);
	if (flag) {
		const int q = static_cast<int>(n[12]);
		const int low = p < q ? p : q;
		if (low < 0)
			p = low;
		else if (p <= q)
			p = q;
	}
	return p;
}

__int64 Value(const DWORD *n, bool flag)
{
	unsigned __int64 v = n[10] | static_cast<unsigned __int64>(n[11]) << 32;
	if (flag)
		v += n[14] | static_cast<unsigned __int64>(n[15]) << 32;
	return static_cast<__int64>(v);
}

bool Less(const Node &a, const Node &b)
{
	const bool flag = (a.d[1] >> 23) & 1;
	const int pa = Primary(a.d, flag), pb = Primary(b.d, flag);
	if (pa != pb)
		return pa < pb;
	const __int64 va = Value(a.d, flag), vb = Value(b.d, flag);
	if (va != vb)
		return va < vb;
	return b.d[3] < a.d[3];
}

void PushHeapCore(Node *first, int hole, int top, const Node &val)
{
	for (int idx = (hole - 1) / 2; top < hole && Less(first[idx], val); idx = (hole - 1) / 2) {
		first[hole] = first[idx];
		hole = idx;
	}
	first[hole] = val;
}

void __fastcall PushHeapHook(Node *first, int hole, int top, Node val, DWORD /*pred*/)
{
	PushHeapCore(first, hole, top, val);
}

void __fastcall AdjustHeapHook(Node *first, int hole, int bottom, Node val, DWORD /*pred*/)
{
	const int top = hole;
	int idx = 2 * hole + 2;
	for (; idx < bottom; idx = 2 * idx + 2) {
		if (Less(first[idx], first[idx - 1]))
			--idx;
		first[hole] = first[idx];
		hole = idx;
	}
	if (idx == bottom) {
		first[hole] = first[bottom - 1];
		hole = bottom - 1;
	}
	PushHeapCore(first, hole, top, val);
}

// Self-test of the port against the game's own code, before it is used: random
// nodes (with many equal costs, both signs, the flag bit on and off) through
// both operator<s, and random heaps through both _Push_heap and _Adjust_heap.
// Any difference keeps the game's open list.
typedef bool(__fastcall *LessFn)(const Node &a, const Node &b);
typedef void(__fastcall *HeapFn)(Node *first, int hole, int other, Node val, DWORD pred);

DWORD g_seed = 12345;
DWORD Random()
{
	g_seed = g_seed * 1103515245u + 12345u;
	return g_seed >> 8;
}

void RandomNode(Node &n)
{
	for (DWORD &d : n.d)
		d = Random();
	static const int small[] = {-2, -1, 0, 0, 1, 2};
	n.d[1] = (n.d[1] & ~0x00800000u) | ((Random() & 1) << 23);
	n.d[3] = Random() % 3;
	for (int i : {8, 10, 11, 12, 14, 15})
		if (Random() % 4)
			n.d[i] = static_cast<DWORD>(small[Random() % 6]);
}

bool QueueMatchesGame()
{
	const LessFn game_less = reinterpret_cast<LessFn>(0x00c08fd0);
	Node a, b;
	for (int i = 0; i < 20000; ++i) {
		RandomNode(a);
		RandomNode(b);
		if (game_less(a, b) != Less(a, b)) {
			hooklog::Write("fast paths: open list order differs from the game's (self-test %d)", i);
			return false;
		}
	}
	const int kSize = 40;
	Node mine[kSize], game[kSize];
	for (int round = 0; round < 300; ++round) {
		const int size = 2 + static_cast<int>(Random() % (kSize - 1));
		for (int i = 0; i < size; ++i) {
			RandomNode(mine[i]);
			game[i] = mine[i];
		}
		Node val;
		RandomNode(val);
		const int hole = static_cast<int>(Random() % size);
		if (round & 1) {
			reinterpret_cast<HeapFn>(kAdjustHeap)(game, hole, size, val, 0);
			AdjustHeapHook(mine, hole, size, val, 0);
		} else {
			reinterpret_cast<HeapFn>(kPushHeap)(game, hole, 0, val, 0);
			PushHeapHook(mine, hole, 0, val, 0);
		}
		if (memcmp(mine, game, size * sizeof(Node)) != 0) {
			hooklog::Write("fast paths: heap differs from the game's (self-test round %d)", round);
			return false;
		}
	}
	return true;
}

bool Setting(const wchar_t *path, const wchar_t *key, int fallback)
{
	return GetPrivateProfileIntW(L"ai", key, fallback, path) != 0;
}

}  // namespace

namespace fastpaths {

Tile *g_tiles = nullptr;
DWORD g_generation = 1;
bool g_count_hits = false;

void CountHit(WORD check)
{
	unsigned long bit = 0;
	_BitScanForward(&bit, check);
	++g_hits;
	++g_check_hits[bit & 7];
}

// The object pool's slot 2 (0x0045d6b0 for NWorld::CMobileObjectsPool) finds a
// live object at node->position: what 0x009d19a0 tests first. Without one,
// 0x009d19a0 (called with flag 0, as the expansion does) returns false and
// changes nothing; "live" is the virtual base's bit 31 at +8 being clear.
bool LiveObjectAt(BYTE *pf, const DWORD *node)
{
	typedef void *(__fastcall * PoolAtFn)(void *pool, void *edx, const DWORD *node);
	void *pool = reinterpret_cast<void *>(At(pf, 4));
	if (!pool)
		return false;
	const BYTE *object = static_cast<const BYTE *>((*reinterpret_cast<PoolAtFn *const *>(pool))[2](pool, nullptr, node));
	if (!object)
		return false;
	const int base = *reinterpret_cast<const int *>(*reinterpret_cast<const DWORD *>(object + 4) + 4);
	return !(object[base + 0xb] & 0x80);
}

bool CheckMiss(WORD check, BYTE *pf, const DWORD *node, Tile *t)
{
	unsigned long bit = 0;
	_BitScanForward(&bit, check);
	++g_misses;
	++g_check_misses[bit & 7];
	bool value = false;
	switch (check) {
	case kCheckCanEnter:
		value = Timed(static_cast<int>(bit), [&] { return reinterpret_cast<NodeCheckFn>(kCanEnter)(pf, nullptr, node); });
		break;
	case kCheckCanEnterDry:
		value = Timed(static_cast<int>(bit), [&] { return reinterpret_cast<NodeCheckFn>(kCanEnterDry)(pf, nullptr, node); });
		break;
	case kCheckVisitable:
		value = Timed(static_cast<int>(bit), [&] { return reinterpret_cast<NodeCheckFn>(kVisitable)(pf, nullptr, node); });
		break;
	case kCheckLiveObject:
		value = Timed(static_cast<int>(bit), [&] { return LiveObjectAt(pf, node); });
		break;
	}
	return Store(t, check, value);
}

void NotePathfinder(BYTE *pf)
{
	if (pf != g_pathfinder) {
		g_pathfinder = pf;
		NewSearch();
	}
}

bool GoAshore(void *world, void *player, const DWORD *node, char flag)
{
	return g_go_ashore ? GoAshoreHook(world, player, node, flag)
	                   : reinterpret_cast<GoAshoreFn>(kGoAshore)(world, player, node, flag);
}

bool CanEnter(BYTE *pf, const DWORD *node)
{
	return g_can_enter ? CanEnterHook(pf, nullptr, node) : reinterpret_cast<NodeCheckFn>(kCanEnter)(pf, nullptr, node);
}

bool CanEnterDry(BYTE *pf, const DWORD *node)
{
	return g_can_enter_dry ? CanEnterDryHook(pf, nullptr, node)
	                       : reinterpret_cast<NodeCheckFn>(kCanEnterDry)(pf, nullptr, node);
}

// The object pool's slot 1 (0x0045d600 for NWorld::CMobileObjectsPool): true
// when something stands at node->position, and then *out = its value.
bool StandsHere(void *pool, DWORD *out, const Node *node)
{
	typedef bool(__fastcall * StandsHereFn)(void *pool, void *edx, DWORD *out, const Node *node);
	const StandsHereFn game = (*reinterpret_cast<StandsHereFn *const *>(pool))[1];
	Tile *t = g_tiles ? TileAt(node->d[0]) : nullptr;
	const int known = t ? Recall(t, kCheckStandsHere) : -1;
	if (known > 0)
		*out = t->stands_here;
	if (known >= 0)
		return known != 0;
	DWORD value = *out;
	const bool found = Timed(5, [&] { return game(pool, nullptr, &value, node); });
	if (found) {
		*out = value;
		if (t)
			t->stands_here = value;
	}
	return Store(t, kCheckStandsHere, found);
}

void AdjustHeap(Node *first, int hole, int bottom, const Node &val)
{
	AdjustHeapHook(first, hole, bottom, val, 0);
}

// Whatever the search loop's expansion call goes to now: the game's function,
// the port, or a robot trace stacked on top.
void RunExpand(BYTE *pf, Node *node, int count, const int *dirs, int remaining, char flag, DWORD *out)
{
	typedef void(__fastcall * ExpandFn)(BYTE * pf, void *edx, Node *node, int count, const int *dirs, int remaining,
	                                    DWORD flag, DWORD *out);
	const DWORD target = kExpandCall + 5 + *reinterpret_cast<const LONG *>(kExpandCall + 1);
	reinterpret_cast<ExpandFn>(target)(pf, nullptr, node, count, dirs, remaining, static_cast<BYTE>(flag), out);
}

bool Visitable(BYTE *pf, const DWORD *node)
{
	return g_visitable ? VisitableHook(pf, nullptr, node) : reinterpret_cast<NodeCheckFn>(kVisitable)(pf, nullptr, node);
}

void Relax(BYTE *pf, Node *from, Node *grid, Node *candidate, int step, Cost cost, const DWORD *extra, DWORD flag)
{
	if (g_port_relax)
		PortedRelax(pf, from, grid, candidate, step, cost, extra, flag);
	else
		reinterpret_cast<RelaxFn>(kRelax)(pf, nullptr, from, grid, candidate, step, ToArg(cost), extra, flag);
}

}  // namespace fastpaths

void ApplyFastPathsFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	if (!Setting(path, L"fast_paths", 0)) {
		hooklog::Write("fast paths: off");
		return;
	}
	if (Setting(path, L"remember_checks", 1)) {
		g_tiles = static_cast<Tile *>(VirtualAlloc(nullptr, kTiles * sizeof(Tile), MEM_COMMIT, PAGE_READWRITE));
		// The search hooks go first: without them the remembered answers could
		// outlive a search.
		if (g_tiles)
			g_search = reinterpret_cast<SearchFn>(hookpatch::RedirectCalls(
				kSearchCalls, sizeof(kSearchCalls) / sizeof(kSearchCalls[0]), kSearch, &SearchHook));
		if (!g_search) {
			hooklog::Write("fast paths: %s: no remembered checks", g_tiles ? "search calls not as expected" : "no memory");
		} else {
			g_go_ashore = reinterpret_cast<GoAshoreFn>(hookpatch::RedirectCall(kGoAshoreCall, kGoAshore, &GoAshoreHook));
			for (AshoreMemory &m : g_ashore_memories)
				m.tiles = static_cast<AshoreTile *>(VirtualAlloc(nullptr, kTiles * sizeof(AshoreTile), MEM_COMMIT, PAGE_READWRITE));
			if (g_go_ashore && Setting(path, L"fast_ashore", 0)) {
				g_fast_ashore = fastpaths::FastGoAshoreInit();
				hooklog::Write("fast paths: fast go-ashore check %s", g_fast_ashore ? "on" : "off (no memory)");
			}
			g_can_enter = reinterpret_cast<NodeCheckFn>(hookpatch::RedirectCall(kCanEnterCall, kCanEnter, &CanEnterHook));
			g_can_enter_dry = reinterpret_cast<NodeCheckFn>(hookpatch::RedirectCalls(
				kCanEnterDryCalls, sizeof(kCanEnterDryCalls) / sizeof(kCanEnterDryCalls[0]), kCanEnterDry,
				&CanEnterDryHook));
			g_visitable = reinterpret_cast<NodeCheckFn>(hookpatch::RedirectCalls(
				kVisitableCalls, sizeof(kVisitableCalls) / sizeof(kVisitableCalls[0]), kVisitable, &VisitableHook));
			hooklog::Write("fast paths: remembered checks: go ashore %s, step onto %s, step onto dry %s, visitable %s",
			               g_go_ashore ? "on" : "NOT as expected", g_can_enter ? "on" : "NOT as expected",
			               g_can_enter_dry ? "on" : "NOT as expected", g_visitable ? "on" : "NOT as expected");
		}
	}
	if (Setting(path, L"fast_queue", 1) && !QueueMatchesGame()) {
		hooklog::Write("fast paths: faster open list off (self-test failed)");
	} else if (Setting(path, L"fast_queue", 1)) {
		const bool push = hookpatch::RedirectCall(kPushHeapCall, kPushHeap, &PushHeapHook) != 0;
		const bool adjust = hookpatch::RedirectCall(kAdjustHeapCall, kAdjustHeap, &AdjustHeapHook) != 0;
		hooklog::Write("fast paths: faster open list: push %s, pop %s", push ? "on" : "NOT as expected",
		               adjust ? "on" : "NOT as expected");
	}
	// Ported pathfinder functions (fast_paths_*.cpp), off by default while they
	// are being checked against the game's. The ported search loop needs the
	// search hooks (remembered checks on) and the ported open list.
	if (Setting(path, L"port_expand", 0)) {
		g_port_expand = hookpatch::RedirectCall(kExpandCall, kExpand, &ExpandSiteHook) != 0;
		hooklog::Write("fast paths: ported expansion %s", g_port_expand ? "on" : "NOT as expected");
	}
	if (Setting(path, L"port_search", 0)) {
		g_port_search = g_search && Setting(path, L"fast_queue", 1) && QueueMatchesGame();
		hooklog::Write("fast paths: ported search loop %s", g_port_search ? "on" : "off (needs remembered checks and the open list)");
	}
	if (Setting(path, L"port_relax", 0)) {
		g_port_relax = hookpatch::RedirectCalls(kRelaxCalls, sizeof(kRelaxCalls) / sizeof(kRelaxCalls[0]), kRelax,
		                                        &RelaxHook) != 0;
		hooklog::Write("fast paths: ported relax %s", g_port_relax ? "on" : "NOT as expected");
	}
}

void FastPathsStats(LONGLONG *hits, LONGLONG *misses)
{
	*hits = g_hits;
	*misses = g_misses;
}

void FastPathsOnAiStep(bool thinking)
{
	g_in_ai_step = thinking;
	NewAshoreGeneration();
}

void FastPathsCheckStats(char *out, size_t size)
{
	static const char *const names[] = {"ashore", "ashore1", "enter", "dry", "visit", "stands", "object"};
	size_t used = 0;
	out[0] = 0;
	for (int i = 0; i < 7 && used + 40 < size; ++i)
		used += static_cast<size_t>(_snprintf_s(out + used, size - used, _TRUNCATE, "%s%s=%lld/%lld", i ? " " : "",
		                                        names[i], g_check_misses[i], g_check_hits[i] + g_check_misses[i]));
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	if (used + 40 < size)
		used += static_cast<size_t>(_snprintf_s(out + used, size - used, _TRUNCATE, " ashore_ms=%.0f",
		                                        g_ashore_miss_ticks * 1e3 / frequency.QuadPart));
	for (int i = 2; i < 7 && used + 30 < size; ++i)
		used += static_cast<size_t>(_snprintf_s(out + used, size - used, _TRUNCATE, " %s_ms=%.0f", names[i],
		                                        g_miss_ticks[i] * 1e3 / frequency.QuadPart));
}

namespace fastpaths {

LONGLONG g_helper_ticks[kHelperSlots] = {};
LONGLONG g_helper_calls[kHelperSlots] = {};

bool Measuring()
{
	return g_measure;
}

void AddHelperTime(int slot, LONGLONG ticks)
{
	g_helper_ticks[slot] += ticks;
	++g_helper_calls[slot];
}

}  // namespace fastpaths

void FastPathsHelperStats(char *out, size_t size)
{
	static const char *const names[] = {"9d19a0", "9d1700", "terrain", "9d1c40", "9d1310", "ships", "pool", "valuation"};
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	size_t used = 0;
	out[0] = 0;
	for (int i = 0; i < fastpaths::kHelperSlots && used + 40 < size; ++i)
		used += static_cast<size_t>(_snprintf_s(out + used, size - used, _TRUNCATE, "%s%s=%.0fms/%lld", i ? " " : "",
		                                        names[i], fastpaths::g_helper_ticks[i] * 1e3 / frequency.QuadPart,
		                                        fastpaths::g_helper_calls[i]));
}

void FastPathsMeasure(bool on)
{
	fastpaths::g_count_hits = true;  // the robot's path trace is on
	g_measure = on;
}

}  // namespace mods
