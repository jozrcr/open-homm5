// Mod: a list of the game's players drawn over the adventure map (see
// player_list.cpp). Display only: it reads the game, never changes it.
#pragma once

namespace mods {

// From hooks::InstallEarly (DLL load, a build recognized then): only the
// Direct3D import, so the game's first device is seen too. Reads the settings.
void InstallPlayerListEarly();

// From hooks::InstallAll: the rest, with [ui] player_list=1 (the default).
void ApplyPlayerListFromSettings();

}  // namespace mods
