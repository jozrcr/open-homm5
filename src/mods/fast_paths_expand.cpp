// Mod part: the hero pathfinder's node expansion 0x00c07f80, ported.
//
// The search loop 0x00c08890 calls the expansion once for every node it takes
// off the open list (at 0x00c08b2d): __thiscall(pf, node, count, dirs,
// remaining, flag, out), ret 0x18, with count = 8 and dirs = the 8 directions
// at 0x00f757c8 (pairs of ints dx, dy; the 4 diagonals come last). For each
// direction it builds a candidate node one tile further, decides what the move
// costs and whether it is allowed, and hands the candidate to relax 0x00c09d90,
// which keeps it if it is better than what the neighbour tile has.
//
// This port does exactly what the game's code does, in the same order, so the
// search stays the same step for step (docs/subsystems/ai-pathfinder.md). It is
// written from the disassembly (analysis/export/dump/00c07f80.txt); the labels
// below name the game's addresses so each part can be checked against it.
//   - Every call into the game with possible side effects or unknown internals
//     happens in the same order with the same arguments (0x009d1c40,
//     0x009d1310, 0x009d1700, 0x009d19a0, 0x004e9280, and the virtual calls on
//     the object pool at pf+4 and the ships pool at pf+0xa4).
//   - The four remembered checks go through fastpaths::GoAshore, CanEnter,
//     CanEnterDry and Visitable; relax goes through fastpaths::Relax.
//   - The step cost 0x00ca9a70 changes nothing and depends only on the tile and
//     the search's settings: asked once per tile and search (StepCost). The
//     world tile's two kinds are remembered per tile too (Kinds), which saves
//     reading the big world map for every neighbour.
//   - Four tiny game functions that only compute an address are written out
//     instead of called (they read nothing but the position and fixed tables):
//     0x00423bd0 (two positions differ), 0x0059e980 and 0x00c09220 (the node of
//     the grid at pf+0xa8 / pf+0xb4 at a position), 0x0041fec0 (the world tile).
//   - The game passes some cost "pad" dwords from uninitialized locals (the
//     local at esp+0x34); the pad of a cost is never read (see Cost), so 0 is
//     passed instead.
#include "fast_paths_internal.h"

#include <limits.h>
#include <string.h>

namespace mods {
namespace fastpaths {
namespace {

// Game functions called from the expansion. __thiscall ones are declared
// __fastcall with an unused edx, which passes `this` in ecx the same way.
typedef bool(__fastcall *PfNodeCheckFn)(BYTE *pf, void *edx, const Node *candidate);                       // ret 4
typedef bool(__fastcall *PfTwoNodeCheckFn)(BYTE *pf, void *edx, const Node *candidate, const Node *node);  // ret 8
typedef bool(__fastcall *PfNodeFlagCheckFn)(BYTE *pf, void *edx, Node *candidate, DWORD zero);             // ret 8
typedef int(__fastcall *TerrainCostFn)(const DWORD *tile, const Node *candidate, DWORD diagonal, int remaining,
                                       BYTE *settings, int kind);                                          // ret 0x10
typedef bool(__fastcall *ObjectCheckFn)(void *object, void *edx);                                          // ret
typedef void *(__fastcall *PoolAtFn)(void *pool, void *edx, const Node *candidate);                        // vtable slot 0
typedef bool(__fastcall *ShipsCheckFn)(void *ships, void *edx, const Node *candidate);                     // vtable 0, 2
typedef int(__fastcall *ShipsValueFn)(void *ships, void *edx, const Node *candidate);                      // vtable 3

const PfNodeCheckFn kCheck9d1c40 = reinterpret_cast<PfNodeCheckFn>(0x009d1c40);
const PfNodeCheckFn kCheck9d1310 = reinterpret_cast<PfNodeCheckFn>(0x009d1310);
const PfTwoNodeCheckFn kCheck9d1700 = reinterpret_cast<PfTwoNodeCheckFn>(0x009d1700);
const PfNodeFlagCheckFn kCheck9d19a0 = reinterpret_cast<PfNodeFlagCheckFn>(0x009d19a0);
const TerrainCostFn kTerrainCost = reinterpret_cast<TerrainCostFn>(0x00ca9a70);
const ObjectCheckFn kObjectCheck = reinterpret_cast<ObjectCheckFn>(0x004e9280);

void *VirtualAt(void *object, int slot)
{
	return (*reinterpret_cast<void ***>(object))[slot];
}

BYTE Byte(const BYTE *pf, DWORD offset)
{
	return pf[offset];
}

// The node at `position` in the node grid at pf+grid (0xa8 = layer 0, 0xb4 =
// layer 1): a vector of 16-byte floor descriptors {data, rows, width, height}.
// What 0x0059e980 (pf+0xa8) and 0x00c09220 (pf+0xb4) return.
Node *GridNode(BYTE *pf, DWORD grid, DWORD position)
{
	const BYTE *floor = reinterpret_cast<const BYTE *>(At(pf, grid)) + ((position >> 20) & 0xf) * 16;
	const DWORD *rows = *reinterpret_cast<const DWORD *const *>(floor + 4);
	return reinterpret_cast<Node *>(rows[(position >> 10) & 0x3ff] + (position & 0x3ff) * 64);
}

// The world's tile at `position` (36 bytes; +8 terrain kind, 2 = water, 5 =
// blocked; +0x10 another kind): *pf is the world map, its +0x20 holds floors
// of 0x58 bytes with the row pointers at +4. What 0x0041fec0 returns.
const DWORD *WorldTile(BYTE *pf, DWORD position)
{
	const BYTE *world = *reinterpret_cast<BYTE *const *>(pf);
	const BYTE *floors = *reinterpret_cast<const BYTE *const *>(world + 0x20);
	const DWORD *rows = *reinterpret_cast<const DWORD *const *>(floors + ((position >> 20) & 0xf) * 0x58 + 4);
	return reinterpret_cast<const DWORD *>(rows[(position >> 10) & 0x3ff] + (position & 0x3ff) * 0x24);
}

// 0x00423bd0: x, y and floor of two positions differ.
bool Differ(DWORD a, DWORD b)
{
	return ((a ^ b) & 0x00ffffff) != 0;
}

Cost CostAt(const Node &n, int index)  // the cost at d[index..index+3], all 16 bytes
{
	Cost c;
	memcpy(&c, &n.d[index], sizeof(c));
	return c;
}

const Cost kNoCost = {0, 0, 0};

// The world tile's kinds at +8 and +0x10, remembered per tile (memo from
// TileFor, may be nullptr). Sets `tile` when the world tile had to be read.
void Kinds(BYTE *pf, Tile *memo, DWORD position, const DWORD *&tile, DWORD &kind8, DWORD &kind10)
{
	if (memo && (memo->known & kCheckWorld)) {
		kind8 = memo->kind8;
		kind10 = memo->kind10;
		return;
	}
	tile = WorldTile(pf, position);
	kind8 = tile[2];
	kind10 = tile[4];
	if (memo && kind8 < 256 && kind10 < 256) {
		memo->kind8 = static_cast<BYTE>(kind8);
		memo->kind10 = static_cast<BYTE>(kind10);
		memo->known |= kCheckWorld;
	}
}

int GameStepCost(const DWORD *tile, Node *c, bool diagonal, int remaining, BYTE *pf, int kind)
{
	return Measure(kHelperTerrain, [&] { return kTerrainCost(tile, c, diagonal ? 1u : 0u, remaining, pf + 0x20, kind); });
}

// 0x00ca9a70's choice for a diagonal step: the diagonal cost `full`, except
// when the movement left is at least `straight` but below `full`.
int Pick(int straight, int full, bool diagonal, int remaining)
{
	if (!diagonal)
		return straight;
	return (remaining < straight || remaining >= full) ? full : straight;
}

// Kinds 1 and 2 do not look at the tile: their costs, per search.
DWORD g_kind_generation[3] = {};
int g_kind_straight[3] = {};
int g_kind_full[3] = {};

// The step cost 0x00ca9a70 __fastcall(world tile, (unused), diagonal,
// remaining, settings = pf+0x20, kind), ret 0x10. It changes nothing and
// reads only its arguments, the settings and (kind 0) the tile's first two
// dwords. For kinds 0 to 2 it computes a cost `straight` (kind 0: from the
// tile and settings; kinds 1, 2: 100), then for a diagonal step `full` =
// straight x sqrt(2) rounded down, and returns Pick(straight, full). Both are
// asked from the game itself once per tile and search (remaining 0 and
// INT_MAX); kind 3 (another function) always goes to the game.
int StepCost(BYTE *pf, Tile *memo, DWORD position, const DWORD *&tile, Node *c, bool diagonal, int remaining, int kind)
{
	if (memo && kind == 0) {
		if (!(memo->known & kCheckStepCost)) {
			if (!tile)
				tile = WorldTile(pf, position);
			const int straight = GameStepCost(tile, c, false, 0, pf, 0);
			const int full = GameStepCost(tile, c, true, INT_MAX, pf, 0);
			if (straight < 0 || straight > 0xffff || full < 0 || full > 0xffff)
				return GameStepCost(tile, c, diagonal, remaining, pf, kind);
			memo->cost = static_cast<WORD>(straight);
			memo->diagonal = static_cast<WORD>(full);
			memo->known |= kCheckStepCost;
		}
		return Pick(memo->cost, memo->diagonal, diagonal, remaining);
	}
	if (memo && (kind == 1 || kind == 2)) {
		if (g_kind_generation[kind] != g_generation) {
			if (!tile)
				tile = WorldTile(pf, position);
			g_kind_straight[kind] = GameStepCost(tile, c, false, 0, pf, kind);
			g_kind_full[kind] = GameStepCost(tile, c, true, INT_MAX, pf, kind);
			g_kind_generation[kind] = g_generation;
		}
		return Pick(g_kind_straight[kind], g_kind_full[kind], diagonal, remaining);
	}
	if (!tile)
		tile = WorldTile(pf, position);
	return GameStepCost(tile, c, diagonal, remaining, pf, kind);
}

}  // namespace

void Expand(BYTE *pf, Node *node, int count, const int *dirs, int remaining, char flag, DWORD *out)
{
	NotePathfinder(pf);
	const DWORD *from_tile = nullptr;  // esp+0x24, the node's world tile, read when not remembered
	DWORD from_kind8 = 0, from_kind10 = 0;
	Kinds(pf, TileFor(node->d[0]), node->d[0], from_tile, from_kind8, from_kind10);
	DWORD no_extra = 0;                                   // esp+0x20, the "extra" some relax calls get
	Node c;                                               // esp+0x40, the candidate
	const DWORD flags = node->d[1];

	// 0x00c07fd9: a node on the boat layer whose previous tile is another one
	// may also go ashore: candidate = the node itself, back on layer 0, from the
	// layer-0 node at its previous tile.
	if ((flags & 0x40) && Differ(node->d[5], node->d[0]) &&
	    GoAshore(reinterpret_cast<void *>(At(pf, 0x70)), reinterpret_cast<void *>(At(pf, 0x38)), node->d,
	             static_cast<char>((flags >> 1) & 1))) {
		c = *node;
		c.d[3] += 500;
		c.d[1] = (c.d[1] & ~0x40u) | 0x4000200;
		Node *from = GridNode(pf, 0xa8, node->d[5]);  // 0x0059e980(pf, &node->d[5])
		Node *grid = GridNode(pf, 0xa8, c.d[0]);
		Tile *memo = TileFor(c.d[0]);
		const DWORD visitable = Remembered(memo, kCheckVisitable, pf, c.d);  // 0x00c08076
		Relax(pf, from, grid, &c, 0, CostAt(*from, 8), &no_extra, visitable, memo);  // 0x00c080aa
	}

	for (int i = 0; i < count; ++i) {
		// 0x00c080d0: the candidate is the node moved one tile in direction i.
		c = *node;
		const int dx = dirs[2 * i], dy = dirs[2 * i + 1];
		const DWORD pos = node->d[0];
		c.d[0] = (((static_cast<DWORD>(dy) << 10) + pos) & 0xffc00) | ((static_cast<DWORD>(dx) + pos) & 0x3ff) |
		         (c.d[0] & 0xfff00000);
		const DWORD floor = (c.d[0] >> 20) & 0xf;
		if (floor >= static_cast<DWORD>(static_cast<int>(At(pf, 0xac) - At(pf, 0xa8)) >> 4))
			continue;
		const BYTE *desc = reinterpret_cast<const BYTE *>(At(pf, 0xa8)) + floor * 16;
		const DWORD x = c.d[0] & 0x3ff, y = (c.d[0] >> 10) & 0x3ff;
		if (x >= *reinterpret_cast<const DWORD *>(desc + 8) || y >= *reinterpret_cast<const DWORD *>(desc + 0xc))
			continue;

		// 0x00c08175: on the boat layer, every neighbour is offered as it is, on
		// layer 1, for free.
		if (node->d[1] & 0x40) {
			Relax(pf, node, GridNode(pf, 0xb4, c.d[0]), &c, 0, kNoCost, &no_extra, 0);  // 0x00c0885b
			continue;
		}

		// 0x00c081b8: with `flag` (the search loop found an object here), only the
		// object's own tile *out may follow.
		if (flag && Differ(c.d[0], *out))
			continue;

		// 0x00c081db
		Tile *memo = TileFor(c.d[0]);  // remembered answers for this tile
		const DWORD *tile = nullptr;   // esp+0x18, the world tile, read when not remembered
		DWORD kind8 = 0, kind10 = 0;   // its kinds at +8 and +0x10
		Kinds(pf, memo, c.d[0], tile, kind8, kind10);
		c.d[1] |= 0x200;
		bool stop = false;           // esp+0x10: no ordinary move to this tile
		bool cannot_enter = false;   // esp+0x11 (reused for the ships pool's answer below)
		bool no_dry_step = false;    // esp+0x12
		bool can_enter = false;      // esp+0x13
		DWORD cost = 0;              // esp+0x14 (also kept in ebx)
		bool go = false;             // bl at 0x00c08746
		Node *grid = nullptr;
		if (kind8 == 5 || (Byte(pf, 0x76) && !Measure(kHelper9d1c40, [&] { return kCheck9d1c40(pf, nullptr, &c); }))) {
			// 0x00c0828e
			c.d[1] &= ~0x200u;
			c.d[3] += 100;
			stop = true;
		} else {
			// 0x00c08234: what the step costs
			int kind = 0;
			if (At(pf, 0x44) == 3)
				kind = 3;
			else if (node->d[1] & 2)
				kind = 1;
			else if (c.d[1] & 8)
				kind = 2;
			cost = static_cast<DWORD>(StepCost(pf, memo, c.d[0], tile, &c, i >= 4, remaining, kind));
		}

		// 0x00c082a8: movement left after the step; when it does not reach, only
		// nodes with flag 0x200 go on, with a full day's movement.
		if (cost > node->d[4]) {
			if (!(node->d[1] & 0x200))
				continue;
			c.d[4] = (node->d[1] & 2) ? At(pf, 0x4c) : At(pf, 0x48);
		} else {
			c.d[4] = node->d[4] - cost;
		}

		// 0x00c082d3
		can_enter = Remembered(memo, kCheckCanEnter, pf, c.d);
		if (!can_enter) {
			c.d[1] &= ~0x200u;
			cannot_enter = true;
		}
		if (!(node->d[1] & 2)) {
			// 0x00c082f7
			if ((Byte(pf, 0x76) || Byte(pf, 0x77)) && !Remembered(memo, kCheckCanEnterDry, pf, c.d))  // 0x00c0830a
				c.d[1] &= ~0x200u;
			if ((c.d[1] & 8) && kind8 == 2)  // 0x0041fec0(*pf, &c) is the same tile
				c.d[1] &= ~0x200u;
			if (Remembered(memo, kCheckCanEnterDry, pf, c.d)) {  // 0x00c08349
				if (Byte(pf, 0x76))
					c.d[1] &= ~8u;
				if (Byte(pf, 0x77))
					c.d[1] &= ~0x10u;
			}
		}

		// 0x00c0836f: water, boats and coasts
		if (At(pf, 0x44) != 3) {
			if (kind8 == 2) {
				if (Byte(pf, 0x82)) {
					// 0x00c08391: stepping from land into water
					if (from_kind8 != 2) {
						c.d[2] += c.d[4];
						c.d[4] = At(pf, 0x4c);
						c.d[1] = (c.d[1] & ~0x18u) | 2;
					}
				} else if (node->d[1] & 2) {
					// 0x00c083c8: a diagonal step on water needs water on both sides
					if (from_kind8 == 2 && dx != 0 && dy != 0) {
						const DWORD side_x = pos ^ (((pos + static_cast<DWORD>(dx)) ^ pos) & 0x3ff);
						const DWORD side_y = pos ^ ((((static_cast<DWORD>(dy) << 10) + pos) ^ pos) & 0xffc00);
						if (WorldTile(pf, side_x)[2] != 2 || WorldTile(pf, side_y)[2] != 2)  // 2 x 0x0041fec0
							stop = true;
					}
				} else {
					// 0x00c08454
					if (!Measure(kHelper9d1310, [&] { return kCheck9d1310(pf, nullptr, &c); }) || Byte(pf, 0x81)) {
						if (!cannot_enter)
							no_dry_step = true;
						c.d[1] &= ~0x200u;
					}
				}
			} else if ((node->d[1] & 2) || (Byte(pf, 0x82) && from_kind8 == 2)) {
				// 0x00c08498: leaving the water
				if (kind8 != 1) {
					c.d[1] &= ~0x200u;
					stop = true;
				} else if ((c.d[1] & 0x200) && Byte(pf, 0x66)) {
					if (!Byte(pf, 0x30)) {
						cost = node->d[4] < cost ? At(pf, 0x4c) + node->d[4] : node->d[4];
						c.d[4] = At(pf, 0x48);
					}
					c.d[1] &= 0xffffffe5;
				}
			}
		}

		// 0x00c084dc: something to visit here
		if (Remembered(memo, kCheckVisitable, pf, c.d) &&
		    !Measure(kHelper9d1700, [&] { return kCheck9d1700(pf, nullptr, &c, node); }))  // 0x00c084e3, 0x00c084f4
			goto clear_200;
		if (cannot_enter)
			goto after_clear;
		if (stop)
			goto check_dry;
		// 0x00c08514; without a live object on the tile 0x009d19a0 only returns false
		if (Remembered(memo, kCheckLiveObject, pf, c.d) &&
		    Measure(kHelper9d19a0, [&] { return kCheck9d19a0(pf, nullptr, &c, 0); }))
			goto clear_200;
		goto check_dry;

	clear_200:  // 0x00c0851d
		c.d[1] &= ~0x200u;
	after_clear:  // 0x00c08522
		if (!(c.d[1] & 8)) {
			if (Byte(pf, 0x76)) {
				c.d[4] = At(pf, 0x48) - cost;
				c.d[1] |= 8;
				stop = false;
				goto offer;
			}
			c.d[1] &= ~0x200u;
			stop = true;
		}

	check_dry:  // 0x00c08551
		if (no_dry_step && !(c.d[1] & 0x10)) {
			if (Byte(pf, 0x77)) {
				c.d[4] = At(pf, 0x48) - cost;
				c.d[1] |= 0x10;
				stop = false;
				goto offer;
			}
			if (Byte(pf, 0x76)) {
				c.d[1] |= 8;
				stop = false;
				goto offer;
			}
			// 0x00c0859e: ask the ships pool (boarding or leaving a boat here)
			if (!(node->d[1] & 0x200) || !At(pf, 0xa4))
				continue;
			{
				void *ships = reinterpret_cast<void *>(At(pf, 0xa4));
				const bool visitable = Remembered(memo, kCheckVisitable, pf, c.d);  // 0x00c085bc
				if (!visitable && !(node->d[1] & 4) && Byte(pf, 0x55))
					cannot_enter = Measure(kHelperShips, [&] { return reinterpret_cast<ShipsValueFn>(VirtualAt(ships, 3))(ships, nullptr, &c); }) != 0;
				else
					cannot_enter = false;
				const bool ship0 = Measure(kHelperShips, [&] { return reinterpret_cast<ShipsCheckFn>(VirtualAt(ships, 0))(ships, nullptr, &c); }) &&
				                   !(node->d[1] & 0x1000000);
				const bool ship2 = Measure(kHelperShips, [&] { return reinterpret_cast<ShipsCheckFn>(VirtualAt(ships, 2))(ships, nullptr, &c); });
				if (!ship2 && !ship0 && !cannot_enter) {
					c.d[1] &= ~0x200u;
					stop = true;
					goto boat_layer;
				}
				// 0x00c08637
				if (!Byte(pf, 0x30)) {
					cost = node->d[4] < cost ? At(pf, 0x48) + node->d[4] : node->d[4];
					c.d[4] = At(pf, 0x4c);
				}
				c.d[1] |= 2;
				if (!ship2) {
					if (cannot_enter) {
						c.d[1] |= 0x2000000;
						c.d[3] += 500;
					} else if (ship0) {
						c.d[1] |= 0x1000000;
						c.d[3] += 250;
					}
				}
			}
		}

		// 0x00c0868b
		if (!stop)
			goto offer;
	boat_layer:  // 0x00c08692: instead, offer the tile on the boat layer
		if (!Byte(pf, 0x56) || !(node->d[1] & 0x200) || (node->d[1] & 4))
			continue;
		if (static_cast<int>(At(pf, 0x88)) > 0 && node->d[2] > At(pf, 0x88))
			continue;
		c.d[1] |= 0x40;
		Relax(pf, node, GridNode(pf, 0xb4, c.d[0]), &c, static_cast<int>(At(pf, 0x58)), kNoCost, &no_extra,
		      0);  // 0x00c086fc, grid from 0x00c09220
		c.d[1] &= ~0x40u;

	offer:  // 0x00c08706 / 0x00c0870a: offer the candidate on layer 0
		grid = GridNode(pf, 0xa8, c.d[0]);
		if (c.d[1] & 8) {
			// 0x00c087f6
			if (!kCheck9d1c40(pf, nullptr, &c))
				continue;
			const DWORD keep = ((c.d[1] & 0x200) && kind10 != 2) ? 0x200u : 0u;
			c.d[1] = (c.d[1] & ~0x200u) | keep;
			Relax(pf, node, grid, &c, static_cast<int>(cost), kNoCost, &no_extra, 0, memo);  // 0x00c0885b
			continue;
		}
		go = can_enter && !stop && (kind10 != 3 || (node->d[1] & 0x200));
		if (Byte(pf, 0x76)) {
			if (!go)
				continue;
			if (kind10 == 2 && !(node->d[1] & 0x200))
				continue;
			void *pool = reinterpret_cast<void *>(At(pf, 4));
			if (pool) {
				const bool object_ok = Measure(kHelperPool, [&] {
					void *object = reinterpret_cast<PoolAtFn>(VirtualAt(pool, 0))(pool, nullptr, &c);
					return kObjectCheck(object, nullptr);  // 0x004e9280
				});
				if (object_ok && !(node->d[1] & 0x200))
					continue;
			}
		}
		if (!go)
			continue;
		{
			const DWORD visitable = Remembered(memo, kCheckVisitable, pf, c.d);  // 0x00c087be
			Relax(pf, node, grid, &c, static_cast<int>(cost), CostAt(c, 8), &c.d[6], visitable, memo);  // 0x00c0885b
		}
	}
}

}  // namespace fastpaths
}  // namespace mods
