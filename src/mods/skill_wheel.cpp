// Mod: the skill wheel in the game (the owner's wish, 2026-11-06: "bringing the external skill
// wheel inside the game with a small button like mmh5.5 does").
//
// In plain words: a panel that shows, for the hero's class, the skills it can be offered (with
// their chances at a level up) and, for each, the abilities this class can take, in the game's
// own pictures and words. What the hero has is framed in gold, what it can take now in a light
// frame, the rest is grey; the mouse over a picture tells what it does and what it needs first.
// Arrows show the other classes' wheels. A small button on the hero screen opens it (the
// original game: MMH5.5 has its own wheel there), or a key ([ui] skill_wheel_key, none by
// default). Read only: nothing of the game changes.
//
// The data (skill_wheel_logic.cpp): GameMechanics/RefTables/Skills.xdb and HeroClass.xdb, read
// through the game's own files (so MMH5.5's tables when MMH5.5 runs). The hero: the hero screen's
// (or the selected one: hero_skins.cpp skins::ShownHero), an IHero (vtable 0x00e7e484). Its data
// file is at +0xd0 (its path at +0x20: its <Class>); its mastery of skill number N is its vtable
// slot 0x174 (0x00b4d150, thiscall(N) -> 0..4; the script command HasHeroSkill, 0x005c3ee0,
// asks the same), for skills and abilities alike (an ability: 1 when it has it).
#include "skill_wheel.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "hero_skins_internal.h"
#include "rmg_builder_widgets.h"
#include "skill_wheel_logic.h"

#include "../hook/game_build.h"
#include "../hook/log.h"
#include "../hook/overlay.h"
#include "../hook/robot.h"
#include "../hook/texts.h"
#include "../hook/third_party/imgui/imgui.h"

namespace mods {
namespace {

namespace ui = builder_ui;

const char kName[] = "skill wheel";
const char kTables[] = "/GameMechanics/RefTables/";

// --- the game ------------------------------------------------------------------------------------
const DWORD kHeroVftable = 0x00e7e484;  // NWorld::CHero's IHero part
const DWORD kHeroShared = 0xd0;         // IHero: its data file (the object first)
const DWORD kResourcePath = 0x20;       // an NDb resource: its own file (a narrow string: begin, end)
const DWORD kMasterySlot = 0x174;       // IHero: thiscall(int skill) -> its mastery (0..4; an ability: 0 or 1)
typedef int(__fastcall *MasteryFn)(void *hero, void *edx, int skill);

// --- settings -------------------------------------------------------------------------------------
bool g_button = true;  // [ui] skill_wheel: the button on the hero screen (the original game)
int g_key = 0;         // [ui] skill_wheel_key (none by default)
bool g_key_down = false;
bool g_installed = false;

// --- layout (1024x768 units, from the panel's top-left corner) -----------------------------------
const float kRefHeight = 768.0f;
const float kPanelW = 780.0f, kPanelH = 724.0f;
const float kTitleY = 24.0f, kTitleH = 34.0f;
const float kContentX0 = 24.0f, kContentY0 = 66.0f, kContentX1 = 756.0f, kContentY1 = 646.0f;
const float kRowY = 72.0f, kRowH = 44.0f, kIcon = 38.0f;
const float kIconX = 36.0f, kTextX = 82.0f, kPerkX = 262.0f, kPerkStep = 44.0f;
const float kLegendY = 652.0f, kButtonY = 678.0f, kButtonW = 122.0f, kButtonH = 34.0f;
const float kTitleFont = 20.0f, kButtonFont = 15.0f, kBodyFont = 13.0f, kSmallFont = 11.0f;
const float kBackgroundTexW = 1024.0f, kBackgroundTexH = 512.0f;
const float kUsedW = 649.0f, kUsedH = 508.0f, kRim = 44.0f;  // the panel in that picture, its rim
const float kTipW = 300.0f;
// The hero screen's button: next to the Look button (hero_skins_panel.cpp, 403,94, 24x24), where
// MMH5.5 has its own wheel's round button (432,93).
const float kHsButtonX = 432.0f, kHsButtonY = 94.0f, kHsButtonSize = 24.0f;

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

// --- words: the [wheel] texts ----------------------------------------------------------------------
struct Words {
	const char *title, *title_hero, *chance, *needs, *legend, *legend_other, *close, *button_tip, *no_data;
};
const Words kKeys = {"wheel.title", "wheel.title_hero", "wheel.chance", "wheel.needs",  "wheel.legend",
                     "wheel.legend_other", "wheel.close", "wheel.button_tip", "wheel.no_data"};

// --- the game's files ------------------------------------------------------------------------------
bool Readable(const void *p, size_t n)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (!p || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
		return false;
	return static_cast<const BYTE *>(p) + n <= static_cast<const BYTE *>(mbi.BaseAddress) + mbi.RegionSize;
}

DWORD Read(const void *base, DWORD offset)
{
	const BYTE *p = static_cast<const BYTE *>(base) + offset;
	return Readable(p, 4) ? *reinterpret_cast<const DWORD *>(p) : 0;
}

std::string FileText(const std::string &path)
{
	std::string raw;
	return !path.empty() && overlay::ReadGameFile(path.c_str(), &raw) ? raw : std::string();
}

// A game text file (UTF-16) as UTF-8, its tags (<color=...>, <br>) taken off, lines joined with
// a space; cached.
std::map<std::string, std::string> g_texts;
const std::string &GameText(const std::string &path)
{
	auto it = g_texts.find(path);
	if (it != g_texts.end())
		return it->second;
	const std::string raw = FileText(path);
	std::wstring w;
	bool tag = false;
	for (size_t at = raw.size() >= 2 && raw[0] == '\xff' && raw[1] == '\xfe' ? 2 : 0; at + 1 < raw.size(); at += 2) {
		const wchar_t c = static_cast<wchar_t>(static_cast<unsigned char>(raw[at]) | static_cast<unsigned char>(raw[at + 1]) << 8);
		if (c == L'<')
			tag = true;
		else if (c == L'>' && tag)
			tag = false;
		else if (!tag && c != L'\r')
			w += c == L'\n' || c == L'\t' ? L' ' : c;
	}
	while (!w.empty() && w.back() == L' ')
		w.pop_back();
	return g_texts[path] = texts::Narrow(w);
}

// A texture .xdb's picture (.dds), cached.
std::map<std::string, std::string> g_pictures;
const std::string &Picture(const std::string &texture)
{
	auto it = g_pictures.find(texture);
	if (it != g_pictures.end())
		return it->second;
	return g_pictures[texture] = texture.empty() ? std::string() : wheel::TexturePicture(FileText(texture), texture);
}

wheel::Tables g_tables;
bool g_tables_tried = false;

// Its pictures (the game's skill and ability icons) are freed when it closes or shows another
// class, at the next frame's start (the frame being built may still draw them).
const char kPicturesPrefix[] = "/Textures/HeroScreen/";
bool g_release = false;

bool Tables()
{
	if (!g_tables_tried) {
		g_tables_tried = true;
		const std::string folder = kTables;
		const bool skills = wheel::ParseSkills(FileText(folder + "Skills.xdb"), folder, &g_tables);
		const bool classes = wheel::ParseClasses(FileText(folder + "HeroClass.xdb"), folder, &g_tables);
		hooklog::Write("skill wheel: the game's tables: %d skills and abilities, %d classes%s",
		               static_cast<int>(g_tables.skills.size()), static_cast<int>(g_tables.classes.size()),
		               skills && classes ? "" : " (not readable: no wheel)");
	}
	return !g_tables.skills.empty() && !g_tables.classes.empty();
}

// --- the hero -----------------------------------------------------------------------------------------
// Its masteries by skill number, read under a guard (the game's own function).
bool ReadMasteries(void *hero, std::vector<int> *out)
{
	__try {
		void **vt = *static_cast<void ***>(hero);
		MasteryFn mastery = reinterpret_cast<MasteryFn>(vt[kMasterySlot / 4]);
		for (size_t n = 1; n < out->size(); ++n)
			(*out)[n] = mastery(hero, nullptr, static_cast<int>(n));
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// Its data file's path ("" when not readable).
std::string HeroFile(void *hero)
{
	const BYTE *shared = reinterpret_cast<const BYTE *>(Read(hero, kHeroShared));
	if (!shared)
		return std::string();
	const char *b = reinterpret_cast<const char *>(Read(shared, kResourcePath));
	const char *e = reinterpret_cast<const char *>(Read(shared, kResourcePath + 4));
	if (!b || e <= b || e - b > 400 || !Readable(b, e - b))
		return std::string();
	return std::string(b, e);
}

// <tag>...</tag>'s text in a hero file, else "".
std::string XmlValue(const std::string &xml, const char *tag)
{
	const std::string open = std::string("<") + tag + ">";
	const size_t a = xml.find(open);
	if (a == std::string::npos)
		return std::string();
	const size_t b = xml.find('<', a + open.size());
	return b == std::string::npos ? std::string() : xml.substr(a + open.size(), b - a - open.size());
}

// --- the panel's state ------------------------------------------------------------------------------
struct State {
	int cls = 0;                  // the class shown (g_tables.classes)
	int hero_cls = -1;            // the hero's class, -1 none
	std::string hero_name;
	std::vector<int> mastery;     // the hero's, by skill number (empty: no hero)
	std::vector<wheel::Row> rows; // the class shown's wheel
	bool logged_first = false;
};
State g;

void ShowClass(int cls)
{
	const int n = static_cast<int>(g_tables.classes.size());
	g_release = g_release || (cls % n + n) % n != g.cls;
	g.cls = (cls % n + n) % n;
	g.rows = wheel::WheelOf(g_tables, g_tables.classes[g.cls].id);
}

// The classes with a wheel (HERO_CLASS_NONE has none): the arrows skip the others.
void StepClass(int step)
{
	const int n = static_cast<int>(g_tables.classes.size());
	for (int i = 1; i <= n; ++i) {
		const int c = ((g.cls + step * i) % n + n) % n;
		if (!g_tables.classes[c].skills.empty()) {
			ShowClass(c);
			return;
		}
	}
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
		hooklog::Write("skill wheel: not opened (%s): not on the map, the hero screen or the town screen (%s)", why,
		               top[0] ? top : "no screen");
		return;
	}
	if (!Tables())
		return;
	State fresh;
	bool on_hero_screen = false;
	void *hero = skins::ShownHero(&on_hero_screen);
	const std::string file = hero ? HeroFile(hero) : std::string();
	const std::string xml = file.empty() ? std::string() : FileText(file.substr(0, file.find('#')));
	const std::string cls = XmlValue(xml, "Class");
	for (size_t i = 0; i < g_tables.classes.size(); ++i)
		if (g_tables.classes[i].id == cls)
			fresh.hero_cls = static_cast<int>(i);
	if (fresh.hero_cls >= 0) {
		fresh.mastery.assign(g_tables.skills.size(), 0);
		if (!ReadMasteries(hero, &fresh.mastery)) {
			hooklog::Write("skill wheel: the hero's skills could not be read (its own function failed)");
			fresh.mastery.clear();
		}
		const size_t name_at = xml.find("<NameFileRef href=\"", xml.find("<Editable>") == std::string::npos ? 0 : xml.find("<Editable>"));
		if (name_at != std::string::npos) {
			const size_t q = xml.find('"', name_at + 19);
			fresh.hero_name = GameText(wheel::Resolve(xml.substr(name_at + 19, q - name_at - 19), wheel::FolderOf(file)));
		}
	}
	g = fresh;
	if (g.hero_cls >= 0)
		ShowClass(g.hero_cls);
	else {
		g.cls = 0;
		StepClass(1);
	}
	// (opened from Wanted, between frames: the last wheel's pictures can go now)
	if (g_release)
		overlay::ReleaseGameTextures(kPicturesPrefix);
	g_release = false;
	if (!overlay::OpenModal(kName))
		return;
	std::string has;
	for (size_t n = 1; n < g.mastery.size(); ++n)
		if (g.mastery[n] > 0)
			has += (has.empty() ? "" : ", ") + g_tables.skills[n].id.substr(11) + (g_tables.skills[n].kind == wheel::kSkill
			                                                                           ? " " + std::to_string(g.mastery[n])
			                                                                           : std::string());
	hooklog::Write("skill wheel: opened (%s) for %s (%s, %s): %d skills; it has: %s", why,
	               hero ? (g.hero_name.empty() ? file.c_str() : g.hero_name.c_str()) : "no hero",
	               on_hero_screen ? "the hero screen's" : "the selected one", g_tables.classes[g.cls].id.c_str(),
	               static_cast<int>(g.rows.size()), has.empty() ? "nothing read" : has.c_str());
}

void Close(const char *why)
{
	if (overlay::ModalOpen(kName))
		hooklog::Write("skill wheel: closed (%s)", why);
	overlay::CloseModal(kName);
	g_release = true;
}

// The robot's "panel|skillwheel ..." (it cannot click).
void RobotCommand(const char *text)
{
	if (strcmp(text, "open") == 0)
		OpenPanel("robot");
	else if (strcmp(text, "close") == 0)
		Close("robot");
	else if (strcmp(text, "next") == 0 && overlay::ModalOpen(kName))
		StepClass(1);
	else if (strcmp(text, "prev") == 0 && overlay::ModalOpen(kName))
		StepClass(-1);
	else
		hooklog::Write("skill wheel: robot command \"%s\" not known (or the panel is closed)", text);
}

// --- the hero screen's button ---------------------------------------------------------------------
bool g_on_hero_screen = false;
bool g_button_hovered = false, g_button_down = false;
float g_back_w = 1024.0f, g_back_h = 768.0f;

bool HeroScreenOnTop()
{
	char top[96];
	return overlay::TopScreen(top, sizeof(top)) && strstr(top, "HeroScreen") != nullptr;
}

void ButtonRect(float h, ImVec2 *p0, ImVec2 *p1)
{
	const float k = h / kRefHeight;
	*p0 = ImVec2(floorf(kHsButtonX * k + 0.5f), floorf(kHsButtonY * k + 0.5f));
	*p1 = ImVec2(floorf((kHsButtonX + kHsButtonSize) * k + 0.5f), floorf((kHsButtonY + kHsButtonSize) * k + 0.5f));
}

void ButtonInput(float w, float h)
{
	ImVec2 p0, p1;
	ButtonRect(h, &p0, &p1);
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
			OpenPanel("the hero screen's button");
	}
}

// A small wheel in the game's gold: a ring, six spokes, a hub.
void WheelGlyph(ImDrawList *draw, ImVec2 c, float r, ImU32 ink, float k)
{
	draw->AddCircle(c, r, ink, 24, 1.6f * k);
	for (int i = 0; i < 6; ++i) {
		const float a = 3.14159265f * i / 3.0f + 0.26f;
		draw->AddLine(ImVec2(c.x + cosf(a) * r * 0.28f, c.y + sinf(a) * r * 0.28f), ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r),
		              ink, 1.2f * k);
	}
	draw->AddCircleFilled(c, r * 0.28f, ink, 12);
}

void DrawHeroScreenButton(const overlay::Frame &frame)
{
	ImVec2 p0, p1;
	ButtonRect(frame.height, &p0, &p1);
	const float k = frame.height / kRefHeight;
	ImDrawList *draw = ImGui::GetForegroundDrawList();
	const bool pressed = g_button_down && g_button_hovered;
	draw->AddRectFilled(p0, p1, IM_COL32(20, 16, 12, 235), 3.0f * k);
	const ImVec2 c((p0.x + p1.x) / 2 + (pressed ? 1.0f : 0.0f), (p0.y + p1.y) / 2 + (pressed ? 1.0f : 0.0f));
	WheelGlyph(draw, c, (p1.x - p0.x) * 0.36f, g_button_hovered ? ui::kGold : ui::kParchment, k);
	draw->AddRect(p0, p1, g_button_hovered ? ui::kGold : ui::kEdge, 3.0f * k, 0, (g_button_hovered ? 2.0f : 1.5f) * k);
	if (g_button_hovered) {
		const char *tip = texts::Table(kKeys).button_tip;
		const float px = Px(kBodyFont, k);
		ImFont *font = overlay::Font(px);
		const float w = ui::TextWidth(font, px, tip);
		const ImVec2 t0(p0.x, p1.y + 4 * k), t1(p0.x + w + 12 * k, p1.y + 4 * k + px + 8 * k);
		draw->AddRectFilled(t0, t1, IM_COL32(20, 16, 12, 240), 3.0f * k);
		draw->AddRect(t0, t1, ui::kEdge, 3.0f * k, 0, 1.0f * k);
		ui::ShadowText(draw, font, px, ImVec2(t0.x + 6 * k, t0.y + 4 * k), ui::kParchment, tip);
	}
}

// --- drawing the panel -------------------------------------------------------------------------------
void *Texture(const overlay::Frame &frame, const std::string &texture_xdb)
{
	const std::string &dds = Picture(texture_xdb);
	int no_loads = 0;
	return dds.empty() ? nullptr : overlay::GameTexture(frame.device, dds.c_str(), &no_loads);
}

// Text wrapped into lines of at most `width` pixels, from (x, y); returns the y after it (draw = null: measure).
float Wrapped(ImDrawList *draw, ImFont *font, float px, float x, float y, float width, ImU32 colour, const std::string &text)
{
	std::string line, word;
	const std::string all = text + " ";
	for (char c : all) {
		if (c != ' ') {
			word += c;
			continue;
		}
		const std::string candidate = line.empty() ? word : line + " " + word;
		if (!line.empty() && ui::TextWidth(font, px, candidate.c_str()) > width) {
			if (draw)
				ui::ShadowText(draw, font, px, ImVec2(x, y), colour, line.c_str());
			y += px * 1.25f;
			line = word;
		}
		else
			line = candidate;
		word.clear();
	}
	if (!line.empty()) {
		if (draw)
			ui::ShadowText(draw, font, px, ImVec2(x, y), colour, line.c_str());
		y += px * 1.25f;
	}
	return y;
}

struct Tip {
	bool shown = false;
	std::string name, text, needs;
};

std::string NeedsLine(const wheel::Perk &perk)
{
	std::string list;
	for (int n : perk.needs) {
		const wheel::Skill &s = g_tables.skills[n];
		const std::string name = s.names.empty() ? s.id : GameText(s.names[0]);
		list += (list.empty() ? "" : ", ") + (name.empty() ? s.id : name);
	}
	return list.empty() ? std::string() : texts::Fill(texts::Table(kKeys).needs, {{"list", list}});
}

bool Has(int number)
{
	return number > 0 && number < static_cast<int>(g.mastery.size()) && g.mastery[number] > 0;
}

// What a row's skill shows: its level (the hero's mastery, at least 1), and whether the hero has it.
int RowLevel(const wheel::Row &row, bool own)
{
	const int m = own ? g.mastery[row.skill] : 0;
	return m < 1 ? 1 : m > 3 ? 3 : m;
}

// What an ability shows: 2 the hero has it, 1 it can take it now, 0 not yet (own: the hero's
// class is shown); its picture in colour or grey.
int PerkState(const wheel::Row &row, const wheel::Perk &perk, bool own)
{
	if (!own)
		return 0;
	if (Has(perk.number))
		return 2;
	if (!Has(row.skill))
		return 0;
	for (int n : perk.needs)
		if (!Has(n))
			return 0;
	return 1;
}

const std::string &PerkTexture(const wheel::Row &row, const wheel::Perk &perk, bool own)
{
	static const std::string none;
	const wheel::Skill &ability = g_tables.skills[perk.number];
	const size_t t = !own || PerkState(row, perk, own) > 0 ? 1 : 0;
	return t < ability.textures.size() ? ability.textures[t] : none;
}

const std::string &SkillTexture(const wheel::Row &row, bool own)
{
	static const std::string none;
	const wheel::Skill &skill = g_tables.skills[row.skill];
	const size_t level = static_cast<size_t>(RowLevel(row, own));
	return level < skill.textures.size() ? skill.textures[level] : none;
}

bool Own()
{
	return g.cls == g.hero_cls && !g.mastery.empty();
}

void DrawTip(const Tip &tip, float k)
{
	if (!tip.shown)
		return;
	ImDrawList *draw = ImGui::GetForegroundDrawList();
	const float name_px = Px(kBodyFont + 1, k), body_px = Px(kBodyFont - 1, k), small_px = Px(kSmallFont, k);
	ImFont *name_font = overlay::Font(name_px), *body = overlay::Font(body_px), *small = overlay::Font(small_px);
	const float w = kTipW * k, pad = 8 * k;
	float h = pad + name_px * 1.3f;
	if (!tip.text.empty())
		h = Wrapped(nullptr, body, body_px, 0, h, w - 2 * pad, 0, tip.text);
	if (!tip.needs.empty())
		h = Wrapped(nullptr, small, small_px, 0, h + 3 * k, w - 2 * pad, 0, tip.needs);
	h += pad;
	const ImVec2 mouse = ImGui::GetIO().MousePos, screen = ImGui::GetIO().DisplaySize;
	ImVec2 p0(mouse.x + 18 * k, mouse.y + 18 * k);
	if (p0.x + w > screen.x)
		p0.x = mouse.x - 12 * k - w;
	if (p0.y + h > screen.y)
		p0.y = screen.y - h - 4 * k;
	const ImVec2 p1(p0.x + w, p0.y + h);
	draw->AddRectFilled(p0, p1, IM_COL32(20, 16, 12, 245), 4.0f * k);
	draw->AddRect(p0, p1, ui::kEdge, 4.0f * k, 0, 1.0f * k);
	float y = p0.y + pad;
	ui::ShadowText(draw, name_font, name_px, ImVec2(p0.x + pad, y), ui::kGold, tip.name.c_str());
	y += name_px * 1.3f;
	if (!tip.text.empty())
		y = Wrapped(draw, body, body_px, p0.x + pad, y, w - 2 * pad, ui::kParchment, tip.text);
	if (!tip.needs.empty())
		Wrapped(draw, small, small_px, p0.x + pad, y + 3 * k, w - 2 * pad, ui::kDim, tip.needs);
}

// A picture in a square, in a frame: 0 none, 1 light (it can take it now), 2 gold (it has it).
void Square(ImDrawList *draw, void *tex, ImVec2 p0, ImVec2 p1, float alpha, int frame, float k)
{
	draw->AddRectFilled(p0, p1, IM_COL32(12, 10, 8, 230), 2.0f * k);
	if (tex)
		draw->AddImage(reinterpret_cast<ImTextureID>(tex), p0, p1, ImVec2(0, 0), ImVec2(1, 1),
		               IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)));
	if (frame == 2)
		draw->AddRect(ImVec2(p0.x - 1 * k, p0.y - 1 * k), ImVec2(p1.x + 1 * k, p1.y + 1 * k), ui::kGold, 2.0f * k, 0, 2.2f * k);
	else if (frame == 1)
		draw->AddRect(p0, p1, ui::kParchment, 2.0f * k, 0, 1.2f * k);
	else
		draw->AddRect(p0, p1, ui::kEdge, 2.0f * k, 0, 1.0f * k);
}

// The random map screen's panel (the builder's background: only its 649x508 top-left part is the
// panel, the rest of the picture is empty), stretched to any size with its rims kept: nine parts.
void Background(ImDrawList *draw, ImTextureID tex, ImVec2 p0, ImVec2 p1, float k)
{
	const float used_w = kUsedW / kBackgroundTexW, used_h = kUsedH / kBackgroundTexH;
	const float bu = kRim / kBackgroundTexW, bv = kRim / kBackgroundTexH, b = kRim * k;
	const float xs[4] = {p0.x, p0.x + b, p1.x - b, p1.x}, ys[4] = {p0.y, p0.y + b, p1.y - b, p1.y};
	const float us[4] = {0, bu, used_w - bu, used_w}, vs[4] = {0, bv, used_h - bv, used_h};
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			draw->AddImage(tex, ImVec2(xs[j], ys[i]), ImVec2(xs[j + 1], ys[i + 1]), ImVec2(us[j], vs[i]), ImVec2(us[j + 1], vs[i + 1]));
}

void DrawPanel(const overlay::Frame &frame)
{
	const Layout l = LayoutFor(frame.width, frame.height);
	const Words &w = texts::Table(kKeys);
	ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(frame.width, frame.height), IM_COL32(0, 0, 0, 115));
	const ImVec2 p0 = l.At(0, 0), p1 = l.At(kPanelW, kPanelH);
	ImGui::SetNextWindowPos(p0, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::Begin("##skill_wheel", nullptr,
	             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
	                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImDrawList *draw = ImGui::GetWindowDrawList();
	if (ImTextureID bg = ui::Tex(ui::kBackground))
		Background(draw, bg, p0, p1, l.k);
	else {
		draw->AddRectFilled(p0, p1, IM_COL32(30, 24, 18, 248), 8.0f * l.k);
		draw->AddRect(p0, p1, IM_COL32(150, 116, 70, 255), 8.0f * l.k, 0, 2.0f * l.k);
	}
	draw->AddRectFilled(l.At(kContentX0, kContentY0), l.At(kContentX1, kContentY1), IM_COL32(34, 27, 19, 245), 4.0f * l.k);
	draw->AddRect(l.At(kContentX0, kContentY0), l.At(kContentX1, kContentY1), ui::kEdge, 4.0f * l.k, 0, 1.0f * l.k);
	const float title_px = Px(kTitleFont, l.k), body_px = Px(kBodyFont, l.k), small_px = Px(kSmallFont, l.k);
	ImFont *title_font = overlay::Font(title_px), *body = overlay::Font(body_px), *small = overlay::Font(small_px);
	const bool own = Own();

	// the title: the class (and the hero), arrows for the other classes
	const wheel::HeroClass &cls = g_tables.classes[g.cls];
	const std::string class_name = GameText(cls.name_file).empty() ? cls.id : GameText(cls.name_file);
	const std::string title = own && !g.hero_name.empty()
	                              ? texts::Fill(w.title_hero, {{"class", class_name}, {"hero", g.hero_name}})
	                              : texts::Fill(w.title, {{"class", class_name}});
	const std::string fitted = ui::Fit(title_font, title_px, title.c_str(), (kPanelW - 160) * l.k);
	const float tw = ui::TextWidth(title_font, title_px, fitted.c_str());
	const float title_mid = l.At(0, kTitleY + kTitleH / 2).y;
	ui::ShadowText(draw, title_font, title_px, ImVec2(floorf(l.At(kPanelW / 2, 0).x - tw / 2), title_mid - title_px / 2), ui::kGold,
	               fitted.c_str());
	for (int side = -1; side <= 1; side += 2) {  // the arrows: triangles in the game's gold
		const float cx = kPanelW / 2 + side * (tw / l.k / 2 + 28.0f);
		const ImVec2 a0 = l.At(cx - 12, kTitleY + 5), a1 = l.At(cx + 12, kTitleY + kTitleH - 5);
		ImGui::SetCursorScreenPos(a0);
		if (ImGui::InvisibleButton(side < 0 ? "##prev" : "##next", ImVec2(a1.x - a0.x, a1.y - a0.y)))
			StepClass(side);
		const ImU32 ink = ImGui::IsItemHovered() ? ui::kGold : ui::kParchment;
		const float mx = (a0.x + a1.x) / 2, my = (a0.y + a1.y) / 2, s = 7 * l.k;
		if (side < 0)
			draw->AddTriangleFilled(ImVec2(mx - s, my), ImVec2(mx + s * 0.7f, my - s), ImVec2(mx + s * 0.7f, my + s), ink);
		else
			draw->AddTriangleFilled(ImVec2(mx + s, my), ImVec2(mx - s * 0.7f, my - s), ImVec2(mx - s * 0.7f, my + s), ink);
	}

	// the rows: a skill, its chance and the hero's mastery, then its abilities
	Tip tip;
	for (size_t r = 0; r < g.rows.size(); ++r) {
		const wheel::Row &row = g.rows[r];
		const wheel::Skill &skill = g_tables.skills[row.skill];
		const float y = kRowY + r * kRowH;
		const int m = own ? g.mastery[row.skill] : 0;
		const int level = RowLevel(row, own);
		const ImVec2 i0 = l.At(kIconX, y), i1 = l.At(kIconX + kIcon, y + kIcon);
		void *tex = Texture(frame, SkillTexture(row, own));
		Square(draw, tex, i0, i1, own && m == 0 ? 0.45f : 1.0f, own && m > 0 ? 2 : 0, l.k);
		const std::string name = skill.names.empty() ? skill.id : GameText(skill.names[level - 1 < static_cast<int>(skill.names.size()) ? level - 1 : 0]);
		const std::string shown_name = ui::Fit(body, body_px, name.c_str(), (kPerkX - kTextX - 8) * l.k);
		ui::ShadowText(draw, body, body_px, l.At(kTextX, y + 3), own && m > 0 ? ui::kGold : ui::kParchment, shown_name.c_str());
		const std::string chance = texts::Fill(w.chance, {{"n", std::to_string(row.chance)}});
		ui::ShadowText(draw, small, small_px, l.At(kTextX, y + 22), ui::kDim, chance.c_str());
		if (own)  // three pips: the hero's mastery
			for (int i = 0; i < 3; ++i) {
				const ImVec2 c = l.At(kTextX + 44 + i * 11, y + 28);
				if (i < m)
					draw->AddCircleFilled(c, 3.6f * l.k, ui::kGold, 12);
				else
					draw->AddCircle(c, 3.4f * l.k, ui::kEdge, 12, 1.0f * l.k);
			}
		if (ImGui::IsMouseHoveringRect(i0, ImVec2(l.At(kPerkX - 6, 0).x, i1.y))) {
			tip.shown = true;
			tip.name = name;
			tip.text = level - 1 < static_cast<int>(skill.descriptions.size()) ? GameText(skill.descriptions[level - 1]) : "";
		}
		for (size_t p = 0; p < row.perks.size(); ++p) {
			const wheel::Perk &perk = row.perks[p];
			const wheel::Skill &ability = g_tables.skills[perk.number];
			const float x = kPerkX + p * kPerkStep;
			const ImVec2 q0 = l.At(x, y), q1 = l.At(x + kIcon, y + kIcon);
			const int state = PerkState(row, perk, own);
			const bool colour = !own || state > 0;
			Square(draw, Texture(frame, PerkTexture(row, perk, own)), q0, q1, colour ? 1.0f : 0.8f, state, l.k);
			if (perk.kind == wheel::kUltimate)  // the ultimate: a small gold star mark
				draw->AddCircleFilled(ImVec2(q1.x - 4 * l.k, q0.y + 4 * l.k), 3.0f * l.k, ui::kGold, 10);
			if (ImGui::IsMouseHoveringRect(q0, q1)) {
				tip.shown = true;
				tip.name = ability.names.empty() ? ability.id : GameText(ability.names[0]);
				tip.text = ability.descriptions.empty() ? "" : GameText(ability.descriptions[0]);
				tip.needs = NeedsLine(perk);
			}
		}
	}

	// the legend and the button
	const std::string legend = own ? w.legend : w.legend_other;
	Wrapped(draw, small, small_px, l.At(kContentX0 + 8, 0).x, l.At(0, kLegendY).y, (kContentX1 - kContentX0 - 16) * l.k, ui::kDim,
	        legend);
	ImFont *button_font = overlay::Font(Px(kButtonFont, l.k));
	if (ui::Button("##close", l.At((kPanelW - kButtonW) / 2, kButtonY), l.At((kPanelW + kButtonW) / 2, kButtonY + kButtonH), w.close,
	               button_font, Px(kButtonFont, l.k), true, l.k))
		Close("its button");
	ImGui::End();
	ImGui::PopStyleVar(3);
	DrawTip(tip, l.k);
	if (!g.logged_first) {
		g.logged_first = true;
		hooklog::Write("skill wheel: drawn (%.0fx%.0f, %d rows, %.2f pixels per layout unit)", frame.width, frame.height,
		               static_cast<int>(g.rows.size()), l.k);
	}
}

// --- the overlay client --------------------------------------------------------------------------------
bool g_was_open = false;

bool Wanted()
{
	const bool open = overlay::ModalOpen(kName);
	g_release = g_release || (g_was_open && !open);  // (also when the overlay closed it: Esc, another screen)
	g_was_open = open;
	if (g_release) {  // (the panel closed, or shows another class: its pictures, freed)
		g_release = false;
		overlay::ReleaseGameTextures(kPicturesPrefix);
	}
	char command[256];
	if (robot::TakePanelCommand(command, sizeof(command), "skillwheel "))
		RobotCommand(command);
	g_on_hero_screen = g_button && !overlay::AnyModalOpen() && !GameHasMmh55() && HeroScreenOnTop();
	if (g_on_hero_screen)
		ButtonInput(g_back_w, g_back_h);
	else
		g_button_down = g_button_hovered = false;
	if (g_key && overlay::KeyPressed(g_key, &g_key_down)) {
		if (overlay::ModalOpen(kName))
			Close("its key");
		else
			OpenPanel("its key");
	}
	return overlay::ModalOpen(kName) || g_on_hero_screen;
}

void Prepare(const overlay::Frame &frame)
{
	ui::LoadPictures(frame.device);
	const float k = frame.height / kRefHeight;
	for (float size : {kTitleFont, kButtonFont, kBodyFont, kSmallFont, kBodyFont + 1, kBodyFont - 1})
		overlay::WantFont(Px(size, k));
	if (!overlay::ModalOpen(kName))
		return;
	int loads = 16;  // the pictures drawn (64x64 each, kept once read), several per frame
	const bool own = Own();
	for (const wheel::Row &row : g.rows) {
		const std::string &skill = Picture(SkillTexture(row, own));
		if (!skill.empty())
			overlay::GameTexture(frame.device, skill.c_str(), &loads);
		for (const wheel::Perk &perk : row.perks) {
			const std::string &dds = Picture(PerkTexture(row, perk, own));
			if (!dds.empty())
				overlay::GameTexture(frame.device, dds.c_str(), &loads);
		}
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
	g_button = GetPrivateProfileIntW(L"ui", L"skill_wheel", 1, path) != 0;
	wchar_t key[16];
	GetPrivateProfileStringW(L"ui", L"skill_wheel_key", L"none", key, 16, path);
	g_key = overlay::ParseKey(key, 0);
}

}  // namespace

void ReloadSkillWheelSettings()
{
	ReadSettings();
	hooklog::Write("skill wheel: from the settings menu: button %s, key %s", g_button ? "on" : "off",
	               g_key ? ("F" + std::to_string(g_key - VK_F1 + 1)).c_str() : "none");
}

void ApplySkillWheelFromSettings()
{
	ReadSettings();
	const overlay::Client client = {kName, &Wanted, &Prepare, &Draw};
	if (!overlay::AddClient(client)) {
		hooklog::Write("skill wheel: off (the overlay cannot draw in this game)");
		return;
	}
	overlay::EnableModalInput();
	g_installed = true;
	hooklog::Write("skill wheel: on (the hero screen's button: %s; key: %s)",
	               !g_button ? "off" : GameHasMmh55() ? "not with MMH5.5, which has its own" : "on",
	               g_key ? ("F" + std::to_string(g_key - VK_F1 + 1)).c_str() : "none");
}

}  // namespace mods
