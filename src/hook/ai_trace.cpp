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

void __fastcall TurnDriverHook(void *self, void *edx, void *commands, int flag)
{
	const DWORD stage_before = Field(self, kOffStage);
	LastCall &last = SlotFor(self);
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	const double gap = last.returned.QuadPart ? Ms(last.returned, t0) : -1.0;
	robot::OnAiThinking(true);  // for the AI profiler (robot mode only)
	g_original(self, edx, commands, flag);
	robot::OnAiThinking(false);
	QueryPerformanceCounter(&t1);
	last.returned = t1;
	const DWORD stage_after = Field(self, kOffStage);
	robot::OnAiStep();
	if (stage_before == 0 && stage_after != 0)  // stage 0 = InitDay: a new turn began
		robot::OnAiTurnStart(self);
	char types[96];
	CommandTypes(commands, types, sizeof(types));
	hooklog::Write("ai-step ai=%p flag=%d stage %lu->%lu calls=%lu cmds=%d turn_age=%lums think=%.2fms gap=%.1fms types=%s",
	               self, flag & 0xff, stage_before, stage_after, Field(self, kOffCallCount),
	               CommandCount(commands), GetTickCount() - Field(self, kOffTurnStart), Ms(t0, t1), gap, types);
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
	if (PatchPointer(kVftableSlot, kTurnDriver, reinterpret_cast<DWORD>(&TurnDriverHook)))
		hooklog::Write("hooked AI turn driver (CPlayerAI vftable slot 0 at %08lx)", kVftableSlot);
	else
		hooklog::Write("could not hook the AI turn driver: vftable slot changed or not writable");
}

}  // namespace hooks
