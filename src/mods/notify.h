// Mod: a "your turn" notification while the game is in the background (see notify.cpp).
#pragma once

namespace mods {

// With the settings of open-homm5.ini, section [notify]: turn=1, sound=1,
// popup=1 (the defaults), trace=0. Only after DetectGameBuild() said the build
// is hookable.
void ApplyNotifyFromSettings();

}  // namespace mods
