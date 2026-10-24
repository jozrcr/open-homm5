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

// --- the pathfinder object (only what these functions read) ----------------------

// A map object as the pool returns it. It has a virtual base whose byte at +7 holds
// flag bit 0x80 (checked by 0x00c09690).
struct ObjBase {
	char pad[7];
	unsigned char flags;

	bool Flag80() const { return (flags & 0x80) != 0; }
};

struct Obj : virtual ObjBase {
	virtual void Slot0();
};

// The object pool at pf+0x04 (NWorld::CMobileObjectsPool according to the docs).
struct Pool {
	virtual Obj *Find(const unsigned *pos);                  // slot 0
	virtual bool Probe(unsigned *out, const unsigned *pos);  // slot 1
};

struct Player;

// The object at pf+0x70; slot 26 (offset 0x68) gives a player for a number.
struct Owner {
	virtual void Slot00();
	virtual void Slot01();
	virtual void Slot02();
	virtual void Slot03();
	virtual void Slot04();
	virtual void Slot05();
	virtual void Slot06();
	virtual void Slot07();
	virtual void Slot08();
	virtual void Slot09();
	virtual void Slot10();
	virtual void Slot11();
	virtual void Slot12();
	virtual void Slot13();
	virtual void Slot14();
	virtual void Slot15();
	virtual void Slot16();
	virtual void Slot17();
	virtual void Slot18();
	virtual void Slot19();
	virtual void Slot20();
	virtual void Slot21();
	virtual void Slot22();
	virtual void Slot23();
	virtual void Slot24();
	virtual void Slot25();
	virtual Player *GetPlayer(unsigned number);  // 26
};

// Not decompiled yet: 0x00c27d20, __fastcall(owner, object, player), ret 4.
int __fastcall CheckObjectForPlayer(Owner *owner, Obj *obj, Player *player);

// The open list's comparator: takes the nodes by value (the compiler copies them to
// the stack before calling the node order).
struct NodeCompare {
	bool operator()(const Node a, const Node b) const { return NodeLess(a, b); }
};

// FUNCTION: 0x00c09500
// _Push_heap: move the hole up while the parent is "less" than val, then put val in.
void __fastcall PushHeap(Node *first, int hole, int top, Node val, NodeCompare comp)
{
	int parent = (hole - 1) / 2;
	while (hole > top && comp(*(first + parent), val)) {
		*(first + hole) = *(first + parent);
		hole = parent;
		parent = (hole - 1) / 2;
	}
	*(first + hole) = val;
}

// FUNCTION: 0x00c095b0
// _Adjust_heap: move the hole down along the larger child to the bottom, then push val up.
void __fastcall AdjustHeap(Node *first, int hole, int bottom, Node val, NodeCompare comp)
{
	int top = hole;
	int idx = 2 * hole + 2;
	while (idx < bottom) {
		if (comp(*(first + idx), *(first + (idx - 1))))
			--idx;
		*(first + hole) = *(first + idx);
		hole = idx;
		idx = 2 * (idx + 1);
	}
	if (idx == bottom) {
		*(first + hole) = *(first + (idx - 1));
		hole = idx - 1;
	}
	PushHeap(first, hole, top, val, comp);
}

// The open list: a priority queue of nodes at pf+0x10: {start, finish,
// end_of_storage} followed by the comparator.
struct OpenList {
	Node *start;
	Node *finish;
	Node *end_of_storage;
	NodeCompare comp;

	// Not decompiled yet: 0x005d4c70, vector<Node>::_M_insert_overflow(pos, x, 1 element, at end), ret 0x10.
	void insert_overflow(Node *pos, const Node &x, int n, bool atend);
	void push(const Node &x);
};

inline void PushHeapAux(Node *first, Node *last, NodeCompare comp)
{
	PushHeap(first, int(last - first) - 1, 0, Node(*(last - 1)), comp);
}

inline void push_heap(Node *first, Node *last, NodeCompare comp)
{
	PushHeapAux(first, last, comp);
}

// FUNCTION: 0x00c099d0
// priority_queue::push: append, then push_heap.
void OpenList::push(const Node &x)
{
	if (finish != end_of_storage) {
		new (finish) Node(x);
		++finish;
	} else
		insert_overflow(finish, x, 1, true);
	push_heap(start, finish, comp);
}

class HeroPathfinder {
public:
	char pad00[4];
	Pool *pool;  // 0x04
	char pad08[8];
	OpenList open;  // 0x10
	char pad20[0x18];
	unsigned d38;  // 0x38
	char pad3c[0x34];
	Owner *owner;  // 0x70
	char pad74[0x34];
	vec<Array2D<Node> > grids0;           // 0xa8 nodes, layer 0
	vec<Array2D<Node> > grids1;           // 0xb4 nodes, layer 1
	vec<Array2D<unsigned char> > marks0;  // 0xc0 "reached" marks, layer 0
	vec<Array2D<unsigned char> > marks1;  // 0xcc
	unsigned char mark;                   // 0xd8: the current search's mark value

	bool IsMarked1(const unsigned *pos);
	Node *NodeAt1(const unsigned *pos);
	bool Probe(unsigned *out, const unsigned *pos, bool flag);
};

// --- the search settings (0x6c bytes, at pf+0x20) -------------------------------

// A reference-counted object with a virtual base, as the settings hold two of them.
// The count is at +8 of the virtual base; 0x008c6150 frees the object.
struct Counted {
	virtual void Slot0();
	int unused;
	int refs;

	void AddRef() { ++refs; }
	void Release()
	{
		if (--refs == 0)
			Free();
	}
	void Free();  // not decompiled yet: 0x008c6150, thiscall, no arguments
};

struct Shared : virtual Counted {
	virtual void Slot1();
};

// An intrusive smart pointer (the assignment is inlined in the game's code).
template <class T>
struct Ptr {
	T *p;

	Ptr &operator=(const Ptr &o)
	{
		T *n = o.p;
		T *old = p;
		p = n;
		if (n)
			n->AddRef();
		if (old)
			old->Release();
		return *this;
	}
};

// The first 0x18 bytes of the settings.
struct SettingsHead {
	bool b0;
	bool b1;
	bool b2;
	int i4;
	Ptr<Shared> p8;
	int ic;
	bool b10;
	int i14;

	SettingsHead &operator=(const SettingsHead &o);  // 0x00c09960, memberwise
};

struct Settings {
	SettingsHead head;  // 0x00
	int i18;
	int i1c;
	int i20;
	int i24;
	int i28;
	int i2c;
	int i30;
	bool b34;
	bool b35;
	bool b36;
	int i38;
	int i3c;
	bool b40;
	bool b41;
	bool b42;
	bool b43;
	bool b44;
	bool b45;
	bool b46;
	bool b47;
	bool b48;
	bool b49;
	Ptr<Shared> p4c;
	Ptr<Shared> p50;
	bool b54;
	bool b55;
	bool b56;
	bool b57;
	bool b58;
	int i5c;
	bool b60;
	bool b61;
	bool b62;
	bool b63;
	bool b64;
	bool b65;
	int i68;

	Settings &operator=(const Settings &o);  // 0x00c09c20, memberwise
};

// FUNCTION: 0x00c09960
SettingsHead &SettingsHead::operator=(const SettingsHead &o)
{
	b0 = o.b0;
	b1 = o.b1;
	b2 = o.b2;
	i4 = o.i4;
	p8 = o.p8;
	ic = o.ic;
	b10 = o.b10;
	i14 = o.i14;
	return *this;
}

// FUNCTION: 0x00c09c20
Settings &Settings::operator=(const Settings &o)
{
	head = o.head;
	i18 = o.i18;
	i1c = o.i1c;
	i20 = o.i20;
	i24 = o.i24;
	i28 = o.i28;
	i2c = o.i2c;
	i30 = o.i30;
	b34 = o.b34;
	b35 = o.b35;
	b36 = o.b36;
	i38 = o.i38;
	i3c = o.i3c;
	b40 = o.b40;
	b41 = o.b41;
	b42 = o.b42;
	b43 = o.b43;
	b44 = o.b44;
	b45 = o.b45;
	b46 = o.b46;
	b47 = o.b47;
	b48 = o.b48;
	b49 = o.b49;
	p4c = o.p4c;
	p50 = o.p50;
	b54 = o.b54;
	b55 = o.b55;
	b56 = o.b56;
	b57 = o.b57;
	b58 = o.b58;
	i5c = o.i5c;
	b60 = o.b60;
	b61 = o.b61;
	b62 = o.b62;
	b63 = o.b63;
	b64 = o.b64;
	b65 = o.b65;
	i68 = o.i68;
	return *this;
}

// FUNCTION: 0x00c091d0
// Is the tile at pos marked as reached in this search (layer 1)?
bool HeroPathfinder::IsMarked1(const unsigned *pos)
{
	unsigned p = *pos;
	return marks1.start[(p >> 20) & 0xf].rows[(p >> 10) & 0x3ff][p & 0x3ff] == mark;
}

// FUNCTION: 0x00c09220
// The node at pos in the layer-1 grid.
Node *HeroPathfinder::NodeAt1(const unsigned *pos)
{
	unsigned p = *pos;
	return &grids1.start[(p >> 20) & 0xf].rows[(p >> 10) & 0x3ff][p & 0x3ff];
}

// FUNCTION: 0x00c09690
bool HeroPathfinder::Probe(unsigned *out, const unsigned *pos, bool flag)
{
	Pool *pl = pool;
	if (!pl)
		return false;
	if (pl->Probe(out, pos))
		return true;
	if (flag) {
		Obj *o = pool->Find(pos);
		if (o && !o->Flag80()) {
			Owner *ow = owner;
			if (ow) {
				Player *player = ow->GetPlayer(d38);
				if (CheckObjectForPlayer(ow, o, player) >= 0) {
					*out = *pos;
					return true;
				}
			}
		}
	}
	return false;
}
