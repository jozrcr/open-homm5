// Mod: modern timings for the game's LAN link (see net_tuning.cpp).
#pragma once
#include <windows.h>

namespace mods {

// Idle links send every keepalive_ms (game: 1000); links may send a packet every
// send_interval_ms (game: 50); idle PCs batch up to batched_segments segment
// reports (game: net_batched_segments, 3). 0 ms / -1 = leave that one as it is.
// Only after DetectGameBuild() said the build is hookable.
void ApplyNetTuning(DWORD keepalive_ms, DWORD send_interval_ms, int batched_segments);

// The same with the settings of open-homm5.ini ([net] tuning=1, keepalive_ms=50,
// send_interval_ms=10, batched_segments=0 by default).
void ApplyNetTuningFromSettings();

}  // namespace mods
