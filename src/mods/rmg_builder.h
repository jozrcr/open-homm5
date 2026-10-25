// Mod: the template builder, a modal panel over the game's random map screen
// (see rmg_builder.cpp and docs/plans/in-game-generator.md).
#pragma once

namespace mods {

// From hooks::InstallEarly: reads [rmg] builder; the overlay's Direct3D import.
void InstallRmgBuilderEarly();
// From hooks::InstallAll: the panel (an overlay client) and modal input.
void ApplyRmgBuilderFromSettings();

}  // namespace mods
