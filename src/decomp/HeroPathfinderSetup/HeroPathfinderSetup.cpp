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

class CHeroPathfinder {
public:
	void SetField_dc(int value);

	char m_pad[0xdc];
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
