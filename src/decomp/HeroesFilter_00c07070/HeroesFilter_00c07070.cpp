// The hero pathfinder's map setup and search driver (0x00c07ba0, 0x00c08890, 0x00c08be0).
// The original file name is unknown; the folder is named after the lowest address of the group.
// See docs/subsystems/ai-pathfinder.md. All names are guesses.
//
// Types are declared locally (copied from HeroPathfinderSetup / HeroPathfinderQueue, which
// own the other functions of this class); functions of those units are only declared here.
//
// Built without /GX like the neighbouring HeroPathfinderQueue.

#include <string.h>

// --- the node (as in HeroPathfinderQueue) --------------------------------------

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

struct NodeBase {
	union {
		unsigned pos;
		unsigned char posb[4];
		struct {
			unsigned px : 10;
			unsigned py : 10;
			unsigned pfloor : 4;
		};
	};
	union {
		unsigned flags;
		struct {
			unsigned flagsLow : 27;
			unsigned flagsTop : 5;
		};
	};
	int moved;
	unsigned tie;
	unsigned d10;
	union {
		unsigned prev;
		unsigned char prevb[4];
	};
	union {
		unsigned d18;
		unsigned char d18b[4];
	};
	unsigned d1c;

	NodeBase()
	{
		posb[3] = 0;
		prevb[3] = 0;
		d18b[3] = 0;
		flagsTop = 0;
	}
};

struct Node : NodeBase {
	Cost cost;
	Cost cost2;
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

struct SPos {
	unsigned int x : 10;
	unsigned int y : 10;
	unsigned int f : 4;
	unsigned int top : 8;
};

// The map object the pathfinder watches: a class with a virtual base (vptr +0, vbptr +4),
// the map's extent as a packed position at +0x1c.
class CMapObj : public virtual CRefBase {
public:
	virtual void Slot0();
	char m_pad[0x14];
	SPos m_size;
};

class CHeroPathfinder {
public:
	CMapObj *m_map;  // 0x00
	char m_04[0xa8 - 4];
	vec<Array2D<Node> > m_a8;
	vec<Array2D<Node> > m_b4;
	vec<Array2D<unsigned char> > m_c0;
	vec<Array2D<unsigned char> > m_cc;
	unsigned char m_d8;

	void SetMap(CMapObj *map);                       // 0x00c07ba0
	void Prepare();                                  // 0x00c07920
	void InitNodes(vec<Array2D<Node> > *grids);      // 0x00c09260
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
