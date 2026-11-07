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
// Input: it is not a modal panel. It fades while the mouse is over it (read with
// GetCursorPos), and the only key is the toggle (F9 by default), read with
// GetAsyncKeyState while the game's window is in front. Only the rows of allies
// whose view this PC may follow (spectate.cpp) take clicks: a click there starts or
// stops following that ally's view, and the game does not get it
// (overlay::BlockClicksIn). Everything else passes to the game.
//
// Settings (h5-together.ini):
//   [ui]
//   player_list=1          1 = the overlay, 0 = nothing is installed
//   player_list_key=F9     the key that hides / shows it (F1..F12, or none)
//   player_list_top=34     where it starts, in % of the screen height
//   player_list_left=2     its distance from the left edge, in % of the screen height
//                          (the game's interface scales with the height)
//   language=auto          its words (and every panel's): auto (the game's language),
//                          or a language code with a texts file: en, fr... (src/hook/texts.h)
//   trace_players=0        testing: 1 = log each change of a player's status,
//                          2 = also raw dumps of the game's data (research)
//   player_list_test_rows=0  testing: show N rows (copies of the real players,
//                          in teams of two), to check the layout with 8 players
#include "player_list.h"

#include <windows.h>
#include <stdlib.h>
#include <wchar.h>


#include "player_list_internal.h"
#include "spectate.h"

#include "../hook/log.h"
#include "../hook/overlay.h"

namespace mods {
namespace {

bool g_enabled = false;  // the setting
bool g_early = false;    // the settings were read at DLL load
bool g_visible = true;   // F9
int g_key = VK_F9;
plist::Look g_look = {1.0f, 0.34f, 0.02f, nullptr, nullptr, nullptr};
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
	hooklog::PathNextToExe(L"h5-together", L".ini", path, MAX_PATH);
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
	// ([ui] language is read by the texts: src/hook/texts.h)
}

// From the settings menu: its key and place again (the main thread draws with them).
void ReadKeyAndPlace()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together", L".ini", path, MAX_PATH);
	wchar_t text[32];
	GetPrivateProfileStringW(L"ui", L"player_list_key", L"F9", text, 32, path);
	g_key = overlay::ParseKey(text, VK_F9);
	g_look.top = Percent(path, L"player_list_top", 34.0f, 0.0f, 90.0f) / 100.0f;
	g_look.left = Percent(path, L"player_list_left", 2.0f, 0.0f, 100.0f) / 100.0f;
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

// Following a friend's view (spectate.cpp): the rows' eyes, and clicks on allies' rows.
plist::Hits g_hits = {};
float g_back_w = 0, g_back_h = 0;
int g_hover = 0;    // the followable player under the cursor, 0 = none
int g_pressed = 0;  // the row the button went down on
int g_sword_hover = 0, g_sword_pressed = 0;  // the same for a row's sword (an ally in a battle)
bool g_button = false;
bool g_shown_now = false;  // the list is drawn this frame (its rows' places are current)
const char *const kRowOwners[plist::kMaxPlayers] = {"player list row 1", "player list row 2", "player list row 3",
                                                    "player list row 4", "player list row 5", "player list row 6",
                                                    "player list row 7", "player list row 8"};

void Spectate(plist::Snapshot *s)
{
	g_hover = 0;
	float x = 0, y = 0;
	HWND window = overlay::GameWindow();
	const bool front = window && GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) == GetAncestor(window, GA_ROOTOWNER);
	const bool cursor = front && g_back_w > 0 && overlay::CursorInBackBuffer(g_back_w, g_back_h, &x, &y);
	g_sword_hover = 0;
	for (int i = 0; i < g_hits.count; ++i) {
		const float *r = g_hits.rect[i];
		if (cursor && x >= r[0] && x < r[2] && y >= r[1] && y < r[3] && SpectateCanFollow(g_hits.id[i]))
			g_hover = g_hits.id[i];
		const float *sw = g_hits.sword[i];
		if (cursor && sw[2] > sw[0] && x >= sw[0] && x < sw[2] && y >= sw[1] && y < sw[3] &&
		    SpectateInBattle(g_hits.id[i]))
			g_sword_hover = g_hits.id[i];
	}
	const bool down = front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (down && !g_button) {
		g_pressed = g_hover;
		g_sword_pressed = g_sword_hover;
	}
	else if (!down && g_button) {
		if (g_sword_pressed && g_sword_pressed == g_sword_hover)
			SpectateWatchBattle(g_sword_pressed, "a click on the sword in the player list");
		else if (g_pressed && g_pressed == g_hover)
			SpectateToggle(g_pressed, "a click on the player list");
		g_pressed = g_sword_pressed = 0;
	}
	g_button = down;
	const int followed = SpectateFollowing();
	for (int i = 0; i < s->count; ++i) {
		plist::Row &row = s->rows[i];
		if (row.human)
			SpectateNoteName(row.id, row.name);
		row.eye = row.id == followed            ? plist::kEyeFollowed
		          : SpectateFollowsMe(row.id)   ? plist::kEyeFollowsMe
		          : row.id == g_hover           ? plist::kEyeHover
		                                        : plist::kEyeNone;
		row.sword = !SpectateInBattle(row.id) ? plist::kSwordNone
		            : row.id == g_sword_hover  ? plist::kSwordHover
		                                       : plist::kSwordShown;
	}
}

// Every frame: drawn while the adventure map is on top (and F9 did not hide it).
bool Wanted()
{
	PollKey();
	if (!g_visible || !plist::ReadSnapshot(&g_snapshot)) {
		g_hits.count = 0;
		g_hover = g_pressed = 0;
		g_shown_now = false;
		return false;
	}
	Spectate(&g_snapshot);
	g_shown_now = true;
	return true;
}

void Prepare(const overlay::Frame &frame)
{
	plist::Snapshot &snapshot = g_snapshot;
	if (g_test_rows > snapshot.count && snapshot.count > 0)
		AddTestRows(&snapshot);
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
	bool sword = g_look.sword != nullptr;  // (read the first time an ally is in a battle)
	for (int i = 0; i < snapshot.count && !sword; ++i)
		sword = snapshot.rows[i].sword != plist::kSwordNone;
	if (sword)
		g_look.sword = overlay::GameTexture(frame.device, plist::kSwordPicture, &loads);

	plist::FontSizes(frame.height, &g_font_px, &g_bold_px);
	overlay::WantFont(g_font_px);
	overlay::WantFont(g_bold_px);
	// Almost invisible while the mouse is over it, so it never hides the map.
	float mx, my;
	const bool over = overlay::MouseInBackBuffer(frame.width, frame.height, &mx, &my) && g_rect[2] > g_rect[0] &&
	                  mx >= g_rect[0] - 4 && mx <= g_rect[2] + 4 && my >= g_rect[1] - 4 && my <= g_rect[3] + 4;
	const float target = over && !g_hover ? 0.12f : 1.0f;  // (an ally's row under the cursor: a click follows)
	const float step = frame.delta * 8.0f;
	g_look.alpha += (target - g_look.alpha) * (step < 1.0f ? step : 1.0f);
}

void Draw(const overlay::Frame &frame)
{
	g_look.font = overlay::Font(g_font_px);
	g_look.font_bold = overlay::Font(g_bold_px);
	g_back_w = frame.width;
	g_back_h = frame.height;
	plist::DrawPanel(g_snapshot, g_look, g_rect, &g_hits);
	for (int i = 0; i < g_hits.count; ++i)
		if (SpectateCanFollow(g_hits.id[i]))
			overlay::BlockClicksIn(kRowOwners[i], g_hits.rect[i][0], g_hits.rect[i][1], g_hits.rect[i][2], g_hits.rect[i][3],
			                       frame.width, frame.height);
	static bool first = true;
	if (first) {
		first = false;
		hooklog::Write("player list: first frame drawn (%.0fx%.0f, %d players)", frame.width, frame.height,
		               g_snapshot.count);
	}
	static int swords_told = 0;  // (where the sword is: robot runs click it)
	static bool sword_before[plist::kMaxPlayers + 1] = {};
	for (int i = 0; i < g_hits.count; ++i) {
		const int id = g_hits.id[i];
		const bool shown = g_hits.sword[i][2] > g_hits.sword[i][0];
		if (id >= 1 && id <= plist::kMaxPlayers && shown != sword_before[id]) {
			sword_before[id] = shown;
			if (swords_told++ < 20)
				hooklog::Write("player list: player %d's sword %s (%.0f,%.0f - %.0f,%.0f)", id, shown ? "shown" : "gone",
				               g_hits.sword[i][0], g_hits.sword[i][1], g_hits.sword[i][2], g_hits.sword[i][3]);
		}
	}
}

}  // namespace

bool PlayerListRowPlace(int player, float *right, float *top, float *bottom)
{
	if (!g_shown_now)
		return false;
	for (int i = 0; i < g_hits.count; ++i)
		if (g_hits.id[i] == player) {
			*right = g_hits.rect[i][2];
			*top = g_hits.pill[i][1];
			*bottom = g_hits.pill[i][3];
			return true;
		}
	return false;
}

void InstallPlayerListEarly()
{
	ReadSettings();
	if (!g_enabled)
		return;
	g_early = true;
	overlay::InstallEarly();
}

void ReloadPlayerListSettings()
{
	ReadKeyAndPlace();
	hooklog::Write("player list: from the settings menu: key %s, place %.0f%% from the top, %.0f%% from the left%s",
	               g_key ? ("F" + std::to_string(g_key - VK_F1 + 1)).c_str() : "none", g_look.top * 100.0f, g_look.left * 100.0f,
	               g_enabled ? "" : " (the list itself is off)");
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
