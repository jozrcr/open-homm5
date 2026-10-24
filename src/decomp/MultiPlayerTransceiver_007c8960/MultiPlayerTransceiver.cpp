// NWorld::CMultiPlayerTransceiver: the lockstep transceiver that exchanges world commands
// with the other players (0x007c8960..0x007c9410 of the vanilla 3.1 exe).
//
// The original file name is unknown; "MultiPlayerTransceiver" comes from the class name.
// See docs/subsystems/multiplayer-sync.md. Member and helper names are guesses; offsets
// come from the code. Declarations are local to this unit.
//
// Built without /GX: no function in 0x007c7xxx..0x007ccxxx has an exception frame.

// --- globals ---------------------------------------------------------------------------

extern int g_fe4d10;        // 0x00fe4d10: set by the pacing (the transceiver's game time)
extern int g_fe4d14;        // 0x00fe4d14
extern float g_speed;       // 0x00f3fa5c: the speed factor the pacing computes
extern bool g_mpCheckCrc;   // 0x00fe4d18 "mp_check_crc"
extern bool g_mpCheckObjCrc; // 0x00fe4d19 "mp_check_object_crc"
extern bool g_mpObserver2;  // 0x00fe4d1a "mp_observer_mode_for_player2"
extern bool g_asyncSave;    // 0x00f3fa60

// --- helper types ---------------------------------------------------------------------

// The game timer object at transceiver+0x28: only the time at +0x14 is read here.
struct STimer {
	char pad[0x14];
	unsigned time;  // 0x14
};

// The game's vector: {start, finish, end_of_storage}.
template <class T>
struct vec {
	T *start;
	T *finish;
	T *end_of_storage;

	int size() { return finish - start; }
};

// The game's doubly linked list: a heap-allocated sentinel node holds the links.
template <class T>
struct SListNode {
	SListNode *next;
	SListNode *prev;
	T value;
};

template <class T>
struct CList {
	SListNode<T> *head;
};

// One entry of the player table (28 bytes).
struct SPlayer {
	int type;  // 0x00
	int f04;
	int f08;
	int id;    // 0x0c
	char pad[0x0c];
};

// The record of one lockstep segment, the value of a node of the transceiver's list.
struct SSegment {
	int number;          // 0x00
	vec<int> *cmds;      // 0x04 one command list per player (12 bytes each)
	int f08;
	int f0c;
	unsigned mask;       // 0x10 the players that still owe their report
};

// The game's text log (a global stream object at 0x00ff540c).
struct SLevel {
	int value;
	SLevel(int v) : value(v) {}
};
struct SLog {
	SLog &Level(const SLevel &level);
	SLog &Str(const char *s);
	SLog &Int(const int &n);
	SLog &Manip(void (*f)());
};
extern SLog g_log;  // 0x00ff540c
void EndlManip();   // 0x00403550

// A reference-counted object (the count is at +8).
struct SRef {
	int f00;
	int f04;
	int count;  // 0x08
};

// The sub-object at transceiver+0x08.
struct SSub08 {
	char pad[0x18];
	bool Check(SRef *r);  // 0x007cc260
};

// The network object at transceiver+0x34 (virtual interface).
struct INet {
	virtual void N00();
	virtual void N01();
	virtual void N02();
	virtual void N03();
	virtual void N04();
	virtual void N05();
	virtual void N06();
	virtual void N07();
	virtual void N08();
	virtual void N09();
	virtual void N10();
	virtual void N11();
	virtual void N12();
	virtual float Ping(int playerId);  // 13
};

// The speed/timing object at transceiver+0x24.
struct SSpeed {
	char pad[4];
	void Set(float speed, int arg);  // 0x00475de0
};

void __fastcall Func_00b0f580(int a, void *world, int b);
float __stdcall FMax(float a, float b);  // 0x00465640: a < b ? b : a
float __stdcall FMin(float a, float b);  // 0x00465610

int Func_007c8a10();
int Func_007c8a20();
float Func_007c8a30();

// --- the transceiver ----------------------------------------------------------------------

class CMultiPlayerTransceiver {
public:
	virtual void Slot00();
	virtual void Slot01(int a, SRef *obj, int c);
	virtual void Slot02(int a, int b);
	virtual void Slot03();
	virtual void Slot04();
	virtual void Slot05(float dt, int arg);
	virtual bool Slot06(unsigned *next);
	virtual void Slot07();
	virtual int GetTime();
	virtual bool Slot09(int id);
	virtual bool Slot10();
	virtual bool Slot11(int type);
	virtual void Slot12();
	virtual int Slot13();
	virtual void Slot14();
	virtual bool Slot15();

	int FindPlayer(int id);
	bool AllReported(SSegment *seg);
	void DropPlayer(int index);

	int f04;
	SSub08 m_sub08;      // 0x08
	void *m_world;       // 0x20
	SSpeed m_speed;      // 0x24
	STimer *m_timer;     // 0x28
	unsigned m_segTime;  // 0x2c
	int f30;
	INet *m_net;         // 0x34
	CList<SSegment> m_segments;  // 0x38
	vec<int> m_reported; // 0x3c
	vec<SPlayer> m_players; // 0x48
	int m_me;            // 0x54
	unsigned m_active;   // 0x58
	char pad5c[0xd4 - 0x5c];
	unsigned m_dMask;    // 0xd4
	unsigned m_baseTime; // 0xd8
};

// FUNCTION: 0x007c8a10
int Func_007c8a10()
{
	return g_fe4d10;
}

// FUNCTION: 0x007c8a20
int Func_007c8a20()
{
	return g_fe4d14;
}

// FUNCTION: 0x007c8a30
float Func_007c8a30()
{
	return g_speed;
}

// FUNCTION: 0x007c8a00
bool CMultiPlayerTransceiver::Slot15()
{
	return (m_active & ~m_dMask) ? true : false;
}

// FUNCTION: 0x007c8b00
int CMultiPlayerTransceiver::GetTime()
{
	return m_timer->time - 400;
}

// FUNCTION: 0x007c8b10
bool CMultiPlayerTransceiver::Slot06(unsigned *next)
{
	unsigned t = m_segTime;
	if (m_timer->time > t) {
		*next = t + 200;
		return true;
	}
	return false;
}

// FUNCTION: 0x007c8b30
// Is this PC the first active player (the host)?
bool CMultiPlayerTransceiver::Slot10()
{
	int me = m_me;
	if (me < 0)
		return false;
	int first = -1;
	for (int i = 0; i < m_players.size(); i++) {
		if (m_active & (1 << i)) {
			first = i;
			break;
		}
	}
	return first == me;
}

// FUNCTION: 0x007c8bc0
void CMultiPlayerTransceiver::Slot02(int a, int b)
{
	Func_00b0f580(a, m_world, b);
}

// FUNCTION: 0x007c8c10
int CMultiPlayerTransceiver::FindPlayer(int id)
{
	for (int i = 0; i < m_players.size(); i++) {
		if (m_players.start[i].id == id)
			return i;
	}
	return -1;
}

// FUNCTION: 0x007c8c80
bool CMultiPlayerTransceiver::AllReported(SSegment *seg)
{
	bool ok = true;
	for (int i = 0; i < m_players.size(); i++) {
		unsigned bit = 1 << i;
		if ((bit & seg->mask) && (m_active & bit) && m_reported.start[i] <= seg->number)
			ok = false;
	}
	return ok;
}

// FUNCTION: 0x007c8d10
void CMultiPlayerTransceiver::DropPlayer(int index)
{
	if (index < 0)
		return;
	unsigned bit = 1 << index;
	m_active &= ~bit;
	for (int s = m_reported.start[index]; s <= m_segments.head->prev->value.number; s++) {
		for (SListNode<SSegment> *n = m_segments.head->next; n != m_segments.head; n = n->next) {
			if (n->value.number == s) {
				SSegment *seg = &n->value;
				if (seg)
					seg->mask &= ~bit;
				break;
			}
		}
	}
	m_reported.start[index] = -1;
	g_log.Level(SLevel(1)).Str("Drop player ").Int(index).Manip(EndlManip);
}

// FUNCTION: 0x007c8dd0
// Pacing: compares this PC's segment with the slowest player's and sets the speed factor.
void CMultiPlayerTransceiver::Slot05(float dt, int arg)
{
	int me = m_me;
	int mine = (me < 0) ? m_reported.start[0] : m_reported.start[me];
	if (mine < 0)
		return;
	int slowest = mine;
	for (int i = 0; i < m_reported.size(); i++) {
		int v = m_reported.start[i];
		if (v >= 0 && v < slowest)
			slowest = v;
	}
	float speed;
	if (me < 0) {
		int behind = (m_timer->time - (slowest + 150) * 200 - m_baseTime) / 200;
		speed = FMax(1.0f - behind * 0.05f, 0.0f);
	} else {
		int ahead = m_reported.start[me] - slowest;
		int n;
		if (ahead < 20) {
			n = 0;
			if (ahead >= 0)
				n = ahead;
		} else {
			n = 20;
		}
		speed = FMin(FMax(1.0f - n * 0.05f, 0.0f), 1.0f);
	}
	g_speed = speed;
	m_speed.Set(g_speed * dt, arg);
	g_fe4d10 = GetTime();
}

// FUNCTION: 0x007c8f00
int CMultiPlayerTransceiver::Slot13()
{
	int result = 0;
	for (int i = 0; i < m_players.size(); i++) {
		if (i != m_me) {
			float f = (float)result;
			result = (int)((f <= m_net->Ping(m_players.start[i].id)) ? m_net->Ping(m_players.start[i].id) : f);
		}
	}
	return result;
}

// FUNCTION: 0x007c8fd0
bool CMultiPlayerTransceiver::Slot09(int id)
{
	int me = m_me;
	if (me < 0)
		return false;
	SListNode<SSegment> *head = m_segments.head;
	for (SListNode<SSegment> *n = head->next; n != head; n = n->next) {
		for (int i = 0; i < n->value.cmds[me].size(); i++) {
			if (id == n->value.cmds[me].start[i])
				return true;
		}
	}
	return false;
}

// FUNCTION: 0x007c9050
bool CMultiPlayerTransceiver::Slot11(int type)
{
	if (g_mpObserver2 && type == 2)
		return true;
	for (int i = 0; i < m_players.size(); i++) {
		if (m_players.start[i].type == type && (m_active & (1 << i)))
			return true;
	}
	return false;
}
