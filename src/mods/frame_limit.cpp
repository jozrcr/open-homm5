// Mod: a frame rate limit and the vertical sync (each player's choice; [video] fps_limit
// and vsync, both the game's own way by default).
//
// In plain words: the game draws as many frames as it can: about 400 a second in its
// menus, which only heats the PC and spins its fans. fps_limit=N waits at each frame
// until 1/N s has passed since the last one (a short sleep, then a busy wait for the last
// moment, so the pace is even). vsync=1 shows each frame at the screen's refresh (no
// tearing; the game's own is off on most PCs: its hidden gfx_vsync), vsync=0 never.
//
// How:
//   - the limit: a frame callback of our Direct3D hook (d3d9_hook.cpp), the last one
//     before the game's Present (after our overlay has drawn). The clock's ticks are made
//     1 ms long (timeBeginPeriod) while a limit is on, so the sleeps are short.
//   - vsync: our hook of the device's creation and reset hands us the presentation
//     settings before the game's go to Direct3D: PresentationInterval ONE (on) or
//     IMMEDIATE (off). From the next start of the game (the device is made once).
// Display only: the world, the lockstep and the computer players do not depend on the
// frame rate (the AI takes at most one decision a frame, far below any limit offered).
#include "frame_limit.h"

#include <windows.h>
#include <d3d9.h>
#include <mmsystem.h>

#include "../hook/d3d9_hook.h"
#include "../hook/log.h"

#pragma comment(lib, "winmm.lib")

namespace mods {
namespace {

const int kOrder = 1000;  // after every other frame callback (the overlay draws at lower orders)

volatile LONG g_fps = 0;  // 0 = no limit
bool g_period = false;    // timeBeginPeriod(1) taken
int g_vsync = -1;         // -1 the game's, 0 off, 1 on
LARGE_INTEGER g_freq = {};
LONGLONG g_next = 0;      // when the next frame may go, in performance-counter ticks
LONG g_logged = 0;

void ReadSettings(int *fps, int *vsync)
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together", L".ini", path, MAX_PATH);
	const int f = GetPrivateProfileIntW(L"video", L"fps_limit", 180, path);
	*fps = f >= 20 && f <= 500 ? f : 0;
	wchar_t v[16];
	GetPrivateProfileStringW(L"video", L"vsync", L"game", v, 16, path);
	*vsync = lstrcmpiW(v, L"1") == 0 ? 1 : lstrcmpiW(v, L"0") == 0 ? 0 : -1;
}

void SetPeriod(bool on)
{
	if (on && !g_period) {
		g_period = timeBeginPeriod(1) == TIMERR_NOERROR;
	}
	else if (!on && g_period) {
		timeEndPeriod(1);
		g_period = false;
	}
}

void Limit(IDirect3DDevice9 *)
{
	const LONG fps = g_fps;
	if (fps <= 0)
		return;
	const LONGLONG period = g_freq.QuadPart / fps;
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	if (g_next == 0 || now.QuadPart > g_next + period) {
		g_next = now.QuadPart + period;  // first frame, or late by more than a frame: no debt to catch up
		return;
	}
	for (;;) {
		QueryPerformanceCounter(&now);
		const LONGLONG left = g_next - now.QuadPart;
		if (left <= 0)
			break;
		const double ms = left * 1000.0 / g_freq.QuadPart;
		if (ms > 2.0)
			Sleep(static_cast<DWORD>(ms - 1.5));
		else if (ms > 0.5)
			Sleep(0);
		// else: a busy wait for the last half millisecond
	}
	g_next += period;
}

void Filter(D3DPRESENT_PARAMETERS *params)
{
	if (!params || g_vsync < 0)
		return;
	const UINT before = params->PresentationInterval;
	params->PresentationInterval = g_vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
	if (InterlockedIncrement(&g_logged) <= 4)
		hooklog::Write("frame limit: vsync %s (the game asked for interval %08x)", g_vsync ? "on" : "off", before);
}

}  // namespace

int FrameLimit()
{
	return g_fps;
}

void ReloadFrameLimitSettings()
{
	int fps = 0, vsync = -1;
	ReadSettings(&fps, &vsync);
	InterlockedExchange(&g_fps, fps);
	g_next = 0;
	SetPeriod(fps > 0);
	hooklog::Write("frame limit: from the settings menu: %s", fps ? "on" : "off (the game's own: as many frames as it can)");
	if (fps)
		hooklog::Write("frame limit: at most %d frames a second", fps);
}

void ApplyFrameLimitFromSettings()
{
	QueryPerformanceFrequency(&g_freq);
	int fps = 0, vsync = -1;
	ReadSettings(&fps, &vsync);
	g_fps = fps;
	g_vsync = vsync;
	SetPeriod(fps > 0);
	d3dhook::AddPresentCallback(&Limit, kOrder);  // (always: the limit can be turned on from the menu)
	d3dhook::SetPresentParamsFilter(&Filter);
	hooklog::Write("frame limit: %s; vsync: %s", fps ? "on" : "off (the game's own)",
	               vsync < 0 ? "the game's own" : vsync ? "on" : "off");
	if (fps)
		hooklog::Write("frame limit: at most %d frames a second", fps);
}

}  // namespace mods
