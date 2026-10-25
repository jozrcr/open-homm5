// AI turn tracing: log every call of the AI turn driver with its timing.
//
// The adventure-map AI (NWorld::CPlayerAI) is driven by IPlayerAI slot 0,
// 0x0086dd40 (docs/subsystems/ai-turn.md). It is a __thiscall method with two
// stack arguments (it ends with "ret 8"). Its only caller, the AI manager at
// 0x006082f0, pushes them like this (0x00608728):
//     push flag            ; 2nd argument, a bool
//     lea  eax,[commands]  ; 1st argument, an empty list on the caller's stack
//     push eax
//     call [edx]           ; ai->slot0(&commands, flag)
// and then sends every command the AI put in the list. Every CPlayerAI object
// uses the vftable at 0x00e4c8bc, so replacing slot 0 there routes all calls
// through us, without patching code.
#include <windows.h>
#include <stdio.h>

#include "hooks.h"
#include "log.h"
#include "robot.h"
#include "watchdog.h"
#include "../mods/fast_paths.h"

namespace {

const DWORD kVftableSlot = 0x00e4c8bc;
const DWORD kTurnDriver = 0x0086dd40;

// Offsets inside CPlayerAI (object size 0x148), read from the turn driver's code.
const DWORD kOffCallCount = 0x10;    // incremented at the end of each call
const DWORD kOffStage = 0x128;       // current stage of the turn's state machine
const DWORD kOffTurnStart = 0x12c;   // GetTickCount() when the turn started

// A __thiscall function with 2 stack arguments looks like this __fastcall one
// (this in ecx, edx unused, callee pops the 8 bytes of arguments).
typedef void(__fastcall *TurnDriverFn)(void *self, void *edx, void *commands, int flag);
TurnDriverFn g_original = reinterpret_cast<TurnDriverFn>(kTurnDriver);

LARGE_INTEGER g_freq;

// When each AI object's previous call returned, to measure the wait between
// calls (hypothesis H1 in docs/subsystems/ai-turn.md: in multiplayer the AI
// waits a network round trip after every command it sends).
struct LastCall {
	void *ai;
	LARGE_INTEGER returned;
};
LastCall g_last[16];

double Ms(LARGE_INTEGER from, LARGE_INTEGER to)
{
	return 1000.0 * static_cast<double>(to.QuadPart - from.QuadPart) / static_cast<double>(g_freq.QuadPart);
}

LastCall &SlotFor(void *ai)
{
	for (LastCall &c : g_last) {
		if (c.ai == ai)
			return c;
	}
	for (LastCall &c : g_last) {
		if (!c.ai) {
			c.ai = ai;
			return c;
		}
	}
	return g_last[0];  // more than 16 AI objects: share a slot (gap values get wrong, nothing worse)
}

DWORD Field(void *self, DWORD offset)
{
	return *reinterpret_cast<DWORD *>(static_cast<char *>(self) + offset);
}

int CommandCount(void *commands)
{
	// a vector: { begin, end, capacity } of 4-byte elements. Guarded, because we
	// only inferred what this argument is: a bad guess must not crash the game.
	if (!commands)
		return -1;
	__try {
		const DWORD *v = static_cast<const DWORD *>(commands);
		return static_cast<int>((v[1] - v[0]) / 4);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

// The vftable address of each command the AI produced (first 8), e.g.
// "00e2c1a8,00e2c1a8". tools/robot maps them to class names (analysis/classes.txt).
void CommandTypes(void *commands, char *out, size_t size)
{
	out[0] = 0;
	if (!commands)
		return;
	__try {
		const DWORD *v = static_cast<const DWORD *>(commands);
		const DWORD *items = reinterpret_cast<const DWORD *>(v[0]);
		const int n = static_cast<int>((v[1] - v[0]) / 4);
		size_t used = 0;
		for (int i = 0; i < n && i < 8 && used + 10 < size; ++i) {
			const DWORD object = items[i];
			const DWORD vtable = object ? *reinterpret_cast<const DWORD *>(object) : 0;
			used += static_cast<size_t>(_snprintf_s(out + used, size - used, _TRUNCATE, i ? ",%08lx" : "%08lx", vtable));
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		out[0] = 0;
	}
}

// --- for the hang watchdog (watchdog.h): where the AI is, and its last steps ---------
// Written by the main thread only; the watchdog reads them after it looked at
// the main thread (a value being written may be off by one step, nothing worse).
volatile LONG g_in_step = 0;    // 1 while the game's turn driver runs
void *volatile g_step_ai = nullptr;
volatile DWORD g_step_stage = 0;  // the stage when it was called
volatile DWORD g_step_calls = 0;
volatile DWORD g_step_since = 0;  // GetTickCount() when it was called
struct Step {
	DWORD tick;  // when it returned
	void *ai;
	DWORD from, to;
	int cmds;
	float think_ms, gap_ms;
	DWORD first_type;  // the vtable of its first command, 0 = none
};
const int kSteps = 20;
Step g_steps[kSteps];
volatile LONG g_step_count = 0;  // steps recorded so far (the ring holds the last kSteps)

const char *StageName(DWORD stage)
{
	static const char *const kNames[] = {
		"INIT_DAY", "MOVE_HERO", "COLLECT_PASSABLE_OBJECTS", "BUILD_HERO_CHOICES", "ESTIMATE_CHOICES",
		"EXECUTE_BEST_CHOICE", "MANAGE_TOWNS", "HIRE_HEROES", "LAST_WAIT", "RETURN_TURN", "START_CHOOSE_DESTINATION",
		"CHOOSE_DESTINATION", "MANAGE_HEROES", "STEP_REARRANGE_TOWN_HEROES", "STEP_ORDER_CARAVANS",
	};  // the game's own names (0x00866550)
	return stage < sizeof(kNames) / sizeof(kNames[0]) ? kNames[stage] : "?";
}

DWORD FirstType(void *commands)
{
	__try {
		const DWORD *v = static_cast<const DWORD *>(commands);
		if (!commands || v[1] <= v[0])
			return 0;
		const DWORD object = *reinterpret_cast<const DWORD *>(v[0]);
		return object ? *reinterpret_cast<const DWORD *>(object) : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

void ReportForWatchdog()
{
	const DWORD now = GetTickCount();
	if (g_in_step)
		hooklog::Write("watchdog: AI: INSIDE the game's AI turn driver for %lu ms (ai=%p, called at stage %lu %s, its "
		               "call %lu)", now - g_step_since, g_step_ai, g_step_stage, StageName(g_step_stage),
		               g_step_calls + 1);
	else
		hooklog::Write("watchdog: AI: not inside the AI turn driver");
	const LONG count = g_step_count;
	if (!count) {
		hooklog::Write("watchdog: AI: no AI step since the game started");
		return;
	}
	// the last steps, oldest first, 5 per line
	const LONG first = count > kSteps ? count - kSteps : 0;
	char line[700];
	size_t used = 0;
	for (LONG i = first; i < count; ++i) {
		const Step &s = g_steps[i % kSteps];
		const int n = _snprintf_s(line + used, sizeof(line) - used, _TRUNCATE, "%s%lu ms ago ai=%p %lu->%lu cmds=%d (%08lx) "
		                          "think %.0f gap %.0f", used ? "; " : "", now - s.tick, s.ai, s.from, s.to, s.cmds,
		                          s.first_type, s.think_ms, s.gap_ms);
		used = n > 0 ? used + static_cast<size_t>(n) : sizeof(line) - 1;
		if ((i - first) % 5 == 4 || i + 1 == count) {
			hooklog::Write("watchdog: AI: last steps: %s", line);
			used = 0;
			line[0] = 0;
		}
	}
}

void __fastcall TurnDriverHook(void *self, void *edx, void *commands, int flag)
{
	const DWORD stage_before = Field(self, kOffStage);
	LastCall &last = SlotFor(self);
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	const double gap = last.returned.QuadPart ? Ms(last.returned, t0) : -1.0;
	g_step_ai = self;
	g_step_stage = stage_before;
	g_step_calls = Field(self, kOffCallCount);
	g_step_since = GetTickCount();
	g_in_step = 1;
	robot::OnAiThinking(true);  // for the AI profiler (robot mode only)
	mods::FastPathsOnAiStep(true);
	g_original(self, edx, commands, flag);
	mods::FastPathsOnAiStep(false);
	robot::OnAiThinking(false);
	g_in_step = 0;
	QueryPerformanceCounter(&t1);
	last.returned = t1;
	const DWORD stage_after = Field(self, kOffStage);
	robot::OnAiStep();
	if (stage_before == 0 && stage_after != 0)  // stage 0 = InitDay: a new turn began
		robot::OnAiTurnStart(self);
	char types[96];
	CommandTypes(commands, types, sizeof(types));
	const int cmds = CommandCount(commands);
	const double think = Ms(t0, t1);
	Step &s = g_steps[g_step_count % kSteps];
	s = {GetTickCount(), self, stage_before, stage_after, cmds, static_cast<float>(think), static_cast<float>(gap),
	     FirstType(commands)};
	g_step_count = g_step_count + 1;
	hooklog::Write("ai-step ai=%p flag=%d stage %lu->%lu calls=%lu cmds=%d turn_age=%lums think=%.2fms gap=%.1fms types=%s",
	               self, flag & 0xff, stage_before, stage_after, Field(self, kOffCallCount), cmds,
	               GetTickCount() - Field(self, kOffTurnStart), think, gap, types);
}

bool PatchPointer(DWORD address, DWORD expected, DWORD replacement)
{
	DWORD *slot = reinterpret_cast<DWORD *>(address);
	if (*slot != expected)
		return false;
	DWORD old;
	if (!VirtualProtect(slot, sizeof(DWORD), PAGE_READWRITE, &old))
		return false;
	*slot = replacement;
	VirtualProtect(slot, sizeof(DWORD), old, &old);
	return true;
}

}  // namespace

namespace hooks {

#ifdef AI_TRACE_TEST
// For src/hook/test/ai_trace_test.cpp: call the hook without the game.
void *AiTraceHookForTest(void *fake_driver)
{
	QueryPerformanceFrequency(&g_freq);
	g_original = reinterpret_cast<TurnDriverFn>(fake_driver);
	return reinterpret_cast<void *>(&TurnDriverHook);
}
#endif

void InstallAiTrace()
{
	QueryPerformanceFrequency(&g_freq);
	if (PatchPointer(kVftableSlot, kTurnDriver, reinterpret_cast<DWORD>(&TurnDriverHook))) {
		hooklog::Write("hooked AI turn driver (CPlayerAI vftable slot 0 at %08lx)", kVftableSlot);
		watchdog::AddReporter(&ReportForWatchdog);
		watchdog::NameFunction(&TurnDriverHook, "ai_trace TurnDriverHook");
	}
	else {
		hooklog::Write("could not hook the AI turn driver: vftable slot changed or not writable");
	}
}

}  // namespace hooks
