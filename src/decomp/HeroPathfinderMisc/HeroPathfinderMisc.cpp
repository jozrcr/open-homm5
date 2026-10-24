// Functions 0x00c0a200 .. 0x00c0af20 of the vanilla 3.1 exe.
//
// The unit name "HeroPathfinderMisc" is a guess made by the task list: these
// functions sit right after the hero pathfinder's relax function (0x00c09d90),
// but they are NOT pathfinder code. What they really look like (all names are
// guesses from reading the code):
//
//   * CBonusList (0x24 bytes): a short list of {kind, level} entries (kind 1..10)
//     plus the summed per-kind bonus of all entries (SBonus, at +0xc). Looks like
//     a hero's chosen "specialisations": at most 3 entries, limited by a hero value.
//   * CGrid: a grid (vector of rows, 16 bytes each) of cells (40 bytes each:
//     a "used" byte and a CBonusList), with helpers that read, compare and swap cells.
//   * a few small helpers for text (tooltips), reference-counted handles and
//     vectors of ints.
//
// Compiler: Visual C++ 2003 (13.10.3077), /O2 /G7 /GR, but WITHOUT /GX (see unit.json
// "flags"): the original CGrid::SwapItems (0x00c0af20) has a local object with a
// destructor and no exception-handling frame, and nothing in this unit shows one.
// Free functions use __fastcall (the game seems to be built with /Gr).
//
// Matching notes (what the compiler taught us here):
//   * A "const" member function (CGrid::ColCount) lets the compiler keep member loads
//     across calls to it.
//   * "do { if (bad) break; ... return x; } while (0); <fallback>" lays the blocks out
//     like the original GetItem; plain if/else puts the return first.
//   * Stack slots go to locals in the order their address is first used in the code, so
//     the order of temporaries and arguments in an expression matters (Describe).
//   * A constructor-style call on a temporary (SFmtA(...).Conv()) reuses EAX, a named
//     local re-loads its address.
//   * m_total = SBonus(); compiles to the same stores as the original's inlined reset.

// ---------------------------------------------------------------------------
// Things outside this unit (addresses in progress/symbols.txt)

void __cdecl Free_00879240(void *p);          // 0x00879240: operator delete / free (cdecl)

// An object with a big virtual table (a hero?).
class IHeroLike {
public:
	virtual void S00(); virtual void S01(); virtual void S02(); virtual void S03();
	virtual void S04(); virtual void S05(); virtual void S06();
	virtual int GetValue();                   // slot 7, +0x1c
	virtual void S08(); virtual void S09();
	virtual void S10(); virtual void S11(); virtual void S12(); virtual void S13(); virtual void S14();
	virtual void S15(); virtual void S16(); virtual void S17(); virtual void S18(); virtual void S19();
	virtual void S20(); virtual void S21(); virtual void S22(); virtual void S23(); virtual void S24();
	virtual void S25(); virtual void S26(); virtual void S27(); virtual void S28(); virtual void S29();
	virtual void S30(); virtual void S31(); virtual void S32(); virtual void S33(); virtual void S34();
	virtual void S35(); virtual void S36(); virtual void S37(); virtual void S38(); virtual void S39();
	virtual void S40(); virtual void S41(); virtual void S42(); virtual void S43(); virtual void S44();
	virtual void S45(); virtual void S46(); virtual void S47(); virtual void S48(); virtual void S49();
	virtual void S50(); virtual void S51(); virtual void S52(); virtual void S53(); virtual void S54();
	virtual void S55(); virtual void S56(); virtual void S57(); virtual void S58(); virtual void S59();
	virtual void S60(); virtual void S61(); virtual void S62(); virtual void S63(); virtual void S64();
	virtual void S65(); virtual void S66(); virtual void S67(); virtual void S68(); virtual void S69();
	virtual void S70(); virtual void S71(); virtual void S72(); virtual void S73(); virtual void S74();
	virtual void S75(); virtual void S76(); virtual void S77(); virtual void S78(); virtual void S79();
	virtual void S80(); virtual void S81(); virtual void S82(); virtual void S83(); virtual void S84();
	virtual void S85(); virtual void S86(); virtual void S87(); virtual void S88(); virtual void S89();
	virtual void S90(); virtual void S91(); virtual void S92();
	virtual int GetStat(int which);           // slot 93, +0x174
};

// ---------------------------------------------------------------------------
// SBonus: a kind and ten per-kind byte counters (16 bytes).

struct SBonus {
	int kind;                      // +0
	unsigned char b[12];           // +4; only b[0]..b[9] are used

	SBonus();                      // 0x00c0a200
	SBonus &Add(const SBonus *o);  // 0x00c0a220 (returns *this)
};

// FUNCTION: 0x00c0a200
SBonus::SBonus()
{
	kind = 0;
	for (int i = 0; i < 12; i++)
		b[i] = 0;
}

// ---------------------------------------------------------------------------
// CBonusList

struct SEntry {
	int kind;
	int level;
};

// A vector of SEntry (begin, end, capacity), the first member of CBonusList.
struct SEntryVec {
	SEntry *begin;         // +0
	SEntry *end;           // +4
	SEntry *cap;           // +8

	SEntryVec() : begin(0), end(0), cap(0) {}
	void PushBack(const SEntry *e);                 // 0x00a68e00 (not decompiled)
};

class CBonusList {
public:
	SEntryVec m_vec;       // +0
	SBonus m_total;        // +0xc
	int m_1c;              // +0x1c
	bool m_flag;           // +0x20

	CBonusList();                                   // 0x00c0a870
	CBonusList(const CBonusList &o);                // 0x00434350 (not decompiled)
	~CBonusList() { if (m_vec.begin) Free_00879240(m_vec.begin); }
	bool Equals(const CBonusList *o);               // 0x006404d0 (not decompiled)
	void Describe(struct SText *out, int kind, IHeroLike *hero);   // 0x00c0a450
	void DescribeAt(struct SText *out, int idx);    // 0x00c0a630

	void GetTotal(SBonus *out);                     // 0x00c0a2c0
	int Count();                                    // 0x00c0a440
	int KindAt(int i);                              // 0x00c0a530
	void GetBonus(SBonus *out, int i);              // 0x00c0a560
	bool CanAdd(int kind, IHeroLike *hero);         // 0x00c0a700
	bool Recalc(int level);                         // 0x00c0a770
	void Clear();                                   // 0x00c0a8a0
	void Add(int kind, IHeroLike *hero);            // 0x00c0a8f0
};

int __fastcall BonusLevel(int kind, int n);         // 0x00c0a2f0

// FUNCTION: 0x00c0a870
CBonusList::CBonusList()
{
	m_1c = 0;
	m_flag = false;
}

// FUNCTION: 0x00c0a440
int CBonusList::Count()
{
	return m_vec.end - m_vec.begin;
}

// FUNCTION: 0x00c0a530
int CBonusList::KindAt(int i)
{
	if (i >= 0 && i < Count())
		return m_vec.begin[i].kind;
	return 0;
}

// FUNCTION: 0x00c0a560
void CBonusList::GetBonus(SBonus *out, int i)
{
	if (out && i >= 0 && i < Count()) {
		int k = m_vec.begin[i].kind;
		int v = m_vec.begin[i].level;
		out->kind = k;
		out->b[0] = (k == 4) ? v : 0;
		out->b[1] = (k == 1) ? v : 0;
		out->b[2] = (k == 2) ? v : 0;
		out->b[3] = (k == 5) ? v : 0;
		out->b[4] = (k == 3) ? v : 0;
		out->b[5] = (k == 6) ? v : 0;
		out->b[6] = (k == 7) ? v : 0;
		out->b[7] = (k == 8) ? v : 0;
		out->b[8] = (k == 9) ? v : 0;
		out->b[9] = (k == 10) ? v : 0;
	}
}

// FUNCTION: 0x00c0a700
bool CBonusList::CanAdd(int kind, IHeroLike *hero)
{
	if (!hero)
		return false;
	if (kind == 1)
		return false;
	int limit = hero->GetStat(0x11);
	if (limit <= 0)
		return false;
	int n = Count();
	if (n >= 3)
		return false;
	if (n + 1 > limit)
		return false;
	for (int i = 0; i < n; i++) {
		if (m_vec.begin[i].kind == kind)
			return false;
	}
	return true;
}

// FUNCTION: 0x00c0a8a0
void CBonusList::Clear()
{
	if (Count() > 0) {
		if (m_vec.begin)
			Free_00879240(m_vec.begin);
		m_vec.begin = 0;
		m_vec.end = 0;
		m_vec.cap = 0;
		m_total = SBonus();
	}
}

// FUNCTION: 0x00c0a8f0
void CBonusList::Add(int kind, IHeroLike *hero)
{
	if (hero && CanAdd(kind, hero)) {
		int level = BonusLevel(kind, hero->GetValue());
		if (level >= 0) {
			{
				SEntry e;
				e.kind = kind;
				e.level = level;
				m_vec.PushBack(&e);
			}
			m_flag = m_flag || hero->GetStat(0x11) >= 4;
			{
				SBonus tmp;
				GetBonus(&tmp, Count() - 1);
				m_total.Add(&tmp);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// A vector of ints (begin, end, capacity) with its out-of-line insert.

struct SIntVec {
	int *begin;
	int *end;
	int *cap;
	void Insert(int *pos, const int *val, int n, int flag);     // 0x00b1c6e0
	void PushBack(const int &v)
	{
		int *last = end;
		if (last != cap) {
			if (last)
				*last = v;
			end = last + 1;
		} else
			Insert(last, &v, 1, 1);
	}
};

// FUNCTION: 0x00c0a9b0
void __fastcall FillIds(SIntVec *v)
{
	if (v) {
		v->PushBack(1);
		v->PushBack(2);
		v->PushBack(3);
		v->PushBack(4);
		v->PushBack(5);
		v->PushBack(6);
		v->PushBack(7);
		v->PushBack(8);
		v->PushBack(9);
		v->PushBack(10);
	}
}

// ---------------------------------------------------------------------------
// CGrid: a grid of CBonusList cells. Rows are 16 bytes (a vector of cells),
// cells are 40 bytes: a "used" byte and the CBonusList at +4.

struct SCell {
	char used;                 // +0
	CBonusList item;           // +4
};

struct SRow {
	SCell *begin;              // +0
	SCell *end;                // +4
	SCell *cap;                // +8
	int pad;                   // +0xc

	CBonusList *At(int col);                        // 0x00c0b780 (not decompiled)
	bool IsUsed(int col) const;                         // 0x00c0b700 (not decompiled)
	void Set(int col, const CBonusList *item);      // 0x00c0bcc0 (not decompiled)
};

class CGrid {
public:
	SRow *m_begin;             // +0
	SRow *m_end;               // +4
	SRow *m_cap;               // +8

	int ColCount(int row) const;                        // 0x00433d20 (not decompiled)
	CBonusList *GetItem(int row, int col);          // 0x00c0ac80
	bool SameContents(CGrid *other);                // 0x00c0ad30
	void SwapItems(int r1, int c1, int r2, int c2); // 0x00c0af20
};

// FUNCTION: 0x00c0ac80
CBonusList *CGrid::GetItem(int row, int col)
{
	do {
		if (row < 0)
			break;
		if (row >= m_end - m_begin)
			break;
		if (col < 0)
			break;
		if (col >= ColCount(row))
			break;
		return m_begin[row].At(col);
	} while (0);
	static CBonusList s_empty;
	return &s_empty;
}

// 0x00433d20 (not in this unit's list; written here so that the compiler can see it)
int CGrid::ColCount(int row) const
{
	if (row >= 0 && row < m_end - m_begin)
		return m_begin[row].end - m_begin[row].begin;
	return 0;
}

// FUNCTION: 0x00c0ad30
bool CGrid::SameContents(CGrid *other)
{
	if (m_end - m_begin != other->m_end - other->m_begin)
		return false;
	int n = m_end - m_begin;
	int i = 0;
	while (i < n) {
		int a = other->ColCount(i);
		int b = ColCount(i);
		if (a != b)
			return false;
		i++;
	}
	int r = 0;
	while (r < m_end - m_begin) {
		int c = 0;
		while (c < ColCount(r)) {
			CBonusList *a = other->GetItem(r, c);
			CBonusList *b = GetItem(r, c);
			if (!a->Equals(b))
				return false;
			c++;
		}
		r++;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Reference-counted things (refcount at +8) and the two helpers that change it.

struct SRefObj {
	int a;
	int b;
	int refs;                  // +8
};

SRefObj * __fastcall Func_00c75330(void *p, void *q);   // 0x00c75330: returns p
void __fastcall Func_008c6150(SRefObj *o);              // 0x008c6150: destroys o
SRefObj * __fastcall Func_00ada320(int handle);         // 0x00ada320: handle -> object

// A smart pointer to an SRefObj with a flag byte; Get() is at 0x00ae3e90.
struct SHolder {
	SRefObj *ptr;              // +0
	char flag;                 // +4
	char *Get();               // 0x00ae3e90
	void Assign(const void *src);                       // 0x00415520

	// Drops the object (decrements its count, destroys it at 0) and clears the flag.
	void Reset()
	{
		SRefObj *old = ptr;
		ptr = 0;
		if (old) {
			SRefObj *r = Func_00c75330(old, old);
			if (--r->refs == 0)
				Func_008c6150(r);
		}
		flag = 0;
	}
};

struct SHandle {
	int a;
	int b;
	void Assign(const void *src);                       // 0x00415520
};

// A source record: a vector of 8-byte entries at +0 (begin, end), a handle at +0x1c.
struct SSource {
	char *begin;               // +0
	char *end;                 // +4
	int pad[5];
	int handle;                // +0x1c
};

// A wide string (begin, end, capacity) and a localized text that resolves to one.
struct SWStr {
	unsigned short *begin;
	unsigned short *end;
	unsigned short *cap;
	void Assign(const unsigned short *first, const unsigned short *last);     // 0x0040e960

	void Clear()
	{
		if (begin != end) {
			*begin = 0;
			end = begin;
		}
	}
};

struct SLocText {
	int x[6];
	SWStr *Resolve();                                           // 0x004c7430
};

// A name, a description and an icon (a refcounted pointer with a flag).
struct SName {
	SWStr name;                // +0
	SWStr desc;                // +0xc
	SHolder icon;              // +0x18 (flag at +0x1c)
};

int Func_00acb3a0();                                    // 0x00acb3a0: number of records
char * __fastcall Func_00ada2e0(int idx);               // 0x00ada2e0: record idx

// FUNCTION: 0x00c0abc0
void __fastcall ResetFrom(SName *p, int idx)
{
	if (p) {
		p->name.Clear();
		p->desc.Clear();
		p->icon.Reset();
		if (idx >= 0 && idx < Func_00acb3a0()) {
			char *rec = Func_00ada2e0(idx);
			SWStr *s = ((SLocText *)(rec + 0x44))->Resolve();
			if (s != &p->name)
				p->name.Assign(s->begin, s->end);
			s = ((SLocText *)(rec + 0x5c))->Resolve();
			if (s != &p->desc)
				p->desc.Assign(s->begin, s->end);
			p->icon.Assign(rec + 0x68);
		}
	}
}

// ---------------------------------------------------------------------------
// Text builders (a tooltip text?). The classes are guesses from the call sites.

struct SBuf28 {
	char b[28];
	SBuf28() {}
};

struct SFmtA {
	char b[28];
	SFmtA(const void *fmt, void *buf, int n);                   // 0x009d0330
	void *Conv();                                               // 0x009d0f60
};

struct SFmtB {
	char b[28];
	SFmtB *Init(int a1, int a2, int a3, int a4, int a5, int a6, int a7, const SBuf28 &p, int q);   // 0x009d0370 (a constructor)
};

struct SFmtSub {
	char x[20];
	void *Conv();                                               // 0x009d0e00
};

struct SFmtC {
	char pad[8];
	SFmtSub sub;               // +8
};

struct SText {
	void Assign(void *v);                                       // 0x009d03b0
	SFmtSub *Combine(SFmtSub *z, SFmtB *b);                     // 0x009d0a60
};

void *Func_009d0910();                                          // 0x009d0910: the default text

// FUNCTION: 0x00c0a450
void CBonusList::Describe(SText *out, int kind, IHeroLike *hero)
{
	SFmtB b;
	SFmtC z;
	if (out) {
		if (kind < 0 || kind >= Func_00acb3a0()) {
			out->Assign(Func_009d0910());
		} else {
			out->Assign(SFmtA(Func_00ada2e0(kind) + 0x70, &b, Count() + 1).Conv());
			if (hero && hero->GetStat(0x11) >= 4)
				out->Assign(out->Combine(&z.sub, b.Init(1, 1, 1, 1, 1, 1, 1, SBuf28(), 2))->Conv());
		}
	}
}

// FUNCTION: 0x00c0a630
void CBonusList::DescribeAt(SText *out, int idx)
{
	SFmtB b;
	SFmtC z;
	if (out && idx >= 0 && idx < Count()) {
		int kind = m_vec.begin[idx].kind;
		if (kind < 0 || kind >= Func_00acb3a0()) {
			out->Assign(Func_009d0910());
		} else {
			out->Assign(SFmtA(Func_00ada2e0(kind) + 0x70, &b, idx + 1).Conv());
			if (m_flag)
				out->Assign(out->Combine(&z.sub, b.Init(1, 1, 1, 1, 1, 1, 1, SBuf28(), 2))->Conv());
		}
	}
}

// Something that holds a count, a handle and a SHolder (0x10 bytes + flag).
struct SOwner {
	int count;                 // +0
	SHandle h;                 // +4
	SHolder holder;            // +0xc
};

// FUNCTION: 0x00c0ae80
void __fastcall SetupOwner(SOwner *p, SSource *q)
{
	if (p && q) {
		SRefObj *obj = Func_00ada320(q->handle);
		SRefObj *old = p->holder.ptr;
		SHolder *hp = &p->holder;
		hp->ptr = obj;
		if (obj)
			obj->refs++;
		if (old) {
			if (--old->refs == 0)
				Func_008c6150(old);
		}
		hp->flag = 0;
		int n = (q->end - q->begin) >> 3;
		p->count = n;
		switch (n) {
		case 1:
			p->h.Assign(hp->Get() + 0x5c);
			break;
		case 2:
			p->h.Assign(hp->Get() + 0x64);
			break;
		case 3:
			p->h.Assign(hp->Get() + 0x6c);
			break;
		}
	}
}
