// The template builder's data: what does not need the game or ImGui, so a test
// exe can check it (src/hook/test/rmg_builder_test.cpp). The panel itself is
// rmg_builder.cpp; the game's random map screen is rmg_screen.cpp.
#pragma once

#include <string>
#include <vector>

#include "rmg_maker.h"

namespace mods {
namespace rmg {
namespace builder {

// --- presets: the ready templates (tools/rmg/make_templates.js, READY) ------------------
struct Preset {
	const char *recipe;  // "OH-X1-..." (rmg_recipe.h)
	const char *name;    // the ready file's name, e.g. "OH-2v2v2-Lu-Normal"
};
extern const Preset kPresets[];
extern const int kPresetCount;
// The preset whose recipe the choices encode to, or -1.
int PresetOf(const Choices &choices);

// --- words -------------------------------------------------------------------------------
enum Language { kEnglish = 0, kFrench = 1 };  // as overlay::Language
struct Words {
	const char *title;
	const char *start_from, *custom;
	const char *teams_section, *teams, *per_team, *ally_path, *ally_items[2], *mines, *mines_items[2];
	const char *neutral_section, *team_zones, *border_zones, *centre_zones, *border_towns, *team_towns;
	const char *loot_section, *loot, *loot_items[4], *guards, *guard_items[3], *by_distance, *same, *rings[4];
	const char *map_section, *size, *auto_size, *sizes[7], *underground;
	const char *legend;
	const char *stats;         // printf: players, zones, neutral zones
	const char *size_line;     // printf: the size (sizes[i])
	const char *size_line_ug;  // the same, with underground
	const char *use, *save, *cancel;
	const char *saved, *not_saved, *use_failed;  // printf: a file name / a reason
	const char *native_button, *native_tooltip;
};
const Words &WordsFor(int language);

// The loot / guard words of the maker, in order ("poor"... / "weak"...).
extern const char *const kLootKeys[4];
extern const char *const kGuardKeys[3];

// A warning or an error of the maker in the panel's language (English: as it is).
std::string Localize(const std::string &maker_text, int language);

// --- the zone map: the page's positions() (tools/rmg/template-maker.html) ---------------
// In the page's drawing units: a 560x480 box, the circle centred at 280,240, radius 190.
struct Point {
	float x, y;
};
const float kMapCentreX = 280.0f, kMapCentreY = 240.0f, kMapRadius = 190.0f;
std::vector<Point> ZonePositions(const Template &t, const Choices &choices);

// --- the native "Template..." button: changed UI files of the random map screen ----------
// The game's files they are made from, and the fingerprints (rmg::Fingerprint)
// of the bytes we know: a different file is left alone (then the panel draws
// its own button instead).
extern const char kMainPanelPath[];   // "UI/RMGScreen/MainPanel.(WindowSimpleShared).xdb"
extern const char kSaveButtonPath[];  // "UI/RMGScreen/Save.(WindowMSButton).xdb"
extern const char kSaveReactionPath[];  // "UI/RMGScreen/Save.(UISRunReaction).xdb"
extern const char kButtonPath[];      // "UI/RMGScreen/OpenHomm5Builder.(WindowMSButton).xdb" (ours)
extern const char kReactionPath[];    // "UI/RMGScreen/OpenHomm5Builder.(UISRunReaction).xdb" (ours)
extern const char kLabelPath[];       // "UI/RMGScreen/OpenHomm5Builder.txt" (ours)
extern const char kTooltipPath[];     // "UI/RMGScreen/TTOpenHomm5Builder.txt" (ours)
extern const char kCommand[];         // "oh_builder": what our button sends to the screen
const unsigned long long kMainPanelFingerprint = 0x92c8eafe13c2abaaull;   // 1559 bytes, data.pak
const unsigned long long kSaveButtonFingerprint = 0x5c0a673331aa679cull;  // 1973 bytes
const unsigned long long kSaveReactionFingerprint = 0x19fcab28aa823d07ull;  // 337 bytes
// Where our button sits: the free slot between Save (188) and Back (491).
const int kButtonX = 340, kButtonY = 430;

// Each makes our file from the game's one; false (and *out untouched) when the
// game's file is not the one we know.
bool MakeMainPanel(const std::string &original, std::string *out);
bool MakeButton(const std::string &save_button, std::string *out);
bool MakeReaction(const std::string &save_reaction, std::string *out);
// A text file of the game: UTF-16 (little end first) with its byte order mark.
std::string GameText(const char *utf8);

}  // namespace builder
}  // namespace rmg
}  // namespace mods
