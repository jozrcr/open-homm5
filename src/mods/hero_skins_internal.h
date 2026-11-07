// Hero looks: what the panel (hero_skins_panel.cpp) asks of the engine
// (hero_skins.cpp). Game thread only (the overlay draws in it).
#pragma once

#include <string>
#include <vector>

namespace mods {
namespace skins {

// One look the panel offers: a group of heroes of the faction that show the
// same map and battle figures.
struct Look {
	int hero = -1;             // the hero whose name the choice is saved under (the group's best name)
	std::vector<int> members;  // every hero of the group (catalog positions)
	bool own = false;          // the panel's hero is in it: its own look
	std::string portrait;      // the .dds of the hero's portrait (data path), or ""
	std::string name_file;     // the text file of its name, or ""
	std::string internal;      // its InternalName
};

struct PanelData {
	int hero = -1;            // the hero the panel is for (catalog position)
	bool from_hero_screen = false;  // it is the hero the hero screen shows (else the selected hero)
	std::string internal;     // its InternalName
	std::string name_file;
	std::string portrait;
	int chosen = -1;          // the look chosen for it now (catalog position), -1 = its own
	std::vector<Look> looks;  // its own look first
	bool multiplayer = false;
	bool decided = false;     // multiplayer: the looks' rule of segment 20 has been applied
	bool agreed = false;      // multiplayer: looks on in this game
};

// The hero the panel is for: the hero the hero screen shows when it is on top
// (its arrows and the town screen open it for heroes that are not the selected
// one), else this PC's player's selected hero (the adventure map, the town
// screen). False when there is none, when the hero screen shows a hero whose
// look this PC's player does not choose, or when the looks are off; *why says why.
bool PanelHero(PanelData *out, const char **why);

// Saves a choice to h5-together-skins.ini and makes it count: at once in single
// player; in multiplayer it is sent and counts on every PC at the same segment
// (a few seconds), if this game's looks are on. Returns a line for the log.
enum Applied { kAppliedNow, kAppliedSoon, kSavedForNextGames, kNotApplied };
Applied ApplyLook(int hero, int look, std::string *note);

// The portrait (.dds) of the look chosen for the panel's hero (PanelHero's: on the
// hero screen, the hero it shows), its own if none, for the hero screen's button;
// false when there is no such hero.
bool PanelLookPortrait(std::string *dds);

// After these heroes' looks changed (catalog positions): at the next adventure
// frame, a hero screen on top showing one of them closes and opens again (its
// "next hero" with the same hero), so its 3D figure shows the look. Logs the model.
void RefreshHeroScreen(const std::vector<int> &heroes, const char *why);

// Robot mode only (the panel's robot commands): "hero_screen" (the adventure
// screen's "hero_attributes"), "town <n>" (its "input_select_town_<n+1>", then
// "adv_show_town"), "town_hero_screen" (the town screen's "hero_attributes"),
// "hero_screen_next" / "hero_screen_prev" (the hero screen's own arrows),
// "lua <code>" (a line of Lua in the adventure controller's script machine, as
// the console's script commands: single player), "figures" (logs every map
// figure with its hero's look). Done at the next adventure frame, by the game's
// own message handlers.
void RobotScreenCommand(const char *text);

// The catalog's InternalName of a position, and the text file of its name ("" if none).
std::string InternalName(int hero);
std::string NameFile(int hero);

// The hero the hero screen on top shows (*on_hero_screen = true), else this PC's player's
// selected hero (the adventure map, the town screen); its IHero (NWorld::CHero's IHero part), or
// nullptr. Any owner's. The game's thread. (The skill wheel, skill_wheel.cpp.)
void *ShownHero(bool *on_hero_screen);

}  // namespace skins

// The panel (hero_skins_panel.cpp): adds the overlay client. The key comes
// from h5-together.ini [skins] key (F7 by default).
void InstallHeroSkinsPanel();
void InstallHeroSkinsPanelEarly();
// From the settings menu: [skins] key and hero_screen_button again, at once.
void ReloadHeroSkinsPanelSettings();
// From the settings menu: [skins] live_hero_screen again, at once.
void ReloadHeroSkinsSettings();

}  // namespace mods
