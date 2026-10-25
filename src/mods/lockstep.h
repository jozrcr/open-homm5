// Mod: a quicker multiplayer lockstep (see lockstep.cpp).
#pragma once
#include <windows.h>

namespace mods {

// With the settings of open-homm5.ini: [net] drain_reports=1 (default),
// view_delay_ms=100 (with co-op timing ON; 50..400), catch_up_ms=600 (with
// co-op timing ON; the game's 150), scale_pacing=1 (with co-op timing ON),
// net_sleep_ms=2 (the game's 10), trace_lockstep=1. Only after
// DetectGameBuild() said the build is hookable.
void ApplyLockstepFromSettings();

// Called by the co-op guard: true when its shorter segments of `segment_ms`
// start, false at the start of a game (the margin, catch-up and pacing slope
// only apply while ON).
void LockstepCoopTiming(bool on, DWORD segment_ms);

}  // namespace mods
