// Test the hang watchdog (watchdog.cpp) without the game.
//
// This thread plays the game's main thread: it "takes its messages" (beats)
// for a while, then freezes twice, once busy (a spinning loop, as in the
// owner's freeze) and once blocked (a long Sleep). The watchdog must notice
// each freeze once, look at this thread 20 times without harming it, and find:
//   - busy: EIP in the spinning function, the return address into its caller
//     on the stack, and ~100% CPU;
//   - blocked: little CPU.
// and say when the beats come back. Each freeze lasts 4.5 s; the watchdog
// looks after 1.5 s without a beat (the game: 20 s).
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <string.h>

#include "../log.h"
#include "../watchdog.h"

#pragma intrinsic(_ReturnAddress)

namespace {

volatile DWORD g_counter = 0;
volatile DWORD g_after = 0;

__declspec(noinline) void Spin(DWORD ms)
{
	watchdog::ExpectReturnForTest(reinterpret_cast<DWORD>(_ReturnAddress()));
	const DWORD start = GetTickCount();
	for (;;) {
		for (int i = 0; i < 200000; ++i)
			g_counter = g_counter + 1;
		if (GetTickCount() - start >= ms)
			break;
	}
}

__declspec(noinline) void Caller(DWORD ms)
{
	Spin(ms);
	g_after = g_after + 1;  // (so the call is not turned into a jump)
}

void Beat(DWORD ms)
{
	const DWORD start = GetTickCount();
	while (GetTickCount() - start < ms) {
		watchdog::BeatForTest();
		Sleep(20);
	}
}

int g_failures = 0;

void Check(bool ok, const char *what)
{
	printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
	if (!ok)
		++g_failures;
}

int CountInLog(const char *text)
{
	FILE *f = nullptr;
	int n = 0;
	char line[1200];
	if (fopen_s(&f, "open-homm5.log", "r") != 0 || !f)
		return 0;
	while (fgets(line, sizeof(line), f))
		n += strstr(line, text) != nullptr;
	fclose(f);
	return n;
}

}  // namespace

int main()
{
	DeleteFileW(L"open-homm5.log");
	hooklog::Open(nullptr);
	const DWORD spin = reinterpret_cast<DWORD>(&Spin);
	watchdog::ExpectEipForTest(spin, spin + 0x100);
	watchdog::StartForTest(GetCurrentThreadId(), 1500);

	Beat(1200);
	Caller(4500);  // frozen, busy
	Beat(1500);
	const watchdog::TestResult busy = watchdog::LastResultForTest();
	Sleep(4500);   // frozen, blocked
	Beat(1500);
	const watchdog::TestResult blocked = watchdog::LastResultForTest();
	hooklog::Close();

	printf("busy: %d report(s), %d samples, %d with EIP in Spin (at %08lx), top EIP %08lx x%d, CPU %lu of %lu ms, "
	       "%d stack hits, caller's return address %s\n",
	       busy.reports, busy.samples, busy.eip_in_range, spin, busy.top_eip, busy.top_count, busy.busy_ms,
	       busy.window_ms, busy.stack_hits, busy.found_return ? "found" : "NOT found");
	printf("blocked: %d report(s), %d samples, CPU %lu of %lu ms\n", blocked.reports, blocked.samples, blocked.busy_ms,
	       blocked.window_ms);
	Check(busy.reports == 1, "busy freeze: exactly one look");
	Check(busy.samples == 20, "busy freeze: 20 samples");
	Check(busy.eip_in_range >= 15, "busy freeze: EIP mostly in the spinning function");
	Check(busy.busy_ms * 10 >= busy.window_ms * 7, "busy freeze: the thread used most of the CPU time");
	Check(busy.found_return, "busy freeze: the return address into its caller is on the stack");
	Check(blocked.reports == 2, "blocked freeze: one more look (once per freeze)");
	Check(blocked.samples == 20, "blocked freeze: 20 samples");
	Check(blocked.busy_ms * 10 < blocked.window_ms, "blocked freeze: little CPU time");
	Check(CountInLog("watchdog: EIP") >= 2, "log: EIP lines");
	Check(CountInLog("watchdog: stack") >= 1, "log: stack lines");
	Check(CountInLog("busy (it is running") == 1 && CountInLog("mostly waiting") == 1, "log: busy, then waiting");
	Check(CountInLog("takes its messages again") == 2, "log: the end of each freeze");
	Check(g_after == 1, "the frozen thread went on normally after the looks");
	if (g_failures) {
		printf("watchdog test: %d failure(s)\n", g_failures);
		return 1;
	}
	printf("OK: the watchdog sees busy and blocked freezes, once each, and leaves the thread alone\n");
	return 0;
}
