// The hero pathfinder's setup code: construction, preparing a search, small
// getters/setters and a few lookups into its grids (0x00c07070...0x00c07b60).
// The original file name is unknown; "HeroPathfinderSetup" is a guess from the
// role of the functions. All class and member names are guesses too.
//
// See docs/subsystems/ai-pathfinder.md for what the pathfinder does.

// A cost as the pathfinder stores it in a node (+0x20 and +0x30): `pad` is never read.
struct SCost {
	int a;
	unsigned int pad;
	__int64 b;

	bool Greater(const SCost &o) const;
};

// The pathfinder holds several reference-counted objects. Their classes have a
// virtual base with the counts: strong at +4, weak at +8 (guesses).
class CRefBase {
public:
	unsigned int m_pad;
	unsigned int m_strong;
	unsigned int m_weak;

	void AddRef() { ++m_strong; }
	void Release()
	{
		--m_strong;
		if ((m_strong & 0xfffff) == 0)
			Destroy(0xfffff);
	}
	void AddWeak() { ++m_weak; }
	void ReleaseWeak()
	{
		if (--m_weak == 0)
			FreeWeak();
	}
	void Destroy(unsigned int mask);  // 0x008c61d0
	void FreeWeak();                  // 0x008c6150
};

class CRefObjA;
CRefBase * __fastcall ToBase(CRefObjA *p, CRefObjA *q);  // 0x00bc5400

struct SPos;

// An interface object the pathfinder holds (the object pool, a valuation callback, ...).
// All have a vtable at +0, then the virtual base's pointer at +4.
class CRefObjA : public virtual CRefBase {
public:
	virtual CRefObjA *Slot0(const SPos *p);
	virtual void Slot1();
	virtual void Slot2();
	virtual bool Slot3(const SPos *p);
};

// A packed map position (24 bits used).
struct SPos {
	unsigned int x : 10;
	unsigned int y : 10;
	unsigned int f : 4;  // floor
	unsigned int top : 8;
};

// The settings block of a search (0x6c bytes), at pathfinder+0x20.
extern int g_f79000;  // 0x00f79000

class CPathSettingsBase {
public:
	CPathSettingsBase(int arg);  // 0x00ca9c50
	char m_raw[0x18];
};

class CPathSettings : public CPathSettingsBase {
public:
	CPathSettings(int mode, int arg);

	int m_18;
	int m_1c;
	int m_20;
	int m_24;
	int m_28;
	int m_2c;
	int m_30;
	char m_34;
	bool m_35;
	bool m_36;
	char m_37[9];
	bool m_40, m_41, m_42, m_43, m_44, m_45, m_46, m_47, m_48, m_49;
	char m_4a[2];
	int m_4c;
	int m_50;
	bool m_54, m_55, m_56, m_57, m_58;
	char m_59[3];
	float m_5c;
	bool m_60, m_61, m_62, m_63, m_64, m_65;
	char m_66[2];
	int m_68;
};

// A vector of 64-byte nodes or of 16-byte floor descriptors: {first, last, end}.
struct SPtrVec {
	char *first;
	char *last;
	char *end;
};

// The open list is such a vector; Reserve is 0x00c09370.
struct SOpenList : SPtrVec {
	SOpenList()
	{
		first = 0;
		last = 0;
		end = 0;
		Reserve(10);
	}
	void Reserve(int count);
};

template <class T>
struct SVec {
	T *first;
	T *last;
	T *end;
};

// One floor of the pathfinder's grids (16 bytes): rows[y][x] is a tile's node or mark.
struct SNode;
struct SNodeFloor {
	SNode *data;
	SNode **rows;
	unsigned int w;
	unsigned int h;
};
struct SMarkFloor {
	unsigned char *data;
	unsigned char **rows;
	unsigned int w;
	unsigned int h;
};

// The flags of a node. Only the grouping that the code shows is known.
struct SNodeFlags {
	unsigned int low : 15;
	unsigned int counter : 8;  // bits 15..22, used on the boat layer
	unsigned int high : 9;
};

// A search node (64 bytes).
struct SNode {
	SPos pos;               // 0x00
	SNodeFlags flags;       // 0x04
	unsigned int moved;     // 0x08 movement spent
	int tie;                // 0x0c tie-breaker
	unsigned int left;      // 0x10
	SPos prev;              // 0x14
	unsigned int m18;
	unsigned int m1c;
	SCost cost1;            // 0x20
	SCost cost2;            // 0x30
};

struct SMapInfo {
	char m_pad[0x1c];
	SPos m_size;
};

struct CRefPtr {
	CRefObjA *p;
	CRefPtr() : p(0) {}
};

class CHeroPathfinder {
public:
	CHeroPathfinder();
	void SetField_dc(int value);
	void SetRefA(CRefObjA *p);
	void SetRefB(CRefObjA *p);
	void SetRefC(CRefObjA *p);
	void SetWeakA4(CRefObjA *p);
	void SetWeak94(CRefObjA *p);
	bool InBounds(const SPos *p);                 // 0x0086e1b0
	unsigned int MovesAt(const SPos *p);          // 0x00c071b0
	unsigned int MovesAtChecked(const SPos *p);   // 0x00c07230
	unsigned int BoatCounterAt(const SPos *p);    // 0x00c07260
	bool NodeCheck(SNode *n);                     // 0x00c091d0

	SMapInfo *m_map;       // 0x00
	CRefPtr m_04;
	CRefPtr m_08;
	CRefPtr m_0c;
	SOpenList m_open;      // 0x10
	char m_1c[4];
	CPathSettings m_settings;  // 0x20
	SPos m_start;          // 0x8c
	bool m_90;
	bool m_91;
	char m_92[2];
	CRefObjA *m_94;
	SPtrVec m_98;
	CRefObjA *m_a4;
	SVec<SNodeFloor> m_a8;
	SVec<SNodeFloor> m_b4;
	SVec<SMarkFloor> m_c0;
	SVec<SMarkFloor> m_cc;
	unsigned char m_d8;
	bool m_d9;
	char m_da[2];
	int m_dc;
};

// FUNCTION: 0x00c070c0
bool SCost::Greater(const SCost &o) const
{
	if (a > o.a)
		return true;
	if (a < o.a)
		return false;
	return b > o.b;
}

// FUNCTION: 0x00c07100
void CHeroPathfinder::SetField_dc(int value)
{
	m_dc = value;
}

// FUNCTION: 0x00c07a30
void CHeroPathfinder::SetRefA(CRefObjA *p)
{
	CRefObjA *old = m_04.p;
	m_04.p = p;
	if (p)
		p->AddRef();
	if (old)
		old->Release();
}

// FUNCTION: 0x00c07a80
void CHeroPathfinder::SetRefB(CRefObjA *p)
{
	CRefObjA *old = m_08.p;
	m_08.p = p;
	if (p)
		p->AddRef();
	if (old)
		old->Release();
}

// FUNCTION: 0x00c07ad0
void CHeroPathfinder::SetRefC(CRefObjA *p)
{
	CRefObjA *old = m_0c.p;
	m_0c.p = p;
	if (p)
		p->AddRef();
	if (old)
		old->Release();
}

// FUNCTION: 0x00c07b60
void CHeroPathfinder::SetWeakA4(CRefObjA *p)
{
	CRefObjA *old = m_a4;
	m_a4 = p;
	if (p)
		p->AddWeak();
	if (old)
		old->ReleaseWeak();
}

// FUNCTION: 0x00c07b20
void CHeroPathfinder::SetWeak94(CRefObjA *p)
{
	CRefObjA *old = m_94;
	m_94 = p;
	if (p)
		ToBase(p, p)->AddWeak();
	if (old)
		ToBase(old, old)->ReleaseWeak();
}

// FUNCTION: 0x00c076d0
CPathSettings::CPathSettings(int mode, int arg) : CPathSettingsBase(arg)
{
	m_18 = 0;
	m_1c = g_f79000;
	m_20 = 0;
	m_24 = 0;
	m_28 = 0;
	m_2c = 0;
	m_30 = 0;
	m_35 = false;
	m_36 = false;
	m_40 = false;
	m_41 = false;
	m_42 = false;
	m_43 = false;
	m_44 = false;
	m_45 = false;
	m_46 = false;
	m_47 = false;
	m_48 = true;
	m_49 = true;
	m_4c = 0;
	m_50 = 0;
	m_54 = false;
	m_55 = false;
	m_56 = false;
	m_57 = false;
	m_58 = false;
	m_5c = 1.0f;
	m_60 = false;
	m_61 = false;
	m_62 = false;
	m_63 = false;
	m_64 = false;
	m_65 = true;
	m_68 = 0;
	switch (mode) {
	case 0:
		m_41 = true;
		m_46 = true;
		m_49 = true;
		m_47 = true;
		m_45 = true;
		m_43 = true;
		break;
	default:
		m_44 = true;
		m_65 = false;
		m_48 = false;
		m_63 = true;
		m_46 = true;
		m_49 = true;
		m_47 = true;
		m_45 = true;
		m_43 = true;
		break;
	case 3:
		m_58 = true;
	case 2:
		m_44 = false;
		m_65 = false;
		m_48 = false;
		m_63 = true;
		m_46 = true;
		m_49 = true;
		m_47 = true;
		m_45 = true;
		m_43 = true;
		break;
	case 7:
		m_48 = false;
		m_65 = false;
		m_54 = true;
		m_63 = true;
		m_45 = true;
		m_43 = true;
		break;
	case 4:
	case 5:
		m_43 = false;
		m_44 = false;
		m_45 = false;
		m_46 = false;
		m_47 = false;
		m_48 = false;
		m_49 = false;
		m_63 = false;
		m_65 = false;
		break;
	case 6:
		m_54 = false;
		m_55 = false;
		m_44 = false;
		m_58 = true;
		m_5c = 1.0f;
		m_62 = false;
		m_48 = false;
		m_63 = true;
		m_46 = true;
		m_49 = true;
		m_47 = true;
		m_45 = true;
		m_43 = true;
		break;
	case 8:
		m_44 = false;
		m_46 = false;
		m_49 = false;
		m_63 = false;
		m_65 = false;
		m_48 = true;
		m_64 = true;
		m_47 = true;
		m_45 = true;
		m_43 = true;
		break;
	case 9:
		m_44 = false;
		m_45 = false;
		m_46 = false;
		m_49 = false;
		m_63 = false;
		m_65 = false;
		m_47 = true;
		m_48 = true;
		m_43 = true;
		break;
	}
}

// FUNCTION: 0x00c07860
CHeroPathfinder::CHeroPathfinder() : m_map(0), m_settings(0, 0)
{
	m_start.top = 0;
	m_90 = false;
	m_91 = false;
	m_94 = 0;
	m_98.first = 0;
	m_98.last = 0;
	m_98.end = 0;
	m_a4 = 0;
	m_a8.first = 0;
	m_a8.last = 0;
	m_a8.end = 0;
	m_b4.first = 0;
	m_b4.last = 0;
	m_b4.end = 0;
	m_c0.first = 0;
	m_c0.last = 0;
	m_c0.end = 0;
	m_cc.first = 0;
	m_cc.last = 0;
	m_cc.end = 0;
	m_d9 = false;
	m_dc = 0;
	m_d8 = 0xff;
}

// FUNCTION: 0x00c071b0
unsigned int CHeroPathfinder::MovesAt(const SPos *p)
{
	SNode *n = &m_a8.first[p->f].rows[p->y][p->x];
	if (m_c0.first[n->pos.f].rows[n->pos.y][n->pos.x] == m_d8)
		return n->moved;
	return 0xfffffe;
}

// FUNCTION: 0x00c07230
unsigned int CHeroPathfinder::MovesAtChecked(const SPos *p)
{
	if (!InBounds(p))
		return 0xfffffe;
	return MovesAt(p);
}

// FUNCTION: 0x00c07260
unsigned int CHeroPathfinder::BoatCounterAt(const SPos *p)
{
	if (InBounds(p)) {
		SNode *n = &m_b4.first[p->f].rows[p->y][p->x];
		if (n && NodeCheck(n))
			return n->flags.counter;
	}
	return 0;
}
