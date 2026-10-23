// Robot-mode virtual clock (virtual_clock=1 in open-homm5-robot.ini): the
// game's main thread sees a clock that advances by a fixed step per frame and
// by the requested time in Sleep, not with real time. The date and time of day
// are fixed too.
//
// Why: the AI works in time slices (aai_step_limit, ai_time_limit, see
// docs/subsystems/ai-turn.md) and other parts of the game read the clock, so
// the original game's decisions can depend on how fast it runs. Two runs of the
// same save then drift apart as soon as one of them is faster or slower (a mod
// that speeds up the AI, another program using the CPU). With this clock, AI
// thinking takes no time at all as far as the game can tell, so the same save
// plays the same way however fast the code is: a plain run and a mod run can
// be compared over whole turns (tools/robot/path_report.py).
//
// Only the exe's own imports are redirected (KERNEL32 GetTickCount,
// QueryPerformanceCounter, Sleep, GetSystemTimeAsFileTime, GetLocalTime,
// GetSystemTime; MSVCR71 clock, time), and only calls from the main thread get
// the virtual values; the robot's own timers keep real time.
#include <windows.h>
#include <time.h>

#include "log.h"
#include "robot.h"

namespace robot {
namespace {

typedef DWORD(WINAPI *GetTickCountFn)();
typedef BOOL(WINAPI *QpcFn)(LARGE_INTEGER *);
typedef void(WINAPI *SleepFn)(DWORD);
typedef void(WINAPI *FileTimeFn)(LPFILETIME);
typedef void(WINAPI *SystemTimeFn)(LPSYSTEMTIME);
typedef clock_t(__cdecl *ClockFn)();
typedef __int32(__cdecl *TimeFn)(__int32 *);

GetTickCountFn g_real_tick = nullptr;
QpcFn g_real_qpc = nullptr;
SleepFn g_real_sleep = nullptr;
FileTimeFn g_real_filetime = nullptr;
SystemTimeFn g_real_localtime = nullptr;
SystemTimeFn g_real_systemtime = nullptr;
ClockFn g_real_clock = nullptr;
TimeFn g_real_time = nullptr;

DWORD g_main_thread_id = 0;
double g_now_ms = 0;      // virtual milliseconds since the clock started
DWORD g_tick_base = 0;    // GetTickCount when the clock started
double g_frame_ms = 16;   // added per frame
LARGE_INTEGER g_qpc_frequency;
LONGLONG g_qpc_base = 0;
// a fixed date: 2026-01-01 12:00:00 UTC, then virtual time
const unsigned __int64 kFileTimeBase = 134116992000000000ull + 12ull * 3600 * 10000000;  // 100 ns since 1601
const __int32 kUnixBase = 1767268800;

bool OnMainThread()
{
	return GetCurrentThreadId() == g_main_thread_id;
}

DWORD WINAPI VirtualGetTickCount()
{
	if (!OnMainThread())
		return g_real_tick();
	g_now_ms += 0.001;  // a busy-wait on the clock still moves on
	return g_tick_base + static_cast<DWORD>(g_now_ms);
}

BOOL WINAPI VirtualQpc(LARGE_INTEGER *counter)
{
	if (!OnMainThread())
		return g_real_qpc(counter);
	g_now_ms += 0.001;
	counter->QuadPart = g_qpc_base + static_cast<LONGLONG>(g_now_ms * g_qpc_frequency.QuadPart / 1000.0);
	return TRUE;
}

void WINAPI VirtualSleep(DWORD ms)
{
	if (OnMainThread())
		g_now_ms += ms;
	g_real_sleep(ms ? 1 : 0);  // let other threads run, but do not wait the real time
}

unsigned __int64 VirtualFileTime()
{
	return kFileTimeBase + static_cast<unsigned __int64>(g_now_ms * 10000.0);
}

void WINAPI VirtualGetSystemTimeAsFileTime(LPFILETIME ft)
{
	if (!OnMainThread())
		return g_real_filetime(ft);
	const unsigned __int64 t = VirtualFileTime();
	ft->dwLowDateTime = static_cast<DWORD>(t);
	ft->dwHighDateTime = static_cast<DWORD>(t >> 32);
}

void WINAPI VirtualGetSystemTime(LPSYSTEMTIME st)
{
	if (!OnMainThread())
		return g_real_systemtime(st);
	FILETIME ft;
	VirtualGetSystemTimeAsFileTime(&ft);
	FileTimeToSystemTime(&ft, st);
}

void WINAPI VirtualGetLocalTime(LPSYSTEMTIME st)
{
	if (!OnMainThread())
		return g_real_localtime(st);
	VirtualGetSystemTime(st);  // the fixed date is the same in every time zone
}

clock_t __cdecl VirtualClock()
{
	if (!OnMainThread())
		return g_real_clock();
	return static_cast<clock_t>(g_now_ms);  // CLOCKS_PER_SEC is 1000
}

__int32 __cdecl VirtualTime(__int32 *out)
{
	if (!OnMainThread())
		return g_real_time(out);
	const __int32 t = kUnixBase + static_cast<__int32>(g_now_ms / 1000.0);
	if (out)
		*out = t;
	return t;
}

template <class Fn>
void Redirect(const char *dll, const char *name, void *replacement, Fn *real, int *ok)
{
	*real = reinterpret_cast<Fn>(PatchImport(dll, name, replacement));
	if (*real)
		++*ok;
	else
		hooklog::Write("robot: virtual clock: %s!%s not found", dll, name);
}

}  // namespace

void InstallVirtualClock(DWORD frame_ms)
{
	if (g_main_thread_id)
		return;
	g_main_thread_id = GetCurrentThreadId();
	g_frame_ms = frame_ms ? frame_ms : 16;
	g_tick_base = GetTickCount();
	QueryPerformanceFrequency(&g_qpc_frequency);
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	g_qpc_base = now.QuadPart;
	int ok = 0;
	Redirect("KERNEL32.dll", "GetTickCount", reinterpret_cast<void *>(&VirtualGetTickCount), &g_real_tick, &ok);
	Redirect("KERNEL32.dll", "QueryPerformanceCounter", reinterpret_cast<void *>(&VirtualQpc), &g_real_qpc, &ok);
	Redirect("KERNEL32.dll", "Sleep", reinterpret_cast<void *>(&VirtualSleep), &g_real_sleep, &ok);
	Redirect("KERNEL32.dll", "GetSystemTimeAsFileTime", reinterpret_cast<void *>(&VirtualGetSystemTimeAsFileTime),
	         &g_real_filetime, &ok);
	Redirect("KERNEL32.dll", "GetSystemTime", reinterpret_cast<void *>(&VirtualGetSystemTime), &g_real_systemtime, &ok);
	Redirect("KERNEL32.dll", "GetLocalTime", reinterpret_cast<void *>(&VirtualGetLocalTime), &g_real_localtime, &ok);
	Redirect("MSVCR71.dll", "clock", reinterpret_cast<void *>(&VirtualClock), &g_real_clock, &ok);
	Redirect("MSVCR71.dll", "time", reinterpret_cast<void *>(&VirtualTime), &g_real_time, &ok);
	hooklog::Write("robot: virtual clock on (%d of 8 clock imports), %.0f ms per frame", ok, g_frame_ms);
}

void VirtualClockFrame()
{
	if (g_main_thread_id && OnMainThread())
		g_now_ms += g_frame_ms;
}

}  // namespace robot
