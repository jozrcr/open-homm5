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
// How it draws: it is a client of the overlay (src/hook/overlay.h), shared with
// the other panels: Dear ImGui over the game's frames, the fonts, the pictures
// from the game's data (the portraits, the resource bar's frame) and the
// screen on top. It draws only while the adventure map is the screen on top.
//
// Input: none is taken from the game (it is not a modal panel). The panel
// ignores the mouse (it only fades while the mouse is over it, read with
// GetCursorPos) and the only key is the toggle (F9 by default), read with
// GetAsyncKeyState while the game's window is in front, so the game still gets
// every key and click.
//
// Settings (open-homm5.ini):
//   [ui]
//   player_list=1          1 = the overlay, 0 = nothing is installed
//   player_list_key=F9     the key that hides / shows it (F1..F12, or none)
//   player_list_top=34     where it starts, in % of the screen height
//   player_list_left=2     its distance from the left edge, in % of the screen height
//                          (the game's interface scales with the height)
//   language=auto          its words: auto (the game's language), en, fr
//   trace_players=0        testing: 1 = log each change of a player's status,
//                          2 = also raw dumps of the game's data (research)
//   player_list_test_rows=0  testing: show N rows (copies of the real players,
//                          in teams of two), to check the layout with 8 players
#include "player_list.h"

#include <windows.h>
#include <stdlib.h>
#include <wchar.h>


#include "player_list_internal.h"

#include "../hook/log.h"
#include "../hook/overlay.h"

namespace mods {
namespace {

bool g_enabled = false;  // the setting
bool g_early = false;    // the settings were read at DLL load
bool g_visible = true;   // F9
int g_key = VK_F9;
plist::Look g_look = {1.0f, 0.34f, 0.02f, plist::kEnglish, nullptr, nullptr, nullptr};
int g_trace = 0;
int g_test_rows = 0;     // testing: show this many rows (copies of the real ones)

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
	g_test_rows = static_cast<int>(GetPrivateProfileIntW(L"ui", L"player_list_test_rows", 0, path));
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
	g_look.left = Percent(path, L"player_list_left", 2.0f, 0.0f, 100.0f) / 100.0f;
	// ([ui] language is read by the overlay: overlay::PanelLanguage)
}

// --- the toggle key ------------------------------------------------------------------
void PollKey()
{
	static bool was_down = false;
	if (!g_key)
		return;
	HWND window = overlay::GameWindow();
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

// Testing the layout (player_list_test_rows=N): N rows, copies of the real
// ones, in teams of two (the tallest list: 8 players in 4 teams).
void AddTestRows(plist::Snapshot *s)
{
	const int real = s->count;
	for (int i = 0; i < g_test_rows && i < plist::kMaxPlayers; ++i) {
		plist::Row &r = s->rows[i];
		if (i >= real) {
			r = s->rows[i % real];
			r.id = i + 1;
			r.colour = i + 1;
			r.status = static_cast<plist::Status>(i % 4);
			_snprintf_s(r.name, sizeof(r.name), _TRUNCATE, i % 3 ? "Test %d" : "Test player number %d", i + 1);
		}
		r.team = i / 2;
	}
	s->count = g_test_rows < plist::kMaxPlayers ? g_test_rows : plist::kMaxPlayers;
	s->teams = s->count > 1;
}

// --- the overlay client -------------------------------------------------------------------
plist::Snapshot g_snapshot;      // this frame's
float g_font_px = 0, g_bold_px = 0;
float g_rect[4] = {0, 0, 0, 0};  // the panel on the last frame

// Every frame: drawn while the adventure map is on top (and F9 did not hide it).
bool Wanted()
{
	PollKey();
	return g_visible && plist::ReadSnapshot(&g_snapshot);
}

void Prepare(const overlay::Frame &frame)
{
	plist::Snapshot &snapshot = g_snapshot;
	if (g_test_rows > snapshot.count && snapshot.count > 0)
		AddTestRows(&snapshot);
	g_look.language = static_cast<plist::Language>(overlay::PanelLanguage());
	int loads = 2;  // picture files read per frame at most (each ~0.1-1 ms)
	bool plain = false;
	g_look.frame = overlay::GameTexture(frame.device, plist::kFramePicture, &loads, &plain);
	static bool told = false;
	if (plain && !told) {
		told = true;
		hooklog::Write("player list: the game's resource bar picture (%s) is not readable: plain style",
		               plist::kFramePicture);
	}
	for (int i = 0; i < snapshot.count; ++i)
		snapshot.rows[i].face_texture = overlay::GameTexture(frame.device, snapshot.rows[i].face, &loads);

	plist::FontSizes(frame.height, &g_font_px, &g_bold_px);
	overlay::WantFont(g_font_px);
	overlay::WantFont(g_bold_px);
	// Almost invisible while the mouse is over it, so it never hides the map.
	float mx, my;
	const bool over = overlay::MouseInBackBuffer(frame.width, frame.height, &mx, &my) && g_rect[2] > g_rect[0] &&
	                  mx >= g_rect[0] - 4 && mx <= g_rect[2] + 4 && my >= g_rect[1] - 4 && my <= g_rect[3] + 4;
	const float target = over ? 0.12f : 1.0f;
	const float step = frame.delta * 8.0f;
	g_look.alpha += (target - g_look.alpha) * (step < 1.0f ? step : 1.0f);
}

void Draw(const overlay::Frame &frame)
{
	g_look.font = overlay::Font(g_font_px);
	g_look.font_bold = overlay::Font(g_bold_px);
	plist::DrawPanel(g_snapshot, g_look, g_rect);
	static bool first = true;
	if (first) {
		first = false;
		hooklog::Write("player list: first frame drawn (%.0fx%.0f, %d players)", frame.width, frame.height,
		               g_snapshot.count);
	}
}

}  // namespace

void InstallPlayerListEarly()
{
	ReadSettings();
	if (!g_enabled)
		return;
	g_early = true;
	overlay::InstallEarly();
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
	const overlay::Client client = {"player list", &Wanted, &Prepare, &Draw};
	if (!overlay::AddClient(client))
		return;
	if (g_key)
		hooklog::Write("player list: on (F%d hides / shows it)", g_key - VK_F1 + 1);
	else
		hooklog::Write("player list: on (no key)");
}

}  // namespace mods
