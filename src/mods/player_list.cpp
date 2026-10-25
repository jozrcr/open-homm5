// Mod: the player list overlay. A small panel on the adventure map's left edge
// with every player of the game: the hero's portrait, the player's name and
// colour, the team, and the turn status (playing, turn ended, AI thinking,
// waiting), with the day and how many humans are done. Made for co-op games
// with simultaneous turns, where everyone wants to know who still plays. See
// src/mods/README.md and docs/subsystems/players.md.
//
// Display only: it reads the game's players (player_list_game.cpp) and draws on
// the finished frame; it never changes the game, so it cannot desync a game,
// and each player can have it on or off.
//
// How it draws: Direct3D 9 hooks (src/hook/d3d9_hook.cpp) call us just before
// each frame is shown (IDirect3DDevice9::Present); we draw with Dear ImGui
// (src/hook/third_party/imgui, its DirectX 9 renderer) between our own
// BeginScene/EndScene, only while the adventure map is the screen on top. On a
// device reset (fullscreen switch, alt-tab) ImGui's Direct3D objects are
// released before and made again after; the portraits live in D3DPOOL_MANAGED.
//
// Input: none is taken from the game. The panel ignores the mouse (it only
// fades while the mouse is over it, read with GetCursorPos) and the only key
// is the toggle (F9 by default), read with GetAsyncKeyState while the game's
// window is in front, so the game still gets every key and click.
//
// Settings (open-homm5.ini):
//   [ui]
//   player_list=1          1 = the overlay, 0 = nothing is installed
//   player_list_key=F9     the key that hides / shows it (F1..F12, or none)
//   player_list_top=34     where it starts, in % of the screen height
//   player_list_left=1.5   its distance from the left edge, in % of the screen width
//   language=auto          its words: auto (the game's language), en, fr
//   trace_players=0        testing: 1 = log each change of a player's status,
//                          2 = also raw dumps of the game's data (research)
#include "player_list.h"

#include <windows.h>
#include <d3d9.h>
#include <stdlib.h>
#include <wchar.h>

#include <string>

#include "player_list_internal.h"

#include "../hook/d3d9_hook.h"
#include "../hook/log.h"
#include "../hook/third_party/imgui/imgui.h"
#include "../hook/third_party/imgui/backends/imgui_impl_dx9.h"

namespace mods {
namespace {

bool g_enabled = false;  // the setting
bool g_early = false;    // the Direct3D import went in at DLL load
bool g_visible = true;   // F9
int g_key = VK_F9;
int g_language = -1;     // -1 = auto (found on the first frame), else plist::Language
plist::Look g_look = {1.0f, 0.34f, 0.015f, plist::kEnglish, nullptr, nullptr};
int g_trace = 0;

// --- settings ----------------------------------------------------------------------
float Percent(const wchar_t *path, const wchar_t *key, float fallback, float low, float high)
{
	wchar_t text[32];
	GetPrivateProfileStringW(L"ui", key, L"", text, 32, path);
	if (!text[0])
		return fallback;
	const float value = static_cast<float>(_wtof(text));
	if (value < low || value > high) {
		hooklog::Write("player list: %ls=%ls is not between %.0f and %.0f: using %.1f", key, text, low, high, fallback);
		return fallback;
	}
	return value;
}

void ReadSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	g_enabled = GetPrivateProfileIntW(L"ui", L"player_list", 1, path) != 0;
	g_trace = static_cast<int>(GetPrivateProfileIntW(L"ui", L"trace_players", 0, path));
	wchar_t text[32];
	GetPrivateProfileStringW(L"ui", L"player_list_key", L"F9", text, 32, path);
	if (_wcsicmp(text, L"none") == 0 || text[0] == L'0') {
		g_key = 0;
	}
	else if ((text[0] == L'F' || text[0] == L'f') && _wtoi(text + 1) >= 1 && _wtoi(text + 1) <= 12) {
		g_key = VK_F1 + _wtoi(text + 1) - 1;
	}
	else {
		hooklog::Write("player list: player_list_key=%ls is not F1..F12 or none: using F9", text);
		g_key = VK_F9;
	}
	g_look.top = Percent(path, L"player_list_top", 34.0f, 0.0f, 90.0f) / 100.0f;
	g_look.left = Percent(path, L"player_list_left", 1.5f, 0.0f, 50.0f) / 100.0f;
	GetPrivateProfileStringW(L"ui", L"language", L"auto", text, 32, path);
	if (_wcsicmp(text, L"en") == 0)
		g_language = plist::kEnglish;
	else if (_wcsicmp(text, L"fr") == 0)
		g_language = plist::kFrench;
	else
		g_language = -1;
}

// The game's language: a text of the original game, the red player's name
// ("Joueur rouge" / "Red player"); else the language of Windows.
plist::Language GameLanguage()
{
	std::string text;
	if (plist::ReadGameFile("Text/Game/Player/Colors/red_player.txt", &text) && text.size() >= 4) {
		const wchar_t *w = reinterpret_cast<const wchar_t *>(text.data());
		const std::wstring words(w, text.size() / 2);
		const bool french = words.find(L"Joueur") != std::wstring::npos;
		hooklog::Write("player list: the game's texts are %s", french ? "French" : "not French: English");
		return french ? plist::kFrench : plist::kEnglish;
	}
	const bool french = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;
	hooklog::Write("player list: the game's language is unknown: %s, as Windows", french ? "French" : "English");
	return french ? plist::kFrench : plist::kEnglish;
}

// --- Dear ImGui ----------------------------------------------------------------------
IDirect3DDevice9 *g_imgui_device = nullptr;  // the device ImGui's renderer works with
float g_font_px = 0;                          // the size the fonts were made for
LARGE_INTEGER g_freq, g_last;
float g_rect[4] = {0, 0, 0, 0};  // the panel on the last frame

// A font of the system with Latin and Cyrillic letters: Segoe UI on Windows,
// Tahoma under Wine, Arial; ImGui's own font if none is there.
ImFont *AddFont(const wchar_t *const *names, int count, float px)
{
	static const ImWchar kRanges[] = {
		0x0020, 0x00ff,  // Basic Latin, Latin-1
		0x0100, 0x017f,  // Latin Extended-A
		0x0400, 0x052f,  // Cyrillic
		0x2000, 0x206f,  // punctuation (dashes, the ellipsis)
		0,
	};
	ImGuiIO &io = ImGui::GetIO();
	wchar_t dir[MAX_PATH];
	const UINT n = GetWindowsDirectoryW(dir, MAX_PATH);
	for (int i = 0; i < count && n; ++i) {
		wchar_t wide[MAX_PATH];
		lstrcpynW(wide, dir, MAX_PATH);
		lstrcpynW(wide + n, names[i], MAX_PATH - n);
		if (GetFileAttributesW(wide) == INVALID_FILE_ATTRIBUTES)
			continue;
		char utf8[MAX_PATH * 3];
		WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr);
		ImFontConfig config;
		config.OversampleH = 2;
		if (ImFont *font = io.Fonts->AddFontFromFileTTF(utf8, px, &config, kRanges)) {
			hooklog::Write("player list: font %ls, %.0f px", wide, px);
			return font;
		}
	}
	ImFontConfig config;
	config.SizePixels = px;
	hooklog::Write("player list: no system font found: ImGui's own font, %.0f px", px);
	return io.Fonts->AddFontDefault(&config);
}

void BuildFonts(float regular_px, float bold_px)
{
	ImGui::GetIO().Fonts->Clear();
	const wchar_t *regular[] = {L"\\Fonts\\segoeui.ttf", L"\\Fonts\\tahoma.ttf", L"\\Fonts\\arial.ttf"};
	const wchar_t *bold[] = {L"\\Fonts\\segoeuib.ttf", L"\\Fonts\\tahomabd.ttf", L"\\Fonts\\arialbd.ttf"};
	g_look.font = AddFont(regular, 3, regular_px);
	g_look.font_bold = AddFont(bold, 3, bold_px);
	g_font_px = regular_px;
}

bool EnsureImGui(IDirect3DDevice9 *device)
{
	if (!ImGui::GetCurrentContext()) {
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO &io = ImGui::GetIO();
		io.IniFilename = nullptr;  // no imgui.ini next to the game
		io.LogFilename = nullptr;
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
		QueryPerformanceFrequency(&g_freq);
		QueryPerformanceCounter(&g_last);
		hooklog::Write("player list: Dear ImGui %s ready", ImGui::GetVersion());
	}
	if (g_imgui_device != device) {
		if (g_imgui_device) {
			ImGui_ImplDX9_Shutdown();  // a new device: the old one's objects go
			plist::ReleasePortraits();
		}
		g_imgui_device = nullptr;
		if (!ImGui_ImplDX9_Init(device))
			return false;
		g_imgui_device = device;
		hooklog::Write("player list: drawing on device %p", device);
	}
	return true;
}

// The back buffer's size (what the panel is laid out in).
bool BackBufferSize(IDirect3DDevice9 *device, float *w, float *h)
{
	IDirect3DSurface9 *back = nullptr;
	if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back)
		return false;
	D3DSURFACE_DESC desc = {};
	const HRESULT hr = back->GetDesc(&desc);
	back->Release();
	if (FAILED(hr) || !desc.Width || !desc.Height)
		return false;
	*w = static_cast<float>(desc.Width);
	*h = static_cast<float>(desc.Height);
	return true;
}

// The mouse in back buffer pixels, or false when it is not over the game.
bool MouseInBackBuffer(float w, float h, float *x, float *y)
{
	HWND window = d3dhook::DeviceWindow();
	POINT p;
	RECT client;
	if (!window || !GetCursorPos(&p) || !ScreenToClient(window, &p) || !GetClientRect(window, &client) ||
	    client.right <= 0 || client.bottom <= 0)
		return false;
	*x = p.x * w / client.right;
	*y = p.y * h / client.bottom;
	return true;
}

// --- the toggle key ------------------------------------------------------------------
void PollKey()
{
	static bool was_down = false;
	if (!g_key)
		return;
	HWND window = d3dhook::DeviceWindow();
	const HWND front = GetForegroundWindow();
	const bool ours = window && front &&
	                  (front == window || GetAncestor(front, GA_ROOTOWNER) == GetAncestor(window, GA_ROOTOWNER));
	const bool down = ours && (GetAsyncKeyState(g_key) & 0x8000) != 0;
	if (down && !was_down) {
		g_visible = !g_visible;
		hooklog::Write("player list: %s (key)", g_visible ? "shown" : "hidden");
	}
	was_down = down;
}

// --- the frame -------------------------------------------------------------------------
// Off the adventure map for a while (main menu, a battle...): our Direct3D
// objects go, so that we hold nothing of the game's device when it is not needed
// (ImGui's renderer also holds a reference to the device).
void ReleaseDeviceObjects()
{
	if (!g_imgui_device)
		return;
	ImGui_ImplDX9_Shutdown();
	plist::ReleasePortraits();
	g_imgui_device = nullptr;
}

bool DrawFrame(IDirect3DDevice9 *device)
{
	plist::Snapshot snapshot;
	if (!plist::ReadSnapshot(&snapshot))
		return false;
	if (device->TestCooperativeLevel() != D3D_OK)
		return false;  // lost (fullscreen, alt-tab): nothing can be drawn now
	float w, h;
	if (!BackBufferSize(device, &w, &h) || !EnsureImGui(device))
		return false;
	if (g_language < 0)
		g_language = GameLanguage();
	g_look.language = static_cast<plist::Language>(g_language);
	int loads = 2;  // portrait files read per frame at most (each ~0.1-1 ms)
	for (int i = 0; i < snapshot.count; ++i)
		snapshot.rows[i].face_texture = plist::Portrait(device, snapshot.rows[i].face, &loads);

	ImGuiIO &io = ImGui::GetIO();
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	const float dt = static_cast<float>(now.QuadPart - g_last.QuadPart) / static_cast<float>(g_freq.QuadPart);
	g_last = now;
	io.DeltaTime = dt > 0.0f && dt < 1.0f ? dt : 1.0f / 60.0f;
	io.DisplaySize = ImVec2(w, h);

	float regular_px, bold_px;
	plist::FontSizes(h, &regular_px, &bold_px);
	if (regular_px != g_font_px) {
		BuildFonts(regular_px, bold_px);
		ImGui_ImplDX9_InvalidateDeviceObjects();  // the font texture is made again
	}
	// Almost invisible while the mouse is over it, so it never hides the map.
	float mx, my;
	const bool over = MouseInBackBuffer(w, h, &mx, &my) && g_rect[2] > g_rect[0] && mx >= g_rect[0] - 4 &&
	                  mx <= g_rect[2] + 4 && my >= g_rect[1] - 4 && my <= g_rect[3] + 4;
	const float target = over ? 0.12f : 1.0f;
	const float step = io.DeltaTime * 8.0f;
	g_look.alpha += (target - g_look.alpha) * (step < 1.0f ? step : 1.0f);

	ImGui_ImplDX9_NewFrame();
	ImGui::NewFrame();
	plist::DrawPanel(snapshot, g_look, g_rect);
	ImGui::Render();
	if (SUCCEEDED(device->BeginScene())) {
		ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
		device->EndScene();
	}
	static bool first = true;
	if (first) {
		first = false;
		hooklog::Write("player list: first frame drawn (%.0fx%.0f, %d players)", w, h, snapshot.count);
	}
	return true;
}

void OnPresent(IDirect3DDevice9 *device)
{
	static DWORD last_drawn = 0;
	PollKey();
	if (g_visible && DrawFrame(device))
		last_drawn = GetTickCount();
	else if (g_imgui_device && GetTickCount() - last_drawn > 10000)
		ReleaseDeviceObjects();
}

void OnReset(IDirect3DDevice9 *device, bool after, bool ok)
{
	(void)ok;
	if (!after && device == g_imgui_device)
		ImGui_ImplDX9_InvalidateDeviceObjects();  // made again on the next frame
}

}  // namespace

void InstallPlayerListEarly()
{
	ReadSettings();
	if (!g_enabled)
		return;
	g_early = d3dhook::Install(false);
}

void ApplyPlayerListFromSettings()
{
	if (!g_early)
		ReadSettings();
	if (!g_enabled) {
		hooklog::Write("player list: off (player_list=0)");
		return;
	}
	plist::SetTrace(g_trace);
	if (!plist::InstallGameHooks())
		return;
	plist::InstallPortraits();
	if (!d3dhook::Install(!g_early))
		return;
	d3dhook::AddPresentCallback(&OnPresent, 0);
	d3dhook::AddResetCallback(&OnReset);
	if (g_key)
		hooklog::Write("player list: on (F%d hides / shows it)", g_key - VK_F1 + 1);
	else
		hooklog::Write("player list: on (no key)");
}

}  // namespace mods
