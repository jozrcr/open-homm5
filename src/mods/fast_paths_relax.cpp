// Fast paths mod, part: the ported "relax a neighbour" of the hero pathfinder,
// 0x00c09d90 (see fast_paths.cpp and docs/subsystems/ai-pathfinder.md).
//
// The node expansion builds a candidate node for a neighbouring tile and calls
// this to decide whether the candidate is better than what the tile's node in
// the grid holds; if so, it overwrites the grid node and pushes a copy on the
// open list. The port follows the game's code step by step (its disassembly,
// addresses in the comments), calling the same game functions in the same
// order, so it decides exactly the same; it only avoids the original's extra
// copies and calls.
//
// Costs are "scores": the bigger the better (the open list pops the greatest).
#include "fast_paths_internal.h"

namespace mods {
namespace fastpaths {
namespace {

typedef int(__fastcall *ValuationFn)(void *self, void *edx, const Node *node);  // vtable slot 0, thiscall
typedef void(__fastcall *QueuePushFn)(void *queue, void *edx, const Node *node);  // 0x00c099d0, ret 4
typedef void(__fastcall *PushPointerFn)(void *vector, void *edx, Node *const *value);  // 0x00b6c890, ret 4

const DWORD kQueuePush = 0x00c099d0;
const DWORD kPushPointer = 0x00b6c890;

// 0x0086e1b0: is the position inside the pathfinder's grid (floor, x, y)?
bool InGrid(BYTE *pf, DWORD position)
{
	const DWORD floor = (position >> 20) & 0xf;
	const DWORD floors = static_cast<DWORD>(static_cast<int>(At(pf, 0xac) - At(pf, 0xa8)) >> 4);
	if (floor >= floors)
		return false;
	const BYTE *grid = reinterpret_cast<const BYTE *>(At(pf, 0xa8) + floor * 16);
	return (position & 0x3ff) < *reinterpret_cast<const DWORD *>(grid + 8) &&
	       ((position >> 10) & 0x3ff) < *reinterpret_cast<const DWORD *>(grid + 12);
}

// The "reached in this search" mark of a tile: grid of bytes at pf+0xc0 (layer
// 0) or pf+0xcc (layer 1), same layout as the node grids.
BYTE *Mark(BYTE *pf, DWORD grid_offset, DWORD position)
{
	const BYTE *floor = reinterpret_cast<const BYTE *>(At(pf, grid_offset) + ((position >> 20) & 0xf) * 16);
	const DWORD *rows = *reinterpret_cast<DWORD *const *>(floor + 4);
	return reinterpret_cast<BYTE *>(rows[(position >> 10) & 0x3ff]) + (position & 0x3ff);
}

// 0x009d1250: c = c "plus" o. The first number becomes the smaller of the two
// if that is negative, else the bigger; the second numbers add up.
void Combine(Cost &c, const Cost &o)
{
	const int low = c.a < o.a ? c.a : o.a;
	if (low < 0)
		c.a = low;
	else
		c.a = c.a > o.a ? c.a : o.a;
	c.b += o.b;
}

// 0x00c070c0 (x > y) and 0x009d1210 (x < y): compare a, then b.
bool Greater(const Cost &x, const Cost &y)
{
	return x.a != y.a ? x.a > y.a : x.b > y.b;
}

bool Smaller(const Cost &x, const Cost &y)
{
	return x.a != y.a ? x.a < y.a : x.b < y.b;
}

Cost CostAt(const Node &n, int first_dword)
{
	Cost c;
	c.a = static_cast<int>(n.d[first_dword]);
	c.pad = n.d[first_dword + 1];
	c.b = static_cast<__int64>(n.d[first_dword + 2] | static_cast<unsigned __int64>(n.d[first_dword + 3]) << 32);
	return c;
}

void PutCost(Node &n, int first_dword, const Cost &c)
{
	n.d[first_dword] = static_cast<DWORD>(c.a);
	n.d[first_dword + 1] = c.pad;
	n.d[first_dword + 2] = static_cast<DWORD>(static_cast<unsigned __int64>(c.b));
	n.d[first_dword + 3] = static_cast<DWORD>(static_cast<unsigned __int64>(c.b) >> 32);
}

int Valuation(void *object, const Node *node)
{
	return (*reinterpret_cast<ValuationFn *const *>(object))[0](object, nullptr, node);
}

}  // namespace

void PortedRelax(BYTE *pf, Node *from, Node *grid, Node *candidate, int step, Cost cost, const DWORD *extra, DWORD flag)
{
	if (!InGrid(pf, candidate->d[0]))  // 0x00c09da2
		return;
	Node n = *candidate;
	const DWORD tie = candidate->d[3] + step;
	n.d[5] = from->d[0];  // previous position
	const int spent = static_cast<int>(from->d[2]) + step;
	DWORD flags = n.d[1];

	if (flags & 0x40) {
		// Boat layer (0x00c09df1). Coming from land starts the counter in
		// bits 15-22, which must stay below 10.
		const bool from_boat = (from->d[1] >> 6) & 1;
		if (!from_boat) {
			if ((flags & 0x7f8000) >= 0x50000)
				return;
			flags ^= ((flags & 0xffff8000) + 0x8000 ^ flags) & 0x7f8000;
			n.d[1] = flags;
		}
		BYTE *mark = Mark(pf, 0xcc, n.d[0]);
		const bool reached = *mark == pf[0xd8];
		if (reached && (grid->d[1] & 0x7f8000) < (flags & 0x7f8000))
			return;
		if (from_boat)
			n.d[5] = from->d[5];
		const DWORD prev = n.d[5];
		const int x = static_cast<int>(n.d[0] & 0x3ff), y = static_cast<int>((n.d[0] >> 10) & 0x3ff);
		const int dx = x - static_cast<int>(prev & 0x3ff), dy = y - static_cast<int>((prev >> 10) & 0x3ff);
		const int distance = dy * dy + dx * dx;  // from the tile before the last
		if (distance > 0x40)
			return;
		if (reached) {
			const DWORD old_prev = grid->d[5];
			const int ox = x - static_cast<int>(old_prev & 0x3ff), oy = y - static_cast<int>((old_prev >> 10) & 0x3ff);
			if (oy * oy + ox * ox <= distance)
				return;
		} else {
			*mark = pf[0xd8];
		}
	} else {
		// Land layer (0x00c09f39).
		flags = (flags & ~0x800000u) | (spent > static_cast<int>(At(pf, 0x50)) ? 0x800000u : 0);
		n.d[1] = flags;
		Cost gain = {};  // what the valuation adds (0x00c09f46: zero unless there is a gain)
		void *valuation = reinterpret_cast<void *>(At(pf, 0x0c));
		if (valuation) {
			const int value = Valuation(valuation, &n);
			if (value > 0) {
				if (pf[0x80] == 0 && Valuation(valuation, from) <= 0 && (n.d[1] & 0x800000))
					return;
				gain.a = -1;
				gain.b = -static_cast<__int64>(value);
			}
			if ((n.d[1] & 0x800000) && !(from->d[1] & 0x800000))
				Combine(cost, CostAt(*from, 12));  // 0x00c09fee: changes the cost written below
		}
		Cost total = cost;
		Combine(total, gain);
		bool better = false;
		BYTE *mark = Mark(pf, 0xc0, n.d[0]);
		const bool reached = *mark == pf[0xd8];
		if (reached) {
			Cost old = CostAt(*grid, 8);
			Combine(old, CostAt(*grid, 12));
			if (Greater(old, total))
				return;
			if (Smaller(old, total))
				better = true;
			else if (tie >= grid->d[3])
				return;
		}
		const int limit = static_cast<int>(At(pf, 0x40));
		if (spent > limit && limit > 0 && !better) {
			pf[0x91] = 1;  // the search hit its limit
			return;
		}
		PutCost(n, 12, gain);  // gain.pad stays 0; the game leaves an uninitialized local there, never read
		if (!reached) {
			*mark = pf[0xd8];
			void *reached_list = reinterpret_cast<void *>(At(pf, 0xdc));
			if ((n.d[1] & 0x200) && reached_list) {
				Node *const grid_pointer = grid;
				reinterpret_cast<PushPointerFn>(kPushPointer)(reached_list, nullptr, &grid_pointer);
			}
		}
	}
	// 0x00c0a176: the new node.
	PutCost(n, 8, cost);
	n.d[3] = tie;
	n.d[6] = *extra;
	n.d[2] = static_cast<DWORD>(spent);
	n.d[1] = (n.d[1] & ~1u) | (flag & 1);
	*grid = n;
	reinterpret_cast<QueuePushFn>(kQueuePush)(pf + 0x10, nullptr, &n);
}

}  // namespace fastpaths
}  // namespace mods
