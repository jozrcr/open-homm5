// Mod: shorter multiplayer segments when every player agrees (the co-op timing
// guard, docs/plans/coop-timing-guard.md).
//
// A multiplayer game advances in "segments" of 200 ms that every PC has
// confirmed (docs/subsystems/multiplayer-sync.md). Each AI move waits at least
// two of them, so shorter segments make co-op AI turns faster (100 ms: 0.60 s
// instead of 0.49 s per AI move, measured with the robot). But the segment
// length decides when things happen in the game world, which every PC runs
// itself in lockstep: all PCs must use the same length, or the game desyncs.
// So the PCs agree first:
//
// 1. Hello. After its first segment, each PC's DLL sends one hello to the
//    others: a world-checksum message (NWorld::CCrcTestCmd) with check number
//    -1, which the game ignores (0x007c9410: the catch-up target
//    base + 200 * -1 + 30000 has already passed, and the compare needs a check
//    number above 0). So a PC without our DLL does nothing with it. Its
//    "checksum" carries a mark, the protocol version and the segment length
//    this PC asks for (200 = the game's: "I don't want it").
// 2. Each PC's messages arrive in order (the game counts the "segment done"
//    reports without numbers, so it relies on that too; robot test: 48 of 48),
//    and a PC cannot get more than 20 segments ahead of the slowest one. So by
//    its segment kDecideAt every PC has the hellos of all the others.
// 3. At its segment kDecideAt, each PC applies the same rule: every other PC
//    sent a hello with the same version and the same length, and that length
//    is not 200. Then every PC switches, from that segment on; otherwise none
//    does (a PC without our DLL never switches either).
//
// The switch changes, in NWorld::CMultiPlayerTransceiver:
//   - three 200s used for pacing: immediates at 0x007c8b20 (segment due),
//     0x007ca9df (own step) and 0x007cc0d4 (slot 12), set to N at the switch
//     and back to 200 when the next game starts;
//   - the game time of segment n, base + 200 n + 30000 (base at transceiver
//     +0xd8), computed with three lea in the segment runner (0x007c9a48, n in
//     ebp, result in ebx) and in the checksum catch-up (0x007c9456, n in ebp,
//     result in eax). Both become a call to SegmentTime(), which gives the same
//     value before the switch and base + 30000 + 200 S + N (n - S) from segment
//     S on, so the game time never jumps.
//
// Settings, open-homm5.ini [coop]:
//   guard=1          0 = take no part (no hello, never switch), as without the DLL
//   fast_segments=1  0 = ask for the game's 200 ms, so nobody switches
//   segment_ms=50    50 or 100 (50 by default since 0.2.0)
#include <windows.h>
#include <string.h>

#include "coop_guard.h"
#include "lockstep.h"

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

const DWORD kMpTransceiverVftable = 0x00e43f1c;  // NWorld::CMultiPlayerTransceiver
const DWORD kOwnStepSlot = kMpTransceiverVftable + 4 * 4;
const DWORD kOwnStep = 0x007ca990;
const DWORD kCheckHandler = 0x007c9410;      // __thiscall(int player, CCrcTestCmd *), ret 8
const DWORD kCheckReceivedCall = 0x007ca8d1;  // in the message pump 0x007ca7c0
const DWORD kPacingImmediates[] = {0x007c8b20, 0x007ca9df, 0x007cc0d4};
const DWORD kRunnerFormula = 0x007c9a48;   // 23 bytes, result in ebx
const DWORD kCatchUpFormula = 0x007c9456;  // 26 bytes, result in eax (with add esi, 0x20 inside)
const DWORD kDecideAt = 50;                 // segment of the decision, 10 s into the game
const DWORD kHelloMark = 0x47000000;        // 'G' in the top byte
const DWORD kProtocol = 1;

typedef void(__fastcall *OwnStepFn)(void *transceiver, void *edx);
typedef void(__fastcall *CheckFn)(void *transceiver, void *edx, int player, const BYTE *cmd);
OwnStepFn g_own_step_next = nullptr;
CheckFn g_check_received_next = nullptr;

DWORD g_asked_ms = 200;    // what this PC asks for
bool g_fast_formula = false;  // the two formula sites call SegmentTime()

// The current game's handshake (one transceiver per multiplayer game).
const BYTE *g_transceiver = nullptr;
DWORD g_last_reports = 0;
const int kMaxPlayers = 8;
DWORD g_hello[kMaxPlayers];  // the hello received from each player, 0 = none
bool g_decided = false;
bool g_switched = false;
DWORD g_switch_segment = 0;
DWORD g_segment_ms = 200;

DWORD Read(const void *base, DWORD offset)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(base) + offset);
}

DWORD Hello(DWORD ms)
{
	return kHelloMark | kProtocol << 16 | (ms & 0xffff);
}

void SetPacing(DWORD ms)
{
	for (DWORD a : kPacingImmediates) {
		DWORD *imm = reinterpret_cast<DWORD *>(a);
		DWORD old;
		if (!VirtualProtect(imm, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &old))
			continue;
		*imm = ms;
		VirtualProtect(imm, sizeof(DWORD), old, &old);
		FlushInstructionCache(GetCurrentProcess(), imm, sizeof(DWORD));
	}
}

// A new multiplayer game: forget the last one and go back to the game's timing.
void StartGame(const BYTE *transceiver)
{
	if (g_switched)
		SetPacing(200);
	LockstepCoopTiming(false);
	g_transceiver = transceiver;
	g_last_reports = 0;
	memset(g_hello, 0, sizeof(g_hello));
	g_decided = false;
	g_switched = false;
	g_switch_segment = 0;
	g_segment_ms = 200;
}

// Sends a world-checksum message (NWorld::CCrcTestCmd, 0x2c bytes, vtable
// 0x00e43db0: check number at +0x0c, checksum at +0x10, two empty lists) to the
// other PCs, as the segment runner does at 0x007c9c74-0x007c9cf6: operator new
// 0x00878fd0, a reference (+4) held while the network object (transceiver
// +0x34) sends it (slot 7), then 0x008c61d0 if that was the last one.
void SendHello(const BYTE *transceiver, DWORD value)
{
	typedef void *(__cdecl * NewFn)(unsigned size);
	typedef void(__fastcall * SendFn)(void *net, void *edx, void *cmd);
	typedef void(__fastcall * ReleaseFn)(void *cmd, void *edx, DWORD mask);
	BYTE *cmd = static_cast<BYTE *>(reinterpret_cast<NewFn>(0x00878fd0)(0x2c));
	if (!cmd)
		return;
	memset(cmd, 0, 0x2c);
	*reinterpret_cast<DWORD *>(cmd) = 0x00e43db0;
	*reinterpret_cast<LONG *>(cmd + 0x0c) = -1;
	*reinterpret_cast<DWORD *>(cmd + 0x10) = value;
	*reinterpret_cast<DWORD *>(cmd + 4) += 1;
	void *net = reinterpret_cast<void *>(Read(transceiver, 0x34));
	(*reinterpret_cast<SendFn *const *>(net))[7](net, nullptr, cmd);
	*reinterpret_cast<DWORD *>(cmd + 4) -= 1;
	if ((*reinterpret_cast<DWORD *>(cmd + 4) & 0xfffff) == 0)
		reinterpret_cast<ReleaseFn>(0x008c61d0)(cmd, nullptr, 0xfffff);
}

// The decision at segment kDecideAt, the same on every PC.
void Decide(const BYTE *transceiver, int me, int players)
{
	g_decided = true;
	if (g_asked_ms == 200) {
		hooklog::Write("co-op timing: OFF: this PC asks for the game's 200 ms segments ([coop] fast_segments=0)");
		return;
	}
	for (int p = 0; p < players; ++p) {
		if (p == me)
			continue;
		if (g_hello[p] == 0) {
			hooklog::Write("co-op timing: OFF: player %d sent no hello (no open-homm5 DLL, an old one, or [coop] guard=0)", p);
			return;
		}
		if (g_hello[p] != Hello(g_asked_ms)) {
			hooklog::Write("co-op timing: OFF: player %d asks for %lu ms segments (protocol %lu), this PC for %lu ms", p,
			               g_hello[p] & 0xffff, (g_hello[p] >> 16) & 0xff, g_asked_ms);
			return;
		}
	}
	g_switch_segment = kDecideAt;
	g_segment_ms = g_asked_ms;
	g_switched = true;
	SetPacing(g_segment_ms);
	LockstepCoopTiming(true);
	hooklog::Write("co-op timing: ON from segment %lu: %lu ms segments (all %d PCs agree)", kDecideAt, g_segment_ms,
	               players);
	(void)transceiver;
}

// This PC's own step (transceiver slot 4): it has just reported one more segment.
void __fastcall OwnStepHook(void *self, void *edx)
{
	g_own_step_next(self, edx);
	const BYTE *t = static_cast<const BYTE *>(self);
	const int me = static_cast<int>(Read(t, 0x54));
	const int players = static_cast<int>((Read(t, 0x40) - Read(t, 0x3c)) / 4);
	if (me < 0 || me >= players || players > kMaxPlayers)
		return;
	const DWORD reports = reinterpret_cast<const DWORD *>(Read(t, 0x3c))[me];
	if (t != g_transceiver || reports < g_last_reports)
		StartGame(t);
	g_last_reports = reports;
	if (reports == 1) {
		SendHello(t, Hello(g_asked_ms));
		hooklog::Write("co-op timing: hello sent (this PC asks for %lu ms segments)", g_asked_ms);
	}
	if (reports == kDecideAt && !g_decided)
		Decide(t, me, players);
}

// A world-checksum message from another PC (message pump): hellos stop here.
void __fastcall CheckReceivedHook(void *self, void *edx, int player, const BYTE *cmd)
{
	if (cmd && *reinterpret_cast<const LONG *>(cmd + 0x0c) == -1 &&
	    (*reinterpret_cast<const DWORD *>(cmd + 0x10) & 0xff000000) == kHelloMark) {
		const DWORD value = *reinterpret_cast<const DWORD *>(cmd + 0x10);
		if (self != g_transceiver)
			StartGame(static_cast<const BYTE *>(self));
		if (player >= 0 && player < kMaxPlayers && !g_hello[player]) {
			g_hello[player] = value;
			hooklog::Write("co-op timing: hello from player %d (asks for %lu ms segments, protocol %lu)", player,
			               value & 0xffff, (value >> 16) & 0xff);
		}
		return;
	}
	g_check_received_next(self, edx, player, cmd);
}

// The game time of segment n (see the top of this file).
DWORD __cdecl SegmentTime(const BYTE *transceiver, DWORD n)
{
	const DWORD base = Read(transceiver, 0xd8);
	if (!g_switched || transceiver != g_transceiver || static_cast<LONG>(n) < static_cast<LONG>(g_switch_segment))
		return base + 200 * n + 30000;
	return base + 30000 + 200 * g_switch_segment + g_segment_ms * (n - g_switch_segment);
}

bool Matches(DWORD address, const BYTE *bytes, size_t size)
{
	return memcmp(reinterpret_cast<const void *>(address), bytes, size) == 0;
}

void WriteCode(DWORD address, const BYTE *bytes, size_t size, size_t span)
{
	BYTE *p = reinterpret_cast<BYTE *>(address);
	DWORD old;
	VirtualProtect(p, span, PAGE_EXECUTE_READWRITE, &old);
	memcpy(p, bytes, size);
	memset(p + size, 0x90, span - size);  // nop up to the next instruction
	VirtualProtect(p, span, old, &old);
	FlushInstructionCache(GetCurrentProcess(), p, span);
}

void PutCall(BYTE *code, DWORD at, DWORD target)
{
	code[0] = 0xe8;
	*reinterpret_cast<LONG *>(code + 1) = static_cast<LONG>(target - (at + 5));
}

// The two formula sites become calls to SegmentTime() (see the top). False if
// the code is not the expected one (then nothing is changed).
bool PatchFormulas()
{
	static const BYTE runner[] = {0x8b, 0x86, 0xd8, 0x00, 0x00, 0x00, 0x8d, 0x8c, 0x6d, 0xc2, 0x01, 0x00,
	                              0x00, 0x8d, 0x94, 0xcd, 0x96, 0x00, 0x00, 0x00, 0x8d, 0x1c, 0xd0};
	static const BYTE catch_up[] = {0x8b, 0x87, 0xd8, 0x00, 0x00, 0x00, 0x8d, 0x8c, 0x6d, 0xc2, 0x01, 0x00, 0x00,
	                                0x8d, 0x94, 0xcd, 0x96, 0x00, 0x00, 0x00, 0x83, 0xc6, 0x20, 0x8d, 0x04, 0xd0};
	if (!Matches(kRunnerFormula, runner, sizeof(runner)) || !Matches(kCatchUpFormula, catch_up, sizeof(catch_up)))
		return false;
	const DWORD target = reinterpret_cast<DWORD>(&SegmentTime);
	// runner: push ebp; push esi; call SegmentTime; add esp, 8; mov ebx, eax; mov eax, [esi+0xd8]
	BYTE a[18] = {0x55, 0x56};
	PutCall(a + 2, kRunnerFormula + 2, target);
	const BYTE a_tail[] = {0x83, 0xc4, 0x08, 0x8b, 0xd8, 0x8b, 0x86, 0xd8, 0x00, 0x00, 0x00};
	memcpy(a + 7, a_tail, sizeof(a_tail));
	// catch-up: push ebp; push edi; call SegmentTime; add esp, 8; add esi, 0x20
	BYTE b[13] = {0x55, 0x57};
	PutCall(b + 2, kCatchUpFormula + 2, target);
	const BYTE b_tail[] = {0x83, 0xc4, 0x08, 0x83, 0xc6, 0x20};
	memcpy(b + 7, b_tail, sizeof(b_tail));
	WriteCode(kRunnerFormula, a, sizeof(a), sizeof(runner));
	WriteCode(kCatchUpFormula, b, sizeof(b), sizeof(catch_up));
	return true;
}

}  // namespace

void ApplyCoopGuardFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	if (!GetPrivateProfileIntW(L"coop", L"guard", 1, path)) {
		hooklog::Write("co-op timing: guard off ([coop] guard=0): this PC sends no hello and keeps the game's timing");
		return;
	}
	const DWORD ms = GetPrivateProfileIntW(L"coop", L"segment_ms", 50, path);
	const bool fast = GetPrivateProfileIntW(L"coop", L"fast_segments", 1, path) != 0;
	g_asked_ms = fast && (ms == 100 || ms == 50) ? ms : 200;
	if (fast && g_asked_ms == 200)
		hooklog::Write("co-op timing: segment_ms must be 100 or 50, not %lu: asking for the game's 200 ms", ms);
	for (DWORD a : kPacingImmediates) {
		if (*reinterpret_cast<const DWORD *>(a) != 200) {
			hooklog::Write("co-op timing: the segment code is not as expected (%08lx): guard off", a);
			return;
		}
	}
	if (g_asked_ms != 200) {
		g_fast_formula = PatchFormulas();
		if (!g_fast_formula) {
			hooklog::Write("co-op timing: the segment time code is not as expected: asking for the game's 200 ms");
			g_asked_ms = 200;
		}
	}
	g_own_step_next = reinterpret_cast<OwnStepFn>(hookpatch::PatchPointer(kOwnStepSlot, kOwnStep, &OwnStepHook));
	g_check_received_next =
		reinterpret_cast<CheckFn>(hookpatch::RedirectCall(kCheckReceivedCall, kCheckHandler, &CheckReceivedHook));
	if (!g_own_step_next || !g_check_received_next) {
		hooklog::Write("co-op timing: the multiplayer code is not as expected: guard off");
		return;
	}
	hooklog::Write("co-op timing: guard on; this PC asks for %lu ms segments in multiplayer games", g_asked_ms);
}

}  // namespace mods
