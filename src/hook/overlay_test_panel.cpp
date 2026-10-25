// The overlay test panel, for testing by hand only ([ui] overlay_test_panel=1).
// F10 (or [ui] overlay_test_key) opens a small modal panel over any screen,
// with buttons, a check box and a text line, and shows what it receives: the
// mouse, the wheel, the keys. It checks that a modal panel gets the mouse and
// the keyboard and the game behind it gets nothing, on Windows and under
// Wine/Proton (the robot cannot click: its game runs in the background). Each
// click is also written to open-homm5.log.
//
// On the adventure map F10 is also the game's own menu key: the game sees the
// press before the panel opens, opens its menu, and the panel closes at once
// (another screen is on top: the safety net at work). Test on another screen
// (main menu, random map screen), or choose another key.
#include <math.h>
#include <stdio.h>

#include "log.h"
#include "overlay.h"
#include "overlay_internal.h"
#include "third_party/imgui/imgui.h"

namespace overlay {
namespace {

const char kName[] = "overlay test panel";
bool g_on = false;
int g_key = VK_F10;
bool g_key_down = false;
int g_clicks[3] = {0, 0, 0};
bool g_checked = false;
char g_text[64] = "";
float g_wheel = 0;

void ReadSettings()
{
	wchar_t path[MAX_PATH], text[32];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	g_on = GetPrivateProfileIntW(L"ui", L"overlay_test_panel", 0, path) != 0;
	GetPrivateProfileStringW(L"ui", L"overlay_test_key", L"F10", text, 32, path);
	g_key = ParseKey(text, VK_F10);
	if (!g_key)
		g_key = VK_F10;
}

float FontPx(float screen_height)
{
	return floorf(15.0f * screen_height / 768.0f + 0.5f);
}

bool Wanted()
{
	if (KeyPressed(g_key, &g_key_down)) {
		if (ModalOpen(kName))
			CloseModal(kName);
		else
			OpenModal(kName);
	}
	return ModalOpen(kName);
}

void Prepare(const Frame &frame)
{
	WantFont(FontPx(frame.height));
}

void Draw(const Frame &frame)
{
	const float k = frame.height / 768.0f;
	ImGuiIO &io = ImGui::GetIO();
	ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(frame.width, frame.height), IM_COL32(0, 0, 0, 110));
	ImGui::PushFont(Font(FontPx(frame.height)));
	ImGui::SetNextWindowPos(ImVec2(frame.width / 2, frame.height / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(440.0f * k, 0.0f), ImGuiCond_Always);
	ImGui::Begin("open-homm5: overlay test", nullptr,
	             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
	                 ImGuiWindowFlags_NoSavedSettings);
	ImGui::TextWrapped("The game behind this panel must get no mouse click, no wheel and no key. Esc or F%d closes it.",
	                   g_key - VK_F1 + 1);
	ImGui::Separator();
	for (int i = 0; i < 3; ++i) {
		char label[32];
		_snprintf_s(label, sizeof(label), _TRUNCATE, "Button %d", i + 1);
		if (i)
			ImGui::SameLine();
		if (ImGui::Button(label)) {
			++g_clicks[i];
			hooklog::Write("overlay test panel: button %d clicked (%d times)", i + 1, g_clicks[i]);
		}
	}
	ImGui::Text("Clicks: %d, %d, %d", g_clicks[0], g_clicks[1], g_clicks[2]);
	if (ImGui::Checkbox("A check box", &g_checked))
		hooklog::Write("overlay test panel: check box %s", g_checked ? "ticked" : "cleared");
	ImGui::InputText("Type here", g_text, sizeof(g_text));
	g_wheel += io.MouseWheel;
	ImGui::Text("Mouse: %.0f, %.0f   wheel: %.0f", io.MousePos.x, io.MousePos.y, g_wheel);
	char keys[160] = "";
	for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
		if (ImGui::IsKeyDown(static_cast<ImGuiKey>(key))) {
			strncat_s(keys, sizeof(keys), ImGui::GetKeyName(static_cast<ImGuiKey>(key)), _TRUNCATE);
			strncat_s(keys, sizeof(keys), " ", _TRUNCATE);
		}
	}
	ImGui::Text("Keys down: %s", keys[0] ? keys : "(none)");
	ImGui::TextWrapped("%s", internal::InputInfo());
	if (ImGui::Button("Close"))
		CloseModal(kName);
	ImGui::End();
	ImGui::PopFont();
}

}  // namespace

void InstallTestPanelEarly()
{
	ReadSettings();
	if (g_on)
		InstallEarly();
}

void ApplyTestPanelFromSettings()
{
	ReadSettings();
	if (!g_on)
		return;
	const Client client = {kName, &Wanted, &Prepare, &Draw};
	if (!AddClient(client))
		return;
	EnableModalInput();
	hooklog::Write("overlay test panel: on (F%d opens it; testing only)", g_key - VK_F1 + 1);
}

}  // namespace overlay
