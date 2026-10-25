// Mod: a "your turn" notification (docs/subsystems/ai-turn.md, "Whose turn it is").
//
// The owner's problem: in co-op games the friends switch to another window
// while the computer players move, and miss the moment they can play again.
//
// What this mod does, settings in open-homm5.ini [notify]:
//   turn=1   when this PC's player can play again and the game window is not
//            in front: the game's taskbar button flashes (until the game is
//            back in front), and the log says "notify: your turn (day N)".
//   sound=1  and a short sound: Windows' "System Notification" sound where
//            Windows has one (Windows 8 and later), otherwise a short chime
//            made in memory (Wine on Linux has no Windows sounds).
//   popup=1  and a pop-up from a notification area (tray) icon, which goes
//            away when the game is back in front. Where there is no
//            notification area (often under Wine), nothing happens.
//   trace=0  1: a log line for each change of the players' turn states.
//
// When is it "your turn"? (addresses in the vanilla 3.1 exe)
// Each player (NWorld::CPlayer; the world and the adventure controller point
// to its IPlayer part, vftable 0x00e1a09c) has a turn state at +0x634 (IPlayer
// slot 0x44 = 0x00500c10 reads it, slot 0x48 = 0x00500c20 sets it). 0 = it is
// playing its turn now, 1 = waiting. The game finds "the player whose turn it
// is" the same way (0x0098d480: the one living player, human or not, with
// state 0). The states are part of the game world, so every PC sees the same
// ones. With simultaneous turns all humans are at 0 together, each goes back to
// 1 when it ends its turn, then the computer players play one after another.
// This PC's player is the adventure controller's +0x3c (the autosave's "first
// living human" test 0x00603ca0 gets it from there; in a hotseat game the
// controller switches it to the human whose turn it is, 0x00607d50).
// So "your turn" = this PC's player is a living human (+0x74 == 0, +0x7c == 1)
// with state 0, and it was not in the frame before. The check runs after each
// frame update of the adventure controller (0x006082f0, its only call at
// 0x0060895e in 0x00608930), the update that also notices new turns and days.
// The first such moment of a game (the start, or loading a save) is not
// notified: the player is at the screen then. A turn that ends in the same frame
// as it begins (only the robot does that) is not seen, and needs no notification.
// The mod only reads the game, so it cannot desync a multiplayer game.
//
// Checked with the robot (2026-10-25): LAN games with and without simultaneous
// turns and a single-player game, A1M4: one line per new turn on each PC, none
// on day 1; world checksums equal on both PCs.
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "notify.h"

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

const DWORD kUpdateCall = 0x0060895e;  // in 0x00608930: call 0x006082f0
const DWORD kUpdate = 0x006082f0;      // __thiscall, 5 stack arguments (ret 0x14)
const DWORD kControllerVftable = 0x00e29a2c;  // NUI::CAdventureController
const DWORD kWorldVftable = 0x00e6260c;       // NWorld::CWorld
const DWORD kPlayerVftable = 0x00e1a09c;      // NWorld::CPlayer, its IPlayer part (at CPlayer +0x1c)

// Fields
const DWORD kControllerWorld = 0x18;  // the world (0x006082f0 reads it there)
const DWORD kControllerLocal = 0x3c;  // this PC's player (IPlayer *)
const DWORD kWorldPlayers = 0x24;     // vector of IPlayer *: begin, end
const DWORD kWorldDay = 0x48;         // 0 on the first day
const DWORD kPlayerHuman = 0x74;      // 0 = human
const DWORD kPlayerNumber = 0x78;
const DWORD kPlayerStatus = 0x7c;     // 1 = in the game
const DWORD kPlayerTurn = 0x634;      // 0 = playing its turn

// The game's code that shows those fields; checked before the hook goes in.
struct Code {
	DWORD slot;  // the vftable cell that holds the function (0: not checked)
	DWORD address;
	BYTE bytes[10];
	int size;
};
const Code kCode[] = {
	{0x00e1a0e0, 0x00500c10, {0x8b, 0x81, 0x34, 0x06, 0x00, 0x00, 0xc3}, 7},  // IPlayer 0x44: mov eax,[ecx+0x634]
	{0x00e1a108, 0x00c61a70, {0x8b, 0x41, 0x7c, 0xc3}, 4},                    // IPlayer 0x6c: mov eax,[ecx+0x7c]
	{0x00e1a220, 0x00500d60, {0x33, 0xc0, 0x39, 0x41, 0x74, 0x0f, 0x94, 0xc0, 0xc3}, 9},  // 0x184: [ecx+0x74] == 0
	{0x00e1a0c8, 0x00720e90, {0x8b, 0x41, 0x78, 0xc3}, 4},                    // IPlayer 0x2c: mov eax,[ecx+0x78]
	{0x00e62670, 0x00a367b0, {0x8d, 0x41, 0x24, 0xc3}, 4},                    // CWorld 0x64: lea eax,[ecx+0x24]
	{0, 0x00c61ab0, {0x8d, 0x41, 0x48, 0xc3}, 4},  // CWorld 0xa8: lea eax,[ecx+0x48] (its cell may be hooked: autosave)
	{0, 0x00608930, {0x56, 0x8b, 0xf1, 0x8b, 0x46, 0x3c}, 6},  // push esi; mov esi,ecx; mov eax,[esi+0x3c]
	{0, 0x006082f8, {0x8b, 0x4e, 0x18, 0x8b, 0x01, 0xff, 0x90, 0xa8, 0x00, 0x00}, 10},  // mov ecx,[esi+0x18]; ... [eax+0xa8]
};

bool CodeAsExpected()
{
	for (const Code &c : kCode) {
		if (c.slot && *reinterpret_cast<const DWORD *>(c.slot) != c.address)
			return false;
		if (memcmp(reinterpret_cast<const void *>(c.address), c.bytes, c.size) != 0)
			return false;
	}
	return true;
}

bool g_sound = true;
bool g_popup = true;
bool g_trace = false;
bool g_system_sound = false;  // Windows has a "System Notification" sound

DWORD Read(const void *base, DWORD offset)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(base) + offset);
}

// --- the game's state -------------------------------------------------------------
struct Player {
	int number;
	bool human;
	int status;
	int turn;
};

bool ReadPlayer(const void *p, Player *out)
{
	if (!p || Read(p, 0) != kPlayerVftable)
		return false;
	out->number = static_cast<int>(Read(p, kPlayerNumber));
	out->human = Read(p, kPlayerHuman) == 0;
	out->status = static_cast<int>(Read(p, kPlayerStatus));
	out->turn = static_cast<int>(Read(p, kPlayerTurn));
	return true;
}

struct Frame {
	const void *world;
	int day;  // 1 = the first day
	bool have_me;
	Player me;
};

bool ReadFrame(const void *controller, Frame *f)
{
	__try {
		if (!controller || Read(controller, 0) != kControllerVftable)
			return false;
		f->world = reinterpret_cast<const void *>(Read(controller, kControllerWorld));
		if (!f->world || Read(f->world, 0) != kWorldVftable)
			return false;
		f->day = static_cast<int>(Read(f->world, kWorldDay)) + 1;
		f->have_me = ReadPlayer(reinterpret_cast<const void *>(Read(controller, kControllerLocal)), &f->me);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// trace=1: " 1h:1/0 2h:1/3 3ai:1/1" (number, h = human, status / turn state).
void FormatPlayers(const void *world, char *out, size_t size)
{
	out[0] = 0;
	size_t used = 0;
	__try {
		const DWORD *begin = reinterpret_cast<const DWORD *>(Read(world, kWorldPlayers));
		const DWORD *end = reinterpret_cast<const DWORD *>(Read(world, kWorldPlayers + 4));
		for (const DWORD *it = begin; it && it < end && it - begin < 16; ++it) {
			Player p;
			if (!ReadPlayer(reinterpret_cast<const void *>(*it), &p))
				continue;
			const int n = _snprintf_s(out + used, size - used, _TRUNCATE, " %d%s:%d/%d", p.number,
			                          p.human ? "h" : "ai", p.status, p.turn);
			if (n < 0)
				break;
			used += static_cast<size_t>(n);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		out[used < size ? used : size - 1] = 0;
	}
}

char g_traced[200] = "";
int g_traced_day = -1;
int g_traced_me = -1;

void TraceStates(const Frame &f)
{
	char players[200];
	FormatPlayers(f.world, players, sizeof(players));
	const int me = f.have_me ? f.me.number : -1;
	if (strcmp(players, g_traced) == 0 && f.day == g_traced_day && me == g_traced_me)
		return;
	strcpy_s(g_traced, players);
	g_traced_day = f.day;
	g_traced_me = me;
	hooklog::Write("notify: day %d, this PC's player %d; players (number, h = human, status/turn state):%s", f.day,
	               me, players);
}

// --- the game window ----------------------------------------------------------------
HWND g_window = nullptr;

BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM found)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	wchar_t name[8];
	if (pid == GetCurrentProcessId() && GetClassNameW(hwnd, name, 8) && wcscmp(name, L"H5") == 0) {
		*reinterpret_cast<HWND *>(found) = hwnd;
		return FALSE;
	}
	return TRUE;
}

// The game's main window (class "H5"; the robot's copies each have their own).
HWND GameWindow()
{
	if (g_window && IsWindow(g_window))
		return g_window;
	g_window = nullptr;
	EnumWindows(&FindGameWindow, reinterpret_cast<LPARAM>(&g_window));
	return g_window;
}

// The window in front belongs to the game (and the game is not minimized).
bool GameInFront(HWND game)
{
	const HWND front = GetForegroundWindow();
	if (!front)
		return false;
	DWORD pid = 0;
	GetWindowThreadProcessId(front, &pid);
	return pid == GetCurrentProcessId() && !(game && IsIconic(game));
}

// --- the sound ------------------------------------------------------------------------
bool HasSystemSound(const wchar_t *name)
{
	wchar_t key[128];
	_snwprintf_s(key, _countof(key), _TRUNCATE, L"AppEvents\\Schemes\\Apps\\.Default\\%s\\.Current", name);
	HKEY h;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &h) != ERROR_SUCCESS)
		return false;
	RegCloseKey(h);
	return true;
}

// A short two-note chime (0.45 s, 22 kHz, 16-bit mono WAV), made once.
const int kRate = 22050;
const int kSamples = kRate * 45 / 100;
BYTE g_chime[44 + kSamples * 2];
bool g_chime_made = false;

void Put16(BYTE *p, DWORD v)
{
	p[0] = static_cast<BYTE>(v);
	p[1] = static_cast<BYTE>(v >> 8);
}

void Put32(BYTE *p, DWORD v)
{
	Put16(p, v);
	Put16(p + 2, v >> 16);
}

double Note(double t, double start, double hz)
{
	if (t < start)
		return 0.0;
	t -= start;
	const double attack = t < 0.005 ? t / 0.005 : 1.0;
	return attack * exp(-t / 0.09) * sin(2.0 * 3.14159265358979 * hz * t);
}

const BYTE *Chime()
{
	if (g_chime_made)
		return g_chime;
	BYTE *h = g_chime;
	memcpy(h, "RIFF", 4);
	Put32(h + 4, 36 + kSamples * 2);
	memcpy(h + 8, "WAVEfmt ", 8);
	Put32(h + 16, 16);         // format chunk size
	Put16(h + 20, 1);          // PCM
	Put16(h + 22, 1);          // mono
	Put32(h + 24, kRate);
	Put32(h + 28, kRate * 2);  // bytes per second
	Put16(h + 32, 2);          // bytes per sample
	Put16(h + 34, 16);         // bits
	memcpy(h + 36, "data", 4);
	Put32(h + 40, kSamples * 2);
	for (int i = 0; i < kSamples; ++i) {
		const double t = static_cast<double>(i) / kRate;
		const double fade = i > kSamples - 441 ? (kSamples - i) / 441.0 : 1.0;  // the last 20 ms
		double v = (Note(t, 0.0, 880.0) + Note(t, 0.12, 1318.5)) * 0.3 * fade;
		v = v > 1.0 ? 1.0 : v < -1.0 ? -1.0 : v;
		Put16(h + 44 + 2 * i, static_cast<DWORD>(static_cast<short>(v * 32767.0)) & 0xffff);
	}
	g_chime_made = true;
	return g_chime;
}

const char *PlayTurnSound()
{
	if (g_system_sound && PlaySoundW(L"SystemNotification", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT))
		return "the system notification sound";
	if (PlaySoundW(reinterpret_cast<LPCWSTR>(Chime()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT))
		return "a chime";
	MessageBeep(MB_ICONASTERISK);
	return "a beep";
}

// --- the pop-up -----------------------------------------------------------------------
// Shell_NotifyIcon talks to the notification area's window and waits for it,
// so it runs on a thread of its own: the game never waits for it.
const UINT kIconId = 0x4835;
HANDLE g_popup_wake = nullptr;
volatile LONG g_popup_request = 0;  // > 0: show it for that day; -1: remove it
HWND volatile g_popup_window = nullptr;
HICON volatile g_popup_icon = nullptr;
bool g_popup_shown = false;  // the main thread asked for it and not for its removal

DWORD WINAPI PopupThread(void *)
{
	bool added = false, failed = false;
	for (;;) {
		WaitForSingleObject(g_popup_wake, INFINITE);
		const LONG request = InterlockedExchange(&g_popup_request, 0);
		NOTIFYICONDATAW nid;
		memset(&nid, 0, sizeof(nid));
		nid.cbSize = sizeof(nid);
		nid.hWnd = g_popup_window;
		nid.uID = kIconId;
		if (request > 0) {
			nid.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
			nid.hIcon = g_popup_icon;
			wcscpy_s(nid.szTip, L"Heroes V: your turn");
			wcscpy_s(nid.szInfoTitle, L"Heroes of Might and Magic V");
			_snwprintf_s(nid.szInfo, _countof(nid.szInfo), _TRUNCATE, L"Your turn (day %ld).", request);
			nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;  // the sound is ours (sound=)
			bool ok = added && Shell_NotifyIconW(NIM_MODIFY, &nid);
			if (!ok)
				ok = Shell_NotifyIconW(NIM_ADD, &nid) || Shell_NotifyIconW(NIM_MODIFY, &nid);
			added = added || ok;
			if (!ok && !failed) {
				failed = true;  // e.g. no notification area under Wine: say it once
				hooklog::Write("notify: no pop-up (the system has no notification area?)");
			}
		}
		else if (request < 0 && added) {
			Shell_NotifyIconW(NIM_DELETE, &nid);
			added = false;
		}
	}
}

void ShowPopup(HWND window, int day)
{
	if (!g_popup_wake || !window)
		return;
	HICON icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM));
	if (!icon)
		icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICON));
	if (!icon)
		icon = LoadIconW(nullptr, MAKEINTRESOURCEW(32516));  // IDI_INFORMATION
	g_popup_window = window;
	g_popup_icon = icon;
	InterlockedExchange(&g_popup_request, day > 0 ? day : 1);
	SetEvent(g_popup_wake);
	g_popup_shown = true;
}

void RemovePopup()
{
	InterlockedExchange(&g_popup_request, -1);
	SetEvent(g_popup_wake);
	g_popup_shown = false;
}

// --- the turn check, after each frame update of the adventure controller --------------
const void *g_controller = nullptr;
const void *g_world = nullptr;
int g_day = 0;
bool g_could_play = false;  // this PC's player could play in the frame before
int g_turns = 0;            // turns this PC's player got since the game began (or was loaded)

void YourTurn(int day)
{
	if (++g_turns == 1) {
		hooklog::Write("notify: your turn (day %d): the first turn of this game, no notification", day);
		return;
	}
	const HWND window = GameWindow();
	if (GameInFront(window)) {
		hooklog::Write("notify: your turn (day %d): the game is in front, no notification", day);
		return;
	}
	if (window) {
		FLASHWINFO flash = {sizeof(flash), window, FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0};
		FlashWindowEx(&flash);
	}
	const char *sound = g_sound ? PlayTurnSound() : nullptr;
	if (g_popup)
		ShowPopup(window, day);
	hooklog::Write("notify: your turn (day %d): %s%s%s%s", day, window ? "taskbar flash" : "no game window found",
	               sound ? ", " : "", sound ? sound : "", g_popup && window ? ", pop-up" : "");
}

void AfterFrame(const void *controller)
{
	Frame f;
	if (!ReadFrame(controller, &f))
		return;
	if (g_trace)
		TraceStates(f);
	if (controller != g_controller || f.world != g_world || f.day < g_day) {
		// another game (a new one, or a save loaded)
		g_controller = controller;
		g_world = f.world;
		g_could_play = false;
		g_turns = 0;
	}
	g_day = f.day;
	const bool can_play = f.have_me && f.me.human && f.me.status == 1 && f.me.turn == 0;
	if (can_play && !g_could_play)
		YourTurn(f.day);
	g_could_play = can_play;
	if (g_popup_shown && GameInFront(GameWindow()))
		RemovePopup();
}

typedef void(__fastcall *UpdateFn)(void *controller, void *edx, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e);
UpdateFn g_update_next = nullptr;

void __fastcall UpdateHook(void *controller, void *edx, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e)
{
	g_update_next(controller, edx, a, b, c, d, e);
	AfterFrame(controller);
}

}  // namespace

void ApplyNotifyFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	if (!GetPrivateProfileIntW(L"notify", L"turn", 1, path)) {
		hooklog::Write("notify: \"your turn\" notification off (open-homm5.ini)");
		return;
	}
	g_sound = GetPrivateProfileIntW(L"notify", L"sound", 1, path) != 0;
	g_popup = GetPrivateProfileIntW(L"notify", L"popup", 1, path) != 0;
	g_trace = GetPrivateProfileIntW(L"notify", L"trace", 0, path) != 0;
	if (CodeAsExpected())
		g_update_next = reinterpret_cast<UpdateFn>(hookpatch::RedirectCall(kUpdateCall, kUpdate, &UpdateHook));
	if (!g_update_next) {
		hooklog::Write("notify: the game's code is not as expected: no \"your turn\" notification");
		return;
	}
	if (g_sound)
		g_system_sound = HasSystemSound(L"SystemNotification");
	if (g_popup) {
		g_popup_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		HANDLE thread = g_popup_wake ? CreateThread(nullptr, 0, &PopupThread, nullptr, 0, nullptr) : nullptr;
		if (thread)
			CloseHandle(thread);
		else
			g_popup = false;
	}
	hooklog::Write("notify: \"your turn\" notification on: taskbar flash%s%s%s", !g_sound ? "" : g_system_sound
	               ? ", the system notification sound" : ", a chime (no system notification sound)",
	               g_popup ? ", pop-up" : "", g_trace ? ", trace" : "");
}

}  // namespace mods
