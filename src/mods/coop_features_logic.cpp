// The co-op features' agreement, pure work (see coop_features_logic.h).
#include "coop_features_logic.h"

namespace features {

// The value: kMark in bits 24-31, the version in bits 16-23, what the sender knows in
// bits 8-15, what it wants in bits 0-3, the computer bonus in bits 4-6.
uint32_t EncodeHello(unsigned knows, unsigned wants, unsigned ai_bonus)
{
	return kMark << 24 | (kVersion & 0xff) << 16 | (knows & 0xff) << 8 | (ai_bonus & 7) << 4 | (wants & 0x0f);
}

Hello DecodeHello(uint32_t value)
{
	Hello h;
	if (value >> 24 != kMark || ((value >> 16) & 0xff) == 0)
		return h;
	h.valid = true;
	h.version = (value >> 16) & 0xff;
	h.knows = (value >> 8) & 0xff;
	h.wants = value & 0x0f;
	h.ai_bonus = (value >> 4) & 7;
	return h;
}

unsigned Decide(const Hello *hellos, int pcs)
{
	if (pcs < 1 || !hellos[0].valid)
		return 0;
	unsigned on = hellos[0].wants & kKnown;
	for (int pc = 0; pc < pcs; ++pc)
		on &= hellos[pc].valid ? hellos[pc].knows : 0;
	return on;
}

const char *Name(int feature)
{
	switch (feature) {
	case kScriptRhythm:
		return "script_rhythm";
	case kParallelAi:
		return "parallel_ai";
	case kAiBonus:
		return "ai_bonus";
	case kEndlessTurns:
		return "endless_turns";
	default:
		return "?";
	}
}

}  // namespace features
