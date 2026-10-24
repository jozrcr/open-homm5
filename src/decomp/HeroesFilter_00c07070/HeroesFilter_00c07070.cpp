// The hero pathfinder's map setup (0x00c07ba0: CHeroPathfinder::SetMap), plus the types that the
// two drafts in drafts.cpp need (0x00c08890: Run, the search loop, and 0x00c08be0: StartSearch).
// The original file name is unknown; the folder is named after the lowest address of the group
// (0x00c07070 is a different class and was not attempted, see attempts.txt).
// See docs/subsystems/ai-pathfinder.md. All names are guesses.
//
// Types are declared locally (copied from HeroPathfinderSetup / HeroPathfinderQueue, which
// own the other functions of this class); functions of those units are only declared here.
//
// Built without /GX like the neighbouring HeroPathfinderQueue (with /GX Run grows to 958 bytes).
//
// Node: the position's top byte is a bit-field (`pos.top = 0` gives the exe's `and byte [..], 0`
// on the stack, `posb[3] = 0` would give `mov byte`), the flags are separate bits.

#include <string.h>

// A packed map position: 24 bits used, the top byte is kept clear.
struct SPos {
	unsigned int x : 10;
	unsigned int y : 10;
	unsigned int f : 4;  // floor
	unsigned int top : 8;
};

inline bool Differs(const SPos *a, const SPos *b)
{
	return a->x != b->x || a->y != b->y || a->f != b->f;
}

// --- the node ------------------------------------------------------------------

struct Cost {
	int a;
	unsigned pad;
	__int64 b;

	Cost()
	{
		a = 0;
		b = 0;
	}
};

// The node flags (+4). Bit 1 and bit 9 are printed by the debug dump, bit 6 is the boat layer.
struct NodeFlags {
	unsigned b0 : 1;
	unsigned b1 : 1;
	unsigned b2 : 1;
	unsigned b3 : 1;
	unsigned b4 : 1;
	unsigned b5 : 1;
	unsigned b6 : 1;
	unsigned b7 : 1;
	unsigned b8 : 1;
	unsigned b9 : 1;
	unsigned b10 : 1;
	unsigned d11 : 4;
	unsigned counter : 8;
	unsigned b23 : 1;
	unsigned g24 : 2;
	unsigned b26 : 1;
	unsigned top : 5;
};

struct NodeBase {
	union {
		SPos pos;  // 0x00
		unsigned posw;
		struct {
			unsigned pos24 : 24;
			unsigned postop : 8;
		};
	};
	union {
		NodeFlags flags;  // 0x04
		unsigned flagsw;
	};
	int moved;     // 0x08
	unsigned tie;  // 0x0c
	unsigned d10;  // 0x10
	union {
		unsigned prev;  // 0x14
		unsigned char prevb[4];
	};
	union {
		unsigned d18;  // 0x18
		unsigned char d18b[4];
	};
	unsigned d1c;  // 0x1c

	NodeBase()
	{
		pos.top = 0;
		prevb[3] = 0;
		d18b[3] = 0;
		flags.top = 0;
	}
};

struct Node : NodeBase {
	Cost cost;   // 0x20
	Cost cost2;  // 0x30
};

// --- grids and vectors of them -------------------------------------------------

template <class T>
struct Array2D {
	T *data;
	T **rows;
	int width;
	int height;

	void Alloc();
};

template <class T>
T *__fastcall CopyRange(T *first, T *last, T *result);
void __fastcall DestroyRange(Array2D<Node> *first, Array2D<Node> *last);
void __fastcall DestroyRange(Array2D<unsigned char> *first, Array2D<unsigned char> *last);

template <class T>
struct vec {
	T *start;
	T *finish;
	T *end_of_storage;

	void insert_n(T *pos, int n);

	int size() const { return int(finish - start); }
	T *begin() { return start; }
	T &operator[](int n) { return *(begin() + n); }

	// destroy the elements, free the storage and forget it
	void clear_all()
	{
		DestroyRange(start, finish);
		if (start)
			operator delete(start);
		start = 0;
		finish = 0;
		end_of_storage = 0;
	}

	void erase(T *first, T *last)
	{
		T *i = CopyRange(last, finish, first);
		DestroyRange(i, finish);
		finish = i;
	}

	void resize(int n)
	{
		if (n < size())
			erase(start + n, finish);
		else
			insert_n(finish, n - size());
	}
};

// --- the open list (a priority queue of nodes, as in HeroPathfinderQueue) ------

bool __fastcall NodeLess(const Node &a, const Node &b);

struct NodeCompare {
	bool operator()(const Node a, const Node b) const { return NodeLess(a, b); }
};

void __fastcall AdjustHeap(Node *first, int hole, int bottom, Node val, NodeCompare comp);

struct NodeVec {
	Node *start;
	Node *finish;
	Node *end_of_storage;

	int size() const { return int(finish - start); }
};

inline void PopHeap(Node *first, Node *last, Node *result, Node val, NodeCompare comp)
{
	*result = *first;
	AdjustHeap(first, 0, int(last - first), val, comp);
}

inline void PopHeapAux(Node *first, Node *last, NodeCompare comp)
{
	PopHeap(first, last - 1, last - 1, Node(*(last - 1)), comp);
}

inline void pop_heap(Node *first, Node *last, NodeCompare comp)
{
	PopHeapAux(first, last, comp);
}

struct OpenList {
	NodeVec c;
	NodeCompare comp;

	void pop()
	{
		pop_heap(c.start, c.finish, comp);
		--c.finish;
	}
};

// --- the weak reference to the map ---------------------------------------------

class CRefBase {
public:
	unsigned int m_pad;
	unsigned int m_strong;
	unsigned int m_weak;

	void AddWeak() { ++m_weak; }
	void ReleaseWeak()
	{
		if (--m_weak == 0)
			FreeWeak();
	}
	void FreeWeak();  // 0x008c6150
};

// The world's tiles: floors[f].rows[y][x] (0x24 bytes per tile, kind at +8 here).
struct SWorldTile {
	char m_pad[8];
	int kind;
	char m_pad2[0x24 - 0xc];
};
struct SWorldFloor {
	char m_pad[4];
	SWorldTile **rows;
	char m_pad2[0x58 - 8];
};

// The map object the pathfinder watches: a class with a virtual base (vptr +0, vbptr +4),
// the map's extent as a packed position at +0x1c.
class CMapObj : public virtual CRefBase {
public:
	virtual void Slot0();
	char m_pad[0x14];
	SPos m_size;
	SWorldFloor *floors;  // 0x20
};

// The object pool at pf+0x04: slot 1 asks whether something stands at a position.
struct Pool {
	virtual void Slot0();
	virtual bool Probe(unsigned *out, const unsigned *pos);
};

// The search settings (0x6c bytes) at pf+0x20: only what these functions use.
struct Settings {
	char pad0[0x18];
	int i18;
	SPos target;  // pf+0x3c: where the search should stop
	int i20;  // pf+0x40: the limit (set by the search loop)
	int i24;
	int i28;  // pf+0x48
	int i2c;
	int i30;  // pf+0x50: movement left at the start
	bool b34;  // pf+0x54: use the extra start positions
	char pad35[0x43 - 0x35];
	bool b43;  // pf+0x63
	char pad44[0x48 - 0x44];
	bool b48;  // pf+0x68
	char pad49[0x6c - 0x49];

	Settings &operator=(const Settings &o);  // 0x00c09c20
};

// The debug log (a global object) and the printf-like formatter of the game.
struct SLog {
	SLog &operator<<(const char *s);              // 0x008c9630
	SLog &operator<<(SLog &(*manip)(SLog &));     // 0x008c9190
};
extern SLog g_log;           // 0x00ff540c
SLog &Endl(SLog &);          // 0x00403550
extern bool g_dbgPaths;      // 0x010c5889: print the grid after a search
extern int g_f79000;         // 0x00f79000
extern int g_dirs[];         // 0x00f757c8: the 8 step directions
char *Format(const char *fmt, ...);  // 0x00a47fd0

class CHeroPathfinder {
public:
	CMapObj *m_map;  // 0x00
	Pool *m_pool;    // 0x04
	void *m_08;
	void *m_0c;
	OpenList m_open;        // 0x10
	Settings m_settings;    // 0x20
	SPos m_start;           // 0x8c
	bool m_90;
	bool m_91;
	char m_92[2];
	void *m_94;
	vec<SPos> m_98;         // 0x98: extra start positions
	void *m_a4;
	vec<Array2D<Node> > m_a8;
	vec<Array2D<Node> > m_b4;
	vec<Array2D<unsigned char> > m_c0;
	vec<Array2D<unsigned char> > m_cc;
	unsigned char m_d8;

	void SetMap(CMapObj *map);                       // 0x00c07ba0
	void Run(int limit);                             // 0x00c08890
	void StartSearch(const SPos *start, const Settings *s);  // 0x00c08be0
	void Prepare();                                  // 0x00c07920
	void InitNodes(vec<Array2D<Node> > *grids);      // 0x00c09260
	bool InBounds(const SPos *p);                    // 0x0086e1b0
	bool InBoundsInline(const SPos *p)               // the same, inlined by the compiler in Run
	{
		return p->f < (unsigned)m_a8.size() && p->x < (unsigned)m_a8.start[p->f].width &&
		       p->y < (unsigned)m_a8.start[p->f].height;
	}
	void InitStartNode(Node *n);                     // 0x00c07110
	void Expand(Node *n, int count, int *dirs, int left, bool flag, unsigned *none);  // 0x00c07f80
	void Relax(Node *from, Node *tile, Node *cand, int a4, Cost c, int *out, int a10);  // 0x00c09d90
	bool Func_009d1350(Node *n);
	bool Func_009d22f0(Node *n);
};

// FUNCTION: 0x00c07ba0
void CHeroPathfinder::SetMap(CMapObj *map)
{
	CMapObj *old = m_map;
	m_map = map;
	if (map)
		map->AddWeak();
	if (old)
		old->ReleaseWeak();
	SPos size = m_map->m_size;
	unsigned w = size.x;
	unsigned h = size.y;
	int floors = size.f;
	m_a8.clear_all();
	m_a8.resize(floors);
	m_b4.clear_all();
	m_b4.resize(floors);
	m_c0.clear_all();
	m_c0.resize(floors);
	m_cc.clear_all();
	m_cc.resize(floors);
	for (int i = 0; i < floors; i++) {
		Array2D<Node> *a = &m_a8.start[i];
		if (a->width != w || a->height != h) {
			if (a->data)
				operator delete(a->data);
			if (a->rows)
				operator delete(a->rows);
			a->width = w;
			a->height = h;
			a->Alloc();
		}
		Array2D<Node> *b = &m_b4.start[i];
		if (b->width != w || b->height != h) {
			if (b->data)
				operator delete(b->data);
			if (b->rows)
				operator delete(b->rows);
			b->width = w;
			b->height = h;
			b->Alloc();
		}
		Array2D<unsigned char> *c = &m_c0.start[i];
		if (c->width != w || c->height != h) {
			if (c->data)
				operator delete(c->data);
			if (c->rows)
				operator delete(c->rows);
			c->width = w;
			c->height = h;
			c->Alloc();
		}
		Array2D<unsigned char> *d = &m_cc.start[i];
		if (d->width != w || d->height != h) {
			if (d->data)
				operator delete(d->data);
			if (d->rows)
				operator delete(d->rows);
			d->width = w;
			d->height = h;
			d->Alloc();
		}
		Array2D<unsigned char> &e = m_c0.start[i];
		memset(e.data, 0, e.height * e.width);
		Array2D<unsigned char> &g = m_cc.start[i];
		memset(g.data, 0, g.height * g.width);
	}
	InitNodes(&m_a8);
	InitNodes(&m_b4);
	m_d8 = 0xff;
	Prepare();
}

// FUNCTION: 0x00c08890
// The search loop: pop the best node, skip it when the next one is the same tile, run the
// checks, expand it; stop at the target or when the open list is empty.
void CHeroPathfinder::Run(int limit)
{
	m_settings.i20 = limit;
	Node cur;
	unsigned none = 0xffffff;
	bool hit;
	while (m_open.c.size() > 0) {
		cur = m_open.c.start[0];
		m_open.pop();
		Node *tile;
		if (cur.flags.b6)
			tile = &m_b4.start[cur.pos.f].rows[cur.pos.y][cur.pos.x];
		else
			tile = &m_a8.start[cur.pos.f].rows[cur.pos.y][cur.pos.x];
		cur = *tile;
		if (m_open.c.size() > 0) {
			Node next = m_open.c.start[0];
			if (next.pos.x == cur.pos.x && next.pos.y == cur.pos.y && next.pos.f == cur.pos.f &&
			    next.flags.b9 == cur.flags.b9 && next.flags.b6 == cur.flags.b6)
				continue;
		}
		hit = false;
		if (!cur.flags.b6) {
			cur.flags.b7 = 0;
			cur.flags.b8 = 0;
			if (m_settings.b48) {
				if (!Func_009d1350(&cur))
					continue;
			}
			if (cur.flags.b0) {
				if (!Func_009d22f0(&cur))
					continue;
			}
			if (cur.flags.b1) {
				if (m_map->floors[cur.pos.f].rows[cur.pos.y][cur.pos.x].kind != 2)
					continue;
			}
			if (!cur.flags.b3 && !m_settings.b43) {
				if ((m_start.x != cur.pos.x || m_start.y != cur.pos.y || m_start.f != cur.pos.f) && m_pool) {
					if (m_pool->Probe(&none, &cur.posw))
						hit = true;
				}
			}
		}
		Expand(&cur, 8, g_dirs, m_settings.i30 - cur.moved, hit, &none);
		if (!cur.flags.b6) {
			if (InBoundsInline(&m_settings.target) && !Differs(&m_settings.target, &cur.pos))
				break;
		}
	}
	if (m_open.c.size() == 0)
		m_90 = true;
}
