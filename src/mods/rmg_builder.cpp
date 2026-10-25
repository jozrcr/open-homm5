// Mod: the template builder, a modal panel over the game's random map screen
// (NUI::CRMGScreen, the same screen in single player and multiplayer). The
// plan: docs/plans/in-game-generator.md. The panel will hold every choice of
// the web template maker (tools/rmg/template-maker.html); "Use template" will
// fill in the game's own screen.
//
// Now (build step 5, first part): the empty panel. It covers the screen's main
// panel exactly: 649x508 in the game's 1024x768 layout, centred, scaled with
// the screen height like the rest of the game's interface (the screen's window
// is centred: UI/RMGScreen/RMGScreen.(WindowScreen).xdb, ALIGN_CENTER). It is
// drawn with the game's own pictures, read from its data through the overlay:
//   UI/RMGScreen/MainBg.(Texture).dds          the screen's background (1024x512,
//                                              its top-left 649x508, as the game draws it);
//   UI/Common/DlgButtons/Button_122_34_*.dds   the game's buttons (Normal, the
//                                              Highlight drawn over it under the mouse, Click);
//   UI/Common/CheckBox/CheckBox_{0,1}_*.dds    check boxes (loaded now, used in step 6).
// Its one button, Cancel, sits where the screen's Back button is (491, 430).
// Until the native "Template..." button exists (served UI files, later), F11
// opens and closes it, only while the random map screen is on top.
//
// It is a modal panel of the overlay (src/hook/overlay.h): while it is open the
// game gets no mouse or keyboard input; Esc closes it; it closes by itself when
// the random map screen is no longer on top, or when no frame was drawn for 2 s.
//
// Settings (open-homm5.ini):
//   [rmg]
//   builder=1      0 = no panel (nothing is installed)
#include "rmg_builder.h"

#include <windows.h>
#include <math.h>
#include <string.h>

#include "../hook/log.h"
#include "../hook/overlay.h"
#include "../hook/third_party/imgui/imgui.h"

namespace mods {
namespace {

const char kName[] = "rmg builder";
const char kScreenClass[] = ".?AVCRMGScreen@NUI@@";  // NUI::CRMGScreen (vtable 0x00e41114)
const int kKey = VK_F11;  // for now: the native button comes later

bool g_enabled = false;
bool g_early = false;
bool g_key_down = false;
bool g_on_screen = false;

// --- the game's layout (1024x768 units, from the panel's top-left corner) ----------------
const float kRefHeight = 768.0f;
const float kPanelW = 649.0f, kPanelH = 508.0f;          // MainPanel
const float kTitleX = 41.0f, kTitleY = 36.0f, kTitleW = 572.0f, kTitleH = 36.0f;  // Title
const float kButtonW = 122.0f, kButtonH = 34.0f;           // Button_122_34
const float kButtonY = 430.0f;
const float kCancelX = 491.0f;                             // where BackBtn is
const float kTitleFont = 20.0f, kButtonFont = 15.0f;
// MainBg.(Texture).dds is 1024x512; the game draws it 1:1 from its top-left.
const float kBackgroundTexW = 1024.0f, kBackgroundTexH = 512.0f;

// --- the pictures --------------------------------------------------------------------------
enum Picture { kBackground, kButtonNormal, kButtonHighlight, kButtonClick, kButtonDisabled, kCheck0, kCheck1, kPictures };
const char *const kPicturePaths[kPictures] = {
	"UI/RMGScreen/MainBg.(Texture).dds",
	"UI/Common/DlgButtons/Button_122_34_Normal.(Texture).dds",
	"UI/Common/DlgButtons/Button_122_34_Highlight.(Texture).dds",
	"UI/Common/DlgButtons/Button_122_34_Click.(Texture).dds",
	"UI/Common/DlgButtons/Button_122_34_Disabled.(Texture).dds",
	"UI/Common/CheckBox/CheckBox_0_Normal.(Texture).dds",
	"UI/Common/CheckBox/CheckBox_1_Normal.(Texture).dds",
};
void *g_pictures[kPictures];
bool g_told[kPictures];

// --- words -----------------------------------------------------------------------------------
struct Words {
	const char *title;
	const char *cancel;
};
const Words kWords[] = {
	{"Template builder", "Cancel"},                                      // English
	{"Cr\xc3\xa9" "ateur de mod\xc3\xa8" "les", "Annuler"},              // French (UTF-8)
};

// --- drawing ---------------------------------------------------------------------------------
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

ImTextureID Tex(void *picture)
{
	return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(picture));
}

// Text with a dark shadow one pixel down and right, as the game's.
void ShadowText(ImDrawList *draw, ImFont *font, float size, ImVec2 p, ImU32 colour, const char *text)
{
	p.x = floorf(p.x + 0.5f);
	p.y = floorf(p.y + 0.5f);
	draw->AddText(font, size, ImVec2(p.x + 1, p.y + 1), IM_COL32(0, 0, 0, 220), text);
	draw->AddText(font, size, p, colour, text);
}

void CentredText(ImDrawList *draw, ImFont *font, float size, ImVec2 p0, ImVec2 p1, ImU32 colour, const char *text)
{
	const ImVec2 t = font->CalcTextSizeA(size, 10000.0f, 0.0f, text);
	ShadowText(draw, font, size, ImVec2((p0.x + p1.x - t.x) / 2, (p0.y + p1.y - t.y) / 2), colour, text);
}

// A button of the game (Button_122_34): its picture by state, its label in the
// middle. True when clicked (on release, as the game's: BCST_ON_RELEASE).
bool GameButton(ImDrawList *draw, const Layout &l, const char *id, const char *label, float x, float y)
{
	const ImVec2 p0 = l.At(x, y), p1 = l.At(x + kButtonW, y + kButtonH);
	ImGui::SetCursorScreenPos(p0);
	const bool clicked = ImGui::InvisibleButton(id, ImVec2(p1.x - p0.x, p1.y - p0.y));
	const bool hovered = ImGui::IsItemHovered();
	const bool pressed = hovered && ImGui::IsItemActive();
	void *picture = g_pictures[pressed ? kButtonClick : kButtonNormal];
	if (picture)
		draw->AddImage(Tex(picture), p0, p1);
	else
		draw->AddRectFilled(p0, p1, pressed ? IM_COL32(70, 52, 30, 255) : IM_COL32(96, 72, 40, 255), 4.0f * l.k);
	if (hovered && !pressed) {
		if (g_pictures[kButtonHighlight])
			draw->AddImage(Tex(g_pictures[kButtonHighlight]), p0, p1);
		else
			draw->AddRect(p0, p1, IM_COL32(240, 208, 128, 255), 4.0f * l.k, 0, 1.5f * l.k);
	}
	const float size = Px(kButtonFont, l.k);
	const float down = pressed ? l.k : 0.0f;
	CentredText(draw, overlay::Font(size), size, ImVec2(p0.x, p0.y + down), ImVec2(p1.x, p1.y + down),
	            IM_COL32(255, 236, 190, 255), label);
	return clicked;
}

// --- the overlay client ----------------------------------------------------------------------
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
		               on_screen ? " (the random map screen: F11 opens the panel)" : "");
	}
	g_on_screen = on_screen;
	if (overlay::KeyPressed(kKey, &g_key_down) && on_screen) {
		if (overlay::ModalOpen(kName)) {
			overlay::CloseModal(kName);
		}
		else if (overlay::OpenModal(kName)) {
			hooklog::Write("rmg builder: panel opened (F11)");
		}
	}
	return overlay::ModalOpen(kName);
}

void Prepare(const overlay::Frame &frame)
{
	int loads = 2;  // picture files read per frame at most
	for (int i = 0; i < kPictures; ++i) {
		bool failed = false;
		g_pictures[i] = overlay::GameTexture(frame.device, kPicturePaths[i], &loads, &failed);
		if (failed && !g_told[i]) {
			g_told[i] = true;
			hooklog::Write("rmg builder: the game's picture %s is not readable: plain style", kPicturePaths[i]);
		}
	}
	const float k = frame.height / kRefHeight;
	overlay::WantFont(Px(kTitleFont, k));
	overlay::WantFont(Px(kButtonFont, k));
}

void Draw(const overlay::Frame &frame)
{
	const Layout l = LayoutFor(frame.width, frame.height);
	const Words &words = kWords[overlay::PanelLanguage()];
	// The game behind, dimmed: it gets no input while the panel is open.
	ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(frame.width, frame.height),
	                                              IM_COL32(0, 0, 0, 115));
	const ImVec2 p0 = l.At(0, 0), p1 = l.At(kPanelW, kPanelH);
	ImGui::SetNextWindowPos(p0, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::Begin("##rmg_builder", nullptr,
	             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
	                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
	ImDrawList *draw = ImGui::GetWindowDrawList();
	if (g_pictures[kBackground])
		draw->AddImage(Tex(g_pictures[kBackground]), p0, p1, ImVec2(0, 0),
		               ImVec2(kPanelW / kBackgroundTexW, kPanelH / kBackgroundTexH));
	else {
		draw->AddRectFilled(p0, p1, IM_COL32(30, 24, 18, 248), 8.0f * l.k);
		draw->AddRect(p0, p1, IM_COL32(150, 116, 70, 255), 8.0f * l.k, 0, 2.0f * l.k);
	}
	const float title = Px(kTitleFont, l.k);
	CentredText(draw, overlay::Font(title), title, l.At(kTitleX, kTitleY), l.At(kTitleX + kTitleW, kTitleY + kTitleH),
	            IM_COL32(240, 208, 128, 255), words.title);
	if (GameButton(draw, l, "##cancel", words.cancel, kCancelX, kButtonY)) {
		hooklog::Write("rmg builder: Cancel");
		overlay::CloseModal(kName);
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
	static bool first = true;
	if (first) {
		first = false;
		hooklog::Write("rmg builder: first frame drawn (%.0fx%.0f, panel at %.0f,%.0f, %.2f pixels per layout unit)",
		               frame.width, frame.height, l.x0, l.y0, l.k);
	}
}

void ReadSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	g_enabled = GetPrivateProfileIntW(L"rmg", L"builder", 1, path) != 0;
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
	const overlay::Client client = {kName, &Wanted, &Prepare, &Draw};
	if (!overlay::AddClient(client))
		return;
	overlay::EnableModalInput();
	hooklog::Write("rmg builder: on (for now F11 opens it on the random map screen)");
}

}  // namespace mods
