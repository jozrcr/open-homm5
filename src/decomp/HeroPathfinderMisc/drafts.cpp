// Drafts for HeroPathfinderMisc: functions that did NOT reach a byte match yet.
// This file is not in unit.json "sources"; it includes the unit's matched source so
// that the types are shared. Each draft says what is still different (see attempts.txt).
// Compile it with the unit's flags to look at the generated code, e.g.
//   cl /nologo /c /O2 /G7 /GR /W3 /FAcs drafts.cpp
// (the compiler needs <math.h> for BonusLevel).

#include <math.h>
#include "HeroPathfinderMisc.cpp"

float __stdcall Func_0046eae0(float x);       // 0x0046eae0: float -> float (probably a sine)

// FUNCTION: 0x00c0a220
SBonus &SBonus::Add(const SBonus *o)
{
	b[0] += o->b[0];
	b[1] += o->b[1];
	b[2] += o->b[2];
	b[3] += o->b[3];
	b[4] += o->b[4];
	b[5] += o->b[5];
	b[6] += o->b[6];
	b[7] += o->b[7];
	int t9 = o->b[9];
	int t8 = o->b[8];
	b[8] += t8;
	b[9] += t9;
	return *this;
}

// FUNCTION: 0x00c0a2f0
int __fastcall BonusLevel(int kind, int n)
{
	int m = (n > 0) ? n : 1;
	n = m;
	switch (kind) {
	case 1: case 4:
		return n / 5 + 1;
	case 2: case 5:
		return n / 4 + 1;
	case 6: {
		double t = log(n * 10.0) / log(600.0);
		t = t * t * t;
		t = (t < 1.0) ? t : 1.0;
		float s = Func_0046eae0((float)(t * 1.5707964f));
		return (int)(s * 85.0);
	}
	case 7:
		return n / 10 + 1;
	case 8:
		return n / 10 + 1;
	case 3: case 9:
		return n / 15 + 1;
	case 10:
		return n;
	default:
		return -1;
	}
}

// FUNCTION: 0x00c0a2c0
void CBonusList::GetTotal(SBonus *out)
{
	if (out)
		*out = m_total;
}

// FUNCTION: 0x00c0a770
bool CBonusList::Recalc(int level)
{
	bool changed = false;
	for (int i = 0; i < Count(); i++) {
		int v = BonusLevel(m_vec.begin[i].kind, level);
		if (v > m_vec.begin[i].level) {
			changed = true;
			m_vec.begin[i].level = v;
		}
	}
	if (!changed)
		return false;
	m_total = SBonus();
	for (int i = 0; i < Count(); i++) {
		SBonus tmp;
		GetBonus(&tmp, i);
		m_total.Add(&tmp);
	}
	return true;
}

// FUNCTION: 0x00c0af20
void CGrid::SwapItems(int r1, int c1, int r2, int c2)
{
	if (r1 < 0)
		return;
	if (r1 >= m_end - m_begin)
		return;
	if (c1 < 0)
		return;
	if (c1 >= ColCount(r1))
		return;
	if (r2 < 0)
		return;
	if (r2 >= m_end - m_begin)
		return;
	if (c2 < 0)
		return;
	if (c2 >= ColCount(r2))
		return;
	if (c1 >= m_begin[r1].end - m_begin[r1].begin)
		return;
	if (m_begin[r1].begin[c1].used)
		return;
	if (m_begin[r2].IsUsed(c2))
		return;
	CBonusList tmp(*m_begin[r1].At(c1));
	m_begin[r1].Set(c1, m_begin[r2].At(c2));
	m_begin[r2].Set(c2, &tmp);
}

