// Mod: the online UI (docs/plans/online-steam.md, step 4), with [online] mode=steam.
// With mode=off (the default) it draws nothing.
//
// It is an overlay client (src/hook/overlay.h) drawn with the game's own art, in
// the look of the waiting room's "Rules..." button and versions line
// (host_rules_panel.cpp) and of the template builder (rmg_builder_widgets.h):
//   - the LAN screen (NUI::CLobbyScreen): a few lines in the big empty box under
//     the games list (the ubi.com chat's box, empty in LAN play): whether Steam
//     works (and if not, why), how many Steam friends play with H5 Together, the
//     invites, and friends' games this PC cannot join (another MMH5.5 or H5
//     Together). The games themselves are in the game's own list above;
//   - the waiting room (NUI::CMPWaitScreen, or a loaded game's
//     NUI::CMPUbiComLoadScreen), for the host whose Steam lobby exists: an
//     "Invite friends..." button (next to "Rules..." in a new game's). It opens Steam's
//     own invite dialog (its overlay) when the overlay is enabled in this game;
//     else, or when the overlay did not open within 2.5 s, our own list of the
//     Steam friends who are online, each with an "Invite" button (Steam's
//     InviteUserToLobby);
//   - an invite (an invite accepted in Steam, "Join game" in Steam's friends list,
//     or Steam starting the game with "+connect_lobby"): a banner on the main
//     menu ("Pierre invites you to play: open Multiplayer > LAN") for 20 s, and,
//     when the game is not in front, the taskbar flash, the sound and the Windows
//     pop-up of notify.cpp. The friend's game then shows in the LAN list.
// Why drawn and not a served UI file (as the Options page, settings_menu.cpp): the
// lines change while the screen is open (a friend starts hosting, an invite comes),
// and the game reads a served text only when it builds the screen.
//
// Steam itself is only asked through virtual_lan.h (the core's lock; the Steam
// calls happen on the Steam transport's thread). No Steam name goes to the log.
//
// Robot steps (it cannot click): "panel|online invite" (as a click on the
// button), "panel|online picker" (our list, whatever the overlay), "panel|online
// close", "panel|online test_invite" and "panel|online pick <row>" (only with
// [online] ui_test=1: made-up friends, nothing sent to Steam).
#include "online_ui.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "notify.h"
#include "online_join.h"
#include "online_ui_logic.h"
#include "rmg_builder_widgets.h"
#include "virtual_lan.h"
#include "virtual_lan_transport.h"

#include "../hook/log.h"
#include "../hook/overlay.h"
#include "../hook/robot.h"
#include "../hook/texts.h"
#include "../hook/version.h"
#include "../hook/third_party/imgui/imgui.h"

namespace mods {
namespace {

namespace ui = builder_ui;

const char kName[] = "online";
const char kLobbyScreen[] = ".?AVCLobbyScreen@NUI@@";
const char kMainMenu[] = ".?AVCMainMenuScreen@NUI@@";
const char kNameBox[] = ".?AVCSimpleEditBox@NUI@@";  // the LAN screen's "Nom du joueur" box, a screen over it

// --- layout: the game's 1024x768 screens, centred and scaled with the height ---------------
const float kRefH = 768.0f, kScreenW = 1024.0f;
// The LAN screen's big box under the games list (its inside, left of its scroll bar:
// the box is about 70..664 x 363..612).
const float kStatusX = 84.0f, kStatusY = 372.0f, kStatusW = 566.0f, kStatusBottom = 606.0f;
const float kStatusTitleFont = 17.0f, kStatusFont = 14.0f, kStatusGap = 5.0f;
// The waiting room: our button right of "Rules..." (500..622), on the title row. A loaded
// game's waiting room has the same title row, without "Rules..." (robot run 20261104-074436).
const float kInviteX = 632.0f, kInviteY = 56.0f, kInviteW = 168.0f, kInviteH = 34.0f, kButtonFont = 15.0f;
// The main menu's banner: just above the menu's scroll (Solo, Multiplayer...), its right
// end at the scroll's (clear of the logo at the top left and of the version lines).
const float kBannerRight = 1012.0f, kBannerY = 412.0f, kBannerW = 560.0f, kBannerFont = 15.0f;
const DWORD kBannerMs = 20000;
// The picker: the game rules panel's size and look (host_rules_panel.cpp).
const float kPanelW = 649.0f, kPanelH = 508.0f;
const float kTitleY = 36.0f, kTitleH = 36.0f, kTitleFont = 20.0f;
const float kContentX0 = 31.0f, kContentY0 = 76.0f, kContentX1 = 618.0f, kContentY1 = 418.0f;
const float kSubY = 81.0f, kListX = 41.0f, kListY = 100.0f, kListW = 567.0f, kListH = 312.0f, kRowH = 38.0f;
const float kSmallFont = 11.0f, kBodyFont = 14.0f;
const float kCloseX = 491.0f, kButtonY = 430.0f, kButtonW = 122.0f, kButtonH = 34.0f;
const float kRowButtonW = 110.0f, kRowButtonH = 28.0f;
const float kBackgroundTexW = 1024.0f, kBackgroundTexH = 512.0f;
const DWORD kDialogWaitMs = 2500;  // Steam's dialog: the overlay must open within this

float Px(float size, float k)
{
	return floorf(size * k + 0.5f);
}

// A point of the game's 1024x768 screen, in back buffer pixels.
ImVec2 ScreenAt(float x, float y, float w, float h)
{
	const float k = h / kRefH;
	const float x0 = floorf((w - kScreenW * k) / 2 + 0.5f);
	return ImVec2(x0 + x * k, y * k);
}

// --- state ---------------------------------------------------------------------------------------
online::Status g_status;
char g_top[96] = "";
bool g_lan = false, g_wait = false, g_menu = false;
online::Room g_room = online::Room::kNone;
volatile LONG g_in_loaded_room = 0;  // g_room == kLoadedGame, for other threads
// the waiting room's button
bool g_button_shown = false, g_button_down = false, g_button_hovered = false;
float g_back_w = 1024.0f, g_back_h = 768.0f;
DWORD g_dialog_at = 0;  // Steam's dialog asked for (0: not waiting for it)
int g_overlay_before = 0;
// the main menu's banner
uint32_t g_seen_serial = 0;
std::string g_banner;
DWORD g_banner_until = 0;
// the picker
std::vector<online::Friend> g_friends;
DWORD g_friends_at = 0;
float g_scroll = 0.0f;
bool g_set_scroll = false;
uint64_t g_pick = 0;  // a row's button was clicked: invite after the frame
bool g_close = false;
bool g_logged_lines = false;
std::string g_told_lines;
// a game message box or dialog is open over the screen; the auto-join's line
bool g_boxed = false;
online::JoinState g_join_shown = online::JoinState::kNone;
std::string g_join_name;

// Is a game message box or dialog open over the screen? Two kinds (robot runs of
// 2026-11-03): dialogs that are screens of their own, on top of the list (the exit
// question NUI::CExitGameQuestionScreen, the LAN screen's name box NUI::CSimpleEditBox:
// then the screen on top is not ours, and nothing of ours shows anyway), and the
// common message boxes, which are not in the screen list at all (the LAN screen's
// "Timeout..." error: TopScreen still says NUI::CLobbyScreen): those the box watch sees.
bool Boxed()
{
	return autojoin::GameBoxOpen();
}

const char *Get(const char *key)
{
	return texts::Get(key);
}

// --- the picker -------------------------------------------------------------------------------------
void OpenPicker(const char *why)
{
	if (overlay::ModalOpen(kName))
		return;
	g_scroll = 0.0f;
	g_set_scroll = true;
	g_pick = 0;
	g_close = false;
	OnlineWantFriends(true);
	OnlineFriends(&g_friends);
	g_friends_at = GetTickCount();
	if (overlay::OpenModal(kName))
		hooklog::Write("online: our invite list opened (%s), %d friend(s) in it so far%s", why, static_cast<int>(g_friends.size()),
		               g_status.test ? " (ui_test: made up)" : "");
	else
		OnlineWantFriends(false);
}

void ClosePicker(const char *why)
{
	OnlineWantFriends(false);
	if (!overlay::ModalOpen(kName))
		return;
	overlay::CloseModal(kName);
	hooklog::Write("online: our invite list closed (%s)", why);
}

// The button (or the robot): Steam's dialog when its overlay works here, else our list.
void InviteClicked(const char *why)
{
	if (g_status.overlay && !g_status.test && OnlineInviteDialog()) {
		g_overlay_before = OnlineOverlayOpened();
		g_dialog_at = GetTickCount();
		if (!g_dialog_at)
			g_dialog_at = 1;
		hooklog::Write("online: Invite friends... (%s): Steam's invite dialog asked for", why);
		return;
	}
	hooklog::Write("online: Invite friends... (%s): %s: our own list", why,
	               g_status.test ? "ui_test" : g_status.overlay ? "no lobby for Steam's dialog" : "Steam's overlay is not enabled");
	OpenPicker(why);
}

// --- robot -------------------------------------------------------------------------------------------
void RobotCommand(const char *text)
{
	if (strcmp(text, "invite") == 0) {
		if (g_wait && g_status.own_lobby)
			InviteClicked("robot");
		else
			hooklog::Write("online: robot: no Invite friends... button here (%s, %s)", g_top[0] ? g_top : "no screen",
			               g_status.own_lobby ? "our lobby exists" : "no lobby of ours");
	}
	else if (strcmp(text, "picker") == 0)
		OpenPicker("robot");
	else if (strcmp(text, "close") == 0)
		ClosePicker("robot");
	else if (strcmp(text, "test_invite") == 0)
		OnlineTestInvite();
	else if (strncmp(text, "test_autojoin", 13) == 0 && (!text[13] || text[13] == ' ')) {
		// as an invite to the first host this PC hears of (the robot's two copies: the host),
		// from a made-up friend whose name may follow
		autojoin::Start(0, 0, text[13] ? text + 14 : "", "robot: an invite to the first host this PC hears of");
	}
	else if (strcmp(text, "games") == 0)
		autojoin::LogGames();
	else if (strncmp(text, "pick ", 5) == 0) {
		const int row = atoi(text + 5);
		if (!g_status.test)
			hooklog::Write("online: robot: \"pick\" only with [online] ui_test=1 (a robot never invites a real friend)");
		else if (row < 0 || row >= static_cast<int>(g_friends.size()))
			hooklog::Write("online: robot: no row %d in the list", row);
		else
			g_pick = g_friends[static_cast<size_t>(row)].id;
	}
	else
		hooklog::Write("online: robot command \"%s\" not known", text);
}

// --- the waiting room's button ---------------------------------------------------------------------
// Its corners in back buffer pixels.
void ButtonRect(float w, float h, ImVec2 *p0, ImVec2 *p1)
{
	*p0 = ScreenAt(kInviteX, kInviteY, w, h);
	*p1 = ScreenAt(kInviteX + kInviteW, kInviteY + kInviteH, w, h);
}

void ButtonInput()
{
	ImVec2 p0, p1;
	ButtonRect(g_back_w, g_back_h, &p0, &p1);
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
			InviteClicked("its button");
	}
}

// --- the overlay client ------------------------------------------------------------------------------
// The game's screens changed: the log says which (research and diagnosis: which
// screen a message box is).
void LogScreens()
{
	static std::string told;
	char names[16][96];
	const int n = overlay::ScreenNames(names, 16);
	std::string all;
	for (int i = 0; i < n; ++i)
		all += std::string(i ? " > " : "") + names[i];
	if (all == told)
		return;
	told = all;
	hooklog::Write("online: the game's screens: %s", all.empty() ? "(none)" : all.c_str());
}

bool Wanted()
{
	g_top[0] = 0;
	overlay::TopScreen(g_top, sizeof(g_top));
	OnlineStatus(&g_status);
	char command[128];
	if (robot::TakePanelCommand(command, sizeof(command), "online "))
		RobotCommand(command);
	const bool steam = g_status.state != online::State::kOff;  // (else mode=loopback: the auto-join only)
	const DWORD now = GetTickCount();
	LogScreens();
	g_boxed = Boxed();
	g_lan = strcmp(g_top, kLobbyScreen) == 0;
	g_room = online::WaitingRoom(g_top);
	InterlockedExchange(&g_in_loaded_room, g_room == online::Room::kLoadedGame ? 1 : 0);
	g_wait = g_room != online::Room::kNone;
	g_menu = strcmp(g_top, kMainMenu) == 0;
	if (steam) {
		NotifyGameInFront();
		// a new invite: the main menu's banner, and the auto-join
		if (g_status.invite_serial != g_seen_serial) {
			g_seen_serial = g_status.invite_serial;
			if (g_status.invite_count > 0) {
				const online::Invite &v = g_status.invites[0];
				g_banner = online::InviteText(v, &Get);
				g_banner_until = now + kBannerMs;
				hooklog::Write("online: an invite: the main menu's banner for %u s", kBannerMs / 1000);
				if (!v.made_up && !v.cannot)
					autojoin::Start(v.lobby, v.host, v.name, "a Steam invite");
			}
		}
		for (int i = 0; i < g_status.invite_count; ++i)
			autojoin::Update(g_status.invites[i].lobby, g_status.invites[i].host);
	}
	autojoin::Frame(g_top, g_boxed || strcmp(g_top, kNameBox) == 0);
	std::string who;
	g_join_shown = g_lan && !g_boxed ? autojoin::State(&who) : online::JoinState::kNone;
	g_join_name = who;
	if (!steam) {
		g_button_shown = false;
		return g_join_shown != online::JoinState::kNone;  // (loopback: only the auto-join's line)
	}
	if (g_boxed && !overlay::ModalOpen(kName)) {  // a game message box over the screen: nothing of ours over it
		g_button_shown = false;
		g_button_down = false;
		return false;
	}
	// Steam's dialog did not open: our own list instead
	if (g_dialog_at && now - g_dialog_at >= kDialogWaitMs) {
		g_dialog_at = 0;
		if (OnlineOverlayOpened() == g_overlay_before) {
			hooklog::Write("online: Steam's overlay did not open in %u s: our own list instead", kDialogWaitMs / 1000);
			if (g_wait && g_status.own_lobby)
				OpenPicker("Steam's dialog did not open");
		}
		else
			hooklog::Write("online: Steam's overlay opened with its invite dialog");
	}
	if (overlay::ModalOpen(kName)) {
		if (!g_wait || !g_status.own_lobby) {
			ClosePicker(!g_wait ? "the waiting room closed" : "our lobby is gone");
			return false;
		}
		if (now - g_friends_at >= 500) {
			OnlineFriends(&g_friends);
			g_friends_at = now;
		}
		if (g_pick) {
			const uint64_t id = g_pick;
			g_pick = 0;
			if (OnlineInvite(id))
				for (online::Friend &f : g_friends)
					if (f.id == id && f.invited == 0)
						f.invited = 1;  // (Steam's answer comes with the next refresh)
		}
		if (g_close) {
			g_close = false;
			ClosePicker("its button");
			return false;
		}
		g_button_shown = false;
		return true;
	}
	g_button_shown = g_wait && g_status.own_lobby;
	if (g_button_shown)
		ButtonInput();
	else
		g_button_down = false;
	return g_lan || g_button_shown || (g_menu && now < g_banner_until);
}

void Prepare(const overlay::Frame &frame)
{
	ui::LoadPictures(frame.device);
	const float k = frame.height / kRefH;
	for (float size : {kStatusTitleFont, kStatusFont, kButtonFont, kBannerFont, kTitleFont, kSmallFont, kBodyFont})
		overlay::WantFont(Px(size, k));
}

// Words that wrap at `width` pixels; returns the y under them.
float Wrapped(ImDrawList *draw, ImFont *font, float px, float x, float y, float width, ImU32 colour, const char *text)
{
	std::string line, word;
	const std::string all = text ? text : "";
	auto flush = [&]() {
		ui::ShadowText(draw, font, px, ImVec2(x, y), colour, line.c_str());
		y += floorf(px * 1.25f + 0.5f);
		line.clear();
	};
	for (size_t i = 0; i <= all.size(); ++i) {
		const char c = i < all.size() ? all[i] : ' ';
		if (c != ' ' && c != '\n') {
			word += c;
			continue;
		}
		if (!word.empty()) {
			const std::string candidate = line.empty() ? word : line + " " + word;
			if (!line.empty() && ui::TextWidth(font, px, candidate.c_str()) > width)
				flush();
			line = line.empty() ? word : line + " " + word;
			word.clear();
		}
		if (c == '\n' && !line.empty())
			flush();
	}
	if (!line.empty())
		flush();
	return y;
}

ImU32 ColourOf(online::Tone tone)
{
	switch (tone) {
	case online::Tone::kTitle: return ui::kGold;
	case online::Tone::kDim: return ui::kDim;
	case online::Tone::kWarn: return ui::kWarn;
	case online::Tone::kGood: return ui::kGold;
	default: return ui::kParchment;
	}
}

// The LAN screen's lines, in the big box under the games list.
void DrawStatus(const overlay::Frame &frame)
{
	const float k = frame.height / kRefH;
	std::vector<online::Line> lines = online::StatusLines(g_status, &Get, OPEN_HOMM5_VERSION);
	if (g_join_shown != online::JoinState::kNone) {  // the auto-join, first after the title
		const online::Line join = online::JoinLine(g_join_shown, g_join_name.c_str(), &Get);
		lines.insert(lines.empty() ? lines.end() : lines.begin() + 1, join);
	}
	ImDrawList *draw = ImGui::GetBackgroundDrawList();  // (over the game, under any panel of ours)
	const ImVec2 at = ScreenAt(kStatusX, kStatusY, frame.width, frame.height);
	const float width = kStatusW * k, bottom = kStatusBottom * k;
	float y = at.y;
	std::string all;
	for (const online::Line &l : lines) {
		if (y >= bottom)
			break;
		const bool title = l.tone == online::Tone::kTitle;
		const float px = Px(title ? kStatusTitleFont : kStatusFont, k);
		y = Wrapped(draw, overlay::Font(px), px, at.x, y, width, ColourOf(l.tone), l.text.c_str()) + (title ? kStatusGap * k : 0.0f);
		all += (all.empty() ? "" : " | ") + std::to_string(static_cast<int>(l.tone));
	}
	// the log: once, and when the kinds of lines change (no names: only how many and which tones)
	if (!g_logged_lines || all != g_told_lines) {
		g_logged_lines = true;
		g_told_lines = all;
		hooklog::Write("online: the LAN screen shows %d line(s) about Steam (state %d, %d friend(s) with H5 Together, %d "
		               "problem game(s), %d invite(s))",
		               static_cast<int>(lines.size()), static_cast<int>(g_status.state), g_status.friends_with_us,
		               g_status.problem_count, g_status.invite_count);
	}
}

void NineButton(ImDrawList *draw, ImVec2 p0, ImVec2 p1, const char *label, float k, bool hovered, bool pressed)
{
	// The game's 122x34 button, wider: its ends kept, its middle stretched.
	ImTextureID picture = ui::Tex(pressed ? ui::kButtonClick : ui::kButtonNormal);
	const float px = Px(kButtonFont, k);
	if (picture)
		ui::NineSlice(draw, picture, p0, p1, ImVec2(122.0f, 34.0f), 12.0f, 12.0f * k);
	else
		draw->AddRectFilled(p0, p1, pressed ? IM_COL32(70, 52, 30, 255) : IM_COL32(96, 72, 40, 255), 4.0f * k);
	if (hovered && !pressed) {
		if (ImTextureID highlight = ui::Tex(ui::kButtonHighlight))
			ui::NineSlice(draw, highlight, p0, p1, ImVec2(122.0f, 34.0f), 12.0f, 12.0f * k);
		else
			draw->AddRect(p0, p1, ui::kGold, 4.0f * k, 0, 1.5f * k);
	}
	ImFont *font = overlay::Font(px);
	const std::string text = ui::Fit(font, px, label, p1.x - p0.x - 12.0f * k);
	const ImVec2 t = font->CalcTextSizeA(px, 10000.0f, 0.0f, text.c_str());
	ui::ShadowText(draw, font, px, ImVec2(floorf((p0.x + p1.x - t.x) / 2), floorf((p0.y + p1.y - t.y) / 2 + (pressed ? k : 0.0f))),
	               IM_COL32(255, 236, 190, 255), text.c_str());
}

void DrawButton(const overlay::Frame &frame)
{
	const float k = frame.height / kRefH;
	ImVec2 p0, p1;
	ButtonRect(frame.width, frame.height, &p0, &p1);
	NineButton(ImGui::GetForegroundDrawList(), p0, p1, Get("online.invite_button"), k, g_button_hovered,
	           g_button_down && g_button_hovered);
	static bool told[2] = {false, false};  // (once in a new game's waiting room, once in a loaded game's)
	const bool loaded = g_room == online::Room::kLoadedGame;
	if (!told[loaded]) {
		told[loaded] = true;
		hooklog::Write("online: our Invite friends... button is drawn in the waiting room%s (Steam's overlay %s)",
		               loaded ? " of a loaded game" : "", g_status.overlay ? "enabled: its dialog" : "not enabled: our own list");
	}
}

void DrawBanner(const overlay::Frame &frame)
{
	const float k = frame.height / kRefH;
	const float px = Px(kBannerFont, k);
	ImFont *font = overlay::Font(px);
	ImDrawList *draw = ImGui::GetForegroundDrawList();
	const std::string text = ui::Fit(font, px, g_banner.c_str(), (kBannerW - 24.0f) * k);
	const float w = ui::TextWidth(font, px, text.c_str());
	const ImVec2 r = ScreenAt(kBannerRight, kBannerY, frame.width, frame.height);
	const float pad = 12.0f * k;
	const ImVec2 b0(floorf(r.x - w - 2 * pad), r.y), b1(r.x, r.y + px + 14.0f * k);
	draw->AddRectFilled(b0, b1, IM_COL32(20, 14, 8, 215), 4.0f * k);
	draw->AddRect(b0, b1, ui::kEdge, 4.0f * k, 0, 1.0f * k);
	ui::ShadowText(draw, font, px, ImVec2(b0.x + pad, floorf(r.y + 7.0f * k)), ui::kGold, text.c_str());
}

void CentredText(ImDrawList *draw, ImFont *font, float px, float x0, float x1, float y, ImU32 colour, const char *text)
{
	const std::string fitted = ui::Fit(font, px, text, x1 - x0);
	const float w = ui::TextWidth(font, px, fitted.c_str());
	ui::ShadowText(draw, font, px, ImVec2(floorf((x0 + x1 - w) / 2), y), colour, fitted.c_str());
}

void DrawPicker(const overlay::Frame &frame)
{
	const float k = frame.height / kRefH;
	const float x0 = floorf((frame.width - kPanelW * k) / 2 + 0.5f), y0 = floorf((frame.height - kPanelH * k) / 2 + 0.5f);
	auto at = [&](float x, float y) { return ImVec2(x0 + x * k, y0 + y * k); };
	ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(frame.width, frame.height), IM_COL32(0, 0, 0, 115));
	const ImVec2 p0 = at(0, 0), p1 = at(kPanelW, kPanelH);
	ImGui::SetNextWindowPos(p0, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::Begin("##online_invite", nullptr,
	             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
	                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImDrawList *draw = ImGui::GetWindowDrawList();
	if (ImTextureID bg = ui::Tex(ui::kBackground))
		draw->AddImage(bg, p0, p1, ImVec2(0, 0), ImVec2(kPanelW / kBackgroundTexW, kPanelH / kBackgroundTexH));
	else {
		draw->AddRectFilled(p0, p1, IM_COL32(30, 24, 18, 248), 8.0f * k);
		draw->AddRect(p0, p1, IM_COL32(150, 116, 70, 255), 8.0f * k, 0, 2.0f * k);
	}
	draw->AddRectFilled(at(kContentX0, kContentY0), at(kContentX1, kContentY1), IM_COL32(34, 27, 19, 245), 4.0f * k);
	draw->AddRect(at(kContentX0, kContentY0), at(kContentX1, kContentY1), ui::kEdge, 4.0f * k, 0, 1.0f * k);
	const float title_px = Px(kTitleFont, k), small_px = Px(kSmallFont, k), body_px = Px(kBodyFont, k);
	ImFont *title_font = overlay::Font(title_px), *small = overlay::Font(small_px), *body = overlay::Font(body_px);
	CentredText(draw, title_font, title_px, at(kListX, 0).x, at(kListX + kListW, 0).x, at(0, kTitleY + kTitleH / 2).y - title_px / 2,
	            ui::kGold, Get("online.picker.title"));
	CentredText(draw, small, small_px, at(kListX, 0).x, at(kListX + kListW, 0).x, at(0, kSubY).y, ui::kDim, Get("online.picker.sub"));

	// the friends, scrolling
	const ImVec2 c0 = at(kListX, kListY), c1 = at(kListX + kListW, kListY + kListH);
	ImGui::SetCursorScreenPos(c0);
	if (g_set_scroll) {
		ImGui::SetNextWindowScroll(ImVec2(-1.0f, g_scroll));
		g_set_scroll = false;
	}
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, Px(10.0f, k));
	ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(24, 20, 14, 255));
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ui::kEdge);
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(190, 146, 84, 255));
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ui::kGold);
	ImGui::BeginChild("##friends", ImVec2(c1.x - c0.x, c1.y - c0.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	ImDrawList *list = ImGui::GetWindowDrawList();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float width = kListW - 14.0f;
	if (g_friends.empty())
		ui::ShadowText(list, body, body_px, ImVec2(origin.x + 4 * k, origin.y + 6 * k), ui::kDim, Get("online.picker.none"));
	float y = 0.0f;
	for (size_t i = 0; i < g_friends.size(); ++i) {
		const online::Friend &f = g_friends[i];
		const ImVec2 r0(origin.x, origin.y + y * k), r1(origin.x + width * k, origin.y + (y + kRowH - 4.0f) * k);
		if (i % 2 == 0)
			list->AddRectFilled(r0, r1, IM_COL32(48, 38, 26, 160), 3.0f * k);
		const float text_w = (width - kRowButtonW - 20.0f) * k;
		ui::ShadowText(list, body, body_px, ImVec2(r0.x + 6 * k, r0.y + 3 * k), f.playing ? ui::kGold : ui::kParchment,
		               ui::Fit(body, body_px, f.name, text_w).c_str());
		ui::ShadowText(list, small, small_px, ImVec2(r0.x + 6 * k, r0.y + 3 * k + body_px + 1 * k), ui::kDim,
		               Get(online::FriendStateKey(f)));
		const ImVec2 b0(r1.x - (kRowButtonW + 4.0f) * k, r0.y + ((kRowH - 4.0f - kRowButtonH) / 2) * k);
		const ImVec2 b1(b0.x + kRowButtonW * k, b0.y + kRowButtonH * k);
		if (f.invited == 0) {
			char id[32];
			_snprintf_s(id, sizeof(id), _TRUNCATE, "##invite%u", static_cast<unsigned>(i));
			if (ui::Button(id, b0, b1, Get("online.picker.invite"), overlay::Font(Px(kButtonFont - 1.0f, k)), Px(kButtonFont - 1.0f, k),
			               true, k))
				g_pick = f.id;
		}
		else {
			const char *word = Get(f.invited > 0 ? "online.picker.invited" : "online.picker.refused");
			const float tw = ui::TextWidth(body, body_px, word);
			ui::ShadowText(list, body, body_px, ImVec2(floorf((b0.x + b1.x - tw) / 2), floorf((b0.y + b1.y - body_px) / 2)),
			               f.invited > 0 ? ui::kGold : ui::kWarn, word);
		}
		y += kRowH;
	}
	ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + y * k));
	ImGui::Dummy(ImVec2(1, 1));
	g_scroll = ImGui::GetScrollY();
	ImGui::EndChild();
	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar();

	const float button_px = Px(kButtonFont, k);
	if (ui::Button("##close", at(kCloseX, kButtonY), at(kCloseX + kButtonW, kButtonY + kButtonH), Get("online.picker.close"),
	               overlay::Font(button_px), button_px, true, k))
		g_close = true;
	ImGui::End();
	ImGui::PopStyleVar(3);
}

void Draw(const overlay::Frame &frame)
{
	g_back_w = frame.width;
	g_back_h = frame.height;
	if (overlay::ModalOpen(kName)) {
		DrawPicker(frame);
		return;
	}
	if (g_lan && !g_boxed)
		DrawStatus(frame);
	if (g_button_shown)
		DrawButton(frame);
	if (g_menu && GetTickCount() < g_banner_until && !g_banner.empty())
		DrawBanner(frame);
}

// A new invite, on the Steam transport's thread (outside the core's lock): the
// taskbar flash, the sound and the Windows pop-up when the game is not in front.
void OnInvite(const online::Invite &invite)
{
	NotifyInvite(texts::Get("online.toast_title"), online::InviteText(invite, &Get));
}

}  // namespace

namespace {

vlan::Mode ModeSetting()
{
	wchar_t path[MAX_PATH], mode[32];
	hooklog::PathNextToExe(L"h5-together", L".ini", path, MAX_PATH);
	GetPrivateProfileStringW(L"online", L"mode", L"steam", mode, 32, path);
	return vlan::ParseMode(mode);
}

}  // namespace

void InstallOnlineUiEarly()
{
	if (ModeSetting() != vlan::Mode::kOff)
		overlay::InstallEarly();  // (an overlay client: the Direct3D import before the game's device)
}

bool OnlineInLoadedGameRoom()
{
	return InterlockedCompareExchange(&g_in_loaded_room, 0, 0) != 0;
}

void ApplyOnlineUiFromSettings()
{
	// mode=steam: everything; mode=loopback (the robot's two copies): only the auto-join, for its tests
	const vlan::Mode mode = ModeSetting();
	if (mode == vlan::Mode::kOff || (mode == vlan::Mode::kLoopback && !VirtualLanOn()))
		return;  // (steam without Steam: the LAN screen says why)
	if (mode == vlan::Mode::kSteam)
		vlan::SetSteamInviteHandler(&OnInvite);
	const bool join = autojoin::Install();
	autojoin::InstallBoxWatch();
	const overlay::Client client = {kName, &Wanted, &Prepare, &Draw};
	if (!overlay::AddClient(client)) {
		hooklog::Write("online: no overlay in this game: no Steam lines on the LAN screen, no Invite friends... button");
		return;
	}
	overlay::EnableModalInput();
	hooklog::Write("online: the online UI is on: Steam's lines on the LAN screen, Invite friends... in the waiting room%s",
	               join ? "; an invite joins its game by itself" : "");
}

}  // namespace mods
