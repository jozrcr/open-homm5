// Mod: the template builder, a modal panel over the game's random map screen
// (NUI::CRMGScreen, the same screen in single player and multiplayer). The
// plan: docs/plans/in-game-generator.md (build steps 5 to 7). It holds every
// choice of the web template maker (tools/rmg/template-maker.html), builds the
// template with the C++ maker (rmg_maker.cpp) at each change, and "Use" fills
// in the game's own screen with the template's recipe name (rmg_recipe.h).
//
// The panel covers the screen's main panel exactly: 649x508 in the game's
// 1024x768 layout, centred, scaled with the screen height like the rest of the
// game's interface (the screen's window is centred: ALIGN_CENTER). Its parts
// sit where the game's own parts are, drawn with the game's pictures
// (rmg_builder_widgets.cpp):
//   - the title, where the screen's title is;
//   - the zone map in the minimap's box (44,76, 263x263): the maker's zones
//     and paths, start zones in their team's colour, treasure zones paler,
//     neutral zones grey, the centre gold; thicker paths for stronger guards,
//     dashed when open; a small gold mark for a town;
//   - under it, in the map name's box: the players and zones, the template's
//     name as the game's list shows it (with its "*"), the map size;
//   - the choices in the options list's box (319,78, 264x331), scrolling with
//     the wheel or the game's scroll bar track beside it (590,76): presets (the
//     ready templates), teams, neutral zones, loot and guards (also by distance
//     from the bases), map size and underground, and the maker's warnings;
//   - Use, Save as file and Cancel on the game's button row (41, 188 and 491).
// Choices the maker refuses are not offered: an arrow that would make more than
// 8 players is greyed, and clicking it shows the maker's own message.
//
// It opens with the screen's "Template..." button (rmg_screen.cpp serves it;
// when the game's files are not the ones we know, the panel draws that button
// itself at the same place), or with F11 ([rmg] builder_key). It is a modal
// panel of the overlay (src/hook/overlay.h): while it is open the game gets no
// mouse or keyboard input; Esc closes it.
//
// Settings (open-homm5.ini):
//   [rmg]
//   builder=1           0 = no panel and no button (nothing is installed)
//   builder_key=F11     the panel's key on the random map screen (none = no key)
//   builder_last=       the last choices used or saved, as a recipe name (written by the panel)
#include "rmg_builder.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <initializer_list>
#include <string>
#include <vector>

#include "rmg_builder_data.h"
#include "rmg_builder_widgets.h"
#include "rmg_maker.h"
#include "rmg_recipe.h"
#include "rmg_screen.h"
#include "rmg_templates.h"

#include "../hook/log.h"
#include "../hook/overlay.h"
#include "../hook/robot.h"
#include "../hook/third_party/imgui/imgui.h"

namespace mods {
namespace {

namespace ui = builder_ui;
namespace data = rmg::builder;

const char kName[] = "rmg builder";
const char kScreenClass[] = ".?AVCRMGScreen@NUI@@";  // NUI::CRMGScreen (vtable 0x00e41114)

bool g_enabled = false;
bool g_early = false;
bool g_ready = false;  // the recipes are served: "Use" can work
int g_key = VK_F11;
bool g_key_down = false;

// --- the game's layout (1024x768 units, from the panel's top-left corner) ----------------
const float kRefHeight = 768.0f;
const float kPanelW = 649.0f, kPanelH = 508.0f;  // MainPanel
const float kTitleX = 41.0f, kTitleY = 36.0f, kTitleW = 572.0f, kTitleH = 36.0f;
const float kMapX = 44.0f, kMapY = 76.0f, kMapW = 263.0f, kMapH = 263.0f;  // MinimapPanel
const float kStatsY = 350.0f;                                             // the map name's box: 348..410
const float kNameX = 58.0f, kNameY = 372.0f, kNameW = 234.0f, kNameH = 16.0f;  // MapNameEdit
const float kSizeY = 394.0f;
const float kListX = 319.0f, kListY = 78.0f, kListW = 264.0f, kListH = 331.0f;  // OptionsContainer's inside
const float kTrackX = 590.0f, kTrackY = 76.0f, kTrackW = 19.0f, kTrackH = 335.0f;  // its scroll bar's track
const float kButtonW = 122.0f, kButtonH = 34.0f, kButtonY = 430.0f;
const float kUseX = 41.0f, kSaveX = 188.0f, kCancelX = 491.0f;  // Create, Save, Back
const float kTitleFont = 20.0f, kButtonFont = 15.0f, kBodyFont = 13.0f, kSmallFont = 11.0f;
const float kBackgroundTexW = 1024.0f, kBackgroundTexH = 512.0f;  // MainBg: drawn 1:1 from its top-left

struct Layout {
	float k;       // pixels per layout unit
	float x0, y0;  // the panel's top-left corner, in pixels
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

// --- the panel's state -----------------------------------------------------------------------
struct State {
	rmg::Choices choices;
	bool by_distance = false;  // the per-distance table is shown
	bool valid = false;
	rmg::Template t;
	std::string error;   // the maker's refusal (then nothing can be used)
	std::string recipe;  // OH-X1-... of the choices
	std::vector<data::Point> pos;
	int preset = -1;
	float scroll = 0.0f, scroll_max = 0.0f, scroll_view = 1.0f;
	bool set_scroll = false;
	std::string status;
	bool status_bad = false;
	DWORD status_until = 0;
	std::string hint;  // why an arrow is greyed (the maker's words)
	DWORD hint_until = 0;
	int pending = 0;  // an action for the next frame: 1 use, 2 save
};
State g;

std::wstring IniPath()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	return path;
}

void Rebuild()
{
	g.valid = rmg::MakeTemplate(g.choices, &g.t, &g.error);
	g.recipe.clear();
	g.pos.clear();
	if (g.valid) {
		g.error.clear();
		if (!rmg::EncodeRecipe(g.choices, &g.recipe))
			g.recipe.clear();
		g.pos = data::ZonePositions(g.t, g.choices);
	}
	g.preset = data::PresetOf(g.choices);
}

// The choices of a recipe name (the last ones used, a preset...).
bool LoadRecipe(const std::string &recipe)
{
	rmg::Choices c;
	if (!rmg::DecodeRecipe(recipe, &c))
		return false;
	g.choices = c;
	g.by_distance = false;
	for (int i = 0; i < 4; ++i)
		g.by_distance = g.by_distance || !c.loot_by_distance[i].empty() || !c.guards_by_distance[i].empty();
	Rebuild();
	return true;
}

void LoadLast()
{
	wchar_t text[64] = L"";
	GetPrivateProfileStringW(L"rmg", L"builder_last", L"", text, 64, IniPath().c_str());
	char narrow[64];
	WideCharToMultiByte(CP_ACP, 0, text, -1, narrow, sizeof(narrow), nullptr, nullptr);
	if (!narrow[0] || !LoadRecipe(narrow)) {
		if (narrow[0])
			hooklog::Write("rmg builder: builder_last=%s is not a recipe name: the first preset instead", narrow);
		g.choices = rmg::Choices();
		g.by_distance = false;
		Rebuild();
	}
}

void SaveLast()
{
	if (g.recipe.empty())
		return;
	wchar_t wide[64];
	MultiByteToWideChar(CP_ACP, 0, g.recipe.c_str(), -1, wide, 64);
	WritePrivateProfileStringW(L"rmg", L"builder_last", wide, IniPath().c_str());
}

void Status(const std::string &text, bool bad)
{
	g.status = text;
	g.status_bad = bad;
	g.status_until = GetTickCount() + 6000;
}

std::string Format(const char *format, const char *text)
{
	char line[512];
	_snprintf_s(line, sizeof(line), _TRUNCATE, format, text);
	return line;
}

void OpenPanel(const char *why)
{
	if (overlay::ModalOpen(kName))
		return;
	LoadLast();
	g.scroll = 0.0f;
	g.set_scroll = true;
	g.status.clear();
	g.hint.clear();
	if (overlay::OpenModal(kName))
		hooklog::Write("rmg builder: panel opened (%s): %s, %s", why, g.recipe.empty() ? "?" : g.recipe.c_str(),
		               g.valid ? g.t.name.c_str() : g.error.c_str());
}

void OpenFromScreen()
{
	OpenPanel("the Template... button");
}

int SizeIndex(const rmg::Template &t)
{
	for (int i = 0; i < 7; ++i)
		if (strcmp(rmg::kSizes[i].key, t.size.key) == 0)
			return i;
	return -1;
}

// "Use": the game's own screen gets our template, then the panel closes.
void DoUse()
{
	const data::Words &w = data::WordsFor(overlay::PanelLanguage());
	if (!g.valid || g.recipe.empty())
		return;
	char top[96];
	void *screen = overlay::TopScreenObject();
	if (!overlay::TopScreen(top, sizeof(top)) || strcmp(top, kScreenClass) != 0 || !screen) {
		Status(w.use_failed, true);
		hooklog::Write("rmg builder: Use: the random map screen is not on top");
		return;
	}
	RegisterRecipe(g.recipe);  // in the game's template list from now on
	ScreenChoice c;
	c.size_index = SizeIndex(g.t);
	c.underground = g.choices.underground;
	c.players = g.t.stats.players;
	c.recipe = g.recipe;
	std::string report;
	const bool ok = c.size_index >= 0 && ApplyToRmgScreen(screen, c, &report);
	hooklog::Write("rmg builder: Use %s (\"%s*\"): %s%s", g.recipe.c_str(), g.t.name.c_str(), ok ? "" : "FAILED: ",
	               report.c_str());
	if (!ok) {
		Status(w.use_failed, true);
		return;
	}
	SaveLast();
	overlay::CloseModal(kName);
}

// "Save as file": the maker's template, as the web page gives it, in open-homm5\templates.
void DoSave()
{
	const data::Words &w = data::WordsFor(overlay::PanelLanguage());
	if (!g.valid)
		return;
	wchar_t dir[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
	while (n > 0 && dir[n - 1] != L'\\')
		--n;
	lstrcpynW(dir + n, L"open-homm5", MAX_PATH - n);
	CreateDirectoryW(dir, nullptr);
	std::wstring folder = std::wstring(dir) + L"\\templates";
	CreateDirectoryW(folder.c_str(), nullptr);
	// The maker's own name; if a different file has it already, "-2", "-3"...
	rmg::Choices c = g.choices;
	rmg::Template t = g.t;
	std::string error;
	for (int i = 2; i < 100; ++i) {
		const std::wstring path = folder + L"\\" + std::wstring(t.file_name.begin(), t.file_name.end());
		HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		if (f == INVALID_HANDLE_VALUE)
			break;
		std::string old;
		const DWORD size = GetFileSize(f, nullptr);
		DWORD got = 0;
		if (size != INVALID_FILE_SIZE && size < (1u << 20)) {
			old.resize(size);
			if (!size || !ReadFile(f, &old[0], size, &got, nullptr) || got != size)
				old.clear();
		}
		CloseHandle(f);
		if (old == t.xml)
			break;  // the same file is there already
		char name[160];
		_snprintf_s(name, sizeof(name), _TRUNCATE, "%s-%d", g.t.name.c_str(), i);
		c.name = name;
		if (!rmg::MakeTemplate(c, &t, &error))
			return;
	}
	const std::wstring path = folder + L"\\" + std::wstring(t.file_name.begin(), t.file_name.end());
	HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	DWORD written = 0;
	const bool ok = f != INVALID_HANDLE_VALUE &&
	                WriteFile(f, t.xml.data(), static_cast<DWORD>(t.xml.size()), &written, nullptr) &&
	                written == t.xml.size();
	const DWORD error_code = GetLastError();
	if (f != INVALID_HANDLE_VALUE)
		CloseHandle(f);
	const std::string shown = "open-homm5\\templates\\" + t.file_name;
	if (ok) {
		hooklog::Write("rmg builder: saved %s (%u bytes; %s): listed by the game from its next start", shown.c_str(),
		               static_cast<unsigned>(t.xml.size()), g.recipe.c_str());
		Status(Format(w.saved, t.file_name.c_str()), false);
		SaveLast();
	}
	else {
		char why[64];
		_snprintf_s(why, sizeof(why), _TRUNCATE, "error %lu", error_code);
		hooklog::Write("rmg builder: %s NOT saved (%s)", shown.c_str(), why);
		Status(Format(w.not_saved, why), true);
	}
}

void Cancel(const char *why)
{
	hooklog::Write("rmg builder: Cancel (%s)", why);
	overlay::CloseModal(kName);
}

// The robot's step "panel|<command>" (it cannot click): open, preset <n | recipe
// | part of a name>, distance on|off, scroll <units> | end, use, save, cancel.
void RobotCommand(const char *text, bool on_screen)
{
	const bool open = overlay::ModalOpen(kName);
	if (strcmp(text, "open") == 0) {
		if (on_screen)
			OpenPanel("robot");
		return;
	}
	if (!open) {
		hooklog::Write("rmg builder: robot command \"%s\" ignored: the panel is not open", text);
		return;
	}
	if (strncmp(text, "preset ", 7) == 0) {
		const char *arg = text + 7;
		int found = -1;
		const int number = atoi(arg);
		if (number >= 1 && number <= data::kPresetCount && strspn(arg, "0123456789") == strlen(arg))
			found = number - 1;
		for (int i = 0; found < 0 && i < data::kPresetCount; ++i)
			if (_stricmp(arg, data::kPresets[i].recipe) == 0 || strstr(data::kPresets[i].name, arg))
				found = i;
		if (found >= 0)
			LoadRecipe(data::kPresets[found].recipe);
		else if (!LoadRecipe(arg))
			hooklog::Write("rmg builder: robot: no preset \"%s\"", arg);
		hooklog::Write("rmg builder: robot: preset %s -> %s (%s)", arg, g.recipe.c_str(), g.valid ? g.t.name.c_str() : g.error.c_str());
	}
	else if (strncmp(text, "distance ", 9) == 0) {
		g.by_distance = strcmp(text + 9, "on") == 0;
		if (!g.by_distance)
			for (int i = 0; i < 4; ++i)
				g.choices.loot_by_distance[i].clear(), g.choices.guards_by_distance[i].clear();
		Rebuild();
	}
	else if (strncmp(text, "scroll ", 7) == 0) {
		g.scroll = strcmp(text + 7, "end") == 0 ? 100000.0f : static_cast<float>(atof(text + 7));
		g.set_scroll = true;
	}
	else if (strcmp(text, "use") == 0)
		g.pending = 1;
	else if (strcmp(text, "save") == 0)
		g.pending = 2;
	else if (strcmp(text, "cancel") == 0)
		Cancel("robot");
	else
		hooklog::Write("rmg builder: robot command \"%s\" not known", text);
}

// --- the button of our own, when the game's files could not get the native one ------------------
bool g_fallback_down = false;  // the left button went down over it
bool g_fallback_hovered = false;

bool FallbackWanted(bool on_screen)
{
	return on_screen && g_ready && !RmgScreenHasNativeButton() && !overlay::ModalOpen(kName);
}

// The button reads the mouse itself (ImGui gets no mouse while no panel is open).
void FallbackInput(float w, float h)
{
	const Layout l = LayoutFor(w, h);
	const ImVec2 p0 = l.At(static_cast<float>(data::kButtonX), static_cast<float>(data::kButtonY));
	const ImVec2 p1 = l.At(data::kButtonX + kButtonW, data::kButtonY + kButtonH);
	float x, y;
	HWND window = overlay::GameWindow();
	const bool front = window && GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) == GetAncestor(window, GA_ROOTOWNER);
	g_fallback_hovered = front && overlay::CursorInBackBuffer(w, h, &x, &y) && x >= p0.x && x < p1.x && y >= p0.y && y < p1.y;
	const bool down = front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (down && !g_fallback_down && g_fallback_hovered)
		g_fallback_down = true;
	else if (!down && g_fallback_down) {
		g_fallback_down = false;
		if (g_fallback_hovered)
			OpenPanel("our Template... button");
	}
}

// --- the overlay client ----------------------------------------------------------------------
float g_back_w = 1024.0f, g_back_h = 768.0f;  // the last frame's back buffer

bool Wanted()
{
	char top[96];
	const bool on_screen = overlay::TopScreen(top, sizeof(top)) && strcmp(top, kScreenClass) == 0;
	// Each change of the screen on top (the first 100): which screens the panel
	// can be over, and what closed it.
	static char last[96] = "?";
	static int changes = 0;
	if (strcmp(top, last) != 0 && changes < 100) {
		++changes;
		lstrcpynA(last, top, sizeof(last));
		hooklog::Write("rmg builder: screen on top: %s%s", top[0] ? top : "(none)",
		               on_screen ? (RmgScreenHasNativeButton() ? " (the random map screen, with our Template... button)"
		                                                        : " (the random map screen)")
		                         : "");
	}
	char command[256];
	if (robot::TakePanelCommand(command, sizeof(command)))
		RobotCommand(command, on_screen);
	if (overlay::KeyPressed(g_key, &g_key_down) && on_screen && g_ready) {
		if (overlay::ModalOpen(kName))
			Cancel("its key");
		else
			OpenPanel("its key");
	}
	if (g.pending && overlay::ModalOpen(kName)) {
		const int action = g.pending;
		g.pending = 0;
		if (action == 1)
			DoUse();
		else
			DoSave();
	}
	if (FallbackWanted(on_screen)) {
		FallbackInput(g_back_w, g_back_h);
		return true;
	}
	g_fallback_down = false;
	return overlay::ModalOpen(kName);
}

void Prepare(const overlay::Frame &frame)
{
	ui::LoadPictures(frame.device);
	const float k = frame.height / kRefHeight;
	for (float size : {kTitleFont, kButtonFont, kBodyFont, kSmallFont})
		overlay::WantFont(Px(size, k));
}

// --- drawing ----------------------------------------------------------------------------------
ImU32 TeamColour(int team)
{
	static const ImU32 kColours[8] = {
		IM_COL32(208, 66, 60, 255),  IM_COL32(60, 120, 208, 255), IM_COL32(79, 154, 58, 255),  IM_COL32(226, 200, 70, 255),
		IM_COL32(224, 122, 34, 255), IM_COL32(138, 68, 184, 255), IM_COL32(31, 154, 160, 255), IM_COL32(208, 85, 154, 255),
	};
	return kColours[((team % 8) + 8) % 8];
}

void DashedLine(ImDrawList *draw, ImVec2 a, ImVec2 b, ImU32 colour, float width, float dash)
{
	const float dx = b.x - a.x, dy = b.y - a.y, length = sqrtf(dx * dx + dy * dy);
	if (length < 1.0f || dash < 1.0f)
		return;
	for (float t = 0.0f; t < length; t += 2 * dash) {
		const float e = t + dash < length ? t + dash : length;
		draw->AddLine(ImVec2(a.x + dx * t / length, a.y + dy * t / length), ImVec2(a.x + dx * e / length, a.y + dy * e / length),
		              colour, width);
	}
}

void DrawZoneMap(ImDrawList *draw, const Layout &l, const data::Words &w)
{
	const ImVec2 b0 = l.At(kMapX, kMapY), b1 = l.At(kMapX + kMapW, kMapY + kMapH);
	draw->PushClipRect(b0, b1, true);
	ImFont *small = overlay::Font(Px(kSmallFont, l.k));
	const float small_px = Px(kSmallFont, l.k);
	const float legend_h = 16.0f;
	if (g.valid && !g.pos.empty()) {
		// the page's drawing (a circle of radius 190, zones of radius 16 to 22) fitted in the box
		const float s = (kMapH - legend_h - 10.0f) / (2.0f * (data::kMapRadius + 24.0f)) * l.k;
		const ImVec2 c = l.At(kMapX + kMapW / 2, kMapY + (kMapH - legend_h) / 2 + 2);
		auto at = [&](const data::Point &p) { return ImVec2(c.x + (p.x - data::kMapCentreX) * s, c.y + (p.y - data::kMapCentreY) * s); };
		const ImU32 path = IM_COL32(205, 187, 146, 170);
		for (const rmg::Connection &cn : g.t.connections) {
			if (cn.source < 1 || cn.dest < 1 || cn.source > static_cast<int>(g.pos.size()) || cn.dest > static_cast<int>(g.pos.size()))
				continue;
			const ImVec2 a = at(g.pos[cn.source - 1]), b = at(g.pos[cn.dest - 1]);
			if (cn.guarded) {
				const float width = (cn.guard_strenght / 4.0f > 1.2f ? cn.guard_strenght / 4.0f : 1.2f) * s * 1.2f;
				draw->AddLine(a, b, path, width > 1.0f ? width : 1.0f);
			}
			else
				DashedLine(draw, a, b, path, 1.4f * s * 1.2f > 1.0f ? 1.4f * s * 1.2f : 1.0f, 4.0f * s * 1.4f);
		}
		ImFont *number_font = small;
		for (size_t i = 0; i < g.t.zones.size() && i < g.pos.size(); ++i) {
			const rmg::Zone &z = g.t.zones[i];
			const ImVec2 p = at(g.pos[i]);
			const float r = (z.kind == rmg::ZoneKind::kCentre ? 22.0f : z.kind == rmg::ZoneKind::kStart ? 19.0f : 16.0f) * s;
			ImU32 fill = z.kind == rmg::ZoneKind::kCentre   ? IM_COL32(214, 158, 32, 255)
			             : z.kind == rmg::ZoneKind::kBorder ? IM_COL32(141, 138, 130, 255)
			                                               : TeamColour(z.team);
			if (z.kind == rmg::ZoneKind::kTeam)
				fill = (fill & 0x00ffffff) | (150u << 24);  // a team's treasure zone: paler
			draw->AddCircleFilled(p, r, fill, 24);
			draw->AddCircle(p, r, IM_COL32(20, 16, 10, 220), 24, 1.5f);
			if (z.town && z.kind != rmg::ZoneKind::kStart) {  // a town: a small gold mark
				const float m = r * 0.45f;
				draw->AddRectFilled(ImVec2(p.x + r * 0.55f, p.y - r - m * 0.4f), ImVec2(p.x + r * 0.55f + m * 1.6f, p.y - r + m * 1.2f),
				                    IM_COL32(240, 208, 128, 255));
			}
			char label[8] = "";
			if (z.kind == rmg::ZoneKind::kStart)
				_snprintf_s(label, sizeof(label), _TRUNCATE, "%d", z.index);
			else if (z.kind == rmg::ZoneKind::kCentre)
				lstrcpynA(label, "C", sizeof(label));
			if (label[0]) {
				const float tw = ui::TextWidth(number_font, small_px, label);
				const ImU32 ink = z.kind == rmg::ZoneKind::kCentre || z.team % 8 == 3 ? IM_COL32(42, 29, 5, 255) : IM_COL32(255, 255, 255, 255);
				draw->AddText(number_font, small_px, ImVec2(floorf(p.x - tw / 2 + 0.5f), floorf(p.y - small_px / 2 - 0.5f)), ink, label);
			}
		}
	}
	const std::string legend = ui::Fit(small, small_px, w.legend, (kMapW - 8) * l.k);
	const float lw = ui::TextWidth(small, small_px, legend.c_str());
	ui::ShadowText(draw, small, small_px, ImVec2(b0.x + ((kMapW * l.k) - lw) / 2, b1.y - (legend_h - 1) * l.k), ui::kDim,
	               legend.c_str());
	draw->PopClipRect();
}

void CentredLine(ImDrawList *draw, ImFont *font, float px, float x0, float x1, float y, ImU32 colour, const char *text)
{
	const std::string s = ui::Fit(font, px, text, x1 - x0);
	const float tw = ui::TextWidth(font, px, s.c_str());
	ui::ShadowText(draw, font, px, ImVec2(x0 + (x1 - x0 - tw) / 2, y), colour, s.c_str());
}

void DrawSummary(ImDrawList *draw, const Layout &l, const data::Words &w)
{
	ImFont *small = overlay::Font(Px(kSmallFont, l.k));
	ImFont *body = overlay::Font(Px(kBodyFont, l.k));
	const float small_px = Px(kSmallFont, l.k), body_px = Px(kBodyFont, l.k);
	const float x0 = l.At(kNameX - 4, 0).x, x1 = l.At(kNameX + kNameW + 4, 0).x;
	char line[256];
	if (g.valid) {
		_snprintf_s(line, sizeof(line), _TRUNCATE, w.stats, g.t.stats.players, g.t.stats.zones, g.t.stats.neutral_zones);
		CentredLine(draw, small, small_px, x0, x1, l.At(0, kStatsY).y, ui::kDim, line);
		const std::string name = g.t.name + "*";
		const std::string shown = ui::Fit(body, body_px, name.c_str(), (kNameW - 8) * l.k);
		ui::ShadowText(draw, body, body_px, ImVec2(l.At(kNameX + 4, 0).x, l.At(0, kNameY + kNameH / 2).y - body_px / 2 - 1),
		               ui::kParchment, shown.c_str());
	}
	const bool status = !g.status.empty() && GetTickCount() < g.status_until;
	if (status)
		CentredLine(draw, small, small_px, x0, x1, l.At(0, kSizeY).y, g.status_bad ? ui::kError : ui::kGold, g.status.c_str());
	else if (g.valid) {
		const int i = SizeIndex(g.t);
		_snprintf_s(line, sizeof(line), _TRUNCATE, g.choices.underground ? w.size_line_ug : w.size_line, i >= 0 ? w.sizes[i] : "?");
		CentredLine(draw, small, small_px, x0, x1, l.At(0, kSizeY).y, ui::kDim, line);
	}
	else
		CentredLine(draw, small, small_px, x0, x1, l.At(0, kSizeY).y, ui::kError, data::Localize(g.error, overlay::PanelLanguage()).c_str());
}

// The choices, in the scrolling box. y and sizes in layout units inside the box.
struct ListLayout {
	ImVec2 origin;  // the box's content top-left in pixels (with the scroll)
	float k, width;  // the content's width in layout units
	float y;
	ImVec2 At(float x, float yy) const { return ImVec2(origin.x + x * k, origin.y + yy * k); }
};

void Section(ImDrawList *draw, ListLayout &ll, const char *title)
{
	ll.y += 3;
	ImFont *font = overlay::Font(Px(kBodyFont, ll.k));
	const float px = Px(kBodyFont, ll.k);
	ui::ShadowText(draw, font, px, ll.At(0, ll.y), ui::kGold, ui::Fit(font, px, title, ll.width * ll.k).c_str());
	const ImVec2 a = ll.At(0, ll.y + 16), b = ll.At(ll.width, ll.y + 16);
	draw->AddLine(a, b, IM_COL32(230, 189, 92, 90), 1.0f);
	ll.y += 21;
}

void Label(ImDrawList *draw, ListLayout &ll, const char *text)
{
	ImFont *font = overlay::Font(Px(kSmallFont, ll.k));
	const float px = Px(kSmallFont, ll.k);
	ui::ShadowText(draw, font, px, ll.At(1, ll.y), ui::kDim, ui::Fit(font, px, text, ll.width * ll.k).c_str());
	ll.y += 14;
}

bool ComboRow(ListLayout &ll, const char *id, const char *const *items, int count, int *current, const char *shown = nullptr,
              float x = 0, float width = -1, float height = 22, bool small = false)
{
	const float size = small ? kSmallFont : kBodyFont;
	const float wdt = width < 0 ? ll.width - x : width;
	const bool changed = ui::Combo(id, ll.At(x, ll.y), ImVec2(wdt * ll.k, height * ll.k), items, count, current, shown,
	                               overlay::Font(Px(size, ll.k)), Px(size, ll.k), ll.k);
	return changed;
}

// A number with arrows, and the maker's own check of the value each arrow would give.
bool StepperRow(ImDrawList *draw, ListLayout &ll, const char *id, const char *label, int *value, int low, int high)
{
	ImFont *font = overlay::Font(Px(kBodyFont, ll.k));
	const float px = Px(kBodyFont, ll.k), h = 18.0f;
	ui::ShadowText(draw, font, px, ll.At(1, ll.y + (h - kBodyFont) / 2 - 1), ui::kParchment,
	               ui::Fit(font, px, label, (ll.width - 70) * ll.k).c_str());
	std::string why;
	auto allowed = [&](int v) {
		if (v < low || v > high)
			return false;
		const int old = *value;
		*value = v;
		const rmg::Choices c = g.choices;
		*value = old;
		std::string error;
		if (rmg::CheckChoices(c, &error))
			return true;
		why = error;
		return false;
	};
	const bool can_down = allowed(*value - 1);
	const std::string why_down = why;
	why.clear();
	const bool can_up = allowed(*value + 1);
	int blocked = 0;
	const int d = ui::Stepper(id, ll.At(ll.width, ll.y), h * ll.k, *value, can_down, can_up, &blocked, font, px, ll.k);
	ll.y += h + 5;
	const std::string &reason = blocked < 0 ? why_down : why;
	if (blocked && !reason.empty()) {
		g.hint = data::Localize(reason, overlay::PanelLanguage());
		g.hint_until = GetTickCount() + 5000;
	}
	if (!d)
		return false;
	*value += d;
	return true;
}

bool CheckRow(ListLayout &ll, const char *id, const char *label, bool *value)
{
	const bool clicked = ui::CheckBox(id, ll.At(0, ll.y), 17.0f * ll.k, label, value, overlay::Font(Px(kBodyFont, ll.k)),
	                                  Px(kBodyFont, ll.k), ll.width * ll.k, ll.k);
	ll.y += 22;
	return clicked;
}

void WrappedText(ImDrawList *draw, ListLayout &ll, const std::string &text, ImU32 colour)
{
	ImFont *font = overlay::Font(Px(kSmallFont, ll.k));
	const float px = Px(kSmallFont, ll.k), wrap = ll.width * ll.k;
	const ImVec2 size = font->CalcTextSizeA(px, 100000.0f, wrap, text.c_str());
	const ImVec2 p = ll.At(0, ll.y);
	draw->AddText(font, px, ImVec2(p.x + 1, p.y + 1), IM_COL32(0, 0, 0, 200), text.c_str(), nullptr, wrap);
	draw->AddText(font, px, p, colour, text.c_str(), nullptr, wrap);
	ll.y += size.y / ll.k + 4;
}

int IndexOf(const std::string &key, const char *const *keys, int count, int none)
{
	for (int i = 0; i < count; ++i)
		if (key == keys[i])
			return i;
	return none;
}

void DrawChoices(ImDrawList *draw, ListLayout &ll, const data::Words &w)
{
	rmg::Choices &c = g.choices;
	bool changed = false;
	ll.y = 2;
	// presets: the ready templates
	Section(draw, ll, w.start_from);
	{
		std::vector<std::string> names;
		std::vector<const char *> items;
		for (int i = 0; i < data::kPresetCount; ++i)
			names.push_back(data::kPresets[i].name + 3);  // without "OH-"
		for (const std::string &n : names)
			items.push_back(n.c_str());
		int current = g.preset;
		if (ComboRow(ll, "##preset", items.data(), data::kPresetCount, &current, g.preset < 0 ? w.custom : nullptr) && current >= 0) {
			LoadRecipe(data::kPresets[current].recipe);
			hooklog::Write("rmg builder: preset %s (%s)", data::kPresets[current].name, data::kPresets[current].recipe);
		}
		ll.y += 26;
	}
	// teams
	Section(draw, ll, w.teams_section);
	changed |= StepperRow(draw, ll, "##teams", w.teams, &c.teams, 2, 8);
	changed |= StepperRow(draw, ll, "##per_team", w.per_team, &c.per_team, 1, 4);
	if (!g.hint.empty() && GetTickCount() < g.hint_until)
		WrappedText(draw, ll, g.hint, ui::kWarn);
	Label(draw, ll, w.ally_path);
	{
		int i = c.ally_link == "open" ? 1 : 0;
		if (ComboRow(ll, "##ally", w.ally_items, 2, &i)) {
			c.ally_link = i ? "open" : "weak";
			changed = true;
		}
		ll.y += 26;
	}
	Label(draw, ll, w.mines);
	{
		int i = c.start_mines == "woodore" ? 1 : 0;
		if (ComboRow(ll, "##mines", w.mines_items, 2, &i)) {
			c.start_mines = i ? "woodore" : "all";
			changed = true;
		}
		ll.y += 26;
	}
	// neutral zones
	Section(draw, ll, w.neutral_section);
	changed |= StepperRow(draw, ll, "##team_zones", w.team_zones, &c.team_zones, 0, 3);
	changed |= StepperRow(draw, ll, "##border_zones", w.border_zones, &c.border_zones, 0, 4);
	changed |= StepperRow(draw, ll, "##centre_zones", w.centre_zones, &c.centre_zones, 0, 3);
	changed |= CheckRow(ll, "##border_towns", w.border_towns, &c.border_towns);
	changed |= CheckRow(ll, "##team_towns", w.team_towns, &c.team_towns);
	// loot and guards
	Section(draw, ll, w.loot_section);
	Label(draw, ll, w.loot);
	{
		int i = IndexOf(c.loot, data::kLootKeys, 4, 1);
		if (ComboRow(ll, "##loot", w.loot_items, 4, &i)) {
			c.loot = data::kLootKeys[i];
			changed = true;
		}
		ll.y += 26;
	}
	Label(draw, ll, w.guards);
	{
		int i = IndexOf(c.guards, data::kGuardKeys, 3, 1);
		if (ComboRow(ll, "##guards", w.guard_items, 3, &i)) {
			c.guards = data::kGuardKeys[i];
			changed = true;
		}
		ll.y += 26;
	}
	if (CheckRow(ll, "##by_distance", w.by_distance, &g.by_distance)) {
		if (!g.by_distance)
			for (int i = 0; i < 4; ++i)
				c.loot_by_distance[i].clear(), c.guards_by_distance[i].clear();
		changed = true;
	}
	if (g.by_distance) {
		// one row per distance ring: its loot and the guards of the paths into it ("Same" = the levels above)
		const float label_w = 74.0f, gap = 4.0f, col = (ll.width - label_w - gap) / 2;
		ImFont *small = overlay::Font(Px(kSmallFont, ll.k));
		const float px = Px(kSmallFont, ll.k);
		ui::ShadowText(draw, small, px, ll.At(label_w, ll.y), ui::kDim, ui::Fit(small, px, w.loot, (col - 2) * ll.k).c_str());
		ui::ShadowText(draw, small, px, ll.At(label_w + col + gap, ll.y), ui::kDim, ui::Fit(small, px, w.guards, (col - 2) * ll.k).c_str());
		ll.y += 14;
		const char *loot_items[5] = {w.same, w.loot_items[0], w.loot_items[1], w.loot_items[2], w.loot_items[3]};
		const char *guard_items[4] = {w.same, w.guard_items[0], w.guard_items[1], w.guard_items[2]};
		for (int ring = 0; ring < 4; ++ring) {
			char label[64];
			_snprintf_s(label, sizeof(label), _TRUNCATE, g.valid ? "%s (%d)" : "%s", w.rings[ring], g.valid ? g.t.stats.by_distance[ring] : 0);
			ui::ShadowText(draw, small, px, ll.At(1, ll.y + (20 - kSmallFont) / 2 - 1), ui::kParchment,
			               ui::Fit(small, px, label, (label_w - 3) * ll.k).c_str());
			char id[32];
			_snprintf_s(id, sizeof(id), _TRUNCATE, "##loot%d", ring);
			int li = c.loot_by_distance[ring].empty() ? 0 : IndexOf(c.loot_by_distance[ring], data::kLootKeys, 4, -1) + 1;
			if (ComboRow(ll, id, loot_items, 5, &li, nullptr, label_w, col, 20, true)) {
				c.loot_by_distance[ring] = li ? data::kLootKeys[li - 1] : "";
				changed = true;
			}
			if (ring > 0) {  // the start zones have no guard of their own (recipe names cannot hold one)
				_snprintf_s(id, sizeof(id), _TRUNCATE, "##guards%d", ring);
				int gi = c.guards_by_distance[ring].empty() ? 0 : IndexOf(c.guards_by_distance[ring], data::kGuardKeys, 3, -1) + 1;
				if (ComboRow(ll, id, guard_items, 4, &gi, nullptr, label_w + col + gap, col, 20, true)) {
					c.guards_by_distance[ring] = gi ? data::kGuardKeys[gi - 1] : "";
					changed = true;
				}
			}
			ll.y += 23;
		}
	}
	// map
	Section(draw, ll, w.map_section);
	Label(draw, ll, w.size);
	{
		char auto_text[96];
		const int best = g.valid && c.size == "auto" ? SizeIndex(g.t) : -1;
		_snprintf_s(auto_text, sizeof(auto_text), _TRUNCATE, w.auto_size, best >= 0 ? w.sizes[best] : "");
		const char *items[8] = {auto_text, w.sizes[0], w.sizes[1], w.sizes[2], w.sizes[3], w.sizes[4], w.sizes[5], w.sizes[6]};
		int i = 0;
		for (int s = 0; s < 7; ++s)
			if (c.size == rmg::kSizes[s].key)
				i = s + 1;
		if (best < 0 && i == 0)
			lstrcpynA(auto_text, "Auto", sizeof(auto_text));
		if (ComboRow(ll, "##size", items, 8, &i)) {
			c.size = i ? rmg::kSizes[i - 1].key : "auto";
			changed = true;
		}
		ll.y += 26;
	}
	changed |= CheckRow(ll, "##underground", w.underground, &c.underground);
	// the maker's warnings, or why it refuses the choices
	ll.y += 2;
	const int language = overlay::PanelLanguage();
	if (!g.valid)
		WrappedText(draw, ll, data::Localize(g.error, language), ui::kError);
	else
		for (const std::string &warning : g.t.warnings)
			WrappedText(draw, ll, data::Localize(warning, language), ui::kWarn);
	ll.y += 4;
	ImGui::SetCursorScreenPos(ll.At(0, ll.y));
	ImGui::Dummy(ImVec2(1, 1));
	if (changed)
		Rebuild();
}

void DrawScrollThumb(ImDrawList *draw, const Layout &l)
{
	const ImVec2 t0 = l.At(kTrackX, kTrackY), t1 = l.At(kTrackX + kTrackW, kTrackY + kTrackH);
	if (g.scroll_max <= 0.5f)
		return;
	const float track_h = t1.y - t0.y;
	const float content = g.scroll_view + g.scroll_max;
	float thumb_h = track_h * g.scroll_view / content;
	if (thumb_h < 24.0f * l.k)
		thumb_h = 24.0f * l.k;
	ImGui::SetCursorScreenPos(t0);
	ImGui::InvisibleButton("##track", ImVec2(t1.x - t0.x, track_h));
	if (ImGui::IsItemActive()) {
		const float my = ImGui::GetIO().MousePos.y;
		float f = (my - t0.y - thumb_h / 2) / (track_h - thumb_h);
		f = f < 0 ? 0 : f > 1 ? 1 : f;
		g.scroll = f * g.scroll_max;
		g.set_scroll = true;
	}
	const float y = t0.y + (track_h - thumb_h) * (g.scroll / g.scroll_max);
	const ImVec2 a(t0.x + 3.0f * l.k, y + 2.0f * l.k), b(t1.x - 3.0f * l.k, y + thumb_h - 2.0f * l.k);
	const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
	draw->AddRectFilledMultiColor(a, b, hot ? IM_COL32(200, 156, 90, 255) : IM_COL32(170, 128, 70, 255),
	                              hot ? IM_COL32(200, 156, 90, 255) : IM_COL32(170, 128, 70, 255), IM_COL32(96, 70, 38, 255),
	                              IM_COL32(96, 70, 38, 255));
	draw->AddRect(a, b, IM_COL32(240, 208, 128, 200), 2.0f * l.k, 0, 1.0f);
}

void DrawPanel(const overlay::Frame &frame)
{
	const Layout l = LayoutFor(frame.width, frame.height);
	const data::Words &w = data::WordsFor(overlay::PanelLanguage());
	// The game behind, dimmed: it gets no input while the panel is open.
	ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(frame.width, frame.height), IM_COL32(0, 0, 0, 115));
	const ImVec2 p0 = l.At(0, 0), p1 = l.At(kPanelW, kPanelH);
	ImGui::SetNextWindowPos(p0, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::Begin("##rmg_builder", nullptr,
	             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
	                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImDrawList *draw = ImGui::GetWindowDrawList();
	if (ImTextureID bg = ui::Tex(ui::kBackground))
		draw->AddImage(bg, p0, p1, ImVec2(0, 0), ImVec2(kPanelW / kBackgroundTexW, kPanelH / kBackgroundTexH));
	else {
		draw->AddRectFilled(p0, p1, IM_COL32(30, 24, 18, 248), 8.0f * l.k);
		draw->AddRect(p0, p1, IM_COL32(150, 116, 70, 255), 8.0f * l.k, 0, 2.0f * l.k);
	}
	const float title = Px(kTitleFont, l.k);
	CentredLine(draw, overlay::Font(title), title, l.At(kTitleX, 0).x, l.At(kTitleX + kTitleW, 0).x,
	            l.At(0, kTitleY + kTitleH / 2).y - title / 2, ui::kGold, w.title);
	DrawZoneMap(draw, l, w);
	DrawSummary(draw, l, w);

	// the choices, scrolling in the options list's box
	const ImVec2 c0 = l.At(kListX, kListY), c1 = l.At(kListX + kListW, kListY + kListH);
	ImGui::SetCursorScreenPos(c0);
	if (g.set_scroll) {
		ImGui::SetNextWindowScroll(ImVec2(-1.0f, g.scroll));
		g.set_scroll = false;
	}
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * l.k, 0));
	ImGui::BeginChild("##choices", ImVec2(c1.x - c0.x, c1.y - c0.y), ImGuiChildFlags_None,
	                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
	ListLayout ll;
	ll.k = l.k;
	ll.origin = ImGui::GetCursorScreenPos();
	ll.width = kListW - 12.0f;
	DrawChoices(ImGui::GetWindowDrawList(), ll, w);
	g.scroll = ImGui::GetScrollY();
	g.scroll_max = ImGui::GetScrollMaxY();
	g.scroll_view = c1.y - c0.y;
	ImGui::EndChild();
	ImGui::PopStyleVar();
	DrawScrollThumb(draw, l);

	// the buttons, on the game's button row
	ImFont *button_font = overlay::Font(Px(kButtonFont, l.k));
	const float button_px = Px(kButtonFont, l.k);
	const bool usable = g.valid && !g.recipe.empty();
	if (ui::Button("##use", l.At(kUseX, kButtonY), l.At(kUseX + kButtonW, kButtonY + kButtonH), w.use, button_font, button_px,
	               usable && g_ready, l.k))
		g.pending = 1;
	if (ui::Button("##save", l.At(kSaveX, kButtonY), l.At(kSaveX + kButtonW, kButtonY + kButtonH), w.save, button_font, button_px,
	               g.valid, l.k))
		g.pending = 2;
	if (ui::Button("##cancel", l.At(kCancelX, kButtonY), l.At(kCancelX + kButtonW, kButtonY + kButtonH), w.cancel, button_font,
	               button_px, true, l.k))
		Cancel("its button");
	ImGui::End();
	ImGui::PopStyleVar(3);
	static bool first = true;
	if (first) {
		first = false;
		hooklog::Write("rmg builder: first frame drawn (%.0fx%.0f, panel at %.0f,%.0f, %.2f pixels per layout unit)", frame.width,
		               frame.height, l.x0, l.y0, l.k);
	}
}

void DrawFallbackButton(const overlay::Frame &frame)
{
	const Layout l = LayoutFor(frame.width, frame.height);
	const data::Words &w = data::WordsFor(overlay::PanelLanguage());
	ImDrawList *draw = ImGui::GetForegroundDrawList();
	const ImVec2 p0 = l.At(static_cast<float>(data::kButtonX), static_cast<float>(data::kButtonY));
	const ImVec2 p1 = l.At(data::kButtonX + kButtonW, data::kButtonY + kButtonH);
	ui::ButtonPicture(draw, p0, p1, w.native_button, overlay::Font(Px(kButtonFont, l.k)), Px(kButtonFont, l.k), g_fallback_hovered,
	                  g_fallback_down && g_fallback_hovered, l.k);
	static bool told = false;
	if (!told) {
		told = true;
		hooklog::Write("rmg builder: our own Template... button is drawn on the random map screen (the game's files did not "
		               "get the native one)");
	}
}

void Draw(const overlay::Frame &frame)
{
	g_back_w = frame.width;
	g_back_h = frame.height;
	if (overlay::ModalOpen(kName))
		DrawPanel(frame);
	else
		DrawFallbackButton(frame);
}

void ReadSettings()
{
	const std::wstring path = IniPath();
	g_enabled = GetPrivateProfileIntW(L"rmg", L"builder", 1, path.c_str()) != 0;
	wchar_t key[16];
	GetPrivateProfileStringW(L"rmg", L"builder_key", L"F11", key, 16, path.c_str());
	g_key = overlay::ParseKey(key, VK_F11);
}

}  // namespace

void InstallRmgBuilderEarly()
{
	ReadSettings();
	g_early = true;
	if (g_enabled)
		overlay::InstallEarly();
}

void ApplyRmgBuilderFromSettings()
{
	if (!g_early)
		ReadSettings();
	if (!g_enabled) {
		hooklog::Write("rmg builder: off (builder=0)");
		return;
	}
	g_ready = RecipesServed();
	if (!g_ready) {
		hooklog::Write("rmg builder: off: recipe templates are not served (templates=0, or the maker's self-check failed)");
		return;
	}
	const overlay::Client client = {kName, &Wanted, &Prepare, &Draw};
	if (!overlay::AddClient(client))
		return;
	overlay::EnableModalInput();
	const bool button = InstallRmgScreen(&OpenFromScreen);
	hooklog::Write("rmg builder: on (the random map screen's %s; key %s)", button ? "Template... button" : "key only",
	               g_key ? "on" : "none");
}

}  // namespace mods
