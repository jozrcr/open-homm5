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

#include <new>

// --- the node ---------------------------------------------------------------

// A cost as the game stores it at node+0x20 and node+0x30. `pad` is never read.
struct Cost {
	int a;
	unsigned pad;
	__int64 b;

};

// A search node (64 bytes). Positions are x | y << 10 | floor << 20 in 24 bits,
// the top byte is cleared by the constructor.
struct Node {
	union {
		unsigned pos;  // 0x00
		unsigned char posb[4];
	};
	union {
		unsigned flags;  // 0x04: bit 6 layer, bit 9, bits 15-22 counter, bit 23 "over the limit"
		struct {
			unsigned flagsLow : 27;
			unsigned flagsTop : 5;
		};
	};
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
		flagsTop = 0;
		posb[3] = 0;
		prevb[3] = 0;
		d18b[3] = 0;
		cost.a = 0;
		cost.b = 0;
		cost2.a = 0;
		cost2.b = 0;
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

// --- 2D arrays and vectors of them ---------------------------------------------

// A grid of T: `data` holds width*height elements, `rows[y]` points to row y.
// The pathfinder has one of these per floor, for nodes and for the "reached" marks.
template <class T>
struct Array2D {
	T *data;    // 0x00
	T **rows;   // 0x04
	int width;  // 0x08
	int height; // 0x0c

	Array2D() : data(0), rows(0), width(0), height(0) {}
	Array2D(const Array2D &o) { Copy(o); }
	Array2D &operator=(const Array2D &o);
	void Copy(const Array2D &o);
	void Alloc();
};

// The game's vector: three pointers {start, finish, end_of_storage} (no allocator
// member, STLport layout). Only the operations seen in this part of the exe.
template <class T>
struct vec {
	T *start;
	T *finish;
	T *end_of_storage;

	void insert_n(T *pos, int n);
	void insert_overflow(T *pos, int n, bool atend);
};

// FUNCTION: 0x00c08f30
template <>
void Array2D<Node>::Alloc()
{
	data = new Node[width * height];
	rows = new Node *[height];
	for (int i = 0; i < height; i++)
		rows[i] = data + width * i;
}

// FUNCTION: 0x00c09110
template <>
void Array2D<Node>::Copy(const Array2D &o)
{
	if (o.rows) {
		width = o.width;
		height = o.height;
		Alloc();
		for (int i = 0; i < height * width; i++)
			data[i] = o.data[i];
	} else {
		width = height = 0;
		data = 0;
		rows = 0;
	}
}

// FUNCTION: 0x00c093e0
template <>
Array2D<Node> &Array2D<Node>::operator=(const Array2D &o)
{
	if (width == o.width && height == o.height) {
		int n = height * width;
		for (int i = 0; i < n; i++)
			data[i] = o.data[i];
	} else {
		if (data)
			delete[] data;
		if (rows)
			delete[] rows;
		Copy(o);
	}
	return *this;
}
