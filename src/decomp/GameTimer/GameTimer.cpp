// NGameTimer::CGameTimer (vftable 0x00e0a8fc, 28 slots).
// The first functions in the exe (0x00401000...). The original file name is unknown;
// "GameTimer" is a guess from the class name.
//
// What the code shows: a timer that advances in fixed steps. m_time is the current
// time, m_stepStart the start of the current step, m_stepLength its length,
// m_stepCount the number of steps taken. All names are guesses.

namespace NGameTimer {

// Doubly linked list with a sentinel head node, as used at CGameTimer+0x3c.
// Layout read from the code: node = { next, prev, value }, list = { head }.
// Probably Nival's nstl::list; this is a minimal stand-in until we know more.
template <class T>
struct SListNode {
	SListNode *next;
	SListNode *prev;
	T value;
};

template <class T>
class CList {
public:
	SListNode<T> *head;

	bool empty() const { return head->next == head; }
	T &back() { return head->prev->value; }
};

class CGameTimer {
public:
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
	virtual unsigned int *GetTimePtr();              // 13
	virtual unsigned int *GetMember24Ptr();          // 14
	virtual unsigned int *GetStepStartPtr();         // 15
	virtual void Slot16();
	virtual bool IsStepDue();                        // 17
	virtual int GetStepCount();                      // 18
	virtual unsigned int GetStepsElapsed();          // 19
	virtual bool Step();                             // 20
	virtual void Slot21(int a, int b);               // 21
	virtual int GetLastListValue();                  // 22
	virtual bool ListContains(int value);            // 23
	virtual void Slot24();

	void Func00401330(int a, int b);                 // not reconstructed yet
	virtual int GetMember40();                       // 25
	virtual int GetMinValue();                       // 26
	virtual int GetMaxValue();                       // 27

	unsigned int m_04;
	unsigned int m_08;
	unsigned int m_0c;
	unsigned int m_time;        // 0x10
	unsigned int m_14;
	unsigned int m_18;
	bool m_1c;
	unsigned int m_20;
	unsigned int m_24[4];       // 0x24
	unsigned int m_stepStart;   // 0x34
	int m_stepCount;            // 0x38
	CList<int> m_list;          // 0x3c
	int m_40;
	unsigned int m_stepLength;  // 0x44
};

// FUNCTION: 0x00401230
unsigned int *CGameTimer::GetTimePtr()
{
	return &m_time;
}

// FUNCTION: 0x00401240
unsigned int *CGameTimer::GetMember24Ptr()
{
	return m_24;
}

// FUNCTION: 0x00401000
unsigned int *CGameTimer::GetStepStartPtr()
{
	return &m_stepStart;
}

// FUNCTION: 0x00401260
bool CGameTimer::IsStepDue()
{
	return m_time >= m_stepStart + m_stepLength;
}

// FUNCTION: 0x00401010
int CGameTimer::GetStepCount()
{
	return m_stepCount;
}

// FUNCTION: 0x00401250
unsigned int CGameTimer::GetStepsElapsed()
{
	return (m_time - m_stepStart) / m_stepLength;
}

// FUNCTION: 0x00401020
bool CGameTimer::Step()
{
	if (!IsStepDue())
		return false;
	m_stepStart += m_stepLength;
	++m_stepCount;
	return true;
}

// FUNCTION: 0x00401410
void CGameTimer::Slot21(int a, int b)
{
	Func00401330(a, b);
	m_1c = !m_list.empty();
}

// FUNCTION: 0x00401270
int CGameTimer::GetLastListValue()
{
	if (m_list.empty())
		return -1;
	return m_list.back();
}

// FUNCTION: 0x00401290
bool CGameTimer::ListContains(int value)
{
	for (SListNode<int> *node = m_list.head->next; node != m_list.head; node = node->next) {
		if (node->value == value)
			return true;
	}
	return false;
}

// FUNCTION: 0x00401040
int CGameTimer::GetMember40()
{
	return m_40;
}

// FUNCTION: 0x00401050
int CGameTimer::GetMinValue()
{
	return -10;
}

// FUNCTION: 0x00401060
int CGameTimer::GetMaxValue()
{
	return 10;
}

}  // namespace NGameTimer
