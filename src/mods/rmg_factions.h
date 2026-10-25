// Mod: a start terrain that matches each player's faction on random maps (see rmg_factions.cpp).
#pragma once

namespace mods {

// With the settings of open-homm5.ini [rmg]: faction_terrain=0 (off by
// default), trace_factions=0. Only after DetectGameBuild() said the build is
// hookable.
void ApplyRmgFactionsFromSettings();

}  // namespace mods
