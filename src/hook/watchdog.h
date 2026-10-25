// The hang watchdog (docs/subsystems/hangs.md): when the game's main thread
// stops looking at its window messages for a while (Windows then calls the
// window "not responding"), a background thread looks where it is stuck and
// writes that to open-homm5.log, once per freeze. It only looks: it never stops,
// kills or changes anything.
#pragma once
#include <windows.h>

namespace watchdog {

// [debug] hang_watchdog=1 (the default): the exe's PeekMessageA / GetMessageA
// import entries count the main thread's progress, and the watching thread
// starts. [debug] hang_seconds (default 20) and hang_test (F12 freezes the
// game on purpose, for a hand test).
void Install();

// A mod's own state for the report. Called on the watchdog's thread, after the
// main thread was looked at (it runs again then): it must only read the mod's
// own memory (the game may be in the middle of anything) and write log lines.
typedef void (*Reporter)();
void AddReporter(Reporter reporter);

// Called before each call of the game's AI turn driver (stage = the AI's stage):
// [debug] hang_test_ai_step=N (robot tests) freezes the main thread for 3 s
// once, instead of the first call that would start an AI turn from the N-th
// call on (true: skip that call), so that the frames after it catch up
// (several segments per frame) as the AI starts its turn.
bool OnAiStep(DWORD stage);

// A name for one of our own functions, so that a return address in our DLL can
// be shown as "after <name>+0x12" (a guess: the linker may put another function
// right after it; the build's map file, build\hook\dinput8.map, is exact).
void NameFunction(const void *function, const char *name);

#ifdef WATCHDOG_TEST
// For src/hook/test/watchdog_test.cpp: watch `thread`, whose progress is
// BeatForTest(), with a short stall time, without the game.
void StartForTest(DWORD thread, DWORD stall_ms);
void BeatForTest();
struct TestResult {
	int reports;      // reports written so far
	int samples;      // samples in the last one
	DWORD top_eip;    // the most frequent EIP of the last one
	int top_count;
	DWORD busy_ms;    // the watched thread's CPU time during the samples
	DWORD window_ms;  // how long the samples took
	int stack_hits;   // return addresses found in at least half of the samples
	bool found_return;  // the address given to ExpectReturnForTest was one of them
	int eip_in_range;   // samples whose EIP was in the range given to ExpectEipForTest
};
TestResult LastResultForTest();
// A return address the test expects on the watched thread's stack.
void ExpectReturnForTest(DWORD address);
// Where the test expects EIP: [from, to).
void ExpectEipForTest(DWORD from, DWORD to);
#endif

}  // namespace watchdog
