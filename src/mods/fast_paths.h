// Mod: faster AI pathfinding with exactly the same results (see fast_paths.cpp).
#pragma once
#include <windows.h>

namespace mods {

// Installs the mod unless open-homm5.ini says [ai] fast_paths=0 (default 1).
// Only after DetectGameBuild() said the build is hookable.
void ApplyFastPathsFromSettings();

// Remembered checks used (hits) and computed (misses) so far, for the robot's logs.
void FastPathsStats(LONGLONG *hits, LONGLONG *misses);
// The AI's turn driver starts (true) or ends (false) a thinking step
// (src/hook/ai_trace.cpp): remembered go-ashore answers last for a whole step.
void FastPathsOnAiStep(bool thinking);

// Per remembered check: "name=misses/calls ...".
void FastPathsCheckStats(char *out, size_t size);
// Time the remembered checks' misses too (robot path trace only: it costs a little).
void FastPathsMeasure(bool on);
// Time spent in game helpers called by the ported functions: "name=ms/calls ...".
void FastPathsHelperStats(char *out, size_t size);

}  // namespace mods
