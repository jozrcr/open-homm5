// Mod: let the camera zoom out farther, on the adventure map and in battles (see camera.cpp).
#pragma once

namespace mods {

// With the settings of open-homm5.ini, section [camera]: zoom_out=100 (the
// adventure map) and combat_zoom_out=100 (battles), in % of the game's own
// farthest zoom; 0, empty or 100 = the game's own (the default), and then
// nothing is installed. Only after DetectGameBuild() said the build is hookable.
void ApplyCameraFromSettings();

}  // namespace mods
