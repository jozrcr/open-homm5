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

class CRefObjA : public virtual CRefBase {
public:
	virtual void Slot0();
};

class CHeroPathfinder {
public:
	void SetField_dc(int value);
	void SetRefA(CRefObjA *p);
	void SetRefB(CRefObjA *p);
	void SetRefC(CRefObjA *p);

	char m_pad0[4];
	CRefObjA *m_04;
	CRefObjA *m_08;
	CRefObjA *m_0c;
	char m_pad[0xdc - 0x10];
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
