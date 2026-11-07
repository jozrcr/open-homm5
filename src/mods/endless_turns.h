// Mod: endless simultaneous turns on the 2008 game without MMH5.5 ([coop] endless_turns,
// the host's choice), as MMH5.5's PEST does. See endless_turns.cpp.
#pragma once

namespace mods {

// Read at start-up: can this PC play with endless simultaneous turns? Only the 2008 game
// without MMH5.5 (with MMH5.5, its 55u.dll does PEST itself, as MMH55Stats.xml says), and
// only when the 8 places hold the 2008 game's own bytes.
void InstallEndlessTurns();
bool EndlessTurnsAvailable();

// On or off from now on (every PC at the same point of the game: the co-op features'
// decision, coop_features.cpp). Off restores the game's own bytes.
void SetEndlessTurns(bool on);

}  // namespace mods
