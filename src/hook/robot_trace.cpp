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
#include <windows.h>

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

}  // namespace

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
