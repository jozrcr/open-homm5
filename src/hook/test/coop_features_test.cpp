// Test of the co-op features' agreement (src/mods/coop_features_logic.cpp): the hello,
// and who decides. No game needed.
#include <stdio.h>

#include "../../mods/coop_features_logic.h"

using namespace features;

namespace {

int g_failures = 0;

void Check(bool ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		++g_failures;
}

const unsigned kRhythm = 1u << kScriptRhythm, kParallel = 1u << kParallelAi, kBonus = 1u << kAiBonus,
               kEndless = 1u << kEndlessTurns;

}  // namespace

int main()
{
	// --- the hello ---
	Hello h = DecodeHello(EncodeHello(kKnown, kRhythm));
	Check(h.valid && h.version == kVersion && h.knows == kKnown && h.wants == kRhythm, "hello: what it knows and wants");
	h = DecodeHello(EncodeHello(kKnown, 0));
	Check(h.valid && h.wants == 0, "hello: wants nothing");
	Check(EncodeHello(kKnown, kRhythm | kParallel) >> 24 == 0x46, "hello: the mark 'F' in the top byte");
	Check(!DecodeHello(0x47010032).valid, "the co-op guard's hello ('G') is not ours");
	Check(!DecodeHello(0x55110001).valid, "Undo End Turn's hello ('U') is not ours");
	Check(!DecodeHello(0x46000301).valid, "version 0: not a hello");
	Check(!DecodeHello(0).valid, "nothing: not a hello");
	Check(kKnown == (kRhythm | kParallel | kBonus | kEndless), "this version knows script_rhythm, parallel_ai, ai_bonus and endless_turns");
	h = DecodeHello(EncodeHello(kKnown, kEndless | kBonus, 4));
	Check(h.valid && h.wants == (kEndless | kBonus) && h.ai_bonus == 4, "hello: endless_turns (bit 3) and the computer bonus travel together");
	Hello endless[2];
	endless[0] = DecodeHello(EncodeHello(kKnown, kEndless | kRhythm));
	endless[1] = DecodeHello(EncodeHello(kKnown & ~kEndless, 0));  // an MMH5.5 PC: does not know it (55u.dll does PEST)
	Check(Decide(endless, 2) == kRhythm, "a PC that does not know endless_turns (MMH5.5, 0.3.4): off, the rest as wanted");
	endless[1] = DecodeHello(EncodeHello(kKnown, 0));
	Check(Decide(endless, 2) == (kEndless | kRhythm), "two PCs of the 2008 game, the host wants endless_turns: on");
	h = DecodeHello(EncodeHello(kKnown, kBonus | kRhythm, 0));
	Check(h.valid && h.wants == (kBonus | kRhythm) && h.ai_bonus == 0, "hello: the computer bonus 0 (off) travels");
	h = DecodeHello(EncodeHello(kKnown, kBonus, 6));
	Check(h.valid && h.wants == kBonus && h.ai_bonus == 6, "hello: the computer bonus 6 (140%) travels");
	Check((EncodeHello(kKnown, kRhythm | kParallel, 6) & 0x03) == (kRhythm | kParallel),
	      "hello: an older version (bits 0-1) still reads script_rhythm and parallel_ai alone");
	Hello mixed[2];
	mixed[0] = DecodeHello(EncodeHello(kKnown, kBonus | kRhythm, 0));
	mixed[1] = DecodeHello(EncodeHello(kRhythm | kParallel, 0));  // a 0.3.3 PC
	Check(Decide(mixed, 2) == kRhythm, "a PC that does not know ai_bonus: the computer bonus is not set, the rest as before");

	// --- the decision ---
	Hello two[2];
	two[0] = DecodeHello(EncodeHello(kKnown, kRhythm));
	two[1] = DecodeHello(EncodeHello(kKnown, 0));
	Check(Decide(two, 2) == kRhythm, "the host wants script_rhythm, every PC knows it: on (the client's own wish does not count)");
	two[1] = DecodeHello(EncodeHello(kKnown, kRhythm | kParallel));
	Check(Decide(two, 2) == kRhythm, "a client that wants parallel_ai does not turn it on: only the host's counts");
	two[0] = DecodeHello(EncodeHello(kKnown, kRhythm | kParallel));
	Check(Decide(two, 2) == (kRhythm | kParallel), "the host wants both, every PC knows both: both on");
	two[1] = Hello();
	Check(Decide(two, 2) == 0, "a PC sent no hello (no add-on, an older one): everything off");
	two[1] = DecodeHello(EncodeHello(kRhythm, 0));
	Check(Decide(two, 2) == kRhythm, "a PC that knows only script_rhythm: parallel_ai off, script_rhythm on");
	two[0] = Hello();
	two[1] = DecodeHello(EncodeHello(kKnown, kKnown));
	Check(Decide(two, 2) == 0, "no hello from the host: everything off");
	two[0] = DecodeHello(EncodeHello(0xff, 0xff));
	two[1] = DecodeHello(EncodeHello(0xff, 0xff));
	Check(Decide(two, 2) == kKnown, "a newer version's features: off (this version does not know them)");
	Hello one[1];
	one[0] = DecodeHello(EncodeHello(kKnown, kParallel));
	Check(Decide(one, 1) == kParallel, "a game of one PC (the host and computer players): the host's wish");
	Check(Decide(one, 0) == 0, "no PCs: off");
	Hello four[4];
	for (Hello &x : four)
		x = DecodeHello(EncodeHello(kKnown, 0));
	four[0] = DecodeHello(EncodeHello(kKnown, kRhythm));
	Check(Decide(four, 4) == kRhythm, "four PCs that know it, the host wants it: on");
	four[3] = Hello();
	Check(Decide(four, 4) == 0, "four PCs, the last sent none: off");

	Check(Name(kScriptRhythm)[0] == 's' && Name(kParallelAi)[0] == 'p' && Name(kAiBonus)[0] == 'a' && Name(kEndlessTurns)[0] == 'e',
	      "names");

	if (g_failures)
		printf("%d FAILED\n", g_failures);
	else
		printf("all co-op features tests passed\n");
	return g_failures ? 1 : 0;
}
