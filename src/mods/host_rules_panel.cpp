// The host rules' panel (docs/plans/host-rules.md): a modal overlay panel in
// the template builder's style, with each rule, a one-line explanation, and a
// check box or a list.
//   - The host chooses on the setup screen (NUI::CMPCreateScreen) or in the
//     waiting room (NUI::CMPWaitScreen). OK saves the choices to
//     h5-together.ini [rules] and the players' panels show them at once.
//   - A client sees the host's rules, read only (what the host sent in the lobby).
//   - On the adventure map, everyone sees this game's rules, read only.
// It opens with its key (F2 by default, [rules] key; a temporary key until the
// owner's sketch places a button) and, in the waiting room, with our "Rules..."
// button over the title row ([rules] button=1). The button reads the mouse
// itself (the game behind gets the click too: there is nothing to click there).
// In the waiting room it also draws a line at the bottom, under the game's frame,
// when the players' H5 Together versions differ (or are all the same).
//
// Robot steps (it cannot click): "panel|rules open", "panel|rules set <key>
// <value>", "panel|rules file" (MMH5.5's values), "panel|rules ok",
// "panel|rules cancel", "panel|rules scroll <units>|end".
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "host_rules_internal.h"
#include "rmg_builder_widgets.h"

#include "../hook/game_build.h"
#include "../hook/log.h"
#include "../hook/overlay.h"
#include "../hook/robot.h"
#include "../hook/texts.h"
#include "../hook/third_party/imgui/imgui.h"

namespace mods {
namespace {

namespace ui = builder_ui;
using rules::kRuleCount;
using rules::kRules;
using rules::Values;
using rules::View;

const char kName[] = "host rules";
const char kWaitScreen[] = ".?AVCMPWaitScreen@NUI@@";
const char kSetupScreen[] = ".?AVCMPCreateScreen@NUI@@";
const char kMapScreen[] = ".?AVCAdventureScreen@NUI@@";

// --- layout: the builder's panel (1024x768 units, from the panel's top-left corner) ------------
const float kRefHeight = 768.0f;
const float kPanelW = 649.0f, kPanelH = 508.0f;
const float kTitleX = 41.0f, kTitleY = 36.0f, kTitleW = 572.0f, kTitleH = 36.0f;
const float kContentX0 = 31.0f, kContentY0 = 76.0f, kContentX1 = 618.0f, kContentY1 = 418.0f;
const float kSubY = 81.0f;
const float kListX = 41.0f, kListY = 98.0f, kListW = 567.0f, kListH = 270.0f;
const float kStatusY = 372.0f;
const float kButtonW = 122.0f, kButtonH = 34.0f, kButtonY = 430.0f, kOkX = 41.0f, kFileX = 188.0f, kCancelX = 491.0f;
const float kTitleFont = 20.0f, kButtonFont = 15.0f, kBodyFont = 13.0f, kSmallFont = 11.0f;
const float kBackgroundTexW = 1024.0f, kBackgroundTexH = 512.0f;
// The waiting room's own layout (1024x768, centred, scaled with the height): our
// button at the right end of its title row ("Options du mode Standard"), where
// a click does nothing in the game.
const float kScreenW = 1024.0f;
const float kLobbyButtonX = 500.0f, kLobbyButtonY = 56.0f;
// The versions line: under the game's frame, between MMH5.5's "Version" text (bottom
// left) and the "Start the game" scroll (bottom right), where the screen is empty.
const float kVersionsX = 172.0f, kVersionsY = 694.0f, kVersionsW = 512.0f, kVersionsFont = 13.0f;

struct Layout {
	float k, x0, y0;
	ImVec2 At(float x, float y) const { return ImVec2(x0 + x * k, y0 + y * k); }
};

Layout LayoutFor(float w, float h, float panel_w, float panel_h)
{
	Layout l;
	l.k = h / kRefHeight;
	l.x0 = floorf((w - panel_w * l.k) / 2 + 0.5f);
	l.y0 = floorf((h - panel_h * l.k) / 2 + 0.5f);
	return l;
}

float Px(float size, float k)
{
	return floorf(size * k + 0.5f);
}

// --- words: the [rules] texts (src/hook/texts.h) ------------------------------------------------------
struct Words {
	const char *title;
	const char *sub_host, *sub_client, *sub_game, *sub_none;
	const char *groups[rules::kGroupCount];
	const char *names[kRuleCount];
	const char *help[kRuleCount];
	const char *ai[7];
	const char *on, *off;
	const char *file_value;  // {value}
	const char *own_value;   // {value}: a rule not in the file (map_scripts): what MMH5.5 does by itself
	const char *apply, *missing, *differ, *setup, *client_wait, *client_apply, *client_no, *client_differ;  // {players}
	const char *game_host, *game_file, *no_file, *no_script, *script_added;
	const char *ok, *file, *cancel, *close;
	const char *button;
	const char *modes[5];  // the game mode rule's values
	const char *mode_with_toe;  // its help when the ToE bonuses are on and it says "the arrows"
	const char *start_no;       // {why}
	const char *client_start_no;
	const char *class_modes[2];  // class_skills' values
	const char *class_note;      // with class_skills unlocked: what still follows MMH5.5's classes
	const char *class_no;        // {why}
	const char *client_class_no;
	const char *class_save;      // in game: the class skills came from the loaded save
};

const Words kKeys = {
	"rules.title",
	"rules.sub.host", "rules.sub.client", "rules.sub.game", "rules.sub.none",
	{"rules.group.start", "rules.group.heroes", "rules.group.towns", "rules.group.computer", "rules.group.items",
	 "rules.group.random_maps"},
	{"rules.name.governance", "rules.name.town_conversion", "rules.name.extra_conversions", "rules.name.town_gate",
	 "rules.name.mine_control", "rules.name.dimension_doors", "rules.name.ai_cheat", "rules.name.mixed_stacks",
	 "rules.name.neutral_stack_size", "rules.name.bank_difficulty", "rules.name.negative_artifacts", "rules.name.rpg_potions",
	 "rules.name.competitive_towns", "rules.name.mana_observatories", "rules.name.moon_disc", "rules.name.game_mode",
	 "rules.name.toe_bonuses", "rules.name.map_scripts", "rules.name.class_skills", "rules.name.class_chance"},
	{"rules.help.governance", "rules.help.town_conversion", "rules.help.extra_conversions", "rules.help.town_gate",
	 "rules.help.mine_control", "rules.help.dimension_doors", "rules.help.ai_cheat", "rules.help.mixed_stacks",
	 "rules.help.neutral_stack_size", "rules.help.bank_difficulty", "rules.help.negative_artifacts", "rules.help.rpg_potions",
	 "rules.help.competitive_towns", "rules.help.mana_observatories", "rules.help.moon_disc", "rules.help.game_mode",
	 "rules.help.toe_bonuses", "rules.help.map_scripts", "rules.help.class_skills", "rules.help.class_chance"},
	{"rules.ai_bonus.0", "rules.ai_bonus.40", "rules.ai_bonus.60", "rules.ai_bonus.80", "rules.ai_bonus.100",
	 "rules.ai_bonus.120", "rules.ai_bonus.140"},
	"rules.on", "rules.off",
	"rules.file_value",
	"rules.own_value",
	"rules.apply", "rules.missing", "rules.differ", "rules.setup", "rules.client_wait", "rules.client_apply", "rules.client_no",
	"rules.client_differ",
	"rules.game_host", "rules.game_file", "rules.no_file", "rules.no_script", "rules.script_added",
	"rules.ok", "rules.file", "rules.cancel", "rules.close",
	"rules.button",
	{"rules.mode.arrows", "rules.mode.default", "rules.mode.savage", "rules.mode.nightmare", "rules.mode.random"},
	"rules.mode_with_toe",
	"rules.start_no",
	"rules.client_start_no",
	{"rules.class.locked", "rules.class.unlocked"},
	"rules.class_note",
	"rules.class_no",
	"rules.client_class_no",
	"rules.class_save",
};

const Words &WordsNow()
{
	return texts::Table(kKeys);
}

// Why a start rule or class_skills cannot be used, in the player's words (the
// technical detail is in the log).
std::string WhyText(const rules::Why &why)
{
	static const char *const kWhy[rules::Why::kCount] = {
		"rules.why.start_not_read", "rules.why.start_missing", "rules.why.start_this_pc", "rules.why.start_host",
		"rules.why.start_differs",  "rules.why.start_waiting", "rules.why.class_this_pc", "rules.why.class_host",
		"rules.why.class_waiting",  "rules.why.class_cannot",  "rules.why.class_differs"};
	if (why.code < 0 || why.code >= rules::Why::kCount)
		return "?";
	return texts::Format(kWhy[why.code], {{"player", why.player}});
}

// "Bob: H55-Settings.lua, Ann: MMH5.5's stats files"
std::string DifferText(const std::vector<rules::Differ> &differ)
{
	static const char *const kFiles[3] = {"rules.differ.settings_rest", "rules.differ.settings", "rules.differ.stats"};
	std::string s;
	for (const rules::Differ &d : differ)
		s += (s.empty() ? "" : ", ") +
		     texts::Format("rules.differ.item", {{"player", d.player}, {"files", texts::Get(kFiles[d.what >= 0 && d.what < 3 ? d.what : 0])}});
	return s;
}

// --- the panel's state ---------------------------------------------------------------------------
struct State {
	View::Role role = View::kNoGame;
	View view;
	Values edit = rules::NoValues();  // the host's values while the panel is open
	float scroll = 0.0f, scroll_max = 0.0f;
	bool set_scroll = false;
	int pending = 0;  // 1 ok, 2 file's values
	bool logged_first = false;
};
State g;
int g_key = VK_F2;
bool g_key_down = false;
bool g_button = true;

// Do these values change a start rule (game mode, ToE bonuses) from MMH5.5's way?
bool StartChanged(const Values &v)
{
	for (int r = 0; r < kRuleCount; ++r)
		if (rules::IsStartRule(r) && !v[r].empty() && v[r] != rules::kStartRuleOff)
			return true;
	return false;
}

// Do these values unlock the class skills?
bool ClassChanged(const Values &v)
{
	return !v[rules::kClassSkills].empty() && v[rules::kClassSkills] != rules::kClassLocked;
}

bool Editable()
{
	return (g.role == View::kHost || g.role == View::kSetup) && g.view.file_known;
}

// The role on this screen: the setup screen = the host; the waiting room = the
// lobby's logic seen last; the map = in game.
View::Role RoleOn(const char *top)
{
	if (strcmp(top, kSetupScreen) == 0)
		return View::kSetup;
	if (strcmp(top, kMapScreen) == 0)
		return View::kInGame;
	if (strcmp(top, kWaitScreen) == 0) {
		const View::Role role = rules::LobbyRole();
		return role == View::kHost || role == View::kClient ? role : View::kNoGame;
	}
	return View::kNoGame;
}

bool PanelScreen(const char *top)
{
	return strcmp(top, kSetupScreen) == 0 || strcmp(top, kMapScreen) == 0 || strcmp(top, kWaitScreen) == 0;
}

void OpenPanel(const char *top, const char *why)
{
	if (overlay::ModalOpen(kName))
		return;
	g.role = RoleOn(top);
	g.view = rules::GetView(g.role);
	g.edit = g.view.shown;
	g.scroll = 0.0f;
	g.set_scroll = true;
	g.pending = 0;
	if (overlay::OpenModal(kName))
		hooklog::Write("rules panel: opened (%s) on %s as %s", why, top,
		               g.role == View::kHost    ? "the host"
		               : g.role == View::kSetup ? "the host (setup screen)"
		               : g.role == View::kClient ? "a client (read only)"
		               : g.role == View::kInGame ? "a player in game (read only)"
		                                         : "nobody (read only)");
}

void Close(const char *why)
{
	if (!overlay::ModalOpen(kName))
		return;
	overlay::CloseModal(kName);
	hooklog::Write("rules panel: closed (%s)", why);
}

void DoOk()
{
	if (Editable())
		rules::SetChoices(g.edit);
	Close("OK");
}

void RobotCommand(const char *text, const char *top)
{
	if (strcmp(text, "open") == 0) {
		if (PanelScreen(top))
			OpenPanel(top, "robot");
		else
			hooklog::Write("rules panel: robot: not on a screen of the panel (%s)", top[0] ? top : "none");
		return;
	}
	if (!overlay::ModalOpen(kName)) {
		hooklog::Write("rules panel: robot command \"%s\" ignored: the panel is not open", text);
		return;
	}
	char key[64] = "", value[32] = "";
	if (sscanf_s(text, "set %63s %31s", key, static_cast<unsigned>(sizeof(key)), value, static_cast<unsigned>(sizeof(value))) == 2) {
		const int r = rules::RuleIndex(key);
		if (!Editable())
			hooklog::Write("rules panel: robot: read only here");
		else if (r < 0 || !rules::ValidValue(kRules[r], value))
			hooklog::Write("rules panel: robot: no rule \"%s\" with value \"%s\"", key, value);
		else {
			g.edit[r] = value;
			hooklog::Write("rules panel: robot: %s = %s", key, value);
		}
	}
	else if (strcmp(text, "file") == 0)
		g.pending = 2;
	else if (strcmp(text, "ok") == 0)
		g.pending = 1;
	else if (strcmp(text, "cancel") == 0 || strcmp(text, "close") == 0)
		Close("robot");
	else if (strncmp(text, "scroll ", 7) == 0) {
		g.scroll = strcmp(text + 7, "end") == 0 ? 100000.0f : static_cast<float>(atof(text + 7));
		g.set_scroll = true;
	}
	else
		hooklog::Write("rules panel: robot command \"%s\" not known", text);
}

// --- the waiting room's versions line -------------------------------------------------------------
rules::VersionsView g_versions;  // this frame's (in the waiting room)
bool g_versions_shown = false;

// "Bob 0.2.7", "Ann 0.3.0 (another build)", "Eve: older (before 0.3.0)", "Max: none"
std::string VersionItems(const rules::VersionsView &v)
{
	std::string s;
	for (const rules::Peer &p : v.differ) {
		std::string item;
		if (p.kind == rules::Peer::kKnown)
			item = texts::Format(p.version == v.mine ? "versions.item_build" : "versions.item", {{"player", p.name}, {"version", p.version}});
		else
			item = texts::Format(p.kind == rules::Peer::kOlder ? "versions.item_older" : "versions.item_none", {{"player", p.name}});
		s += (s.empty() ? "" : ", ") + item;
	}
	return s;
}

// --- the waiting room's button ---------------------------------------------------------------------
bool g_button_shown = false, g_button_down = false, g_button_hovered = false, g_button_warn = false;
float g_back_w = 1024.0f, g_back_h = 768.0f;

ImVec2 ButtonAt(float x, float y, float w, float h)
{
	const float k = h / kRefHeight;
	const float x0 = floorf((w - kScreenW * k) / 2 + 0.5f);
	return ImVec2(x0 + x * k, y * k);
}

void ButtonInput(const char *top)
{
	const ImVec2 p0 = ButtonAt(kLobbyButtonX, kLobbyButtonY, g_back_w, g_back_h);
	const ImVec2 p1 = ButtonAt(kLobbyButtonX + kButtonW, kLobbyButtonY + kButtonH, g_back_w, g_back_h);
	float x, y;
	HWND window = overlay::GameWindow();
	const bool front = window && GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) == GetAncestor(window, GA_ROOTOWNER);
	g_button_hovered = front && overlay::CursorInBackBuffer(g_back_w, g_back_h, &x, &y) && x >= p0.x && x < p1.x && y >= p0.y && y < p1.y;
	const bool down = front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (down && !g_button_down && g_button_hovered)
		g_button_down = true;
	else if (!down && g_button_down) {
		g_button_down = false;
		if (g_button_hovered)
			OpenPanel(top, "its button");
	}
}

// --- the overlay client ------------------------------------------------------------------------------
bool Wanted()
{
	char top[96] = "";
	overlay::TopScreen(top, sizeof(top));
	char command[256];
	if (robot::TakePanelCommand(command, sizeof(command), "rules "))
		RobotCommand(command, top);
	// (the 2008 game without MMH5.5: no MMH5.5 rules to choose, so no key and no button)
	if (overlay::KeyPressed(g_key, &g_key_down) && GameHasMmh55()) {
		if (overlay::ModalOpen(kName))
			Close("its key");
		else if (PanelScreen(top))
			OpenPanel(top, "its key");
	}
	if (overlay::ModalOpen(kName)) {
		// the lobby may change while it is open: the players, what the host sent
		const Values edit = g.edit;
		g.view = rules::GetView(g.role);
		if (Editable())
			g.edit = edit;
		else
			g.edit = g.view.shown;
		{
			const View::Role role = strcmp(top, kWaitScreen) == 0 ? rules::LobbyRole() : View::kNoGame;
			g_versions = rules::GetVersions(role);
			g_versions_shown = (role == View::kHost || role == View::kClient) && g_versions.state != rules::VersionsView::kNothing;
		}
		if (g.pending == 1) {
			g.pending = 0;
			DoOk();
		}
		else if (g.pending == 2) {
			g.pending = 0;
			if (Editable()) {
				g.edit = g.view.file;
				hooklog::Write("rules panel: MMH5.5's file values");
			}
		}
		g_button_shown = false;
		return overlay::ModalOpen(kName);
	}
	const View::Role role = strcmp(top, kWaitScreen) == 0 ? rules::LobbyRole() : View::kNoGame;
	g_versions = rules::GetVersions(role);
	g_versions_shown = (role == View::kHost || role == View::kClient) && g_versions.state != rules::VersionsView::kNothing;
	g_button_shown = g_button && GameHasMmh55() && (role == View::kHost || role == View::kClient);
	if (g_button_shown) {
		ButtonInput(top);
		const View v = rules::GetView(role);
		g_button_warn = (v.map_script == 0 && v.shown[rules::kMapScripts] != "1") || (ClassChanged(v.shown) && !v.class_ok) ||
		                (role == View::kHost ? (!v.missing.empty() || !v.differ.empty()) : (v.this_differs || (v.shown_known && !v.will_apply)));
		static bool told = false;
		if (!told) {
			told = true;
			hooklog::Write("rules panel: our Rules... button is drawn in the waiting room");
		}
	}
	else
		g_button_down = false;
	return g_button_shown || g_versions_shown;
}

void Prepare(const overlay::Frame &frame)
{
	ui::LoadPictures(frame.device);
	const float k = frame.height / kRefHeight;
	for (float size : {kTitleFont, kButtonFont, kBodyFont, kSmallFont, kVersionsFont})
		overlay::WantFont(Px(size, k));
}

void CentredText(ImDrawList *draw, ImFont *font, float px, float x0, float x1, float y, ImU32 colour, const char *text)
{
	const std::string fitted = ui::Fit(font, px, text, x1 - x0);
	const float w = ui::TextWidth(font, px, fitted.c_str());
	ui::ShadowText(draw, font, px, ImVec2(floorf((x0 + x1 - w) / 2), y), colour, fitted.c_str());
}

// Text wrapped into lines of at most `width` pixels, from (x, y); returns the y after it.
float Wrapped(ImDrawList *draw, ImFont *font, float px, float x, float y, float width, ImU32 colour, const char *text)
{
	std::string line, word;
	const std::string all = std::string(text) + " ";
	for (char c : all) {
		if (c != ' ') {
			word += c;
			continue;
		}
		const std::string candidate = line.empty() ? word : line + " " + word;
		if (!line.empty() && ui::TextWidth(font, px, candidate.c_str()) > width) {
			ui::ShadowText(draw, font, px, ImVec2(x, y), colour, line.c_str());
			y += px * 1.25f;
			line = word;
		}
		else
			line = candidate;
		word.clear();
	}
	if (!line.empty()) {
		ui::ShadowText(draw, font, px, ImVec2(x, y), colour, line.c_str());
		y += px * 1.25f;
	}
	return y;
}

// The lobby's map has no map script: will MMH5.5's scripts run on it? With the
// host's rules (`rules_used`), map_scripts as shown; else this PC's MMH5.5 by itself.
float ScriptLine(ImDrawList *draw, ImFont *font, float px, float x, float y, float width, const Words &w, bool rules_used)
{
	const bool on = (rules_used ? g.edit[rules::kMapScripts] : g.view.file[rules::kMapScripts]) == "1";
	return Wrapped(draw, font, px, x, y, width, on ? ui::kDim : ui::kWarn, on ? w.script_added : w.no_script);
}

// A value as words: "On", "Off", "100% (MMH5.5)", "x1.25", "5".
std::string Shown(const rules::RuleDef &rule, const std::string &value, const Words &w)
{
	if (value.empty())
		return "?";
	if (rule.kind == rules::kBool)
		return value == "1" ? w.on : w.off;
	if (rule.kind == rules::kChoice) {
		const int v = atoi(value.c_str());
		if (&rule == &kRules[rules::kGameModeRule])
			return v >= 0 && v < 5 ? w.modes[v] : value;
		if (&rule == &kRules[rules::kClassSkills])
			return v >= 0 && v < 2 ? w.class_modes[v] : value;
		return v >= 0 && v < 7 ? w.ai[v] : value;
	}
	if (rule.kind == rules::kMultiplier)
		return "x" + value;
	return value;
}

// One rule's row: its name (gold when changed), its explanation, and its control
// (or its value, read only). y and sizes in layout units inside the list.
void RuleRow(ImDrawList *draw, ImVec2 origin, float k, float width, float y, int r, const Words &w)
{
	const rules::RuleDef &rule = kRules[r];
	const float body_px = Px(kBodyFont, k), small_px = Px(kSmallFont, k);
	ImFont *body = overlay::Font(body_px), *small = overlay::Font(small_px);
	const auto at = [&](float x, float yy) { return ImVec2(origin.x + x * k, origin.y + yy * k); };
	const bool edit = Editable();
	std::string &value = g.edit[r];
	const std::string &file = g.view.file[r];
	const bool changed = g.view.file_known && !value.empty() && value != file;
	const float control_w = 150.0f;  // a list
	// the texts' room: what the control (or the value, read only) leaves
	const float used = !edit ? 90.0f : rule.kind == rules::kBool ? 26.0f : rule.kind == rules::kInt ? 80.0f : control_w;
	ui::ShadowText(draw, body, body_px, at(4, y), changed ? ui::kGold : ui::kParchment,
	               ui::Fit(body, body_px, w.names[r], (width - used - 12) * k).c_str());
	std::string help = w.help[r];
	if (r == rules::kGameModeRule && (value.empty() || value == rules::kStartRuleOff) && g.edit[rules::kToeBonusRule] == "1")
		help = w.mode_with_toe;
	if (changed && !rules::IsStartRule(r) && !rules::IsClassRule(r)) {  // (not in the file: gold says "not MMH5.5's way")
		help += " (" + texts::Fill(rule.var ? w.file_value : w.own_value, {{"value", Shown(rule, file, w)}}) + ")";
	}
	ui::ShadowText(draw, small, small_px, at(4, y + 16), changed ? ui::kWarn : ui::kDim,
	               ui::Fit(small, small_px, help.c_str(), (width - used - 12) * k).c_str());
	char id[32];
	_snprintf_s(id, sizeof(id), _TRUNCATE, "##rule%d", r);
	if (!edit) {
		const std::string shown = Shown(rule, value, w);
		const float tw = ui::TextWidth(body, body_px, shown.c_str());
		ui::ShadowText(draw, body, body_px, ImVec2(at(width - 4, 0).x - tw, at(0, y + 6).y), changed ? ui::kGold : ui::kParchment,
		               shown.c_str());
		return;
	}
	if (rule.kind == rules::kBool) {
		bool on = value == "1";
		const float h = 17.0f;
		if (ui::CheckBox(id, at(width - 4 - h * 28.0f / 25.0f, y + 6), h * k, "", &on, body, body_px, 40.0f * k, k))
			value = on ? "1" : "0";
	}
	else if (rule.kind == rules::kInt) {
		int blocked = 0;
		const int v = value.empty() ? rule.low : atoi(value.c_str());
		const int d = ui::Stepper(id, at(width - 4, y + 5), 20.0f * k, v, v > rule.low, v < rule.high, &blocked, body, body_px, k);
		if (d)
			value = std::to_string(v + d);
	}
	else {
		std::vector<std::string> keys;
		std::vector<std::string> labels;
		if (rule.kind == rules::kChoice)
			for (int v = rule.low; v <= rule.high; ++v)
				keys.push_back(std::to_string(v));
		else
			for (const char *m : rules::kMultipliers)
				keys.push_back(m);
		int current = -1;
		for (size_t i = 0; i < keys.size(); ++i)
			if (keys[i] == value)
				current = static_cast<int>(i);
		if (current < 0 && !value.empty()) {  // the file's own value, not in the list
			keys.push_back(value);
			current = static_cast<int>(keys.size()) - 1;
		}
		for (const std::string &key : keys)
			labels.push_back(Shown(rule, key, w));
		std::vector<const char *> items;
		for (const std::string &label : labels)
			items.push_back(label.c_str());
		if (ui::Combo(id, at(width - 4 - control_w, y + 2), ImVec2(control_w * k, 24.0f * k), items.data(),
		              static_cast<int>(items.size()), &current, nullptr, body, body_px, k) &&
		    current >= 0)
			value = keys[current];
	}
}

void DrawPanel(const overlay::Frame &frame)
{
	const Layout l = LayoutFor(frame.width, frame.height, kPanelW, kPanelH);
	const Words &w = WordsNow();
	ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(frame.width, frame.height), IM_COL32(0, 0, 0, 115));
	const ImVec2 p0 = l.At(0, 0), p1 = l.At(kPanelW, kPanelH);
	ImGui::SetNextWindowPos(p0, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::Begin("##host_rules", nullptr,
	             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
	                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImDrawList *draw = ImGui::GetWindowDrawList();
	if (ImTextureID bg = ui::Tex(ui::kBackground))
		draw->AddImage(bg, p0, p1, ImVec2(0, 0), ImVec2(kPanelW / kBackgroundTexW, kPanelH / kBackgroundTexH));
	else {
		draw->AddRectFilled(p0, p1, IM_COL32(30, 24, 18, 248), 8.0f * l.k);
		draw->AddRect(p0, p1, IM_COL32(150, 116, 70, 255), 8.0f * l.k, 0, 2.0f * l.k);
	}
	// The builder's background has the random map screen's boxes inside: one plain field over them.
	draw->AddRectFilled(l.At(kContentX0, kContentY0), l.At(kContentX1, kContentY1), IM_COL32(34, 27, 19, 245), 4.0f * l.k);
	draw->AddRect(l.At(kContentX0, kContentY0), l.At(kContentX1, kContentY1), ui::kEdge, 4.0f * l.k, 0, 1.0f * l.k);
	const float title_px = Px(kTitleFont, l.k), small_px = Px(kSmallFont, l.k);
	ImFont *title_font = overlay::Font(title_px), *small = overlay::Font(small_px);
	CentredText(draw, title_font, title_px, l.At(kTitleX, 0).x, l.At(kTitleX + kTitleW, 0).x,
	            l.At(0, kTitleY + kTitleH / 2).y - title_px / 2, ui::kGold, w.title);
	const char *sub = g.role == View::kHost || g.role == View::kSetup ? w.sub_host
	                  : g.role == View::kClient                       ? w.sub_client
	                  : g.role == View::kInGame                       ? w.sub_game
	                                                                  : w.sub_none;
	CentredText(draw, small, small_px, l.At(kListX, 0).x, l.At(kListX + kListW, 0).x, l.At(0, kSubY).y, ui::kDim, sub);

	// the rules, scrolling
	const ImVec2 c0 = l.At(kListX, kListY), c1 = l.At(kListX + kListW, kListY + kListH);
	ImGui::SetCursorScreenPos(c0);
	if (g.set_scroll) {
		ImGui::SetNextWindowScroll(ImVec2(-1.0f, g.scroll));
		g.set_scroll = false;
	}
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, Px(10.0f, l.k));
	ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(24, 20, 14, 255));
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ui::kEdge);
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(190, 146, 84, 255));
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ui::kGold);
	ImGui::BeginChild("##rules", ImVec2(c1.x - c0.x, c1.y - c0.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	ImDrawList *list = ImGui::GetWindowDrawList();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float width = kListW - 16.0f;
	const float body_px = Px(kBodyFont, l.k);
	ImFont *body = overlay::Font(body_px);
	float y = 0.0f;
	if (!g.view.file_known && g.role != View::kClient && !(g.role == View::kInGame && g.view.in_game_rules)) {
		y = (Wrapped(list, body, body_px, origin.x + 4 * l.k, origin.y, (width - 8) * l.k, ui::kWarn, w.no_file) - origin.y) / l.k;
	}
	else {
		for (int group = 0; group < rules::kGroupCount; ++group) {
			y += 2.0f;
			ui::ShadowText(list, body, body_px, ImVec2(origin.x + 2 * l.k, origin.y + y * l.k), ui::kGold, w.groups[group]);
			list->AddLine(ImVec2(origin.x, origin.y + (y + 17) * l.k), ImVec2(origin.x + width * l.k, origin.y + (y + 17) * l.k),
			              IM_COL32(230, 189, 92, 90), 1.0f);
			y += 22.0f;
			// table order, but map_scripts (the table's last) first of its group: it decides
			// whether MMH5.5's scripts, and so the group's other rules, run on a generated map
			for (int i = -1; i < kRuleCount; ++i) {
				const int r = i < 0 ? rules::kMapScripts : i;
				if (kRules[r].group != group || (i >= 0 && r == rules::kMapScripts) || kRules[r].hidden)
					continue;
				RuleRow(list, origin, l.k, width, y, r, w);
				y += 34.0f;
			}
		}
	}
	ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + y * l.k));
	ImGui::Dummy(ImVec2(1, 1));
	g.scroll = ImGui::GetScrollY();
	g.scroll_max = ImGui::GetScrollMaxY();
	ImGui::EndChild();
	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar();

	// what will happen
	float sy = l.At(0, kStatusY).y;
	const float sx = l.At(kListX, 0).x, sw = kListW * l.k;
	std::string line;
	const auto names = [](const std::vector<std::string> &v) {
		std::string s;
		for (const std::string &n : v)
			s += (s.empty() ? "" : ", ") + n;
		return s;
	};
	if (g.role == View::kHost) {
		if (g.view.missing.empty())
			sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kGold, w.apply);
		else {
			line = texts::Fill(w.missing, {{"players", names(g.view.missing)}});
			sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kWarn, line.c_str());
		}
		if (!g.view.differ.empty()) {
			line = texts::Fill(w.differ, {{"players", DifferText(g.view.differ)}});
			sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kError, line.c_str());
		}
		if (g.view.map_script == 0)
			sy = ScriptLine(draw, small, small_px, sx, sy, sw, w, g.view.missing.empty());
		if (StartChanged(g.edit) && !g.view.start_ok && g.view.missing.empty()) {
			line = texts::Fill(w.start_no, {{"why", WhyText(g.view.start_why)}});
			sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kWarn, line.c_str());
		}
		if (ClassChanged(g.edit) && !g.view.class_ok && g.view.missing.empty()) {
			line = texts::Fill(w.class_no, {{"why", WhyText(g.view.class_why)}});
			sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kWarn, line.c_str());
		}
		else if (ClassChanged(g.edit))
			sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kDim, w.class_note);
	}
	else if (g.role == View::kSetup) {
		sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kDim, w.setup);
		if (StartChanged(g.edit) && !g.view.start_ok) {
			line = texts::Fill(w.start_no, {{"why", WhyText(g.view.start_why)}});
			sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kWarn, line.c_str());
		}
		if (ClassChanged(g.edit) && !g.view.class_ok) {
			line = texts::Fill(w.class_no, {{"why", WhyText(g.view.class_why)}});
			sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kWarn, line.c_str());
		}
		else if (ClassChanged(g.edit))
			sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kDim, w.class_note);
	}
	else if (g.role == View::kClient) {
		if (g.view.shown_known && g.view.will_apply && !g.view.start_ok)
			sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kWarn, w.client_start_no);
		if (g.view.shown_known && g.view.will_apply && ClassChanged(g.edit))
			sy = Wrapped(draw, small, small_px, sx, sy, sw, g.view.class_ok ? ui::kDim : ui::kWarn,
			             g.view.class_ok ? w.class_note : w.client_class_no);
		if (!g.view.shown_known)
			sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kWarn, w.client_wait);
		else
			sy = Wrapped(draw, small, small_px, sx, sy, sw, g.view.will_apply ? ui::kGold : ui::kWarn,
			             g.view.will_apply ? w.client_apply : w.client_no);
		if (g.view.this_differs)
			sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kError, w.client_differ);
		if (g.view.map_script == 0)
			sy = ScriptLine(draw, small, small_px, sx, sy, sw, w, g.view.shown_known && g.view.will_apply);
	}
	else if (g.role == View::kInGame) {
		sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kDim, g.view.in_game_rules ? w.game_host : w.game_file);
		if (ClassChanged(g.edit))
			sy = Wrapped(draw, small, small_px, sx, sy, sw, ui::kDim, g.view.class_from_save ? w.class_save : w.class_note);
	}

	// the buttons, on the game's button row
	ImFont *button_font = overlay::Font(Px(kButtonFont, l.k));
	const float button_px = Px(kButtonFont, l.k);
	if (Editable()) {
		if (ui::Button("##ok", l.At(kOkX, kButtonY), l.At(kOkX + kButtonW, kButtonY + kButtonH), w.ok, button_font, button_px, true, l.k))
			g.pending = 1;
		if (ui::Button("##file", l.At(kFileX, kButtonY), l.At(kFileX + kButtonW, kButtonY + kButtonH), w.file, button_font, button_px,
		               true, l.k))
			g.pending = 2;
		if (ui::Button("##cancel", l.At(kCancelX, kButtonY), l.At(kCancelX + kButtonW, kButtonY + kButtonH), w.cancel, button_font,
		               button_px, true, l.k))
			Close("its button");
	}
	else if (ui::Button("##close", l.At(kCancelX, kButtonY), l.At(kCancelX + kButtonW, kButtonY + kButtonH), w.close, button_font,
	                    button_px, true, l.k))
		Close("its button");
	ImGui::End();
	ImGui::PopStyleVar(3);
	if (!g.logged_first) {
		g.logged_first = true;
		hooklog::Write("rules panel: first frame drawn (%.0fx%.0f, %.2f pixels per layout unit)", frame.width, frame.height, l.k);
	}
}

void DrawButton(const overlay::Frame &frame)
{
	const Words &w = WordsNow();
	ImDrawList *draw = ImGui::GetForegroundDrawList();
	const float k = frame.height / kRefHeight;
	const ImVec2 p0 = ButtonAt(kLobbyButtonX, kLobbyButtonY, frame.width, frame.height);
	const ImVec2 p1 = ButtonAt(kLobbyButtonX + kButtonW, kLobbyButtonY + kButtonH, frame.width, frame.height);
	ui::ButtonPicture(draw, p0, p1, w.button, overlay::Font(Px(kButtonFont, k)), Px(kButtonFont, k), g_button_hovered,
	                  g_button_down && g_button_hovered, k);
	if (g_button_warn) {  // something to see in the panel: a small mark on the button
		const ImVec2 c(p1.x - 6.0f * k, p0.y + 6.0f * k);
		draw->AddCircleFilled(c, 6.0f * k, IM_COL32(200, 70, 40, 255));
		draw->AddText(overlay::Font(Px(kSmallFont, k)), Px(kSmallFont, k), ImVec2(c.x - 2.0f * k, c.y - 6.0f * k),
		              IM_COL32(255, 255, 255, 255), "!");
	}
}

// The versions line, at the bottom of the waiting room (on top of the panel too).
void DrawVersions(const overlay::Frame &frame)
{
	using rules::VersionsView;
	const VersionsView &v = g_versions;
	const float k = frame.height / kRefHeight;
	const float x0 = floorf((frame.width - kScreenW * k) / 2 + 0.5f) + kVersionsX * k, w = kVersionsW * k;
	const float px = Px(kVersionsFont, k);
	ImFont *font = overlay::Font(px);
	ImDrawList *draw = ImGui::GetForegroundDrawList();
	std::string first, second;
	ImU32 colour = ui::kWarn;
	switch (v.state) {
	case VersionsView::kSame:
		first = texts::Format("versions.same", {{"ours", v.mine}});
		colour = ui::kDim;
		break;
	case VersionsView::kDiffer:
		first = texts::Format("versions.differ", {{"ours", v.mine}, {"players", VersionItems(v)}});
		second = texts::Get("versions.needed");
		break;
	case VersionsView::kHostOlder:
		first = texts::Format("versions.host_older", {{"ours", v.mine}});
		second = texts::Get("versions.needed");
		break;
	case VersionsView::kHostNone:
		first = texts::Get("versions.host_none");
		break;
	case VersionsView::kHostNewer:
		first = texts::Format("versions.host_newer", {{"ours", v.mine}});
		second = texts::Get("versions.needed");
		break;
	default:
		return;
	}
	const float y0 = kVersionsY * k;
	if (colour != ui::kDim)  // a warning: on a dark field, so it reads over the picture
		draw->AddRectFilled(ImVec2(x0 - 6 * k, y0 - 4 * k), ImVec2(x0 + w + 6 * k, (kRefHeight - 8) * k), IM_COL32(20, 14, 8, 200), 4 * k);
	float y = Wrapped(draw, font, px, x0, y0, w, colour, first.c_str());
	if (!second.empty())
		Wrapped(draw, font, px, x0, y, w, ui::kParchment, second.c_str());
	static std::string told;
	if (first != told) {
		told = first;
		hooklog::Write("rules panel: versions line in the waiting room: \"%s\"%s%s", first.c_str(), second.empty() ? "" : " / ",
		               second.c_str());
	}
}

void Draw(const overlay::Frame &frame)
{
	g_back_w = frame.width;
	g_back_h = frame.height;
	if (overlay::ModalOpen(kName))
		DrawPanel(frame);
	else if (g_button_shown)
		DrawButton(frame);
	if (g_versions_shown)
		DrawVersions(frame);
}

}  // namespace

namespace rules {

void ReloadPanelSettings()
{
	wchar_t path[MAX_PATH], key[16];
	hooklog::PathNextToExe(L"h5-together", L".ini", path, MAX_PATH);
	GetPrivateProfileStringW(L"rules", L"key", L"F2", key, 16, path);
	g_key = overlay::ParseKey(key, VK_F2);
	g_button = GetPrivateProfileIntW(L"rules", L"button", 1, path) != 0;
	hooklog::Write("rules panel: from the settings menu: key %ls, Rules... button %s", key, g_button ? "on" : "off");
}

bool InstallPanel()
{
	g_key = PanelKey();
	g_button = PanelButton();
	const overlay::Client client = {kName, &Wanted, &Prepare, &Draw};
	if (!overlay::AddClient(client))
		return false;
	overlay::EnableModalInput();
	return true;
}

}  // namespace rules
}  // namespace mods
