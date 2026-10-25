// Mod: a quicker multiplayer lockstep (docs/plans/responsiveness.md).
//
// A multiplayer game runs a segment only when every PC has reported it done
// (docs/subsystems/multiplayer-sync.md). Several things in NWorld::
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
//    shown only once every PC surely has them. With our shorter segments the
//    wait is usually much shorter, but every move still starts 400 ms late.
//    [net] view_delay_ms (50..400, default 100) sets that margin, but only
//    while the co-op guard's shorter segments are ON (every PC agreed, see
//    coop_guard.cpp, which calls LockstepCoopTiming); otherwise the game's 400
//    stays, since 200 ms segments wait longer. The owner chose a low default
//    (Q10: everyone on fiber). It only moves when this PC shows things and
//    when its AI goes on; the game world itself follows the segment times, the
//    same on every PC. Too small, and a move whose segment arrives late starts
//    with a small jump. The trace suggests a margin: the p80 of the waits.
//
// 3. Experiment, [net] catch_up_ms (150..1000, the game: 150): the timer that
//    closes this PC's segments moves at most 150 ms per frame (0x00475d80,
//    two immediates 0x96). After a frame that froze longer (390 ms in robot
//    games, during AI turns), the PC stays behind for good, and the others
//    wait for it. A higher cap lets it catch up. Only this PC's own pace
//    changes; the game world still follows the segments.
//
// 4. The pacing (slot 5, 0x007c8dd0) slows a PC that is ahead of the slowest
//    one: its timer runs at max(1 - 0.05 * lead, 0), lead in segments, capped
//    at 20 (where the speed reaches 0: this cap is what stops a PC from
//    running further ahead). The 0.05 (a float at 0x00e12f18, read by "fmul
//    dword ptr [0x00e12f18]" at 0x007c8ea2) was tuned for 200 ms segments: at
//    50 ms the same lead in milliseconds slows the PC 4 times more, and a PC
//    1 s ahead stops. [net] scale_pacing=1, while co-op timing is ON, points
//    that instruction at our own float, 0.05 * segment / 200 (0.0125 at
//    50 ms), and raises the cap to 20 * 200 / segment (80), so the pacing is
//    the game's per millisecond of lead: a PC stops 4 s ahead, as in the game.
//    Only this PC's own pace changes.
//
// 5. The clock: Windows wakes a sleeping thread every 15.6 ms unless a program
//    asks for a finer clock (timeBeginPeriod), and the game never does (no
//    import of it in the exe or its DLLs). Its network thread (0x0047cbd0)
//    loops with Sleep(10), so it really runs every 15.6 ms, and our 10 ms send
//    interval (net_tuning.cpp) is rounded up to that. While the drain or the
//    trace is on, the DLL asks for a 1 ms clock once, and [net] net_sleep_ms
//    (default 2, the game's 10) changes that Sleep(10) ("push 0x0a" at
//    0x0047cc05). Only how often the network is looked at changes.
//
// [net] trace_lockstep=1 (the default; 0 = off) writes, every 10 seconds, how long this PC waited
// from closing its own segment to running it, who it waited for last, the
// frames per second and how many reports a drain took in, so a log from a
// real game shows who everyone waits for. A second line times the commands
// this PC sent (slot 0, 0x007c9810): per kind, how long from the click (or AI
// decision) to the run of its segment, and when it is shown on this PC.
#include <windows.h>
#include <mmsystem.h>
#include <string.h>

#include <algorithm>

#include "lockstep.h"

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

const DWORD kMpTransceiverVftable = 0x00e43f1c;  // NWorld::CMultiPlayerTransceiver
const DWORD kSendSlot = kMpTransceiverVftable + 0 * 4;
const DWORD kSend = 0x007c9810;  // __thiscall(command *), ret 4
const DWORD kPumpSlot = kMpTransceiverVftable + 3 * 4;
const DWORD kPump = 0x007ca7c0;
const DWORD kViewDelayImmediate = 0x007c8b07;  // sub eax, 0x190 in GetTime (0x007c8b00)
const DWORD kCatchUpImmediates[] = {0x00475da4, 0x00475da9};  // mov [esp+8], 0x96 / cmp eax, 0x96
// The pacing reads the 0.05 with "fmul dword ptr [0x00e12f18]" (d8 0d +
// address) at 0x007c8ea2, when this PC plays. (The other one, at 0x007c8e64,
// is for a PC that only watches: its lead is (timer - 200 n - 30000 - base) /
// 200, in units of 200 ms whatever the segment length, so it stays the game's.)
const DWORD kPacingSlopeOperand = 0x007c8ea4;
const DWORD kGameSlope = 0x00e12f18;      // 0.05f
const DWORD kGameSlopeBits = 0x3d4ccccd;  // 0.05f
// The lead's cap of 20 segments: "cmp ecx, 0x14; jge 0x007c8ef3" at 0x007c8e7f
// and "mov eax, 0x14" at 0x007c8ef3.
const DWORD kPacingCapCmp = 0x007c8e7f;
const DWORD kPacingCapMov = 0x007c8ef3;
const DWORD kGameCap = 20;
// The network thread's loop (0x0047cbd0): "mov edi, [Sleep]" at 0x0047cbe4,
// then "push 0x0a; call edi" at 0x0047cc05.
const DWORD kNetSleepLoad = 0x0047cbe4;
const DWORD kNetSleepPush = 0x0047cc05;
const int kMaxPlayers = 8;
const int kRing = 256;     // segments remembered for the trace (12.8 s at 50 ms)
const int kPending = 256;  // commands sent whose segment has not run yet
const int kClasses = 10;   // kinds of commands timed per summary (more: "other")
const int kWaits = 1024;   // waits remembered per summary, for the p80

typedef void(__fastcall *PumpFn)(void *transceiver, void *edx);
typedef void(__fastcall *SendFn)(void *transceiver, void *edx, void *command);
PumpFn g_pump_next = nullptr;
SendFn g_send_next = nullptr;
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

// The margin of point 2 as it is now: the game's 400 or view_delay_ms.
DWORD Margin()
{
	return *reinterpret_cast<const DWORD *>(kViewDelayImmediate);
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

// A command this PC sent, waiting for its segment to run.
struct Pending {
	DWORD vtable;
	LONG segment;  // the segment it joined: this PC's report count when it was sent
	double sent;
};

// The commands of one kind (vtable) in this summary.
struct Kind {
	DWORD vtable;  // 0 = all the others
	int count;
	double run_sum, run_max, show_sum;
};

struct Trace {
	const BYTE *transceiver;
	DWORD thread;  // the thread of the pump (the game's main thread)
	int players;
	LONG last[kMaxPlayers];            // reported counts at the last look
	double reported_at[kRing][kMaxPlayers];  // when each player's report of segment n arrived here
	LONG ran;                           // segments run so far (smallest count of all players)
	double window_start, last_frame, longest_frame;
	int frames, drains_extra, max_drain;
	int segments;
	double wait_sum, wait_max;
	float waits[kWaits];  // this summary's waits, for the p80
	int last_reporter[kMaxPlayers];     // how often each player was the last one waited for
	Pending pending[kPending];          // a ring, oldest at `first`
	int first, queued, dropped;
	Kind kinds[kClasses + 1];
	int kind_count, commands;
};
Trace g_trace_state;

void TraceReset(const BYTE *t, int players, double now)
{
	memset(&g_trace_state, 0, sizeof(g_trace_state));
	g_trace_state.transceiver = t;
	g_trace_state.thread = GetCurrentThreadId();
	g_trace_state.players = players;
	for (int p = 0; p < players; ++p)
		g_trace_state.last[p] = Reported(t)[p];
	LONG low = g_trace_state.last[0];
	for (int p = 1; p < players; ++p)
		low = g_trace_state.last[p] < low ? g_trace_state.last[p] : low;
	g_trace_state.ran = low;
	g_trace_state.window_start = g_trace_state.last_frame = now;
}

// One command whose segment just ran: `run` = when, `wait` = how long after
// this PC's own report. It is shown when the game time passes its segment's,
// i.e. `margin` after this PC's own report, or when it runs if that is later.
void TimeCommand(const Pending &c, double run, double wait)
{
	Trace &s = g_trace_state;
	int k = 0;
	while (k < s.kind_count && s.kinds[k].vtable != c.vtable)
		++k;
	if (k == s.kind_count) {
		if (s.kind_count < kClasses)
			s.kinds[s.kind_count++].vtable = c.vtable;
		else
			k = kClasses;  // "other"
	}
	Kind &kind = s.kinds[k];
	const double to_run = run - c.sent;
	const double late = static_cast<double>(Margin()) - wait;
	kind.count++;
	kind.run_sum += to_run;
	kind.run_max = to_run > kind.run_max ? to_run : kind.run_max;
	kind.show_sum += to_run + (late > 0 ? late : 0);
	s.commands++;
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
		double wait = 0;
		if (me >= 0 && me < s.players && at[me] > 0) {
			wait = at[last] - at[me];
			s.wait_sum += wait;
			if (wait > s.wait_max)
				s.wait_max = wait;
			if (s.segments < kWaits)
				s.waits[s.segments] = static_cast<float>(wait);
			s.segments++;
			s.last_reporter[last]++;
		}
		// The commands this PC sent into this segment (or before) run now.
		while (s.queued && s.pending[s.first].segment <= s.ran) {
			TimeCommand(s.pending[s.first], at[last] > 0 ? at[last] : now, wait);
			s.first = (s.first + 1) % kPending;
			s.queued--;
		}
	}
}

// A name for the common kinds of commands (analysis/classes.txt), else the
// vtable in hex (in `hex`, 9 bytes or more).
const char *KindName(DWORD vtable, char *hex)
{
	static const struct {
		DWORD vtable;
		const char *name;
	} kNames[] = {
		{0x00e0d778, "CStartMoveHeroCmd"},       {0x00e0d730, "CSetWalkPathCmd"},
		{0x00e0d6a0, "CEnterTownCmd"},           {0x00e0d8e0, "CEndOfTurn"},
		{0x00e197d8, "CHireCreaturesCmd"},       {0x00e0d898, "CUpgradeTownBuildingCmd"},
		{0x00e0e348, "CSetHeroAITargetCmd"},     {0x00e0e8e8, "CAIPurchaseCreaturesInTown"},
		{0x00e0fbd8, "CSetPlayerAIThreatsInfoCmd"}, {0x00e1976c, "CHireHeroInTownCmd"},
		{0x00e0f308, "CSwapTownHeroes"},
	};
	if (!vtable)
		return "other";
	for (const auto &n : kNames)
		if (n.vtable == vtable)
			return n.name;
	wsprintfA(hex, "%08lx", vtable);
	return hex;
}

// The second summary line: the commands this PC sent, per kind, and the
// suggested margin (the p80 of the waits).
void CommandsLine(int p80)
{
	Trace &s = g_trace_state;
	char text[900] = "";
	size_t used = 0;
	// The most frequent kinds by name, the rest together as "other".
	const int kShown = 6;
	Kind kinds[kClasses + 1];
	int n = 0;
	for (int k = 0; k < s.kind_count; ++k)
		kinds[n++] = s.kinds[k];
	std::sort(kinds, kinds + n, [](const Kind &a, const Kind &b) { return a.count > b.count; });
	Kind other = s.kinds[kClasses];
	for (int k = kShown; k < n; ++k) {
		other.count += kinds[k].count;
		other.run_sum += kinds[k].run_sum;
		other.run_max = kinds[k].run_max > other.run_max ? kinds[k].run_max : other.run_max;
		other.show_sum += kinds[k].show_sum;
	}
	n = n < kShown ? n : kShown;
	if (other.count) {
		other.vtable = 0;
		kinds[n++] = other;
	}
	for (int k = 0; k < n && used < sizeof(text) - 1; ++k) {
		char hex[12];
		const int wrote = _snprintf_s(text + used, sizeof(text) - used, _TRUNCATE,
		                              "%s%s %d: %d avg/%d max, shown %d", k ? "; " : "",
		                              KindName(kinds[k].vtable, hex), kinds[k].count,
		                              static_cast<int>(kinds[k].run_sum / kinds[k].count),
		                              static_cast<int>(kinds[k].run_max),
		                              static_cast<int>(kinds[k].show_sum / kinds[k].count));
		if (wrote < 0)
			break;
		used += static_cast<size_t>(wrote);
	}
	const int suggested = p80 < 50 ? 50 : p80 > 400 ? 400 : p80;
	if (s.commands)
		hooklog::Write("lockstep: %d commands from this PC, ms from sending to the run of their segment (and to "
		               "being shown, margin %lu ms): %s%s; suggested view_delay_ms=%d (p80 of the waits %d ms)",
		               s.commands, Margin(), text, s.dropped ? " (some not timed: too many waiting)" : "", suggested,
		               p80);
	else
		hooklog::Write("lockstep: no commands from this PC; margin %lu ms; suggested view_delay_ms=%d (p80 of the "
		               "waits %d ms)",
		               Margin(), suggested, p80);
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
	const int kept = s.segments < kWaits ? s.segments : kWaits;
	int p80 = 0;
	if (kept) {
		float *at = s.waits + kept * 4 / 5;
		std::nth_element(s.waits, at, s.waits + kept);
		p80 = static_cast<int>(*at);
	}
	hooklog::Write("lockstep: %d segments in %d s, waited %d ms on average (p80 %d, longest %d) after this PC's own "
	               "report; last report from player [%s]; %d frames/s (longest frame %d ms); drain took %d extra "
	               "reports (at most %d in a frame); spread %ld segments",
	               s.segments, static_cast<int>(span / 1000), s.segments ? static_cast<int>(s.wait_sum / s.segments) : 0,
	               p80, static_cast<int>(s.wait_max), who, static_cast<int>(s.frames * 1000.0 / span),
	               static_cast<int>(s.longest_frame), s.drains_extra, s.max_drain, high - low);
	CommandsLine(p80);
	s.window_start = now;
	s.frames = s.drains_extra = s.max_drain = s.segments = 0;
	s.wait_sum = s.wait_max = s.longest_frame = 0;
	memset(s.last_reporter, 0, sizeof(s.last_reporter));
	memset(s.kinds, 0, sizeof(s.kinds));
	s.kind_count = s.commands = s.dropped = 0;
}

// --- the commands this PC sends (slot 0) --------------------------------------------
// A command joins this PC's open segment, the one numbered with its report
// count (0x007c9810 looks for that segment record), and is sent at once.
void __fastcall SendHook(void *self, void *edx, void *command)
{
	const BYTE *t = static_cast<const BYTE *>(self);
	Trace &s = g_trace_state;
	// Only for the game the trace follows, and from the pump's thread (the
	// game's main thread, which also runs the AI and the UI), so no lock.
	if (g_trace && command && t == s.transceiver && GetCurrentThreadId() == s.thread) {
		const int me = static_cast<int>(Read(t, 0x54));
		if (me >= 0 && me < s.players) {
			if (s.queued < kPending) {
				Pending &c = s.pending[(s.first + s.queued) % kPending];
				c.vtable = *static_cast<const DWORD *>(command);
				c.segment = Reported(t)[me];
				c.sent = Ms();
				s.queued++;
			} else {
				s.dropped++;
			}
		}
	}
	g_send_next(self, edx, command);
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

// Replaces the 4 bytes at `address` (an immediate or an operand's address) if
// they hold `expected`.
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

DWORD g_view_ms = 400;      // the margin with co-op timing ON ([net] view_delay_ms)
DWORD g_catch_up_ms = 150;  // the timer's step limit with co-op timing ON ([net] catch_up_ms)
bool g_scale_pacing = false;  // [net] scale_pacing=1 and the pacing code is as expected
float g_pacing_slope = 0.05f;  // the pacing's slope with co-op timing ON
DWORD g_pacing_cap = kGameCap;  // the cap of the lead, in segments, with co-op timing ON

// Sets a 4-byte immediate that holds either the game's value or ours.
void Switch(DWORD address, DWORD game, DWORD ours, bool on)
{
	const DWORD now = *reinterpret_cast<const DWORD *>(address);
	const DWORD want = on ? ours : game;
	if (now != want && (now == game || now == ours))
		SetImmediate(address, now, want);
}

// The same for a 1-byte immediate.
void SwitchByte(DWORD address, BYTE game, BYTE ours, bool on)
{
	BYTE *imm = reinterpret_cast<BYTE *>(address);
	const BYTE want = on ? ours : game;
	if (*imm == want || (*imm != game && *imm != ours))
		return;
	DWORD old;
	if (!VirtualProtect(imm, 1, PAGE_EXECUTE_READWRITE, &old))
		return;
	*imm = want;
	VirtualProtect(imm, 1, old, &old);
	FlushInstructionCache(GetCurrentProcess(), imm, 1);
}

bool Matches(DWORD address, const BYTE *bytes, size_t size)
{
	return memcmp(reinterpret_cast<const void *>(address), bytes, size) == 0;
}

// Point 4: the fmul reads the game's 0.05 at 0x00e12f18, and the lead is
// capped at 20 segments.
bool PacingAsExpected()
{
	static const BYTE fmul[] = {0xd8, 0x0d, 0x18, 0x2f, 0xe1, 0x00};
	static const BYTE cap_cmp[] = {0x83, 0xf9, 0x14, 0x7d, 0x6f};  // cmp ecx, 0x14; jge 0x007c8ef3
	static const BYTE cap_mov[] = {0xb8, 0x14, 0x00, 0x00, 0x00};  // mov eax, 0x14
	return Matches(kPacingSlopeOperand - 2, fmul, sizeof(fmul)) && Matches(kPacingCapCmp, cap_cmp, sizeof(cap_cmp)) &&
	       Matches(kPacingCapMov, cap_mov, sizeof(cap_mov)) &&
	       *reinterpret_cast<const DWORD *>(kGameSlope) == kGameSlopeBits;
}

// Point 5: does the exe ask for a finer clock itself?
bool ExeImports(const char *dll, const char *function)
{
	const BYTE *base = reinterpret_cast<const BYTE *>(GetModuleHandleW(nullptr));
	const IMAGE_DOS_HEADER *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
	const IMAGE_NT_HEADERS32 *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress)
		return false;
	for (const IMAGE_IMPORT_DESCRIPTOR *imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress);
	     imp->Name; ++imp) {
		if (_stricmp(reinterpret_cast<const char *>(base + imp->Name), dll) != 0 || !imp->OriginalFirstThunk)
			continue;
		for (const IMAGE_THUNK_DATA32 *names = reinterpret_cast<const IMAGE_THUNK_DATA32 *>(base + imp->OriginalFirstThunk);
		     names->u1.AddressOfData; ++names) {
			if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG32)
				continue;
			const IMAGE_IMPORT_BY_NAME *by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
			if (strcmp(reinterpret_cast<const char *>(by_name->Name), function) == 0)
				return true;
		}
	}
	return false;
}

void FineClock()
{
	if (ExeImports("winmm.dll", "timeBeginPeriod")) {
		hooklog::Write("lockstep: the game sets its own clock resolution (timeBeginPeriod): left alone");
		return;
	}
	const MMRESULT r = timeBeginPeriod(1);
	if (r == TIMERR_NOERROR)
		hooklog::Write("lockstep: the clock ticks every 1 ms for this game (timeBeginPeriod; the game: 15.6 ms), so short "
		               "sleeps are short");
	else
		hooklog::Write("lockstep: timeBeginPeriod(1) failed (%u): the clock stays at 15.6 ms", r);
}

// Point 5: the network thread's Sleep(10).
void NetSleep(DWORD ms)
{
	static const BYTE load[] = {0x8b, 0x3d, 0x74, 0xa1, 0xe0, 0x00};  // mov edi, [0x00e0a174] (Sleep)
	static const BYTE push[] = {0x6a, 0x0a, 0xff, 0xd7};              // push 0x0a; call edi
	if (ms == 10)
		return;
	if (ms < 1 || ms > 10) {
		hooklog::Write("lockstep: net_sleep_ms must be 1..10, not %lu: keeping the game's 10 ms", ms);
		return;
	}
	if (!Matches(kNetSleepLoad, load, sizeof(load)) || !Matches(kNetSleepPush, push, sizeof(push))) {
		hooklog::Write("lockstep: the network thread's code is not as expected: keeping its Sleep(10)");
		return;
	}
	BYTE *imm = reinterpret_cast<BYTE *>(kNetSleepPush + 1);
	DWORD old;
	if (!VirtualProtect(imm, 1, PAGE_EXECUTE_READWRITE, &old))
		return;
	*imm = static_cast<BYTE>(ms);
	VirtualProtect(imm, 1, old, &old);
	FlushInstructionCache(GetCurrentProcess(), imm, 1);
	hooklog::Write("lockstep: the network thread sleeps %lu ms between its rounds (game: 10)", ms);
}

}  // namespace

// The co-op guard (coop_guard.cpp) switched to its shorter segments, or a new
// game started with the game's own timing: our margin, catch-up and pacing
// slope apply only while every PC runs the add-on's timing.
void LockstepCoopTiming(bool on, DWORD segment_ms)
{
	Switch(kViewDelayImmediate, 400, g_view_ms, on);
	Switch(kCatchUpImmediates[0], 150, g_catch_up_ms, on);
	Switch(kCatchUpImmediates[1], 150, g_catch_up_ms, on);
	if (g_scale_pacing) {
		// The cap must still fit the cmp's signed byte: segments of 32 ms or more.
		const bool scale = on && segment_ms >= 32 && segment_ms < 200;
		if (scale) {
			g_pacing_slope = 0.05f * static_cast<float>(segment_ms) / 200.0f;
			g_pacing_cap = kGameCap * 200 / segment_ms;
		}
		Switch(kPacingSlopeOperand, kGameSlope, reinterpret_cast<DWORD>(&g_pacing_slope), scale);
		SwitchByte(kPacingCapCmp + 2, static_cast<BYTE>(kGameCap), static_cast<BYTE>(g_pacing_cap), scale);
		Switch(kPacingCapMov + 1, kGameCap, g_pacing_cap, scale);
		if (scale)
			hooklog::Write("lockstep: pacing slows this PC by %.4f per segment of lead, up to %lu segments (game: 0.05 "
			               "per 200 ms segment, up to 20: the same per millisecond)",
			               g_pacing_slope, g_pacing_cap);
	}
}

void ApplyLockstepFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	g_drain = GetPrivateProfileIntW(L"net", L"drain_reports", 1, path) != 0;
	g_trace = GetPrivateProfileIntW(L"net", L"trace_lockstep", 1, path) != 0;
	const DWORD view = GetPrivateProfileIntW(L"net", L"view_delay_ms", 100, path);
	const DWORD catch_up = GetPrivateProfileIntW(L"net", L"catch_up_ms", 600, path);
	const bool scale_pacing = GetPrivateProfileIntW(L"net", L"scale_pacing", 1, path) != 0;
	const DWORD net_sleep = GetPrivateProfileIntW(L"net", L"net_sleep_ms", 2, path);

	if (view != 400) {
		if (view < 50 || view > 400)
			hooklog::Write("lockstep: view_delay_ms must be 50..400, not %lu: keeping the game's 400 ms", view);
		else if (*reinterpret_cast<const DWORD *>(kViewDelayImmediate) != 400)
			hooklog::Write("lockstep: the game time code is not as expected: keeping the game's 400 ms");
		else {
			g_view_ms = view;
			hooklog::Write("lockstep: with co-op timing ON, moves are shown %lu ms after their segment (game: 400)", view);
		}
	}
	if (catch_up != 150) {
		if (catch_up < 150 || catch_up > 1000)
			hooklog::Write("lockstep: catch_up_ms must be 150..1000, not %lu: keeping the game's 150 ms", catch_up);
		else if (*reinterpret_cast<const DWORD *>(kCatchUpImmediates[0]) != 150 ||
		         *reinterpret_cast<const DWORD *>(kCatchUpImmediates[1]) != 150)
			hooklog::Write("lockstep: the timer code is not as expected: keeping the game's 150 ms");
		else {
			g_catch_up_ms = catch_up;
			hooklog::Write("lockstep: with co-op timing ON, the segment timer may move %lu ms in one frame (game: 150)",
			               catch_up);
		}
	}
	if (scale_pacing) {
		if (PacingAsExpected()) {
			g_scale_pacing = true;
			hooklog::Write("lockstep: with co-op timing ON, the pacing slope follows the segment length (game: 0.05 per "
			               "200 ms segment)");
		} else {
			hooklog::Write("lockstep: the pacing code is not as expected: keeping the game's slope");
		}
	}
	NetSleep(net_sleep);
	if (!g_drain && !g_trace)
		return;
	FineClock();
	g_pump_next = reinterpret_cast<PumpFn>(hookpatch::PatchPointer(kPumpSlot, kPump, &PumpHook));
	if (!g_pump_next) {
		hooklog::Write("lockstep: the multiplayer code is not as expected: drain and trace off");
		g_drain = g_trace = false;
		return;
	}
	if (g_drain)
		hooklog::Write("lockstep: each frame takes in every segment report that has arrived (game: one)");
	if (g_trace) {
		g_send_next = reinterpret_cast<SendFn>(hookpatch::PatchPointer(kSendSlot, kSend, &SendHook));
		hooklog::Write("lockstep: trace on, a summary every 10 s%s",
		               g_send_next ? "" : " (commands not timed: the send code is not as expected)");
	}
}

}  // namespace mods
