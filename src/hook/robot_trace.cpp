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

}  // namespace

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
