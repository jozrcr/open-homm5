// Mod: quicker autosaves in multiplayer games (see autosave.cpp).
#pragma once

namespace mods {

// With the settings of open-homm5.ini, section [save]: trace_autosave=1,
// compression=1 (-1 = the game's level 6), autosave_every=1 (the defaults).
// Only after DetectGameBuild() said the build is hookable.
void ApplyAutosaveFromSettings();

}  // namespace mods
