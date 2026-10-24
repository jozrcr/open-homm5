// Optional robot-mode trace of the single-player command "segments", to find
// what paces the AI (docs/subsystems/ai-turn.md, "Measured").
//
// NWorld::CSinglePlayerTransceiver (main vftable 0x00e43c54):
//   slot 6 = 0x007c7ea0  bool SegmentDue(int *segment)   __thiscall, ret 4
//            due when game time (timer at this+0x10, time at timer+0x14) is past
//            the transceiver's time (this+0x14); *segment = this+0x14 + 200
//   slot 4 = 0x007c7b20  void RunSegment()               __thiscall, ret
//            executes the queued commands, then this+0x14 += 200
// Enabled with trace_segments=1 in open-homm5-robot.ini. Vftable entries only.
// The multiplayer version (trace_mp=1) is further down.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "game_build.h"
#include "log.h"
#include "robot.h"

namespace robot {
namespace {

const DWORD kSpTransceiverVftable = 0x00e43c54;
const DWORD kSegmentDue = 0x007c7ea0;
const DWORD kRunSegment = 0x007c7b20;

typedef bool(__fastcall *SegmentDueFn)(void *self, void *edx, int *segment);
typedef void(__fastcall *RunSegmentFn)(void *self, void *edx);

DWORD Read(const void *base, DWORD offset)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const char *>(base) + offset);
}

DWORD GameTime(void *transceiver)
{
	const void *timer = reinterpret_cast<const void *>(Read(transceiver, 0x10));
	return timer ? Read(timer, 0x14) : 0;
}

bool __fastcall SegmentDueHook(void *self, void *edx, int *segment)
{
	const bool due = reinterpret_cast<SegmentDueFn>(kSegmentDue)(self, edx, segment);
	if (due)
		hooklog::Write("seg-due segment=%d tx_time=%lu game_time=%lu", *segment, Read(self, 0x14), GameTime(self));
	return due;
}

void __fastcall RunSegmentHook(void *self, void *edx)
{
	const DWORD before = Read(self, 0x14);
	reinterpret_cast<RunSegmentFn>(kRunSegment)(self, edx);
	hooklog::Write("seg-run tx_time %lu->%lu game_time=%lu", before, Read(self, 0x14), GameTime(self));
}

bool Patch(DWORD slot_address, DWORD expected, void *replacement)
{
	DWORD *slot = reinterpret_cast<DWORD *>(slot_address);
	if (*slot != expected)
		return false;
	DWORD old;
	if (!VirtualProtect(slot, sizeof(DWORD), PAGE_READWRITE, &old))
		return false;
	*slot = reinterpret_cast<DWORD>(replacement);
	VirtualProtect(slot, sizeof(DWORD), old, &old);
	return true;
}

// The 200 ms segment length is an immediate operand in both functions:
//   0x007c7ba6  81 47 14 c8 00 00 00   add dword ptr [edi+14h], 200   (RunSegment)
//   0x007c7ece  05 c8 00 00 00         add eax, 200                   (SegmentDue)
const DWORD kSegmentImmediates[] = {0x007c7ba9, 0x007c7ecf};

// --- multiplayer -------------------------------------------------------------------
// NWorld::CMultiPlayerTransceiver (main vftable 0x00e43f1c), fields used here:
//   +0x28 timer (game time at timer+0x14)   +0x2c this PC's segment time
//   +0x38 segment records (circular list, record+8 = segment number)
//   +0x3c..0x40 each player's last reported segment   +0x54 this PC's player (-1 none)
//   0x00f3fa5c game speed factor set by the pacing (slot 5, 0x007c8dd0): a PC
//   that is ahead of the slowest player slows down.
// A command sent from this PC (slot 0, 0x007c9810) joins this PC's current
// segment; a segment runs only when every player has reported a later one
// (0x007c8c80). Slot 4 (0x007ca990) is this PC's own step (+200 ms).
const DWORD kMpTransceiverVftable = 0x00e43f1c;
const DWORD kMpSend = 0x007c9810;
const DWORD kMpOwnStep = 0x007ca990;
typedef void(__fastcall *MpSendFn)(void *self, void *edx, void *command);
typedef void(__fastcall *MpOwnStepFn)(void *self, void *edx);

void DescribeMp(void *self, char *out, size_t size)
{
	const int me = static_cast<int>(Read(self, 0x54));
	const DWORD *seg = reinterpret_cast<const DWORD *>(Read(self, 0x3c));
	const int players = static_cast<int>((Read(self, 0x40) - Read(self, 0x3c)) / 4);
	char list[96] = "";
	for (int i = 0; i < players && i < 8; ++i) {
		char item[16];
		_snprintf_s(item, sizeof(item), _TRUNCATE, "%s%ld", i ? "," : "", static_cast<LONG>(seg[i]));
		strncat_s(list, sizeof(list), item, _TRUNCATE);
	}
	const DWORD *head = reinterpret_cast<const DWORD *>(Read(self, 0x38));
	int records = 0;
	LONG oldest = -1;
	for (const DWORD *r = reinterpret_cast<const DWORD *>(head[0]); r != head && records < 1000;
	     r = reinterpret_cast<const DWORD *>(r[0]), ++records) {
		if (records == 0)
			oldest = static_cast<LONG>(r[2]);
	}
	const float speed = *reinterpret_cast<const float *>(0x00f3fa5c);
	_snprintf_s(out, size, _TRUNCATE, "me=%d segments=[%s] oldest_waiting=%ld records=%d speed=%.2f tx=%lu game=%lu", me,
	            list, oldest, records, speed, Read(self, 0x2c), Read(reinterpret_cast<void *>(Read(self, 0x28)), 0x14));
}

void SafeDescribeMp(void *self, char *out, size_t size)
{
	__try {
		DescribeMp(self, out, size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		_snprintf_s(out, size, _TRUNCATE, "(unreadable)");
	}
}

void __fastcall MpSendHook(void *self, void *edx, void *command)
{
	char state[256];
	SafeDescribeMp(self, state, sizeof(state));
	hooklog::Write("mp-send cmd=%08lx %s", command ? *static_cast<DWORD *>(command) : 0, state);
	reinterpret_cast<MpSendFn>(kMpSend)(self, edx, command);
}

void __fastcall MpOwnStepHook(void *self, void *edx)
{
	reinterpret_cast<MpOwnStepFn>(kMpOwnStep)(self, edx);
	char state[256];
	SafeDescribeMp(self, state, sizeof(state));
	hooklog::Write("mp-step %s", state);
}

// --- world checksums (desync check) -------------------------------------------------
// With mp_check_crc on (or mp_check_object_crc, which freezes long games), the
// PCs of a multiplayer game checksum the world after every segment and
// exchange the checksums (NWorld::CCrcTestCmd: check number at +0x0c, checksum
// at +0x10); without either flag none are computed (0x007c9be0). The LAN
// runner sets mp_check_crc. The robot logs every checksum: 0x007c9410 (__thiscall(int player, CCrcTestCmd *), ret 8) is called
// for this PC's own checksum (0x007c9cd0) and for each one received (0x007ca8d1).
// Matching "crc" lines from all copies = no desync.
const DWORD kCrcHandler = 0x007c9410;
const DWORD kCrcCallOwn = 0x007c9cd0;
const DWORD kCrcCallReceived = 0x007ca8d1;
typedef void(__fastcall *CrcFn)(void *self, void *edx, int player, const BYTE *cmd);

void __fastcall CrcHook(void *self, void *edx, int player, const BYTE *cmd)
{
	if (cmd)
		hooklog::Write("crc player=%d check=%ld crc=%08lx", player, *reinterpret_cast<const LONG *>(cmd + 0x0c),
		               *reinterpret_cast<const DWORD *>(cmd + 0x10));
	reinterpret_cast<CrcFn>(kCrcHandler)(self, edx, player, cmd);
}

// Redirect a "call rel32" to replacement if it still calls `expected`.
bool PatchCall(DWORD call, DWORD expected, void *replacement)
{
	BYTE *p = reinterpret_cast<BYTE *>(call);
	if (p[0] != 0xE8 || call + 5 + *reinterpret_cast<LONG *>(p + 1) != expected)
		return false;
	DWORD old;
	if (!VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old))
		return false;
	*reinterpret_cast<LONG *>(p + 1) = static_cast<LONG>(reinterpret_cast<DWORD>(replacement) - (call + 5));
	VirtualProtect(p + 1, 4, old, &old);
	FlushInstructionCache(GetCurrentProcess(), p, 5);
	return true;
}

}  // namespace

void InstallCrcTrace()
{
	static bool done = false;
	if (done || !IsHookable(DetectGameBuild()))
		return;
	done = true;
	const bool a = PatchCall(kCrcCallOwn, kCrcHandler, reinterpret_cast<void *>(&CrcHook));
	const bool b = PatchCall(kCrcCallReceived, kCrcHandler, reinterpret_cast<void *>(&CrcHook));
	hooklog::Write("robot: world checksum trace %s", a && b ? "installed" : "FAILED (code not as expected)");
}

void InstallMpTrace()
{
	static bool done = false;
	if (done || !IsHookable(DetectGameBuild()))
		return;
	done = true;
	const bool a = Patch(kMpTransceiverVftable + 0 * 4, kMpSend, reinterpret_cast<void *>(&MpSendHook));
	const bool b = Patch(kMpTransceiverVftable + 4 * 4, kMpOwnStep, reinterpret_cast<void *>(&MpOwnStepHook));
	hooklog::Write("robot: multiplayer segment trace %s", a && b ? "installed" : "FAILED (vftable not as expected)");
}

// EXPERIMENT (robot mode only, segment_ms=N): change the single-player segment
// length in memory, to measure how much it paces the AI. Not a mod: a real
// change belongs in src/mods/ once we understand its side effects.
void SetSegmentLength(DWORD ms)
{
	static bool done = false;
	if (done || !ms || !IsHookable(DetectGameBuild()))
		return;
	done = true;
	for (DWORD address : kSegmentImmediates) {
		DWORD *imm = reinterpret_cast<DWORD *>(address);
		if (*imm != 200) {
			hooklog::Write("robot: segment length at %08lx is %lu, not 200: not changed", address, *imm);
			return;
		}
	}
	for (DWORD address : kSegmentImmediates) {
		DWORD *imm = reinterpret_cast<DWORD *>(address);
		DWORD old;
		VirtualProtect(imm, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &old);
		*imm = ms;
		VirtualProtect(imm, sizeof(DWORD), old, &old);
		FlushInstructionCache(GetCurrentProcess(), imm, sizeof(DWORD));
	}
	hooklog::Write("robot: EXPERIMENT single-player segment length set to %lu ms (was 200)", ms);
}

// EXPERIMENT (robot mode only, mp_segment_ms=100 or 50): the multiplayer
// segment length. Every PC of a game must use the same value (lockstep). The
// 200 ms appears in CMultiPlayerTransceiver as three immediates (next segment
// time, own step, slot 12) and in a formula for the time of segment n,
// base + 200*n + 30000 (the transceiver's clock starts at base + 30000,
// 0x007ca024), computed twice with
//   lea ecx,[ebp+ebp*2+0x1c2]; lea edx,[ebp+ecx*8+0x96]; lea r,[eax+edx*8]
// = (25n + 3750) * 8: in the segment runner (0x007c9a4e, the world time used
// to run segment n's commands, passed to 0x007cc680) and in the checksum
// handler (0x007c945c, catch-up). For N ms segments the robot changes the last
// scale to 8*N/200 and the displacement 0x96 to 30000/scale - 3600, so the
// formula becomes base + N*n + 30000:
//   100 ms: scale 4 (SIB 0x90), displacement 3900;  50 ms: scale 2 (0x50), 11400.
// (Changing only the immediates made hero moves wait about as long as the time
// since the turn began: the world clock ran twice as fast as the segments.)
void SetMpSegmentLength(DWORD ms)
{
	static bool done = false;
	if (done || !ms || !IsHookable(DetectGameBuild()))
		return;
	done = true;
	const DWORD immediates[] = {0x007c8b20, 0x007ca9df, 0x007cc0d4};
	struct Formula {
		DWORD displacement;  // the 0x96 of lea edx,[ebp+ecx*8+0x96]
		DWORD sib;           // the SIB byte (0xd0) of lea r,[eax+edx*8]
	};
	const Formula formulas[] = {{0x007c9a58, 0x007c9a5e}, {0x007c9466, 0x007c946f}};
	const BYTE sib = ms == 100 ? 0x90 : ms == 50 ? 0x50 : 0;
	const DWORD displacement = ms == 100 ? 3900 : 11400;
	if (!sib) {
		hooklog::Write("robot: mp_segment_ms must be 100 or 50, not %lu: not changed", ms);
		return;
	}
	for (DWORD a : immediates) {
		if (*reinterpret_cast<DWORD *>(a) != 200) {
			hooklog::Write("robot: multiplayer segment length at %08lx is not 200: not changed", a);
			return;
		}
	}
	for (const Formula &f : formulas) {
		if (*reinterpret_cast<DWORD *>(f.displacement) != 0x96 || *reinterpret_cast<BYTE *>(f.sib) != 0xd0) {
			hooklog::Write("robot: segment time formula at %08lx not as expected: not changed", f.displacement);
			return;
		}
	}
	DWORD old;
	for (DWORD a : immediates) {
		DWORD *imm = reinterpret_cast<DWORD *>(a);
		VirtualProtect(imm, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &old);
		*imm = ms;
		VirtualProtect(imm, sizeof(DWORD), old, &old);
		FlushInstructionCache(GetCurrentProcess(), imm, sizeof(DWORD));
	}
	for (const Formula &f : formulas) {
		BYTE *p = reinterpret_cast<BYTE *>(f.displacement);
		VirtualProtect(p, 16, PAGE_EXECUTE_READWRITE, &old);
		*reinterpret_cast<DWORD *>(f.displacement) = displacement;
		*reinterpret_cast<BYTE *>(f.sib) = sib;
		VirtualProtect(p, 16, old, &old);
		FlushInstructionCache(GetCurrentProcess(), p, 16);
	}
	hooklog::Write("robot: EXPERIMENT multiplayer segment length set to %lu ms (was 200)", ms);
}

void InstallSegmentTrace()
{
	static bool done = false;
	if (done || !IsHookable(DetectGameBuild()))
		return;
	done = true;
	const bool a = Patch(kSpTransceiverVftable + 6 * 4, kSegmentDue, reinterpret_cast<void *>(&SegmentDueHook));
	const bool b = Patch(kSpTransceiverVftable + 4 * 4, kRunSegment, reinterpret_cast<void *>(&RunSegmentHook));
	hooklog::Write("robot: segment trace %s", a && b ? "installed" : "FAILED (vftable not as expected)");
}

}  // namespace robot
