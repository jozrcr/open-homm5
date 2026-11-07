// Mod: more time for a PC that joins a multiplayer game ([net] join_timeout_s). See join_time.cpp.
#pragma once

namespace mods {

void ApplyJoinTimeFromSettings();

// The first segment of a multiplayer game (coop_features.cpp, every PC): from now on the
// game's own time limit again, so that a PC that goes silent in the game is dropped as fast
// as in the original game.
void JoinTimeGameStarted();

}  // namespace mods
