// Mod: a quicker multiplayer lockstep (docs/plans/responsiveness.md).
//
// A multiplayer game runs a segment only when every PC has reported it done
// (docs/subsystems/multiplayer-sync.md). Two things in NWorld::
// CMultiPlayerTransceiver make each click and each AI move wait longer than
// the network needs:
//
// 1. The message pump (vtable slot 3, 0x007ca7c0, called once per frame) reads
//    the network messages in order but returns after the first "segment done"
//    report it finds (the type-0 branch at 0x007ca920 ends the call). So a PC
//    takes in one other player's report per frame: with 4 PCs and 100 ms
//    segments that is 30 reports a second, and a PC below about 30 frames a
//    second falls behind; the game's pacing then slows everyone down.
//    [net] drain_reports=1 calls the pump again while it still takes in a
//    report, up to a limit, so all reports that have arrived count this
//    frame. The pump does nothing else per call than read messages and run
//    the segments that are complete, as later frames would; the order of
//    everything stays the same, so the PCs stay in step.
//
// 2. The game time the screen and the AI follow ("now", slot 8 at 0x007c8b00)
//    is the transceiver timer minus 400 ms, so that a segment's results are
//    shown only once every PC surely has them. With our 100 ms segments the
//    wait is usually much shorter, but every move still starts 400 ms late.
//    [net] view_delay_ms (100..400, the game: 400) sets that margin. It only
//    moves when this PC shows things and when its AI goes on; the game world
//    itself follows the segment times, the same on every PC. Too small, and a
//    move whose segment arrives late starts with a small jump.
//
// 3. Experiment, [net] catch_up_ms (150..1000, the game: 150): the timer that
//    closes this PC's segments moves at most 150 ms per frame (0x00475d80,
//    two immediates 0x96). After a frame that froze longer (390 ms in robot
//    games, during AI turns), the PC stays behind for good, and the others
//    wait for it. A higher cap lets it catch up. Only this PC's own pace
//    changes; the game world still follows the segments.
//
// [net] trace_lockstep=1 (the default; 0 = off) writes, every 10 seconds, how long this PC waited
// from closing its own segment to running it, who it waited for last, the
// frames per second and how many reports a drain took in, so a log from a
// real game shows who everyone waits for.
#include <windows.h>
#include <string.h>

#include "lockstep.h"

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

const DWORD kMpTransceiverVftable = 0x00e43f1c;  // NWorld::CMultiPlayerTransceiver
const DWORD kPumpSlot = kMpTransceiverVftable + 3 * 4;
const DWORD kPump = 0x007ca7c0;
const DWORD kViewDelayImmediate = 0x007c8b07;  // sub eax, 0x190 in GetTime (0x007c8b00)
const DWORD kCatchUpImmediates[] = {0x00475da4, 0x00475da9};  // mov [esp+8], 0x96 / cmp eax, 0x96
const int kMaxPlayers = 8;
const int kRing = 256;  // segments remembered for the trace (25 s at 100 ms)

typedef void(__fastcall *PumpFn)(void *transceiver, void *edx);
PumpFn g_pump_next = nullptr;
bool g_drain = false;
bool g_trace = false;

DWORD Read(const void *base, DWORD offset)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(base) + offset);
}

// The reported counts (transceiver +0x3c, one int per player): how many
// segments each player has reported done.
int Players(const BYTE *t)
{
	return static_cast<int>((Read(t, 0x40) - Read(t, 0x3c)) / 4);
}

const LONG *Reported(const BYTE *t)
{
	return reinterpret_cast<const LONG *>(Read(t, 0x3c));
}

LONG Total(const BYTE *t, int players)
{
	LONG sum = 0;
	for (int p = 0; p < players; ++p)
		sum += Reported(t)[p];
	return sum;
}

// --- the trace ------------------------------------------------------------------
double Ms()
{
	static LARGE_INTEGER freq = {};
	LARGE_INTEGER now;
	if (!freq.QuadPart)
		QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&now);
	return now.QuadPart * 1000.0 / freq.QuadPart;
}

struct Trace {
	const BYTE *transceiver;
	int players;
	LONG last[kMaxPlayers];            // reported counts at the last look
	double reported_at[kRing][kMaxPlayers];  // when each player's report of segment n arrived here
	LONG ran;                           // segments run so far (smallest count of all players)
	double window_start, last_frame, longest_frame;
	int frames, drains_extra, max_drain;
	int segments;
	double wait_sum, wait_max;
	int last_reporter[kMaxPlayers];     // how often each player was the last one waited for
};
Trace g_trace_state;

void TraceReset(const BYTE *t, int players, double now)
{
	memset(&g_trace_state, 0, sizeof(g_trace_state));
	g_trace_state.transceiver = t;
	g_trace_state.players = players;
	for (int p = 0; p < players; ++p)
		g_trace_state.last[p] = Reported(t)[p];
	LONG low = g_trace_state.last[0];
	for (int p = 1; p < players; ++p)
		low = g_trace_state.last[p] < low ? g_trace_state.last[p] : low;
	g_trace_state.ran = low;
	g_trace_state.window_start = g_trace_state.last_frame = now;
}

// Notes the reports that arrived (and this PC's own ones) since the last look,
// and the segments that became complete: a segment runs when every player has
// reported it, so its wait is from this PC's own report to the last report.
void TraceLook(const BYTE *t, double now)
{
	Trace &s = g_trace_state;
	const int me = static_cast<int>(Read(t, 0x54));
	for (int p = 0; p < s.players; ++p) {
		for (LONG n = s.last[p]; n < Reported(t)[p]; ++n)
			s.reported_at[n % kRing][p] = now;
		s.last[p] = Reported(t)[p];
	}
	LONG low = s.last[0];
	for (int p = 1; p < s.players; ++p)
		low = s.last[p] < low ? s.last[p] : low;
	for (; s.ran < low; ++s.ran) {
		const double *at = s.reported_at[s.ran % kRing];
		int last = 0;
		for (int p = 1; p < s.players; ++p)
			if (at[p] > at[last])
				last = p;
		if (me >= 0 && me < s.players && at[me] > 0) {
			const double wait = at[last] - at[me];
			s.wait_sum += wait;
			if (wait > s.wait_max)
				s.wait_max = wait;
			s.segments++;
			s.last_reporter[last]++;
		}
	}
}

void TraceWindow(const BYTE *t, double now)
{
	Trace &s = g_trace_state;
	const double span = now - s.window_start;
	if (span < 10000)
		return;
	const int me = static_cast<int>(Read(t, 0x54));
	char who[96] = "";
	for (int p = 0; p < s.players; ++p) {
		char one[16];
		wsprintfA(one, "%s%d%s:%d", p ? " " : "", p, p == me ? "(me)" : "", s.last_reporter[p]);
		lstrcatA(who, one);
	}
	LONG low = s.last[0], high = s.last[0];
	for (int p = 1; p < s.players; ++p) {
		low = s.last[p] < low ? s.last[p] : low;
		high = s.last[p] > high ? s.last[p] : high;
	}
	hooklog::Write("lockstep: %d segments in %d s, waited %d ms on average (longest %d) after this PC's own report; "
	               "last report from player [%s]; %d frames/s (longest frame %d ms); drain took %d extra reports "
	               "(at most %d in a frame); spread %ld segments",
	               s.segments, static_cast<int>(span / 1000), s.segments ? static_cast<int>(s.wait_sum / s.segments) : 0,
	               static_cast<int>(s.wait_max), who, static_cast<int>(s.frames * 1000.0 / span),
	               static_cast<int>(s.longest_frame), s.drains_extra, s.max_drain, high - low);
	s.window_start = now;
	s.frames = s.drains_extra = s.max_drain = s.segments = 0;
	s.wait_sum = s.wait_max = s.longest_frame = 0;
	memset(s.last_reporter, 0, sizeof(s.last_reporter));
}

// --- the pump ---------------------------------------------------------------------
void __fastcall PumpHook(void *self, void *edx)
{
	const BYTE *t = static_cast<const BYTE *>(self);
	const int players = Players(t);
	const bool ok = players > 0 && players <= kMaxPlayers;
	double now = 0;
	if (g_trace && ok) {
		now = Ms();
		// A new game: another transceiver, or the counts started again.
		if (t != g_trace_state.transceiver || players != g_trace_state.players ||
		    Reported(t)[0] < g_trace_state.last[0])
			TraceReset(t, players, now);
		const double frame = now - g_trace_state.last_frame;
		if (frame > g_trace_state.longest_frame)
			g_trace_state.longest_frame = frame;
		g_trace_state.last_frame = now;
		g_trace_state.frames++;
		TraceLook(t, now);
	}

	LONG before = ok ? Total(t, players) : 0;
	g_pump_next(self, edx);
	if (ok && g_drain) {
		// The pump stops after one report; go on while it takes in one more.
		// At most two rounds of reports per frame, so a flood can't stall a frame.
		int extra = 0;
		for (int i = 0; i < 2 * players; ++i) {
			const LONG after = Total(t, players);
			if (after == before)
				break;
			before = after;
			g_pump_next(self, edx);
			++extra;
		}
		if (g_trace) {
			g_trace_state.drains_extra += extra;
			if (extra > g_trace_state.max_drain)
				g_trace_state.max_drain = extra;
		}
	}
	if (g_trace && ok) {
		now = Ms();
		TraceLook(t, now);
		TraceWindow(t, now);
	}
}

// Replaces the 4-byte immediate at `address` if it holds `expected`.
bool SetImmediate(DWORD address, DWORD expected, DWORD value)
{
	DWORD *imm = reinterpret_cast<DWORD *>(address);
	if (*imm != expected)
		return false;
	DWORD old;
	if (!VirtualProtect(imm, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &old))
		return false;
	*imm = value;
	VirtualProtect(imm, sizeof(DWORD), old, &old);
	FlushInstructionCache(GetCurrentProcess(), imm, sizeof(DWORD));
	return true;
}

}  // namespace

void ApplyLockstepFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	g_drain = GetPrivateProfileIntW(L"net", L"drain_reports", 1, path) != 0;
	g_trace = GetPrivateProfileIntW(L"net", L"trace_lockstep", 1, path) != 0;
	const DWORD view = GetPrivateProfileIntW(L"net", L"view_delay_ms", 400, path);
	const DWORD catch_up = GetPrivateProfileIntW(L"net", L"catch_up_ms", 150, path);

	if (view != 400) {
		if (view < 100 || view > 400)
			hooklog::Write("lockstep: view_delay_ms must be 100..400, not %lu: keeping the game's 400 ms", view);
		else if (!SetImmediate(kViewDelayImmediate, 400, view))
			hooklog::Write("lockstep: the game time code is not as expected: keeping the game's 400 ms");
		else
			hooklog::Write("lockstep: moves are shown %lu ms after their segment (game: 400)", view);
	}
	if (catch_up != 150) {
		if (catch_up < 150 || catch_up > 1000)
			hooklog::Write("lockstep: catch_up_ms must be 150..1000, not %lu: keeping the game's 150 ms", catch_up);
		else if (*reinterpret_cast<const DWORD *>(kCatchUpImmediates[0]) != 150 ||
		         *reinterpret_cast<const DWORD *>(kCatchUpImmediates[1]) != 150)
			hooklog::Write("lockstep: the timer code is not as expected: keeping the game's 150 ms");
		else {
			SetImmediate(kCatchUpImmediates[0], 150, catch_up);
			SetImmediate(kCatchUpImmediates[1], 150, catch_up);
			hooklog::Write("lockstep: the segment timer may move %lu ms in one frame (game: 150)", catch_up);
		}
	}
	if (!g_drain && !g_trace)
		return;
	g_pump_next = reinterpret_cast<PumpFn>(hookpatch::PatchPointer(kPumpSlot, kPump, &PumpHook));
	if (!g_pump_next) {
		hooklog::Write("lockstep: the multiplayer code is not as expected: drain and trace off");
		g_drain = g_trace = false;
		return;
	}
	if (g_drain)
		hooklog::Write("lockstep: each frame takes in every segment report that has arrived (game: one)");
	if (g_trace)
		hooklog::Write("lockstep: trace on, a summary every 10 s");
}

}  // namespace mods
