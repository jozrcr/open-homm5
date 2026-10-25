// The game's random map screen (NUI::CRMGScreen) for the template builder:
// the native "Template..." button and "Use template" (see rmg_screen.cpp).
#pragma once

#include <string>

namespace mods {

// The button's files (served through the game's file system) and its command.
// `open_panel` runs when the screen gets the command "oh_builder" (our button,
// or the robot's step "ui|NUI::CRMGScreen oh_builder"). False when the game's
// code is not as expected (then nothing is installed).
bool InstallRmgScreen(void (*open_panel)());
// Did the game load our changed main panel, so its screen shows our button?
// (Otherwise the builder draws a button of its own at the same place.)
bool RmgScreenHasNativeButton();

// What "Use template" sets on the game's screen.
struct ScreenChoice {
	int size_index;  // 0..6: the screen's sizes, rmg::kSizes' order
	bool underground;
	int players;          // 2..8
	std::string recipe;   // "OH-X1-..." (must be in the game's template list: rmg::RegisterRecipe)
};
// Sets the screen's own options as if the player had picked them: size,
// levels and players ticked and chosen, "ARMG Templates" ticked, and our
// template chosen in its list. `screen` is the live NUI::CRMGScreen.
// *report says what was done or what went wrong (for the log).
bool ApplyToRmgScreen(void *screen, const ScreenChoice &choice, std::string *report);

}  // namespace mods
