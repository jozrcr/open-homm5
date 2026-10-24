// Drafts for HeroesFilter_00c07070: functions that did NOT reach a verified byte match yet.
// This file is not in unit.json "sources"; it includes the unit's matched source so that the
// types are shared. See attempts.txt for what is still different.
// Compile with the unit's flags: cl /nologo /c /O2 /G7 /GR /W3 /FAcs drafts.cpp

#include "HeroesFilter_00c07070.cpp"

// FUNCTION: 0x00c08be0
// Start a search: take the settings and the start position, put the start node (and the
// extra start positions) in the open list through the relax step, run the search, and
// optionally print the grid.
void CHeroPathfinder::StartSearch(const SPos *start, const Settings *s)
{
	m_settings = *s;
	m_start = *start;
	if (InBounds(&m_start)) {
		Node cur;
		InitStartNode(&cur);
		Node n2 = cur;
		n2.pos24 = g_f79000;
		int hits = 0;
		Relax(&n2, &m_a8.start[cur.pos.f].rows[cur.pos.y][cur.pos.x], &cur, 0, Cost(), &hits, 0);
		if (m_settings.b34 && m_98.size() > 0) {
			int i = 0;
			do {
				Node n3 = cur;
				SPos *p = &m_98.start[i];
				n3.pos.y = p->y;
				n3.pos.x = p->x;
				n3.pos.f = p->f;
				n3.prev = cur.posw;
				n3.d10 = m_settings.i28;
				n3.tie = m_settings.i28 + 200 + (m_98.size() - 1) * 500;
				n3.flags.b5 = 1;
				Relax(&cur, &m_a8.start[n3.pos.f].rows[n3.pos.y][n3.pos.x], &n3, cur.d10, Cost(), &hits, 0);
				i++;
			} while (i < m_98.size());
		}
		Run(m_settings.i20);
		if (g_dbgPaths) {
			for (int y = m_a8.start[0].height - 1; y >= 0; y--) {
				for (int x = 0; x < m_a8.start[0].width; x++) {
					Node &n = m_a8.start[0].rows[y][x];
					g_log << Format("V=%d,P=%d,B=%d,D=%d,MP=%d x%d Y%d\t",
					                m_c0.start[n.pos.f].rows[n.pos.y][n.pos.x] == m_d8, n.flags.b9,
					                n.flags.b1, n.flags.d11, n.moved, x, y);
				}
				g_log << Endl;
			}
		}
	}
}
