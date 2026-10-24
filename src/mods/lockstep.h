// Mod: a quicker multiplayer lockstep (see lockstep.cpp).
#pragma once

namespace mods {

// With the settings of open-homm5.ini: [net] drain_reports=1 (default),
// view_delay_ms=400 (the game's; 100..400), catch_up_ms=150 (the game's), trace_lockstep=1. Only after
// DetectGameBuild() said the build is hookable.
void ApplyLockstepFromSettings();

}  // namespace mods
