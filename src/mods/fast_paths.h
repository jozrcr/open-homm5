// Mod: faster AI pathfinding with exactly the same results (see fast_paths.cpp).
#pragma once
#include <windows.h>

namespace mods {

// Installs the mod if open-homm5.ini says [ai] fast_paths=1 (default 0 for now).
// Only after DetectGameBuild() said the build is hookable.
void ApplyFastPathsFromSettings();

// Remembered checks used (hits) and computed (misses) so far, for the robot's logs.
void FastPathsStats(LONGLONG *hits, LONGLONG *misses);

}  // namespace mods
