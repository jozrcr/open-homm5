// Fast paths mod, part: an exact, faster "can the hero go ashore here?",
// 0x008649f0 (see fast_paths.cpp and docs/subsystems/ai-pathfinder.md).
//
// The expansion asks it for every node on the boat layer. Its slow part is
// three scans over every object of the map floor, each looking for an object
// that "covers" the tile:
//   0x00cea350  the FIRST object (in the floor's list order) that stands on the
//               tile, or whose footprint (vfunc 0xb8) or, for some objects, active
//               cells (vfunc 0xb4) include it;
//   0x00cea010  the LAST object whose active cells (vfunc 0xb4) include it;
//   0x00ceab70  the FIRST living object whose zone of control (radius 0 or 1 from
//               0x00c27d20, Chebyshev distance) covers it.
// 0x008649f0 only looks at the objects these return. Which object covers which
// tile depends only on the objects (the AI player is not used by the scans; it
// passes 0 as "who"), and the objects do not change while the AI thinks (its
// moves are queued as commands and run after its turn-driver call). So the
// three answers are computed for every tile of a floor in one pass over the
// object list, kept in three tables, and each call becomes a few lookups plus
// the cheap tile checks of 0x008649f0 itself.
//
// The tables live until FastGoAshoreInvalidate() (the caller invalidates them
// whenever the world may have changed). Positions outside the tables (x or y of
// 512 or more, floor 2 or more, or outside the map) go to the game's function.
// With [ai] ashore_check=1 every call also runs the game's function, returns
// its answer and logs every difference.
//
// Calling conventions (from the disassembly): 0x008649f0 is __fastcall(world,
// player, const DWORD *node, char flag), ret 8; the three scans are
// __fastcall(const DWORD *position, world, ignore, who), ret 8, called with
// ignore = who = 0; the object methods are __thiscall.
#include "fast_paths_internal.h"

#include <string.h>

#include "../hook/log.h"

namespace mods {
namespace fastpaths {
namespace {

typedef bool(__fastcall *GoAshoreGameFn)(void *world, void *player, const DWORD *node, char flag);
const GoAshoreGameFn kGameGoAshore = reinterpret_cast<GoAshoreGameFn>(0x008649f0);
// 0x004b94b0: __thiscall(map, const DWORD *position), ret 4: inside the map
// (map+0x1c holds the size as a position) and on an existing floor (0x004b90d0).
typedef bool(__fastcall *InMapFn)(void *map, void *edx, const DWORD *position);
const InMapFn kInMap = reinterpret_cast<InMapFn>(0x004b94b0);
// 0x00c27d20: __fastcall(world, object, who), ret 4: the object's zone of
// control radius for `who` (-1 = none, 0, or 1). Only reads the object.
typedef int(__fastcall *RadiusFn)(void *world, void *object, DWORD who);
const RadiusFn kRadius = reinterpret_cast<RadiusFn>(0x00c27d20);

// object and world methods (__thiscall; __fastcall with an unused edx)
typedef void *(__fastcall *GetFn)(void *self, void *edx);
typedef bool(__fastcall *TestFn)(void *self, void *edx);
typedef void *(__fastcall *GetArgFn)(void *self, void *edx, DWORD arg);
typedef bool(__fastcall *TestArgFn)(void *self, void *edx, DWORD arg);

void *Method(void *object, DWORD offset)
{
	return (*reinterpret_cast<void ***>(object))[offset / 4];
}

void *Get(void *object, DWORD offset)
{
	return reinterpret_cast<GetFn>(Method(object, offset))(object, nullptr);
}

bool Test(void *object, DWORD offset)
{
	return reinterpret_cast<TestFn>(Method(object, offset))(object, nullptr);
}

void *GetArg(void *object, DWORD offset, DWORD arg)
{
	return reinterpret_cast<GetArgFn>(Method(object, offset))(object, nullptr, arg);
}

bool TestArg(void *object, DWORD offset, DWORD arg)
{
	return reinterpret_cast<TestArgFn>(Method(object, offset))(object, nullptr, arg);
}

// The "dead" flag every map object has: bit 31 of the dword at +8 of its
// virtual base (vbtable at object+4, the base's offset at vbtable+4). The scans
// and 0x004e9280 test it the same way.
bool Dead(const void *object)
{
	const BYTE *o = static_cast<const BYTE *>(object);
	const int base = *reinterpret_cast<const int *>(*reinterpret_cast<const DWORD *>(o + 4) + 4);
	return (*reinterpret_cast<const DWORD *>(o + base + 8) & 0x80000000u) != 0;
}

// A cell list of an object (vfunc 0xb8 "footprint", vfunc 0xb4 "active cells"):
// a vector {begin, end} of 2-byte (dx, dy) signed offsets from its position.
struct CellList {
	const signed char *begin;
	const signed char *end;
};

// The position of one cell, computed exactly as 0x00cea190 / 0x00cea270 /
// 0x00cea010 do: x and y wrap within their 10 bits, the floor (and the top
// byte) come from the object's position.
DWORD CellAt(DWORD position, int dx, int dy)
{
	DWORD cell = ((position + static_cast<DWORD>(dx)) & 0x3ff) | (position & 0xfffffc00);
	cell ^= ((static_cast<DWORD>(dy) << 10) + cell ^ cell) & 0xffc00;
	return cell;
}

bool SamePlace(DWORD a, DWORD b)  // 0x0041a760: x, y and floor
{
	return ((a ^ b) & 0x00ffffff) == 0;
}

// A cell list includes `target` (0x00cea190 for vfunc 0xb8, 0x00cea270 for 0xb4).
bool ListHas(const CellList *list, DWORD position, DWORD target)
{
	if (!list || ((position ^ target) & 0xf00000))
		return false;
	const int count = static_cast<int>(list->end - list->begin) >> 1;
	for (int i = 0; i < count; ++i)
		if (SamePlace(CellAt(position, list->begin[2 * i], list->begin[2 * i + 1]), target))
			return true;
	return false;
}

const DWORD kBits = 9;
const DWORD kSide = 1u << kBits;
const DWORD kCells = kSide * kSide;
const DWORD kFloors = 2;

struct FloorTables {
	const void *world;
	DWORD generation;  // built for this generation
	void **standing;   // 0x00cea350: first object
	void **active;     // 0x00cea010: last object
	void **zone;       // 0x00ceab70: first living object
};
FloorTables g_tables[kFloors];
DWORD g_generation = 1;
bool g_self_check = false;
long g_differences = 0;
LONGLONG g_checked = 0;

bool InTable(DWORD position, DWORD floor)
{
	return ((position >> 20) & 0xf) == floor && (position & 0x3ff) < kSide && ((position >> 10) & 0x3ff) < kSide;
}

DWORD Index(DWORD position)
{
	return ((position >> 10) & 0x3ff) * kSide + (position & 0x3ff);
}

void SetFirst(void **table, DWORD position, DWORD floor, void *object)
{
	if (InTable(position, floor) && !table[Index(position)])
		table[Index(position)] = object;
}

// One pass over the floor's objects, in list order, doing for every tile what
// the three scans do (the scans are only reached for tiles whose own kind at
// +0x10 is 0, where their after-the-loop parts never apply).
void Build(FloorTables &t, void *world, DWORD floor)
{
	t.world = world;
	t.generation = g_generation;
	memset(t.standing, 0, kCells * sizeof(void *));
	memset(t.active, 0, kCells * sizeof(void *));
	memset(t.zone, 0, kCells * sizeof(void *));
	if (!world || !TestArg(world, 0x2c, floor))  // the floor exists
		return;
	void *container = GetArg(world, 0x28, floor);
	void *const *list = static_cast<void *const *>(Get(container, 0x38));  // {begin, end}
	void *const *begin = static_cast<void *const *>(list[0]);
	void *const *end = static_cast<void *const *>(list[1]);
	void *map = Get(world, 0x160);
	for (void *const *it = begin; it != end; ++it) {
		void *object = *it;
		if (!object)  // the scans skip it (it equals their "ignore" argument, 0)
			continue;
		const bool listed = !TestArg(object, 0x100, 0) && Test(object, 0xec);  // filters of 0x00cea350 and 0x00cea010
		if (listed) {
			const DWORD position = *static_cast<const DWORD *>(Get(object, 0xa4));
			const CellList *footprint = static_cast<const CellList *>(Get(object, 0xb8));
			const CellList *cells = static_cast<const CellList *>(Get(object, 0xb4));
			// 0x00cea350 on the object's own tile (0x00cea3e8): through its owner
			// chain (vfuncs 0x2c, 0x14, 0x218, 0xc), else vfunc 0x1c, else the
			// footprint and active cells like any other tile.
			void *inner = Get(object, 0x2c);
			void *holder = inner ? Get(inner, 0x14) : nullptr;
			void *slot0 = Get(object, 0);
			const bool uses_cells = slot0 && Test(Get(object, 0), 0);  // 0x00cea43d: vfunc 0, then its vfunc 0
			bool own;
			if (inner && holder) {
				void *keeper = Get(holder, 0x218);
				own = !keeper || Test(keeper, 0xc);
			} else {
				own = Get(object, 0x1c) != nullptr || ListHas(footprint, position, position) ||
				      (uses_cells && ListHas(cells, position, position));
			}
			if (own)
				SetFirst(t.standing, position, floor, object);
			// other tiles (0x00cea42c): footprint, or active cells
			if (footprint) {
				const int count = static_cast<int>(footprint->end - footprint->begin) >> 1;
				for (int i = 0; i < count; ++i) {
					const DWORD cell = CellAt(position, footprint->begin[2 * i], footprint->begin[2 * i + 1]);
					if (!SamePlace(cell, position))
						SetFirst(t.standing, cell, floor, object);
				}
			}
			if (uses_cells && cells) {
				const int count = static_cast<int>(cells->end - cells->begin) >> 1;
				for (int i = 0; i < count; ++i) {
					const DWORD cell = CellAt(position, cells->begin[2 * i], cells->begin[2 * i + 1]);
					if (!SamePlace(cell, position))
						SetFirst(t.standing, cell, floor, object);
				}
			}
			// 0x00cea010: the last object whose active cells include the tile
			if (cells) {
				const int count = static_cast<int>(cells->end - cells->begin) >> 1;
				for (int i = 0; i < count; ++i) {
					const DWORD cell = CellAt(position, cells->begin[2 * i], cells->begin[2 * i + 1]);
					if (InTable(cell, floor))
						t.active[Index(cell)] = object;
				}
			}
		}
		// 0x00ceab70: living objects, not filtered by vfunc 0x100 (who = 0), whose
		// zone of control (0x00c27e60) covers the tile: radius from 0x00c27d20,
		// same floor, Chebyshev distance, both positions inside the map
		// (0x00c27c80; its third test needs the tile's kind 2, never true here).
		if (Dead(object) || TestArg(object, 0x100, 0))
			continue;
		const DWORD position = *static_cast<const DWORD *>(Get(object, 0xa4));
		const int radius = kRadius(world, object, 0);
		if (radius < 0 || ((position >> 20) & 0xf) != floor || !kInMap(map, nullptr, &position))
			continue;
		const int x = static_cast<int>(position & 0x3ff), y = static_cast<int>((position >> 10) & 0x3ff);
		for (int ty = y - radius; ty <= y + radius; ++ty)
			for (int tx = x - radius; tx <= x + radius; ++tx)
				if (tx >= 0 && ty >= 0 && tx < static_cast<int>(kSide) && ty < static_cast<int>(kSide) &&
				    !t.zone[ty * kSide + tx])
					t.zone[ty * kSide + tx] = object;
	}
}

FloorTables *TablesFor(void *world, DWORD floor)
{
	FloorTables &t = g_tables[floor];
	if (!t.standing)
		return nullptr;
	if (t.world != world || t.generation != g_generation)
		Build(t, world, floor);
	return &t;
}

// The fast answer, or -1 when the tables cannot answer (then the caller asks
// the game).
int Fast(void *world, void *player, const DWORD *node, char flag)
{
	const DWORD position = node[0];
	const DWORD floor = (position >> 20) & 0xf;
	if (floor >= kFloors || !InTable(position, floor) || !world)
		return -1;
	void *map = Get(world, 0x160);
	if (!kInMap(map, nullptr, &position))
		return -1;
	// 0x00864a06: the tile (0x00bcc310 for a position inside the map)
	const BYTE *floors = *reinterpret_cast<const BYTE *const *>(static_cast<const BYTE *>(map) + 0x20);
	const DWORD *rows = *reinterpret_cast<const DWORD *const *>(floors + floor * 0x58 + 4);
	const DWORD *tile = reinterpret_cast<const DWORD *>(rows[(position >> 10) & 0x3ff] + (position & 0x3ff) * 0x24);
	if (tile[4] != 0)  // +0x10
		return 0;
	if (!flag ? (tile[2] != 1 && tile[2] != 4) : tile[2] != 2)  // +8, 0x00864a11
		return 0;
	void *on_tile = reinterpret_cast<void *>(tile[6]);  // +0x18
	if (on_tile && TestArg(on_tile, 0x14, reinterpret_cast<DWORD>(player)))
		return 0;
	FloorTables *t = TablesFor(world, floor);
	if (!t)
		return -1;
	const DWORD i = Index(position);
	if (t->standing[i] && !Dead(t->standing[i]))  // 0x00864a5b
		return 0;
	if (t->active[i] && !Dead(t->active[i]))  // 0x00864a81
		return 0;
	return t->zone[i] ? 0 : 1;  // 0x00864aa4: no living object's zone covers it
}

}  // namespace

bool FastGoAshoreInit()
{
	for (FloorTables &t : g_tables) {
		void *memory = VirtualAlloc(nullptr, 3 * kCells * sizeof(void *), MEM_COMMIT, PAGE_READWRITE);
		if (!memory)
			return false;
		t.standing = static_cast<void **>(memory);
		t.active = t.standing + kCells;
		t.zone = t.active + kCells;
		t.world = nullptr;
		t.generation = 0;
	}
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	g_self_check = GetPrivateProfileIntW(L"ai", L"ashore_check", 0, path) != 0;
	if (g_self_check)
		hooklog::Write("fast paths: go-ashore self-check on (every call also asks the game)");
	return true;
}

void FastGoAshoreInvalidate()
{
	++g_generation;
}

bool FastGoAshore(void *world, void *player, const DWORD *node, char flag)
{
	const int fast = Fast(world, player, node, flag);
	if (!g_self_check)
		return fast >= 0 ? fast != 0 : kGameGoAshore(world, player, node, flag);
	const bool game = kGameGoAshore(world, player, node, flag);
	++g_checked;
	if (fast >= 0 && (fast != 0) != game && g_differences < 200) {
		++g_differences;
		hooklog::Write("fast paths: go-ashore DIFFERS at %lu,%lu,%lu flag %d: fast %d, game %d (%lld checked)",
		               node[0] & 0x3ff, (node[0] >> 10) & 0x3ff, (node[0] >> 20) & 0xf, flag ? 1 : 0, fast,
		               game ? 1 : 0, g_checked);
	}
	return game;
}

}  // namespace fastpaths
}  // namespace mods
