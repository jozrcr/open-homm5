// Mod: shorter multiplayer segments when every player agrees (see coop_guard.cpp).
#pragma once

namespace mods {

// With the settings of open-homm5.ini: [coop] guard=1, fast_segments=1,
// segment_ms=100 (the defaults). Only after DetectGameBuild() said the build is
// hookable.
void ApplyCoopGuardFromSettings();

}  // namespace mods
