// Hero pathfinder: node grids, open list, search settings.
//
// The original file name is unknown; "HeroPathfinderQueue" is a guess. These are
// the functions at 0x00c08f30..0x00c09c20 of the vanilla 3.1 exe: the pathfinder's
// node grid (a 2D array of 64-byte nodes), the open list (a priority queue of
// nodes whose vector is laid out {start, finish, end_of_storage}, STLport style),
// the heap algorithms, vector helpers for the grids and the search settings copy.
// See docs/subsystems/ai-pathfinder.md.
//
// All names are guesses. Declarations here are local to this unit (pilot).

// --- the node ---------------------------------------------------------------

// A cost as the game stores it at node+0x20 and node+0x30. `pad` is never read.
struct Cost {
	int a;
	unsigned pad;
	__int64 b;

	Cost() : a(0), b(0) {}
};

// A search node (64 bytes). Positions are x | y << 10 | floor << 20 in 24 bits,
// the top byte is cleared by the constructor.
struct Node {
	union {
		unsigned pos;  // 0x00
		unsigned char posb[4];
	};
	unsigned flags;  // 0x04: bit 6 layer, bit 9, bits 15-22 counter, bit 23 "over the limit"
	int moved;       // 0x08
	unsigned tie;    // 0x0c tie-breaker
	unsigned d10;    // 0x10
	union {
		unsigned prev;  // 0x14 previous position
		unsigned char prevb[4];
	};
	union {
		unsigned d18;  // 0x18
		unsigned char d18b[4];
	};
	unsigned d1c;  // 0x1c
	Cost cost;     // 0x20
	Cost cost2;    // 0x30

	Node()
	{
		flags &= 0x7ffffff;
		posb[3] = 0;
		prevb[3] = 0;
		d18b[3] = 0;
	}
};

// FUNCTION: 0x00c08fd0
// The open list's order: a < b. Bit 23 of a's flags decides, for both nodes,
// whether the second cost counts.
bool __fastcall NodeLess(const Node &a, const Node &b)
{
	Cost ca = a.cost;
	unsigned flag = (a.flags >> 23) & 1;
	if (flag) {
		int q = a.cost2.a;
		int low = ca.a < q ? ca.a : q;
		if (low < 0)
			ca.a = low;
		else if (ca.a <= q)
			ca.a = q;
		ca.b += a.cost2.b;
	}
	Cost cb = b.cost;
	if (flag) {
		int q = b.cost2.a;
		int low = cb.a < q ? cb.a : q;
		if (low < 0)
			cb.a = low;
		else if (cb.a <= q)
			cb.a = q;
		cb.b += b.cost2.b;
	}
	if (ca.a < cb.a)
		return true;
	if (ca.a > cb.a)
		return false;
	if (ca.b < cb.b)
		return true;
	if (ca.b > cb.b)
		return false;
	return a.tie > b.tie;
}
