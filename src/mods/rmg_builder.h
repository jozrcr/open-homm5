// Mod: the template builder, a modal panel over the game's random map screen
// (see rmg_builder.cpp and docs/plans/in-game-generator.md).
#pragma once

namespace mods {

// From hooks::InstallEarly: reads [rmg] builder; the overlay's Direct3D import.
void InstallRmgBuilderEarly();
// From hooks::InstallAll: the panel (an overlay client), modal input, and the
// random map screen's "Template..." button (rmg_screen.cpp). Needs the recipe
// templates (rmg_templates.cpp, installed before).
void ApplyRmgBuilderFromSettings();

}  // namespace mods
