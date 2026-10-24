// Drafts for HeroPathfinderMisc: functions that did NOT reach a verified byte match yet.
// This file is not in unit.json "sources"; it includes the unit's matched source so
// that the types are shared. See attempts.txt for what is still different.
// Compile with the unit's flags (/Ow!):
//   cl /nologo /c /O2 /G7 /Ow /GR /W3 /FAcs drafts.cpp

#include <math.h>
#include "HeroPathfinderMisc.cpp"

float __stdcall Func_0046eae0(float x);       // 0x0046eae0: float -> float (probably a sine)

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

// 0x00c0a770 CBonusList::Recalc matched; it is in HeroPathfinderMisc.cpp.
