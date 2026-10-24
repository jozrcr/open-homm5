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

const DWORD kDangerPoolVftable = 0x00e8ab0c;  // NWorld::CDangerPool

int Valuation(void *object, const Node *node)
{
	return (*reinterpret_cast<ValuationFn *const *>(object))[0](object, nullptr, node);
}

// The valuation of a node's tile. For NWorld::CDangerPool, slot 0 (0x00c27500)
// reads only node->position (a bounds check, then a value from its grid,
// which does not change during a search), so the answer is remembered per
// tile (t: the tile's record, or nullptr); other valuation objects are always
// asked.
int TileValuation(void *object, const Node *node, Tile *t)
{
	if (*static_cast<const DWORD *>(object) != kDangerPoolVftable)
		return Valuation(object, node);
	if (t && (t->known & kCheckDanger))
		return t->danger;
	const int value = Valuation(object, node);
	if (t) {
		t->known |= kCheckDanger;
		t->danger = value;
	}
	return value;
}

// The candidate's valuation, as the game asks it: of its copy with the new
// flags and previous position (for CDangerPool only the position counts).
int CandidateValuation(void *object, const Node *candidate, DWORD flags, DWORD prev, Tile *memo)
{
	if (*static_cast<const DWORD *>(object) == kDangerPoolVftable)
		return TileValuation(object, candidate, memo ? memo : TileFor(candidate->d[0]));
	Node n = *candidate;
	n.d[1] = flags;
	n.d[5] = prev;
	return Valuation(object, &n);
}

}  // namespace

// The game's function works on a copy n of the candidate; the port keeps the
// fields it changes (flags, previous position) apart and makes the copy only
// when the candidate wins, which most do not.
void PortedRelax(BYTE *pf, Node *from, Node *grid, Node *candidate, int step, Cost cost, const DWORD *extra, DWORD flag,
                 Tile *memo)
{
	const DWORD position = candidate->d[0];
	if (!InGrid(pf, position))  // 0x00c09da2
		return;
	const DWORD tie = candidate->d[3] + step;
	DWORD prev = from->d[0];  // n's previous position
	const int spent = static_cast<int>(from->d[2]) + step;
	DWORD flags = candidate->d[1];  // n's flags
	Cost gain = {};                 // land: what the valuation adds (0x00c09f46: zero unless there is a gain)

	if (flags & 0x40) {
		// Boat layer (0x00c09df1). Coming from land starts the counter in
		// bits 15-22, which must stay below 10.
		const bool from_boat = (from->d[1] >> 6) & 1;
		if (!from_boat) {
			if ((flags & 0x7f8000) >= 0x50000)
				return;
			flags ^= ((flags & 0xffff8000) + 0x8000 ^ flags) & 0x7f8000;
		}
		BYTE *mark = Mark(pf, 0xcc, position);
		const bool reached = *mark == pf[0xd8];
		if (reached && (grid->d[1] & 0x7f8000) < (flags & 0x7f8000))
			return;
		if (from_boat)
			prev = from->d[5];
		const int x = static_cast<int>(position & 0x3ff), y = static_cast<int>((position >> 10) & 0x3ff);
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
		void *valuation = reinterpret_cast<void *>(At(pf, 0x0c));
		if (valuation) {
			const int value = Measure(kHelperValuation, [&] { return CandidateValuation(valuation, candidate, flags, prev, memo); });
			if (value > 0) {
				if (pf[0x80] == 0 &&
				    Measure(kHelperValuation, [&] { return TileValuation(valuation, from, TileFor(from->d[0])); }) <= 0 &&
				    (flags & 0x800000))
					return;
				gain.a = -1;
				gain.b = -static_cast<__int64>(value);
			}
			if ((flags & 0x800000) && !(from->d[1] & 0x800000))
				Combine(cost, CostAt(*from, 12));  // 0x00c09fee: changes the cost written below
		}
		Cost total = cost;
		Combine(total, gain);
		bool better = false;
		BYTE *mark = Mark(pf, 0xc0, position);
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
		if (!reached) {
			*mark = pf[0xd8];
			void *reached_list = reinterpret_cast<void *>(At(pf, 0xdc));
			if ((flags & 0x200) && reached_list) {
				Node *const grid_pointer = grid;
				reinterpret_cast<PushPointerFn>(kPushPointer)(reached_list, nullptr, &grid_pointer);
			}
		}
	}
	// 0x00c0a176: the new node.
	Node n = *candidate;
	n.d[1] = (flags & ~1u) | (flag & 1);
	n.d[5] = prev;
	if (!(flags & 0x40))
		PutCost(n, 12, gain);  // gain.pad stays 0; the game leaves an uninitialized local there, never read
	PutCost(n, 8, cost);
	n.d[3] = tie;
	n.d[6] = *extra;
	n.d[2] = static_cast<DWORD>(spent);
	*grid = n;
	if (!PushToActiveSearch(pf, n))  // the ported search keeps its own copy of the list
		reinterpret_cast<QueuePushFn>(kQueuePush)(pf + 0x10, nullptr, &n);
}

}  // namespace fastpaths
}  // namespace mods
