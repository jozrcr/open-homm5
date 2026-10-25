// Mod: the AI decides at most once per frame, and never before its own last
// moves have happened (ai_pacing.cpp, docs/subsystems/hangs.md).
#pragma once

namespace mods {

// [ai] one_decision_per_frame=1 (the default): hooks the multiplayer segment runner.
void ApplyAiPacingFromSettings();

// Called by the AI turn driver hook (src/hook/ai_trace.cpp) around each call of
// the game's turn driver: false = do not call it now (it gets its turn in a
// later frame); after the call, how many commands it produced and its stage.
bool AiPacingAllowsStep();
void AiPacingAfterStep(int commands, unsigned long stage_before, unsigned long stage_after);

}  // namespace mods
