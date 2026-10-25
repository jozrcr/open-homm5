// Mod: the AI decides at most once per frame, and never before its own last
// moves have happened: what the game itself guarantees with its own timing
// (docs/subsystems/hangs.md, "Several AI decisions in one frame").
//
// The AI manager (0x006082f0, once per frame) first pumps the network (slot 3,
// 0x007ca7c0: it takes in the reports and runs the segments that are complete,
// through the segment runner 0x007c98e0, its only caller), then loops once per
// segment this PC closes, and calls the AI's turn driver in each pass. The
// AI's commands join this PC's open segment (number = its report count) and
// run when that segment runs: in a later frame's pump, never in the same frame.
// With the game's timing (200 ms segments, the timer moving at most 150 ms per
// frame) the loop makes at most one pass per frame, so the AI never decides
// twice before its last moves have run. Our co-op timing (50 ms segments,
// catch_up_ms=600) allows up to 12 passes per frame: in the owner's game that
// froze, 17% of the AI's decisions came in the same frame as the one before.
//
// With [ai] one_decision_per_frame=1 (the default), in a multiplayer game:
//   1. the turn driver is called at most once per frame (once between two
//      pumps), and
//   2. after a call that produced commands, it is called again only when the
//      segment holding them has run: no segment record numbered up to it is
//      left in the transceiver's list (+0x38) after a pump.
// A call held back is simply not made: the manager sends an empty list, as for
// a call that decided nothing, and the AI is called in a later frame. Only the
// host runs the AI and every PC applies the commands it sends, so this cannot
// desync. If the wait ever lasted 5 s (it should not), the AI goes on anyway
// and the log says so once. 0 = the 0.2.x behaviour.
#include "ai_pacing.h"

#include <windows.h>
#include <string.h>

#include "../hook/log.h"
#include "../hook/patch.h"
#include "../hook/watchdog.h"

namespace mods {
namespace {

const DWORD kMpTransceiverVftable = 0x00e43f1c;  // NWorld::CMultiPlayerTransceiver
const DWORD kRunner = 0x007c98e0;                // __thiscall(transceiver), runs every complete segment
const DWORD kRunnerCalls[] = {0x007ca957, 0x007ca97d};  // its only two callers, in the pump
// sub esp,40h; mov eax,[ecx+38h]; push ebx..edi; mov edi,[eax]; add edi,8: the
// list of segment records at +0x38 (a sentinel node; node +0 = next, node +8 =
// the record), and at 0x007c9927 mov ebp,[edi]: the record starts with its number.
const BYTE kRunnerStart[] = {0x83, 0xec, 0x40, 0x8b, 0x41, 0x38, 0x53, 0x55, 0x56, 0x57, 0x8b, 0x38, 0x83, 0xc7, 0x08};
const DWORD kRunnerNumber = 0x007c9927;
const BYTE kRunnerNumberBytes[] = {0x8b, 0x2f};
const DWORD kStaleMs = 2000;    // no pump for this long: not in a multiplayer game now
const DWORD kMaxWaitMs = 5000;  // the safety valve

bool g_on = false;
typedef void(__fastcall *RunnerFn)(void *transceiver, void *edx);
RunnerFn g_runner_next = nullptr;

// After each pump (main thread only).
const BYTE *g_transceiver = nullptr;
DWORD g_seen = 0;           // GetTickCount() at the last pump
LONG g_pumps = 0;           // runner calls (one at the end of each pump call)
bool g_pending = false;     // a segment record was left after the last run
LONG g_lowest = 0;          // the lowest number of those

// The AI's side.
LONG g_call_pump = -1;      // g_pumps when the turn driver was last called
bool g_waiting = false;     // its last call produced commands that have not run yet
LONG g_wait_segment = 0;    // the segment they joined
DWORD g_wait_since = 0;
bool g_wait_held = false;   // that wait held back at least one call
bool g_valve_told = false;
// For the log, per AI turn: calls held for the next frame, held until the AI's
// own commands had run, and the longest of those waits.
LONG g_held_frame = 0, g_held_commands = 0;
DWORD g_longest_wait = 0;

DWORD Read(const void *base, DWORD offset)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(base) + offset);
}

void NoteSegments(const BYTE *t)
{
	__try {
		const BYTE *sentinel = reinterpret_cast<const BYTE *>(Read(t, 0x38));
		const BYTE *node = reinterpret_cast<const BYTE *>(Read(sentinel, 0));
		bool any = false;
		LONG lowest = 0;
		for (int i = 0; node && node != sentinel && i < 4096; ++i) {
			const LONG n = static_cast<LONG>(Read(node, 8));
			if (!any || n < lowest)
				lowest = n;
			any = true;
			node = reinterpret_cast<const BYTE *>(Read(node, 0));
		}
		g_pending = any;
		g_lowest = lowest;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_pending = false;  // unreadable: do not hold the AI
	}
}

void __fastcall RunnerHook(void *self, void *edx)
{
	g_runner_next(self, edx);
	const BYTE *t = static_cast<const BYTE *>(self);
	if (!t || Read(t, 0) != kMpTransceiverVftable)
		return;
	g_transceiver = t;
	g_seen = GetTickCount();
	++g_pumps;
	NoteSegments(t);
}

bool InMultiplayer()
{
	return g_transceiver && GetTickCount() - g_seen < kStaleMs;
}

// The segment this PC's commands join now: its report count (transceiver +0x3c,
// one per player; this PC is +0x54), or -1.
LONG OpenSegment()
{
	const BYTE *t = g_transceiver;
	const int me = static_cast<int>(Read(t, 0x54));
	const int players = static_cast<int>((Read(t, 0x40) - Read(t, 0x3c)) / 4);
	if (me < 0 || me >= players || players > 8)
		return -1;  // a PC that only watches: it runs no AI
	return reinterpret_cast<const LONG *>(Read(t, 0x3c))[me];
}

void ReportForWatchdog()
{
	if (!InMultiplayer()) {
		hooklog::Write("watchdog: ai pacing: not in a multiplayer game now (no pump for %lu ms)",
		               g_seen ? GetTickCount() - g_seen : 0);
		return;
	}
	hooklog::Write("watchdog: ai pacing: %s; segments still to run: %s %ld; this turn %ld calls held for the next "
	               "frame, %ld until the AI's own commands had run",
	               g_waiting ? "the AI waits for its commands to run" : "the AI does not wait",
	               g_pending ? "from" : "none,", g_lowest, g_held_frame, g_held_commands);
}

}  // namespace

bool AiPacingAllowsStep()
{
	if (!g_on || !InMultiplayer()) {
		g_waiting = false;
		return true;
	}
	if (g_call_pump == g_pumps) {
		++g_held_frame;  // already called since the last pump: this frame
		return false;
	}
	if (g_waiting) {
		const DWORD waited = GetTickCount() - g_wait_since;
		const bool ran = !g_pending || g_lowest > g_wait_segment;
		if (!ran && waited < kMaxWaitMs) {
			++g_held_commands;
			g_wait_held = true;
			return false;
		}
		if (!ran && !g_valve_told) {
			g_valve_told = true;
			hooklog::Write("ai pacing: the AI waited %lu ms for its commands of segment %ld to run (segments from %ld "
			               "still to run): letting it go on (said once)", waited, g_wait_segment, g_lowest);
		}
		if (g_wait_held && waited > g_longest_wait)
			g_longest_wait = waited;  // (only waits that held a call: the AI is not called at all most of the time)
		g_waiting = false;
		g_wait_held = false;
	}
	g_call_pump = g_pumps;
	return true;
}

void AiPacingAfterStep(int commands, unsigned long stage_before, unsigned long stage_after)
{
	if (!g_on || !InMultiplayer())
		return;
	if (commands > 0) {
		const LONG segment = OpenSegment();
		if (segment >= 0) {
			g_waiting = true;
			g_wait_segment = segment;
			g_wait_since = GetTickCount();
		}
	}
	if (stage_before != 0 && stage_after == 0) {  // the turn is over (CEndOfTurn)
		hooklog::Write("ai pacing: this AI turn: %ld calls held for the next frame, %ld until the AI's own commands had "
		               "run (the longest such wait %lu ms)", g_held_frame, g_held_commands, g_longest_wait);
		g_held_frame = g_held_commands = 0;
		g_longest_wait = 0;
	}
}

void ApplyAiPacingFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	if (!GetPrivateProfileIntW(L"ai", L"one_decision_per_frame", 1, path)) {
		hooklog::Write("ai pacing: off: the AI may decide several times per frame in multiplayer games "
		               "([ai] one_decision_per_frame=0)");
		return;
	}
	if (memcmp(reinterpret_cast<const void *>(kRunner), kRunnerStart, sizeof(kRunnerStart)) != 0 ||
	    memcmp(reinterpret_cast<const void *>(kRunnerNumber), kRunnerNumberBytes, sizeof(kRunnerNumberBytes)) != 0) {
		hooklog::Write("ai pacing: the multiplayer segment code is not as expected: off");
		return;
	}
	g_runner_next = reinterpret_cast<RunnerFn>(hookpatch::RedirectCalls(kRunnerCalls, 2, kRunner, &RunnerHook));
	if (!g_runner_next) {
		hooklog::Write("ai pacing: the multiplayer pump is not as expected: off");
		return;
	}
	g_on = true;
	watchdog::AddReporter(&ReportForWatchdog);
	watchdog::NameFunction(&RunnerHook, "ai_pacing RunnerHook");
	hooklog::Write("ai pacing: in multiplayer games the AI decides at most once per frame, and only once its own last "
	               "moves have run (as with the game's own timing)");
}

}  // namespace mods
