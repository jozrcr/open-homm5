// The game's network "transceiver" classes, first part (0x007c80a0..0x007c8910):
// NWorld::CSinglePlayerTransceiver, NWorld::CSimpleTempTransceiver and
// NWorld::CReplayTransceiver share this source file (probably), with the
// save/load ("serialization") helper templates they use. The original file name
// is unknown. All member names are guesses; offsets are real.
//
// Built with /Ob1: the serialization helper templates (Chunk<T>) are free
// functions without `inline`, and the original calls them instead of inlining.
// No exception frames in this area, so no /GX.
//
// The classes have a virtual base, CObjectBase (vtable 0x00e0a7a0: pure slot 0,
// slot 2 = destructor, slot 3 = the save/load method). Their destructor and
// save/load methods override CObjectBase's, so the compiler passes `this` as the
// address of the virtual base (offset 0x90 / 0x2c / 0x68) and adds a vtordisp
// thunk in front (not in this list).

typedef unsigned int uint;

void __cdecl Free(void *p);  // 0x00879240
inline void *__cdecl operator new(unsigned int size, void *p) { return p; }

class IBinSaver;

// The virtual base of the transceivers (and of most game objects): intrusive reference counts.
class CObjectBase {
public:
	virtual void Reset() = 0;
	virtual void Slot1();
	virtual ~CObjectBase() {}
	virtual int operator&(IBinSaver *f);
	virtual void Slot4();
	virtual void Slot5() = 0;
	virtual void Slot6();

	uint m_strong;  // +4, low 20 bits = strong count
	uint m_weak;    // +8

	void Release()
	{
		--m_strong;
		if ((m_strong & 0xfffff) == 0)
			Destroy(0xfffff);
	}
	void AddRef() { ++m_strong; }
	void Destroy(uint mask);  // 0x008c61d0

	// The deleting destructors call this (0x008790b0), not the global one.
	static void operator delete(void *p);
};

// An object with a reference count at +4 and no virtual base of its own.
class CRefObj : public CObjectBase {
};

// A pointee type whose virtual base is reached through the vbptr (+4): the timer.
class CTimerObj : public virtual CObjectBase {
public:
	virtual void Step();  // the real class (IGameTimer) has 28 slots
	virtual void Reset();
	virtual void Slot5();
};

// Another kind, released through a conversion function (0x00c8b150).
class CConvObj;
CObjectBase * __fastcall ToBaseC(CConvObj *p, CConvObj *q);  // 0x00c8b150

class CChatLogic : public CObjectBase {  // NMP::CChatInGameLogicInternal
};

template <class T>
struct CPtrPlain {
	T *p;
	~CPtrPlain()
	{
		if (p)
			p->Release();
	}
};

struct CPtrTimer {
	CTimerObj *p;
	~CPtrTimer()
	{
		if (p)
			p->Release();
	}
};

// Saved with the archive (0x007c8750).
struct CPtrC {
	CConvObj *p;
	~CPtrC()
	{
		if (p)
			ToBaseC(p, p)->Release();
	}
	void OnSet(CConvObj *v);       // 0x007c7460
	void Release(CConvObj *old);   // 0x00611aa0
};

// Saved with the archive (0x007c8910).
struct CPtrD {
	CChatLogic *p;
	~CPtrD()
	{
		if (p)
			p->Release();
	}
	void Release(CChatLogic *old);  // 0x00611aa0 (the same function)
};

struct CBuffer {
	void *p;
	~CBuffer()
	{
		if (p)
			Free(p);
	}
};

// A vector of command pointers: {first, last, end}; the elements hold a count.
class CCmd : public CObjectBase {
};

void __fastcall DestroyRange(CCmd **first, CCmd **last);  // 0x00778a20

struct CmdVec {
	CCmd **first;
	CCmd **last;
	CCmd **end;

	~CmdVec()
	{
		DestroyRange(first, last);
		if (first)
			Free(first);
	}
	void PushBack(CCmd *const &x)
	{
		if (last != end) {
			if (last) {
				*last = x;
				if (x)
					x->AddRef();
			}
			++last;
		} else {
			InsertOverflow(last, &x, 1, 1);
		}
	}
	void PushBackOut(CCmd *const &x);                                     // 0x0079edf0
	void InsertOverflow(CCmd **where, CCmd *const *x, int n, int atEnd);  // 0x0068bd20
	void Reset()
	{
		DestroyRange(first, last);
		if (first)
			Free(first);
		first = 0;
		last = 0;
		end = 0;
	}
	void Resize(int n);  // 0x00706ac0
};

class SubD {  // an item saved with IBinSaver::AddSubD (0x00403d10)
public:
	char m_raw[0x18];
};

// ---- the save/load interface (a binary archive) -------------------------------------
class IBinSaver {
public:
	virtual void V00(); virtual void V01(); virtual void V02(); virtual void V03();
	virtual void V04(); virtual void V05(); virtual void V06();
	virtual bool StartChunk(int id, int n);                    // 0x1c
	virtual void FinishChunk();                                // 0x20
	virtual int CountChunks(int id);                           // 0x24
	virtual void DataChunk(int id, void *p, int size, int n);  // 0x28
	virtual void V11();
	virtual void DataChunk2(void *p);                          // 0x30
	virtual void StoreObject(CObjectBase *o);                  // 0x34
	virtual CObjectBase *LoadObject();                         // 0x38
	virtual bool IsReading();                                  // 0x3c
	virtual void V16();
	virtual int GetVersion();                                  // 0x44

	void AddPtrC(CPtrC *pp);   // 0x007c8750
	void AddPtrD(CPtrD *pp);   // 0x007c8910
	void AddVec(CmdVec *v);    // 0x007c8490
	void AddSubD(int id, SubD *p, int n);  // 0x00403d10
	void AddBuf(CBuffer *b);   // 0x004a5c10
};

CConvObj * __fastcall CastC(CObjectBase *o, int z, int z2);    // 0x00c8b160
CChatLogic * __fastcall CastD(CObjectBase *o, int z, int z2);  // 0x007c8960

// Items the archive code visits. A struct saves itself; a pointer holder is
// handled by the archive.
struct SubB;
struct SubA;
inline void Visit(IBinSaver *f, SubA *p);
inline void Visit(IBinSaver *f, SubB *p);
inline void Visit(IBinSaver *f, CPtrC *p) { f->AddPtrC(p); }
inline void Visit(IBinSaver *f, CPtrD *p) { f->AddPtrD(p); }

// A chunk of a save: ask the archive to open it, save/load the item, close it.
template <class T, class X>
void Chunk(IBinSaver *f, int id, int n, T *p, X *x)
{
	if (f->StartChunk(id, n)) {
		Visit(f, p);
		f->FinishChunk();
	}
}

// Items saved by templates outside this unit (0x0063ae40, 0x0058a100, 0x004fa250, 0x0044b560).
struct STypeB {
	int m_0;
};

template <>
void Chunk<CPtrTimer, IBinSaver *>(IBinSaver *f, int id, int n, CPtrTimer *p, IBinSaver **x);  // 0x0063ae40
template <>
void Chunk<STypeB, IBinSaver *>(IBinSaver *f, int id, int n, STypeB *p, IBinSaver **x);        // 0x0058a100
template <>
void Chunk<CPtrPlain<CRefObj>, IBinSaver *>(IBinSaver *f, int id, int n, CPtrPlain<CRefObj> *p, IBinSaver **x);  // 0x004fa250
template <>
void Chunk<CCmd *, CmdVec *>(IBinSaver *f, int id, int n, CCmd **p, CmdVec **x);         // 0x0044b560

// ---- the helper structs saved by the transceivers --------------------------------------
struct SubB {
	CPtrC m_0;
	char m_4[0xc];
	int m_10;
	SubD m_14;
	char m_2c[0x10];

	int operator&(IBinSaver *f);
};

struct SubA {
	int m_0;
	SubB m_4;
	int m_40;

	~SubA();  // 0x007c63b0
	int operator&(IBinSaver *f);
};

inline void Visit(IBinSaver *f, SubA *p) { p->operator&(f); }
inline void Visit(IBinSaver *f, SubB *p) { p->operator&(f); }

// FUNCTION: 0x007c8630
int SubA::operator&(IBinSaver *f)
{
	Chunk(f, 2, 1, &m_4, &f);
	f->DataChunk(3, &m_40, 4, 1);
	return 0;
}

// FUNCTION: 0x007c86a0
int SubB::operator&(IBinSaver *f)
{
	Chunk(f, 2, 1, &m_0, &f);
	if (f->StartChunk(3, 1)) {
		f->DataChunk2(m_4);
		f->FinishChunk();
	}
	f->DataChunk(4, &m_10, 4, 1);
	f->AddSubD(5, &m_14, 1);
	f->DataChunk(6, m_2c, 0x10, 1);
	return 0;
}

// FUNCTION: 0x007c8750
void IBinSaver::AddPtrC(CPtrC *pp)
{
	if (IsReading()) {
		CObjectBase *o = LoadObject();
		CConvObj *v = CastC(o, 0, 0);
		CConvObj *old = pp->p;
		pp->p = v;
		pp->OnSet(v);
		pp->Release(old);
	} else {
		StoreObject(ToBaseC(pp->p, pp->p));
	}
}

// FUNCTION: 0x007c8910
void IBinSaver::AddPtrD(CPtrD *pp)
{
	if (IsReading()) {
		CObjectBase *o = LoadObject();
		CChatLogic *v = CastD(o, 0, 0);
		CChatLogic *old = pp->p;
		pp->p = v;
		if (v)
			v->AddRef();
		pp->Release(old);
	} else {
		StoreObject(pp->p);
	}
}

// ---- the transceivers -------------------------------------------------------------------
// The interface: slot 0 is "send a command from this PC".
class ITransceiver : public virtual CObjectBase {
public:
	virtual void Send(CCmd *cmd) = 0;
	virtual void T01(); virtual void T02(); virtual void T03(); virtual void T04();
	virtual void T05(); virtual void T06(); virtual void T07(); virtual void T08();
	virtual void T09(); virtual void T10(); virtual void T11(); virtual void T12();
	virtual void T13(); virtual void T14(); virtual void T15();
};

class CSinglePlayerTransceiver : public ITransceiver {
public:
	CSinglePlayerTransceiver();  // 0x007c78a0
	virtual void Send(CCmd *cmd);
	virtual void Reset();
	virtual int operator&(IBinSaver *f);
	virtual void Slot5();

	CPtrTimer m_08;
	STypeB m_0c;
	CPtrPlain<CRefObj> m_10;
	int m_14;
	int m_18;
	SubA m_1c;
	CPtrTimer m_60;
	CPtrC m_64;
	CPtrPlain<CRefObj> m_68;
	int m_6c;
	CPtrD m_70;
	CBuffer m_74;
	int m_78;
	int m_7c;
	CmdVec m_80;
};

// FUNCTION: 0x007c80a0
void CSinglePlayerTransceiver::Send(CCmd *cmd)
{
	if (cmd)
		m_80.PushBackOut(cmd);
}

class CSimpleTempTransceiver : public ITransceiver {
public:
	CSimpleTempTransceiver();  // 0x007c7d40
	virtual void Send(CCmd *cmd);
	virtual void Reset();
	virtual int operator&(IBinSaver *f);
	virtual void Slot5();

	CPtrTimer m_08;
	STypeB m_0c;
	CPtrPlain<CRefObj> m_10;
	int m_14;
	CmdVec m_18;
	int m_24;
};

// FUNCTION: 0x007c8240
void CSimpleTempTransceiver::Send(CCmd *cmd)
{
	if (cmd)
		m_18.PushBack(cmd);
}

class CReplayTransceiver : public ITransceiver {
public:
	virtual ~CReplayTransceiver();
	virtual int operator&(IBinSaver *f);
	virtual void Reset();
	virtual void Slot5();

	CPtrTimer m_08;
	STypeB m_0c;
	int m_10;
	int m_14;
	int m_18;
	SubA m_1c;
	char m_60;
};

// The destructors (0x007c8100, 0x007c82b0) are the compiler's implicit ones: only an implicit
// destructor comes out without vtable-pointer stores like the exe's. The qualified calls in
// Reset() make the compiler emit them as functions.
// The constructors are only here so that the compiler emits the vtables and the deleting
// destructors; their bodies are NOT reconstructed (0x007c78a0 and 0x007c7d40).
CSinglePlayerTransceiver::CSinglePlayerTransceiver()
{
}

CSimpleTempTransceiver::CSimpleTempTransceiver()
{
}

// FUNCTION: 0x007c8390
void CSinglePlayerTransceiver::Reset()
{
	this->CSinglePlayerTransceiver::~CSinglePlayerTransceiver();
	uint weak = m_weak;
	uint strong = m_strong;
	new (this) CSinglePlayerTransceiver;
	m_weak += weak;
	m_strong += strong;
}

// FUNCTION: 0x007c8330
void CSimpleTempTransceiver::Reset()
{
	this->CSimpleTempTransceiver::~CSimpleTempTransceiver();
	uint weak = m_weak;
	uint strong = m_strong;
	new (this) CSimpleTempTransceiver;
	m_weak += weak;
	m_strong += strong;
}

// FUNCTION: 0x007c8410
int CSimpleTempTransceiver::operator&(IBinSaver *pf)
{
	IBinSaver *f = pf;
	Chunk(f, 2, 1, &m_08, &pf);
	Chunk(f, 3, 1, &m_0c, &pf);
	f->DataChunk(4, &m_14, 4, 1);
	if (f->StartChunk(5, 1)) {
		f->AddVec(&m_18);
		f->FinishChunk();
	}
	f->DataChunk(6, &m_24, 4, 1);
	return 0;
}

// FUNCTION: 0x007c8570
int CReplayTransceiver::operator&(IBinSaver *pf)
{
	IBinSaver *f = pf;
	Chunk(f, 2, 1, &m_08, &pf);
	Chunk(f, 3, 1, &m_0c, &pf);
	f->DataChunk(4, &m_14, 4, 1);
	f->DataChunk(5, &m_18, 4, 1);
	Chunk(f, 6, 1, &m_1c, &pf);
	f->DataChunk(7, &m_60, 1, 1);
	return 0;
}

// FUNCTION: 0x007c87c0
int CSinglePlayerTransceiver::operator&(IBinSaver *pf)
{
	IBinSaver *f = pf;
	Chunk(f, 2, 1, &m_08, &pf);
	Chunk(f, 3, 1, &m_0c, &pf);
	f->DataChunk(4, &m_14, 4, 1);
	f->DataChunk(6, &m_18, 4, 1);
	Chunk(f, 7, 1, &m_1c, &pf);
	Chunk(f, 8, 1, &m_60, &pf);
	Chunk(f, 9, 1, &m_64, &pf);
	Chunk(f, 10, 1, &m_68, &pf);
	f->DataChunk(11, &m_6c, 4, 1);
	Chunk(f, 12, 1, &m_70, &pf);
	if (f->StartChunk(13, 1)) {
		f->AddBuf(&m_74);
		f->FinishChunk();
	}
	if (f->StartChunk(14, 1)) {
		f->AddVec(&m_80);
		f->FinishChunk();
	}
	return 0;
}
