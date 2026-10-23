// Fast paths mod, part: the ported search loop of the hero pathfinder,
// 0x00c08890 (see fast_paths.cpp and docs/subsystems/ai-pathfinder.md).
//
// __thiscall(pf, int limit), ret 4. Pops the best node of the open list, takes
// the tile's current node from the grid, skips it when the next node in the
// list is the same tile, runs a few checks on land, asks the object pool
// whether something stands there, expands it, and stops at the target. The
// port follows the game's code step by step (addresses in the comments) and
// calls the same game functions in the same order.
//
// The open list is the game's own std::priority_queue (vector at pf+0x10..0x18,
// comparator byte at pf+0x1c), sorted with the same heap algorithm, so a later
// call that continues the search finds it exactly as the game would leave it.
#include "fast_paths_internal.h"

namespace mods {
namespace fastpaths {
namespace {

typedef bool(__fastcall *NodeTestFn)(BYTE *pf, void *edx, const Node *node);           // thiscall, ret 4

const DWORD kCheck9d1350 = 0x009d1350;  // when pf+0x68 is set
const DWORD kCheck9d22f0 = 0x009d22f0;  // for nodes with flag 1
const DWORD kDirections = 0x00f757c8;   // 8 pairs (dx, dy)

Node *GridNode(BYTE *pf, DWORD grid_offset, DWORD position)
{
	const BYTE *floor = reinterpret_cast<const BYTE *>(At(pf, grid_offset) + ((position >> 20) & 0xf) * 16);
	const DWORD *rows = *reinterpret_cast<DWORD *const *>(floor + 4);
	return reinterpret_cast<Node *>(rows[(position >> 10) & 0x3ff]) + (position & 0x3ff);
}

bool SameTile(DWORD a, DWORD b)
{
	return ((a ^ b) & 0x00ffffff) == 0;  // x, y and floor (the asm tests the three masks one by one)
}

// The terrain type of the world tile (world at *pf, +0x20: per floor 0x58
// bytes, rows, 0x24 bytes per tile, type at +8; 2 = water).
int TerrainType(BYTE *pf, DWORD position)
{
	const BYTE *world = reinterpret_cast<const BYTE *>(At(pf, 0));
	const BYTE *floors = *reinterpret_cast<const BYTE *const *>(world + 0x20);
	const DWORD *rows = *reinterpret_cast<const DWORD *const *>(floors + ((position >> 20) & 0xf) * 0x58 + 4);
	const BYTE *tile = reinterpret_cast<const BYTE *>(rows[(position >> 10) & 0x3ff]) + (position & 0x3ff) * 0x24;
	return *reinterpret_cast<const int *>(tile + 8);
}

}  // namespace

void PortedSearch(BYTE *pf, int limit)
{
	At(pf, 0x40) = static_cast<DWORD>(limit);
	DWORD stands_here = 0xffffff;  // the object pool's answer, kept from node to node (0x00c088b4)
	Node *&first = *reinterpret_cast<Node **>(pf + 0x10);
	Node *&last = *reinterpret_cast<Node **>(pf + 0x14);
	while (last - first > 0) {
		// pop (0x00c088f0): the top goes to the end, the last node is sifted down
		Node top = *first;
		Node moved = last[-1];
		last[-1] = *first;
		AdjustHeap(first, 0, static_cast<int>(last - first) - 1, moved);
		--last;
		Node n = *GridNode(pf, (top.d[1] & 0x40) ? 0xb4 : 0xa8, top.d[0]);
		// the next node is the same tile: it will be handled then (0x00c089d2)
		if (last - first > 0 && SameTile(first->d[0], n.d[0]) && ((first->d[1] ^ n.d[1]) & 0x240) == 0)
			continue;
		char found = 0;
		if (!(n.d[1] & 0x40)) {
			n.d[1] &= 0xfffffe7f;  // 0x00c08a2c
			if (pf[0x68] && !reinterpret_cast<NodeTestFn>(kCheck9d1350)(pf, nullptr, &n))
				continue;
			if ((n.d[1] & 1) && !reinterpret_cast<NodeTestFn>(kCheck9d22f0)(pf, nullptr, &n))
				continue;
			if ((n.d[1] & 2) && TerrainType(pf, n.d[0]) != 2)
				continue;
			void *pool = reinterpret_cast<void *>(At(pf, 4));
			if (!(n.d[1] & 8) && pf[0x63] == 0 && !SameTile(At(pf, 0x8c), n.d[0]) && pool && StandsHere(pool, &stands_here, &n))
				found = 1;
		}
		RunExpand(pf, &n, 8, reinterpret_cast<const int *>(kDirections),
		          static_cast<int>(At(pf, 0x50) - n.d[2]), found, &stands_here);
		if (!(n.d[1] & 0x40)) {
			// reached the target? (0x00c08b39: the target must be inside the grid)
			const DWORD target = At(pf, 0x3c);
			const DWORD floor = (target >> 20) & 0xf;
			const DWORD floors = static_cast<DWORD>(static_cast<int>(At(pf, 0xac) - At(pf, 0xa8)) >> 4);
			if (floor < floors) {
				const BYTE *grid = reinterpret_cast<const BYTE *>(At(pf, 0xa8) + floor * 16);
				if ((target & 0x3ff) < *reinterpret_cast<const DWORD *>(grid + 8) &&
				    ((target >> 10) & 0x3ff) < *reinterpret_cast<const DWORD *>(grid + 12) && SameTile(target, n.d[0]))
					break;
			}
		}
	}
	if (last == first)
		pf[0x90] = 1;  // searched everything
}

}  // namespace fastpaths
}  // namespace mods
