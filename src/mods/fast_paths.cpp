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

// One bit per check in a tile's record.
enum Check : WORD {
	kCheckGoAshore = 1,
	kCheckGoAshoreFlag = 2,
	kCheckCanEnter = 4,
	kCheckCanEnterDry = 8,
	kCheckVisitable = 16,
	kCheckStandsHere = 32  // the object pool's "something stands here" (search loop)
};

// Remembered answers, one record per tile (x and y below 512, 2 floors; other
// positions are not remembered). A record belongs to the current search when
// its generation is g_generation, so a new search needs no clearing.
// Neighbouring tiles sit next to each other, which keeps the lookups in the
// CPU cache.
struct Tile {
	DWORD generation;
	WORD known;  // checks answered in this search
	WORD value;  // their answers
	DWORD stands_here;  // what the object pool wrote when something stands here
};
const DWORD kTileBits = 9;
const DWORD kTileFloors = 2;
const DWORD kTiles = kTileFloors << (2 * kTileBits);
Tile *g_tiles = nullptr;
DWORD g_generation = 1;
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

void NewSearch()
{
	++g_generation;
}

void __fastcall SearchHook(void *self, void *edx, int limit)
{
	NewSearch();
	if (g_port_search)
		fastpaths::PortedSearch(static_cast<BYTE *>(self), limit);
	else
		g_search(self, edx, limit);
	NewSearch();
}

// The record of the tile at `position` for this search, or nullptr for a
// position outside the table (then the caller just runs the check).
Tile *TileAt(DWORD position)
{
	const DWORD x = position & 0x3ff, y = (position >> 10) & 0x3ff, floor = position >> 20;
	if (x >> kTileBits || y >> kTileBits || floor >= kTileFloors)
		return nullptr;
	Tile *t = &g_tiles[(floor << (2 * kTileBits)) | (y << kTileBits) | x];
	if (t->generation != g_generation) {
		t->generation = g_generation;
		t->known = 0;
		t->value = 0;
	}
	return t;
}

// The remembered answer of `check` at `position`, or -1.
int Recall(Tile *t, WORD check)
{
	if (t && (t->known & check)) {
		++g_hits;
		return (t->value & check) != 0;
	}
	++g_misses;
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
	if (world != g_world || player != g_player) {
		g_world = world;
		g_player = player;
		NewSearch();
	}
	const WORD check = flag ? kCheckGoAshoreFlag : kCheckGoAshore;
	Tile *t = TileAt(node[0]);
	const int known = Recall(t, check);
	if (known >= 0)
		return known != 0;
	return Store(t, check, g_go_ashore(world, player, node, flag));
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
	return Store(t, check, check_fn(pathfinder, edx, node));
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
	const bool found = game(pool, nullptr, &value, node);
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

}  // namespace mods
