// The hero looks' panel (plan step 2, docs/plans/hero-skins.md): a modal
// overlay panel in the template builder's style. Its key (F7 by default,
// h5-together.ini [skins] key; for now, until the hero screen gets a button)
// opens it on the adventure map, the hero screen or the town screen: on the hero
// screen for the hero it shows, elsewhere for this PC's player's selected hero
// (skins::PanelHero). It shows the looks of the hero's faction as
// portraits (its own first), with the names of the heroes who wear each one.
// Apply saves the choice to h5-together-skins.ini and makes it count
// (hero_skins.cpp: at once in single player; in multiplayer for every PC at
// the same segment, a few seconds later). A 3D preview is not drawn here: the
// hero screen shows the hero's battle figure in 3D when it opens.
//
// Robot steps (it cannot click): "panel|skins open", "panel|skins pick <n or
// InternalName>", "panel|skins apply", "panel|skins cancel".
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <string>

#include "hero_skins_internal.h"
#include "rmg_builder_widgets.h"

#include "../hook/log.h"
#include "../hook/overlay.h"
#include "../hook/robot.h"
#include "../hook/texts.h"
#include "../hook/third_party/imgui/imgui.h"

namespace mods {
namespace {

namespace ui = builder_ui;

const char kName[] = "hero skins";
int g_key = VK_F7;
bool g_key_down = false;
bool g_early = false;

// --- layout: the builder's panel (1024x768 units, from the panel's top-left corner) ------------
const float kRefHeight = 768.0f;
const float kPanelW = 649.0f, kPanelH = 508.0f;
const float kTitleX = 41.0f, kTitleY = 36.0f, kTitleW = 572.0f, kTitleH = 36.0f;
const float kGridX = 41.0f, kGridY = 84.0f, kGridW = 567.0f, kGridH = 286.0f;
const float kInfoY = 376.0f;
const float kContentX0 = 31.0f, kContentY0 = 76.0f, kContentX1 = 618.0f, kContentY1 = 418.0f;
const float kButtonW = 122.0f, kButtonH = 34.0f, kButtonY = 430.0f, kApplyX = 41.0f, kCancelX = 491.0f;
const float kTitleFont = 20.0f, kButtonFont = 15.0f, kBodyFont = 13.0f, kSmallFont = 11.0f;
const float kBackgroundTexW = 1024.0f, kBackgroundTexH = 512.0f;
const int kColumns = 4;

struct Layout {
	float k, x0, y0;
	ImVec2 At(float x, float y) const { return ImVec2(x0 + x * k, y0 + y * k); }
};

Layout LayoutFor(float w, float h)
{
	Layout l;
	l.k = h / kRefHeight;
	l.x0 = floorf((w - kPanelW * l.k) / 2 + 0.5f);
	l.y0 = floorf((h - kPanelH * l.k) / 2 + 0.5f);
	return l;
}

float Px(float size, float k)
{
	return floorf(size * k + 0.5f);
}

// --- words: the [looks] texts (src/hook/texts.h) ------------------------------------------------
struct Words {
	const char *title;      // {hero} = the hero's name
	const char *own;
	const char *also;       // {heroes} = names
	const char *apply, *cancel;
	const char *single, *multi_soon, *multi_on, *multi_off;
};
const Words kKeys = {"looks.title", "looks.own", "looks.also", "looks.apply", "looks.cancel",
                     "looks.single", "looks.multi_soon", "looks.multi_on", "looks.multi_off"};

const Words &WordsNow()
{
	return texts::Table(kKeys);
}

// --- names: the game's text files (UTF-16), cached ------------------------------------------------
std::map<std::string, std::string> g_names;

std::string NameOf(const std::string &name_file, const std::string &internal)
{
	if (name_file.empty())
		return internal;
	auto it = g_names.find(name_file);
	if (it != g_names.end())
		return it->second.empty() ? internal : it->second;
	std::string raw, text;
	if (overlay::ReadGameFile(name_file.c_str(), &raw) && raw.size() >= 2) {
		size_t at = raw[0] == '\xff' && raw[1] == '\xfe' ? 2 : 0;
		std::wstring wide;
		for (; at + 1 < raw.size(); at += 2) {
			const wchar_t c = static_cast<wchar_t>(static_cast<unsigned char>(raw[at]) | static_cast<unsigned char>(raw[at + 1]) << 8);
			if (c == L'\r' || c == L'\n')
				break;
			wide += c;
		}
		const int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
		text.assign(n > 0 ? n : 0, '\0');
		if (n > 0)
			WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), &text[0], n, nullptr, nullptr);
	}
	g_names[name_file] = text;
	return text.empty() ? internal : text;
}

// --- the panel's state ------------------------------------------------------------------------------
struct State {
	skins::PanelData data;
	std::string hero_name;
	std::vector<std::string> look_names;  // per look: the name of the hero it is named after
	std::vector<std::string> also;        // per look: the other heroes who wear it
	int selected = 0;                     // a position in data.looks
	int pending = 0;                      // an action for the next frame: 1 apply
	bool logged_first = false;
};
State g;

int LookIndexOfChosen(const skins::PanelData &d)
{
	for (size_t i = 0; i < d.looks.size(); ++i) {
		if (d.chosen < 0 && d.looks[i].own)
			return static_cast<int>(i);
		for (int m : d.looks[i].members)
			if (d.chosen >= 0 && m == d.chosen && !d.looks[i].own)
				return static_cast<int>(i);
	}
	return 0;
}

bool OnScreenForPanel(char *top, int size)
{
	return overlay::TopScreen(top, size) &&
	       (strstr(top, "AdventureScreen") || strstr(top, "HeroScreen") || strstr(top, "TownScreen"));
}

void OpenPanel(const char *why)
{
	if (overlay::ModalOpen(kName))
		return;
	char top[96];
	if (!OnScreenForPanel(top, sizeof(top))) {
		hooklog::Write("hero skins: panel not opened (%s): not on the map, the hero screen or the town screen (%s)", why,
		               top[0] ? top : "no screen");
		return;
	}
	State fresh;
	const char *no = "";
	if (!skins::PanelHero(&fresh.data, &no) || fresh.data.looks.empty()) {
		hooklog::Write("hero skins: panel not opened (%s): %s", why, fresh.data.hero >= 0 ? "no look for its faction" : no);
		return;
	}
	g = fresh;
	g.hero_name = NameOf(g.data.name_file, g.data.internal);
	for (const skins::Look &look : g.data.looks) {
		g.look_names.push_back(look.own ? g.hero_name : NameOf(look.name_file, look.internal));
		std::string others;
		for (int m : look.members) {
			if (m == look.hero || (look.own && m == g.data.hero))
				continue;
			others += (others.empty() ? "" : ", ") + NameOf(skins::NameFile(m), skins::InternalName(m));
		}
		g.also.push_back(others);
	}
	g.selected = LookIndexOfChosen(g.data);
	if (overlay::OpenModal(kName))
		hooklog::Write("hero skins: panel opened (%s) for %s (%s): %d look(s) of its faction, now %s", why,
		               g.data.internal.c_str(), g.data.from_hero_screen ? "the hero the hero screen shows" : "the selected hero",
		               static_cast<int>(g.data.looks.size()),
		               g.data.chosen >= 0 ? skins::InternalName(g.data.chosen).c_str() : "its own");
}

void Close(const char *why)
{
	if (overlay::ModalOpen(kName))
		hooklog::Write("hero skins: panel closed (%s)", why);
	overlay::CloseModal(kName);
}

void DoApply()
{
	if (g.selected < 0 || g.selected >= static_cast<int>(g.data.looks.size()))
		return;
	const skins::Look &look = g.data.looks[g.selected];
	std::string note;
	const skins::Applied result = skins::ApplyLook(g.data.hero, look.own ? -1 : look.hero, &note);
	static const char *const kResults[] = {"counts now", "sent: counts for every PC in a few seconds",
	                                       "saved for the next games (this game's looks are off)", "not applied"};
	hooklog::Write("hero skins: Apply: %s: %s", note.c_str(), kResults[result]);
	Close("Apply");
	if (result == skins::kAppliedNow)  // multiplayer: when the look counts, at its segment (hero_skins.cpp)
		skins::RefreshHeroScreen(std::vector<int>(1, g.data.hero), "Apply");
}

// The robot's "panel|skins ..." (it cannot click).
void RobotCommand(const char *text)
{
	if (strcmp(text, "open") == 0) {
		OpenPanel("robot");
		return;
	}
	// Robot helpers: the game's own message handlers, at the next adventure frame (hero_skins.cpp, 8.)
	if (strncmp(text, "hero_screen", 11) == 0 || strncmp(text, "town", 4) == 0 || strncmp(text, "lua ", 4) == 0 ||
	    strcmp(text, "figures") == 0) {
		skins::RobotScreenCommand(text);
		return;
	}
	if (!overlay::ModalOpen(kName)) {
		hooklog::Write("hero skins: robot command \"%s\" ignored: the panel is not open", text);
		return;
	}
	if (strncmp(text, "pick ", 5) == 0) {
		const char *arg = text + 5;
		int found = -1;
		if (strspn(arg, "0123456789") == strlen(arg) && *arg)
			found = atoi(arg);
		for (size_t i = 0; found < 0 && i < g.data.looks.size(); ++i)
			if (_stricmp(g.data.looks[i].internal.c_str(), arg) == 0)
				found = static_cast<int>(i);
		if (found >= 0 && found < static_cast<int>(g.data.looks.size())) {
			g.selected = found;
			hooklog::Write("hero skins: robot: picked look %d (%s)", found, g.data.looks[found].internal.c_str());
		}
		else
			hooklog::Write("hero skins: robot: no look \"%s\"", arg);
	}
	else if (strcmp(text, "apply") == 0)
		g.pending = 1;
	else if (strcmp(text, "cancel") == 0)
		Close("robot");
	else
		hooklog::Write("hero skins: robot command \"%s\" not known", text);
}

// --- the hero screen's button ----------------------------------------------------------------------
// MMH5.5's hero screen (UI/HeroScreen3): the panel SingleHero (512x724) sits at
// x 60 from the left edge and is centred vertically 23 lower, so at the
// reference height its top-left corner is (60, 45), and everything scales with
// the screen height. In its "Armée & Stats" header the skill wheel's round
// button (MMH55-Skillwheel.pak, Single/Army: Button_HS at 372,48, 32x32) is at
// (432, 93). Measured in the owner's screenshots (units of the 768 reference):
//   - 1348x750 (k 0.977): MMH5.5's square button 395..417 px = 404..427, round
//     one 427..449 px = 437..460, both centred on y 102.5 px = 105;
//   - 977x725 (k 0.944): no square button; the round one 408..432 px = 432..458,
//     y 89..112 px, centred on y 100.5 px = 106.5.
// So the header's buttons are centred on y ~106 (the round button's picture
// sits a little above the middle of its 32x32 window), and the square button,
// when MMH5.5 shows it, takes 404..427, about 9 units left of the round one.
// It is not in the hero screen's UI files (MMH5.5 must add it in code), so its
// presence cannot be read from the data: ours takes its slot, 24x24 at
// (403, 94), centred on y 106. [skins] hero_screen_button_x / _y move it.
// Drawn with ImGui (the screen's own command handler is not researched); it
// reads the mouse itself, like the builder's fallback button.
float kHsButtonX = 403.0f, kHsButtonY = 94.0f;
const float kHsButtonSize = 24.0f;
bool g_button = true;            // [skins] hero_screen_button
bool g_test_button = false;      // testing ([skins] test_button_on_map=1): also on the adventure map (the robot cannot open the hero screen)
bool g_on_hero_screen = false;
bool g_button_hovered = false, g_button_down = false;
std::string g_button_face;       // the portrait shown on the button
float g_back_w = 1024.0f, g_back_h = 768.0f;

bool HeroScreenOnTop()
{
	char top[96];
	const bool on = overlay::TopScreen(top, sizeof(top)) && strstr(top, "HeroScreen") != nullptr;
	static char logged[96] = "";
	if (on && strcmp(top, logged) != 0) {
		lstrcpynA(logged, top, sizeof(logged));
		hooklog::Write("hero skins: the hero screen (%s): the Look button is drawn in its header", top);
	}
	return on;
}

void ButtonRect(float w, float h, ImVec2 *p0, ImVec2 *p1)
{
	(void)w;
	const float k = h / kRefHeight;
	*p0 = ImVec2(floorf(kHsButtonX * k + 0.5f), floorf(kHsButtonY * k + 0.5f));
	*p1 = ImVec2(floorf((kHsButtonX + kHsButtonSize) * k + 0.5f), floorf((kHsButtonY + kHsButtonSize) * k + 0.5f));
}

void ButtonInput(float w, float h)
{
	ImVec2 p0, p1;
	ButtonRect(w, h, &p0, &p1);
	float x, y;
	HWND window = overlay::GameWindow();
	const bool front = window && GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) == GetAncestor(window, GA_ROOTOWNER);
	g_button_hovered = front && overlay::CursorInBackBuffer(w, h, &x, &y) && x >= p0.x && x < p1.x && y >= p0.y && y < p1.y;
	const bool down = front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (down && !g_button_down && g_button_hovered)
		g_button_down = true;
	else if (!down && g_button_down) {
		g_button_down = false;
		if (g_button_hovered)
			OpenPanel("the hero screen's Look button");
	}
}

void DrawHeroScreenButton(const overlay::Frame &frame)
{
	ImVec2 p0, p1;
	ButtonRect(frame.width, frame.height, &p0, &p1);
	const float k = frame.height / kRefHeight;
	ImDrawList *draw = ImGui::GetForegroundDrawList();
	const bool pressed = g_button_down && g_button_hovered;
	draw->AddRectFilled(p0, p1, pressed ? IM_COL32(20, 14, 8, 255) : IM_COL32(52, 40, 26, 255), 3.0f * k);
	int no_loads = 0;
	void *tex = g_button_face.empty() ? nullptr : overlay::GameTexture(frame.device, g_button_face.c_str(), &no_loads);
	const float inset = 2.0f * k;
	if (tex)
		draw->AddImage(reinterpret_cast<ImTextureID>(tex), ImVec2(p0.x + inset, p0.y + inset), ImVec2(p1.x - inset, p1.y - inset),
		               ImVec2(0, 0), ImVec2(1, 1), pressed ? IM_COL32(200, 200, 200, 255) : IM_COL32_WHITE);
	draw->AddRect(p0, p1, g_button_hovered ? ui::kGold : ui::kEdge, 3.0f * k, 0, (g_button_hovered ? 2.0f : 1.5f) * k);
	if (g_button_hovered) {  // the tooltip
		const char *tip = texts::Get("looks.button_tip");
		const float px = Px(kBodyFont, k);
		ImFont *font = overlay::Font(px);
		const float tw = ui::TextWidth(font, px, tip);
		const ImVec2 t0(p0.x, p1.y + 4.0f * k), t1(p0.x + tw + 12.0f * k, p1.y + 4.0f * k + px + 8.0f * k);
		draw->AddRectFilled(t0, t1, IM_COL32(24, 18, 12, 235), 3.0f * k);
		draw->AddRect(t0, t1, ui::kEdge, 3.0f * k, 0, 1.0f * k);
		ui::ShadowText(draw, font, px, ImVec2(t0.x + 6.0f * k, t0.y + 4.0f * k), ui::kParchment, tip);
	}
}

// --- the overlay client --------------------------------------------------------------------------
bool Wanted()
{
	char command[256];
	if (robot::TakePanelCommand(command, sizeof(command), "skins "))
		RobotCommand(command);
	char top[96];
	g_on_hero_screen = g_button && !overlay::AnyModalOpen() &&
	                   (HeroScreenOnTop() || (g_test_button && overlay::TopScreen(top, sizeof(top)) && strstr(top, "AdventureScreen")));
	if (g_on_hero_screen)
		ButtonInput(g_back_w, g_back_h);
	else
		g_button_down = g_button_hovered = false;
	if (overlay::KeyPressed(g_key, &g_key_down)) {
		if (overlay::ModalOpen(kName))
			Close("its key");
		else
			OpenPanel("its key");
	}
	if (g.pending && overlay::ModalOpen(kName)) {
		g.pending = 0;
		DoApply();
	}
	return overlay::ModalOpen(kName) || g_on_hero_screen;
}

void Prepare(const overlay::Frame &frame)
{
	ui::LoadPictures(frame.device);
	const float k = frame.height / kRefHeight;
	for (float size : {kTitleFont, kButtonFont, kBodyFont, kSmallFont})
		overlay::WantFont(Px(size, k));
	int loads = 3;
	for (const skins::Look &look : g.data.looks)
		if (!look.portrait.empty())
			overlay::GameTexture(frame.device, look.portrait.c_str(), &loads);
	if (g_on_hero_screen) {
		skins::PanelLookPortrait(&g_button_face);
		if (!g_button_face.empty())
			overlay::GameTexture(frame.device, g_button_face.c_str(), &loads);
	}
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

void DrawPanel(const overlay::Frame &frame)
{
	const Layout l = LayoutFor(frame.width, frame.height);
	const Words &w = WordsNow();
	ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(frame.width, frame.height), IM_COL32(0, 0, 0, 115));
	const ImVec2 p0 = l.At(0, 0), p1 = l.At(kPanelW, kPanelH);
	ImGui::SetNextWindowPos(p0, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::Begin("##hero_skins", nullptr,
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
	const float title_px = Px(kTitleFont, l.k), body_px = Px(kBodyFont, l.k), small_px = Px(kSmallFont, l.k);
	ImFont *title_font = overlay::Font(title_px), *body = overlay::Font(body_px), *small = overlay::Font(small_px);
	char title[200];
	_snprintf_s(title, sizeof(title), _TRUNCATE, "%s", texts::Fill(w.title, {{"hero", g.hero_name}}).c_str());
	CentredText(draw, title_font, title_px, l.At(kTitleX, 0).x, l.At(kTitleX + kTitleW, 0).x,
	            l.At(0, kTitleY + kTitleH / 2).y - title_px / 2, ui::kGold, title);

	// the looks: a grid of portraits
	const int count = static_cast<int>(g.data.looks.size());
	const int rows = (count + kColumns - 1) / kColumns;
	const float gap = 8.0f;
	const float tile_w = (kGridW - gap * (kColumns - 1)) / kColumns;
	const float tile_h = fminf(140.0f, (kGridH - gap * (rows - 1)) / (rows > 0 ? rows : 1));
	const float face = fminf(tile_w - 16.0f, tile_h - 2.6f * kBodyFont - 10.0f);
	for (int i = 0; i < count; ++i) {
		const skins::Look &look = g.data.looks[i];
		const float x = kGridX + (i % kColumns) * (tile_w + gap), y = kGridY + (i / kColumns) * (tile_h + gap);
		const ImVec2 t0 = l.At(x, y), t1 = l.At(x + tile_w, y + tile_h);
		ImGui::SetCursorScreenPos(t0);
		char id[32];
		_snprintf_s(id, sizeof(id), _TRUNCATE, "##look%d", i);
		if (ImGui::InvisibleButton(id, ImVec2(t1.x - t0.x, t1.y - t0.y)))
			g.selected = i;
		const bool hovered = ImGui::IsItemHovered(), chosen = i == g.selected;
		draw->AddRectFilled(t0, t1, chosen ? IM_COL32(70, 52, 24, 220) : IM_COL32(20, 16, 12, hovered ? 200 : 160), 4.0f * l.k);
		draw->AddRect(t0, t1, chosen ? ui::kGold : hovered ? ui::kParchment : ui::kEdge, 4.0f * l.k, 0,
		              (chosen ? 2.5f : 1.0f) * l.k);
		const ImVec2 f0 = l.At(x + (tile_w - face) / 2, y + 6.0f), f1 = l.At(x + (tile_w + face) / 2, y + 6.0f + face);
		int no_loads = 0;
		void *tex = look.portrait.empty() ? nullptr : overlay::GameTexture(frame.device, look.portrait.c_str(), &no_loads);
		if (tex)
			draw->AddImage(reinterpret_cast<ImTextureID>(tex), f0, f1);
		else
			draw->AddRectFilled(f0, f1, IM_COL32(60, 48, 34, 255));
		draw->AddRect(f0, f1, ui::kEdge, 0, 0, 1.0f * l.k);
		const float text_y = l.At(0, y + 10.0f + face).y;
		CentredText(draw, body, body_px, t0.x + 3 * l.k, t1.x - 3 * l.k, text_y, chosen ? ui::kGold : ui::kParchment,
		            g.look_names[i].c_str());
		char sub[64];
		if (look.own)
			_snprintf_s(sub, sizeof(sub), _TRUNCATE, "(%s)", w.own);
		else if (look.members.size() > 1)
			_snprintf_s(sub, sizeof(sub), _TRUNCATE, "+%d", static_cast<int>(look.members.size()) - 1);
		else
			sub[0] = 0;
		if (sub[0])
			CentredText(draw, small, small_px, t0.x + 3 * l.k, t1.x - 3 * l.k, text_y + body_px * 1.2f, ui::kDim, sub);
	}

	// the selected look's other wearers, and what Apply does here
	float y = l.At(0, kInfoY).y;
	const float x = l.At(kGridX, 0).x, width = kGridW * l.k;
	if (g.selected >= 0 && g.selected < count && !g.also[g.selected].empty()) {
		char also[400];
		_snprintf_s(also, sizeof(also), _TRUNCATE, "%s", texts::Fill(w.also, {{"heroes", g.also[g.selected]}}).c_str());
		const std::string fitted = ui::Fit(small, small_px, also, width);
		ui::ShadowText(draw, small, small_px, ImVec2(x, y), ui::kDim, fitted.c_str());
		y += small_px * 1.35f;
	}
	const char *note = !g.data.multiplayer ? w.single : !g.data.decided ? w.multi_soon : g.data.agreed ? w.multi_on : w.multi_off;
	Wrapped(draw, small, small_px, x, y, width, g.data.multiplayer && g.data.decided && !g.data.agreed ? ui::kWarn : ui::kParchment,
	        note);

	ImFont *button_font = overlay::Font(Px(kButtonFont, l.k));
	const float button_px = Px(kButtonFont, l.k);
	if (ui::Button("##apply", l.At(kApplyX, kButtonY), l.At(kApplyX + kButtonW, kButtonY + kButtonH), w.apply, button_font,
	               button_px, g.selected >= 0 && g.selected < count, l.k))
		g.pending = 1;
	if (ui::Button("##cancel", l.At(kCancelX, kButtonY), l.At(kCancelX + kButtonW, kButtonY + kButtonH), w.cancel, button_font,
	               button_px, true, l.k))
		Close("its button");
	ImGui::End();
	ImGui::PopStyleVar(3);
	if (!g.logged_first) {
		g.logged_first = true;
		hooklog::Write("hero skins: panel drawn (%.0fx%.0f, %d look(s), %.2f pixels per layout unit)", frame.width, frame.height,
		               count, l.k);
	}
}

void Draw(const overlay::Frame &frame)
{
	g_back_w = frame.width;
	g_back_h = frame.height;
	if (overlay::ModalOpen(kName))
		DrawPanel(frame);
	else if (g_on_hero_screen)
		DrawHeroScreenButton(frame);
}

void ReadSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together", L".ini", path, MAX_PATH);
	wchar_t key[16];
	GetPrivateProfileStringW(L"skins", L"key", L"F7", key, 16, path);
	g_key = overlay::ParseKey(key, VK_F7);
	g_early = GetPrivateProfileIntW(L"skins", L"skins", 1, path) != 0;
	g_button = GetPrivateProfileIntW(L"skins", L"hero_screen_button", 1, path) != 0;
	g_test_button = GetPrivateProfileIntW(L"skins", L"test_button_on_map", 0, path) != 0;
	kHsButtonX = static_cast<float>(GetPrivateProfileIntW(L"skins", L"hero_screen_button_x", 403, path));
	kHsButtonY = static_cast<float>(GetPrivateProfileIntW(L"skins", L"hero_screen_button_y", 94, path));
}

}  // namespace

void ReloadHeroSkinsPanelSettings()
{
	ReadSettings();
	hooklog::Write("hero skins panel: from the settings menu: key %s, Look button %s",
	               g_key ? ("F" + std::to_string(g_key - VK_F1 + 1)).c_str() : "none", g_button ? "on" : "off");
}

void InstallHeroSkinsPanelEarly()
{
	ReadSettings();
	if (g_early)
		overlay::InstallEarly();
}

void InstallHeroSkinsPanel()
{
	ReadSettings();
	const overlay::Client client = {kName, &Wanted, &Prepare, &Draw};
	if (!overlay::AddClient(client)) {
		hooklog::Write("hero skins: no panel (the overlay cannot draw in this game)");
		return;
	}
	overlay::EnableModalInput();
	hooklog::Write("hero skins: panel on, key %s", g_key ? "F7 (or [skins] key)" : "none");
}

}  // namespace mods
