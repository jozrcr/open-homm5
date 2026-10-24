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
	void Reserve(int count);
};

struct SMapInfo {
	char m_pad[0x1c];
	SPos m_size;
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

	SMapInfo *m_map;       // 0x00
	CRefObjA *m_04;
	CRefObjA *m_08;
	CRefObjA *m_0c;
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
	SPtrVec m_a8;
	SPtrVec m_b4;
	SPtrVec m_c0;
	SPtrVec m_cc;
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
	CRefObjA *old = m_04;
	m_04 = p;
	if (p)
		p->AddRef();
	if (old)
		old->Release();
}

// FUNCTION: 0x00c07a80
void CHeroPathfinder::SetRefB(CRefObjA *p)
{
	CRefObjA *old = m_08;
	m_08 = p;
	if (p)
		p->AddRef();
	if (old)
		old->Release();
}

// FUNCTION: 0x00c07ad0
void CHeroPathfinder::SetRefC(CRefObjA *p)
{
	CRefObjA *old = m_0c;
	m_0c = p;
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
