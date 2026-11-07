// The co-op features' agreement, pure work (no game), unit-tested by
// src/hook/test/coop_features_test.cpp. The mod itself is coop_features.cpp.
//
// In plain words: some of our changes act on the game world, so every PC of a
// multiplayer game must use them, or none (otherwise the PCs' worlds drift apart). At the
// start of each game every PC's DLL sends one hello, a 32-bit value next to the world
// checksums (as the co-op guard, hero looks and Undo End Turn do): "these are the
// features I know" and, from the host, "these are the ones I want". A feature is on for
// the game only if every PC knows it and the host wants it. A PC without our DLL, or with
// an older one, sends no such hello: then every feature is off, for every PC.
#pragma once

#include <stdint.h>

namespace features {

const unsigned kMark = 0x46;    // 'F' in the top byte (the co-op guard 'G', hero looks 'k' and 'K', undo 'U')
const unsigned kVersion = 1;

// The features (bit numbers). A newer version may add some: an older PC does not know
// them, so they stay off when it plays.
enum Feature {
	kScriptRhythm = 0,  // MMH5.5's scripts at their own pace (script_rhythm.cpp, [coop] script_rhythm)
	kParallelAi = 1,    // the computer players side by side (parallel_ai.cpp, [ai] parallel_turns)
	kAiBonus = 2,       // the host's MMH5.5 computer bonus, set in the game itself, also a loaded one
	                    // ([rules] ai_cheat: H55_AICheatMode; coop_features.cpp, 0.3.4)
	kEndlessTurns = 3,  // endless simultaneous turns on the 2008 game without MMH5.5, as MMH5.5's PEST
	                    // ([coop] endless_turns: endless_turns.cpp, 0.3.5). Only a PC of the 2008 game
	                    // knows it (with MMH5.5, its 55u.dll does PEST itself)
	kFeatureCount = 4,  // (the wants byte has room for 4: bits 0-3)
};
const unsigned kKnown = (1u << kFeatureCount) - 1;  // what this version knows

// When things count, in segments (every PC counts the same segments), as Undo End Turn's.
const unsigned kSendAt = 2;     // a PC sends its hello after its own report kSendAt...
const unsigned kSendBy = 10;    // ...and a hello counts only if it came by its sender's report kSendBy
const unsigned kDecideAt = 20;  // the segment at whose start every PC decides

struct Hello {
	bool valid = false;  // false: none came (or not one of ours)
	unsigned version = 0;
	unsigned knows = 0;     // bit f: the sender knows feature f
	unsigned wants = 0;     // bit f: the sender wants feature f (only the host's counts)
	unsigned ai_bonus = 0;  // with kAiBonus wanted: the computer bonus, 0..6 (H55_AICheatMode)
};

// The wants byte: the features in its low 4 bits, the computer bonus in bits 4-6 (an
// older version reads only the features it knows, the low bits).
uint32_t EncodeHello(unsigned knows, unsigned wants, unsigned ai_bonus = 0);
// valid = false when the value is not one of our hellos.
Hello DecodeHello(uint32_t value);

// The decision, the same on every PC: bit f is on only if every one of the `pcs` PCs sent
// a hello that knows f, and the host's (PC 0) wants f.
unsigned Decide(const Hello *hellos, int pcs);

// "script_rhythm parallel_ai" (the names of the bits set), "none" for 0.
const char *Name(int feature);

}  // namespace features
