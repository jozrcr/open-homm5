// Drafts for MultiPlayerTransceiver_007c8960: functions that did NOT reach a verified byte
// match yet. This file is not in unit.json "sources"; it includes the unit's matched source
// so that the types are shared. See attempts.txt for what is still different.
// Compile with the unit's flags:  cl /nologo /c /O2 /G7 /GR /W3 /FAcs drafts.cpp

#include <typeinfo>
#include "MultiPlayerTransceiver.cpp"

// --- the in-game chat logic cast (0x007c8960) ----------------------------------------------

class CObjectBase {
public:
	virtual void O0();
};

class CChatInGameLogicInternal : public CObjectBase {
public:
	virtual void C1();
};

// The compiler's RTTI "complete object locator", found at vtable[-1].
struct SRttiLocator {
	int signature;          // 0x00
	int offset;             // 0x04 where this vtable sits in the complete object
	int cdOffset;           // 0x08
	const void *typeDesc;   // 0x0c
	const void *hierarchy;  // 0x10
};

inline SRttiLocator *LocatorOf(void *p)
{
	return ((SRttiLocator **)*(void ***)p)[-1];
}

// FUNCTION: 0x007c8960
// __fastcall(object, 0, 0): the call sites pass edx = 0 and a 0 on the stack.
// Tries the object's own type first (read from its RTTI), then the full dynamic_cast.
CChatInGameLogicInternal * __fastcall CastToChat(CObjectBase *p, int, int)
{
	if (!p)
		return 0;
	CObjectBase *q = p;
	int off = LocatorOf(p)->offset;
	if (off) {
		q = (CObjectBase *)((char *)p - off);
		SRttiLocator *loc = LocatorOf(q);
		if (!loc || loc->typeDesc != &typeid(CChatInGameLogicInternal))
			q = (CObjectBase *)dynamic_cast<CChatInGameLogicInternal *>(p);
	}
	return (CChatInGameLogicInternal *)q;
}

// FUNCTION: 0x007c8bc0
void CMultiPlayerTransceiver::Slot02(int a, int b)
{
	Func_00b0f580(a, m_world, b);
}

// FUNCTION: 0x007c8dd0
// Pacing: compares this PC's segment with the slowest player's and sets the speed factor.
void CMultiPlayerTransceiver::Slot05(float dt, int arg)
{
	int me = m_me;
	int slowest = (me >= 0) ? m_reported.start[me] : m_reported.start[0];
	if (slowest < 0)
		return;
	for (int i = 0; i < m_reported.size(); i++) {
		int v = m_reported.start[i];
		if (v >= 0 && v < slowest)
			slowest = v;
	}
	float speed;
	if (me < 0) {
		int behind = (m_timer->time - (slowest * 200 + 30000) - m_baseTime) / 200;
		speed = FMax(1.0f - behind * 0.05f, 0.0f);
	} else {
		int ahead = m_reported.start[me] - slowest;
		int n;
		if (ahead < 20) {
			n = 0;
			if (ahead >= 0)
				n = ahead;
		} else {
			ahead = 20;
			n = ahead;
		}
		speed = FMin(FMax(1.0f - n * 0.05f, 0.0f), 1.0f);
	}
	g_speed = speed;
	m_speed.Set(g_speed * dt, arg);
	g_fe4d10 = GetTime();
}

// FUNCTION: 0x007c9140
// A chat command arrived: find the sender's entry (by the player type in the command) and,
// if it passes the command's filter, hand the command (with the sender's name) to the chat sink.
void CMultiPlayerTransceiver::OnChat(CChatCommand *msg)
{
	int idx = -1;
	for (int i = 0; i < m_players.size(); i++) {
		if (i != m_me && m_players.start[i].type == msg->target) {
			idx = i;
			break;
		}
	}
	if (idx >= 0) {
		SPlayer *other = &m_players.start[idx];
		if (m_me >= 0) {
			SPlayer *self = &m_players.start[m_me];
			switch (msg->kind) {
			case 3:
				if (self->type != msg->value)
					return;
				break;
			case 2:
				if (self->f04 != other->f04)
					return;
				break;
			case 1:
				if (self->f08 != other->f08)
					return;
				break;
			}
		}
		if (&other->name != &msg->name)
			msg->name.Assign(other->name.first, other->name.last);
		m_chatSink->Handle(msg);
		return;
	}
	if (m_me >= 0)
		return;
	m_chatSink->Handle(msg);
}
