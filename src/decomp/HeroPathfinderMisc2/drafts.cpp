// Drafts for HeroPathfinderMisc2: functions that did NOT reach a byte match yet.
// This file is not in unit.json "sources"; it includes the unit's matched source.
// Compile with the unit's flags (no /Ow):
//   cl /nologo /c /O2 /G7 /GR /W3 /FAcs drafts.cpp

#include "HeroPathfinderMisc2.cpp"

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
