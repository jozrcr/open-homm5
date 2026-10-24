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
// The open list is the game's std::priority_queue (vector of 64-byte nodes at
// pf+0x10..0x18). During the loop the port keeps it as a heap of indices into
// a pool of node copies, each with its sort key computed once: the very same
// heap algorithm and comparisons (VC++ 2003 _Push_heap / _Adjust_heap with
// operator< 0x00c08fd0, see fast_paths.cpp), but moving 4-byte indices instead
// of 64-byte nodes. When the loop ends, the game's vector gets the remaining
// nodes back in the same array order, so a later call that continues the
// search finds the list exactly as the game would have left it.
#include "fast_paths_internal.h"

#include <vector>

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

// The open list during one ported search (see the top of this file).
class IndexHeap {
public:
	void Import(const Node *first, const Node *last)
	{
		nodes_.clear();
		keys_.clear();
		free_.clear();
		heap_.clear();
		for (const Node *n = first; n < last; ++n)
			heap_.push_back(Store(*n));  // a valid heap array stays one, index for index
	}

	int Size() const { return static_cast<int>(heap_.size()); }
	const Node &Top() const { return nodes_[heap_[0]]; }

	void Push(const Node &n)
	{
		const int i = Store(n);
		heap_.push_back(i);
		PushHeap(Size() - 1, 0, i);
	}

	// std::pop_heap + pop_back: the top's copy, removed from the list.
	Node Pop()
	{
		const int top = heap_[0];
		const int size = Size();
		const int moved = heap_[size - 1];
		heap_[size - 1] = top;
		AdjustHeap(0, size - 1, moved);
		heap_.pop_back();
		free_.push_back(top);
		return nodes_[top];
	}

	// Back into the game's vector, same array order: clear it, then push every
	// node with the game's own push (0x00c099d0); a valid heap pushed in array
	// order does not move, so the vector ends up exactly like the heap.
	void Export(BYTE *pf) const
	{
		typedef void(__fastcall * QueuePushFn)(void *queue, void *edx, const Node *node);
		At(pf, 0x14) = At(pf, 0x10);
		for (int i : heap_)
			reinterpret_cast<QueuePushFn>(0x00c099d0)(pf + 0x10, nullptr, &nodes_[i]);
	}

private:
	// operator< 0x00c08fd0, with both readings of the cost pairs computed once:
	// a's flag bit 23 decides which one counts for both nodes.
	struct Key {
		int primary[2];
		__int64 value[2];
		DWORD tie;
		DWORD flag;
	};

	int Store(const Node &n)
	{
		Key k;
		const int p = static_cast<int>(n.d[8]), q = static_cast<int>(n.d[12]);
		const int low = p < q ? p : q;
		k.primary[0] = p;
		k.primary[1] = low < 0 ? low : (p <= q ? q : p);
		const unsigned __int64 v0 = n.d[10] | static_cast<unsigned __int64>(n.d[11]) << 32;
		const unsigned __int64 v1 = n.d[14] | static_cast<unsigned __int64>(n.d[15]) << 32;
		k.value[0] = static_cast<__int64>(v0);
		k.value[1] = static_cast<__int64>(v0 + v1);
		k.tie = n.d[3];
		k.flag = (n.d[1] >> 23) & 1;
		int i;
		if (!free_.empty()) {
			i = free_.back();
			free_.pop_back();
			nodes_[i] = n;
			keys_[i] = k;
		} else {
			i = static_cast<int>(nodes_.size());
			nodes_.push_back(n);
			keys_.push_back(k);
		}
		return i;
	}

	bool Less(int a, int b) const
	{
		const Key &ka = keys_[a], &kb = keys_[b];
		const DWORD f = ka.flag;
		if (ka.primary[f] != kb.primary[f])
			return ka.primary[f] < kb.primary[f];
		if (ka.value[f] != kb.value[f])
			return ka.value[f] < kb.value[f];
		return kb.tie < ka.tie;
	}

	void PushHeap(int hole, int top, int val)
	{
		for (int idx = (hole - 1) / 2; top < hole && Less(heap_[idx], val); idx = (hole - 1) / 2) {
			heap_[hole] = heap_[idx];
			hole = idx;
		}
		heap_[hole] = val;
	}

	void AdjustHeap(int hole, int bottom, int val)
	{
		const int top = hole;
		int idx = 2 * hole + 2;
		for (; idx < bottom; idx = 2 * idx + 2) {
			if (Less(heap_[idx], heap_[idx - 1]))
				--idx;
			heap_[hole] = heap_[idx];
			hole = idx;
		}
		if (idx == bottom) {
			heap_[hole] = heap_[bottom - 1];
			hole = bottom - 1;
		}
		PushHeap(hole, top, val);
	}

	std::vector<Node> nodes_;
	std::vector<Key> keys_;
	std::vector<int> free_;
	std::vector<int> heap_;
};

// Searches can nest (a callback of one search may start another one): one
// heap per level; deeper than that, the game's vector is used directly.
const int kMaxDepth = 4;
IndexHeap g_heaps[kMaxDepth];
int g_depth = 0;
IndexHeap *g_active = nullptr;  // the innermost ported search's heap
BYTE *g_active_pf = nullptr;

}  // namespace

bool PushToActiveSearch(BYTE *pf, const Node &n)
{
	if (!g_active || pf != g_active_pf)
		return false;
	g_active->Push(n);
	return true;
}

void PortedSearch(BYTE *pf, int limit)
{
	At(pf, 0x40) = static_cast<DWORD>(limit);
	DWORD stands_here = 0xffffff;  // the object pool's answer, kept from node to node (0x00c088b4)
	Node *&first = *reinterpret_cast<Node **>(pf + 0x10);
	Node *&last = *reinterpret_cast<Node **>(pf + 0x14);
	IndexHeap *const outer = g_active;
	BYTE *const outer_pf = g_active_pf;
	IndexHeap *heap = g_depth < kMaxDepth ? &g_heaps[g_depth] : nullptr;
	++g_depth;
	if (heap) {
		heap->Import(first, last);
		g_active = heap;
		g_active_pf = pf;
	}
	while (heap ? heap->Size() > 0 : last - first > 0) {
		// pop (0x00c088f0): the top goes to the end, the last node is sifted down
		Node top;
		if (heap) {
			top = heap->Pop();
		} else {
			top = *first;
			Node moved = last[-1];
			last[-1] = *first;
			AdjustHeap(first, 0, static_cast<int>(last - first) - 1, moved);
			--last;
		}
		Node n = *GridNode(pf, (top.d[1] & 0x40) ? 0xb4 : 0xa8, top.d[0]);
		// the next node is the same tile: it will be handled then (0x00c089d2)
		const Node *next = heap ? (heap->Size() > 0 ? &heap->Top() : nullptr) : (last - first > 0 ? first : nullptr);
		if (next && SameTile(next->d[0], n.d[0]) && ((next->d[1] ^ n.d[1]) & 0x240) == 0)
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
	if (heap) {
		heap->Export(pf);  // whatever is left goes back into the game's vector
		g_active = outer;
		g_active_pf = outer_pf;
	}
	--g_depth;
	if (last == first)
		pf[0x90] = 1;  // searched everything
}

}  // namespace fastpaths
}  // namespace mods
