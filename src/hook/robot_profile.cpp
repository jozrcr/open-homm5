// Robot AI profiler (robot mode, profile_ai=1): where does the AI spend its
// thinking time?
//
// While an AI turn driver call is running (the AI trace marks it, see
// ai_trace.cpp), a sampling thread pauses the main thread every few
// milliseconds, notes its instruction pointer and the game's return addresses
// on its stack, and lets it go on. Each sample is one line of
// open-homm5-profile[-<instance>].txt next to the exe:
//   <eip> <return address> <return address> ...
// tools/robot/profile_report.py turns the samples into a list of the game
// functions the AI spends its time in (self and inclusive), using the inventory.
#include <windows.h>
#include <share.h>
#include <stdio.h>

#include "log.h"
#include "robot.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;  // this DLL's load address (linker symbol)

namespace robot {
namespace {

volatile LONG g_thinking = 0;
HANDLE g_ai_thread = nullptr;
FILE *g_out = nullptr;
volatile LONG g_samples = 0;
DWORD g_exe_begin = 0, g_exe_end = 0;

bool AfterCall(const BYTE *p)
{
	return p[-5] == 0xE8 || (p[-6] == 0xFF && p[-5] == 0x15) || (p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0) ||
	       (p[-3] == 0xFF && (p[-2] & 0xF8) == 0x50) || (p[-6] == 0xFF && (p[-5] & 0xF8) == 0x90);
}

// One sample: eip and up to 24 game return addresses of the paused thread.
// Only copies memory: no C library call while the thread is paused (it might
// hold a lock we would then wait for forever).
int Sample(const CONTEXT &ctx, DWORD *out, int max)
{
	int n = 0;
	out[n++] = ctx.Eip;
	const DWORD *stack = reinterpret_cast<const DWORD *>(ctx.Esp);
	for (int i = 0; i < 8192 && n < max; ++i) {
		__try {
			const DWORD v = stack[i];
			if (v >= g_exe_begin + 5 && v < g_exe_end && AfterCall(reinterpret_cast<const BYTE *>(v)))
				out[n++] = v;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			break;  // end of the stack
		}
	}
	return n;
}

void Write(const DWORD *sample, int n)
{
	char line[512];
	int used = 0;
	for (int i = 0; i < n && used < 480; ++i)
		used += _snprintf_s(line + used, sizeof(line) - used, _TRUNCATE, i ? " %08lx" : "%08lx", sample[i]);
	fprintf(g_out, "%s\n", line);
	if ((InterlockedIncrement(&g_samples) & 63) == 0)
		fflush(g_out);
}

DWORD WINAPI Sampler(LPVOID)
{
	DWORD sample[25];
	for (;;) {
		Sleep(2);
		if (!g_thinking || !g_ai_thread)
			continue;
		if (SuspendThread(g_ai_thread) == static_cast<DWORD>(-1))
			continue;
		CONTEXT ctx = {};
		ctx.ContextFlags = CONTEXT_CONTROL;
		int n = 0;
		if (g_thinking && GetThreadContext(g_ai_thread, &ctx))
			n = Sample(ctx, sample, 25);
		ResumeThread(g_ai_thread);
		if (n)
			Write(sample, n);
	}
}

}  // namespace

void StartAiProfiler()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5-profile", L".txt", path, MAX_PATH);
	g_out = _wfsopen(path, L"w", _SH_DENYWR);
	if (!g_out) {
		hooklog::Write("robot: AI profiler: cannot write %ls", path);
		return;
	}
	// where our DLL is loaded, so samples in it can be named with build/hook/dinput8.map
	fprintf(g_out, "# dll %08lx\n", reinterpret_cast<DWORD>(&__ImageBase));
	const BYTE *exe = reinterpret_cast<const BYTE *>(GetModuleHandleW(nullptr));
	g_exe_begin = reinterpret_cast<DWORD>(exe);
	g_exe_end = g_exe_begin + reinterpret_cast<const IMAGE_NT_HEADERS32 *>(
	                              exe + reinterpret_cast<const IMAGE_DOS_HEADER *>(exe)->e_lfanew)
	                              ->OptionalHeader.SizeOfImage;
	HANDLE t = CreateThread(nullptr, 0, &Sampler, nullptr, 0, nullptr);
	if (t) {
		SetThreadPriority(t, THREAD_PRIORITY_ABOVE_NORMAL);
		CloseHandle(t);
	}
	hooklog::Write("robot: AI profiler on, samples go to %ls", path);
}

void OnAiThinking(bool thinking)
{
	if (!g_out)
		return;
	if (thinking && !g_ai_thread)
		g_ai_thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, GetCurrentThreadId());
	InterlockedExchange(&g_thinking, thinking ? 1 : 0);
}

}  // namespace robot
