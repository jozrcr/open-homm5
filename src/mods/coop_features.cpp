// Mod: which of our world-changing features a multiplayer game uses (the co-op features'
// agreement; docs/plans/performance.md, docs/plans/turn-features.md).
//
// In plain words: some of our changes act on the game world (MMH5.5's script rhythm,
// the computer players side by side). In a multiplayer game every PC runs the same
// world itself, so every PC must use such a change, or none: otherwise their worlds
// drift apart (a desync). So, at each game's start:
//
// 1. Each PC's DLL sends one hello after its own segment report kSendAt (a world-checksum
//    message with check number -1, which the game ignores, as the co-op guard, hero looks
//    and Undo End Turn send theirs): the features this version knows and, for the host
//    (PC 0), those the host wants (its h5-together.ini: [coop] script_rhythm,
//    [ai] parallel_turns). A hello counts only if it came by its sender's report kSendBy:
//    the same hellos count on every PC.
// 2. At the start of segment kDecideAt (the segment runner's call 0x007c9a67, before that
//    segment's commands), every PC decides the same: a feature is on only if every PC
//    knows it and the host wants it (features::Decide). A PC without our DLL, or with an
//    older one, sends no such hello: then all are off.
// 3. The features act only inside the multiplayer segment runner (0x007c98e0, wrapped
//    here: InMultiplayerSegment), where every PC changes its world alike, and never in
//    single player.
// 4. ai_bonus (0.3.4; the owner's wish, 2026-11-05: "the AI army multiplies x2 overnight"):
//    MMH5.5's computer bonus (H55_AICheatMode, read by its script at each new week) is set
//    from the host's [rules] ai_cheat in the game itself, at the decision (the start of
//    segment kDecideAt, the same point of the game on every PC), with the game's own "run
//    a line of script" (script_rhythm.cpp, RunGameScript). So it applies to a loaded game
//    too (MMH5.5 reads its settings file only at a map's start; a save keeps the values):
//    from the next week on. The value: what the host's F2 panel shows (its [rules] ai_cheat
//    choice, else its own H55-Settings.lua's; host_rules.cpp, HostComputerBonus).
// 5. endless_turns (0.3.5; the 2008 game without MMH5.5): simultaneous turns that never end,
//    as MMH5.5's PEST, switched on at the decision on every PC (endless_turns.cpp) and off
//    again at the next game's start. Only a PC of the 2008 game knows it: with MMH5.5 its
//    55u.dll does PEST itself.
#include "coop_features.h"

#include <windows.h>
#include <string.h>

#include <string>

#include "coop_features_logic.h"
#include "endless_turns.h"
#include "host_rules.h"
#include "join_time.h"
#include "script_rhythm.h"

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

// --- the game (vanilla 3.1 addresses, the same in MMH55_Utility) -----------------------------
const DWORD kMpStepCell = 0x00e43f1c + 0x10;  // NWorld::CMultiPlayerTransceiver slot 4 (its own step: reports)
const DWORD kMpStep = 0x007ca990;
const DWORD kCheckCall = 0x007ca8d1;       // in the message pump 0x007ca7c0: call 0x007c9410 (a checksum came)
const DWORD kCheckHandler = 0x007c9410;    // __thiscall(int pc, CCrcTestCmd *), ret 8; ignores check -1
const DWORD kSegmentCall = 0x007c9a67;     // in the segment runner 0x007c98e0, before a segment's commands
const DWORD kSegmentTime = 0x007cc680;     // __thiscall(transceiver + 8, DWORD game time), ret 4
const DWORD kRunner = 0x007c98e0;          // the segment runner, __thiscall(transceiver), no stack arguments
const DWORD kRunnerCalls[] = {0x007ca957, 0x007ca97d};  // in the message pump 0x007ca7c0
const DWORD kCrcCmdVftable = 0x00e43db0;   // NWorld::CCrcTestCmd, 0x2c bytes
const DWORD kGameNew = 0x00878fd0;
const DWORD kReleaseStrong = 0x008c61d0;   // __thiscall(object, mask)
// The transceiver: the network, the players' segment reports (one per PC), this PC's
// index (the host is 0)
const DWORD kNet = 0x34, kReports = 0x3c, kReportsEnd = 0x40, kMe = 0x54;
const int kMaxPcs = 8;

struct Code {
	DWORD address;
	BYTE bytes[10];
	int size;
};
const Code kCode[] = {
	{kMpStep, {0x56, 0x8b, 0xf1, 0xe8, 0xf8, 0x1c, 0x21, 0x00}, 8},
	{kCheckHandler, {0x83, 0xec, 0x60, 0x33, 0xc0, 0x39, 0x44, 0x24}, 8},
	{0x007c9a5f, {0x8d, 0x4e, 0x08, 0x53, 0x89, 0x4c, 0x24, 0x20}, 8},  // lea ecx,[esi+8]; push ebx; ... (then the call)
	{kSegmentTime, {0x53, 0x55, 0x8b, 0x6c, 0x24, 0x0c, 0x57, 0x55, 0x8b, 0xd9}, 10},
	{kRunner, {0x83, 0xec, 0x40, 0x8b, 0x41, 0x38, 0x53, 0x55}, 8},
	{0x007ca955, {0x8b, 0xcf}, 2},  // mov ecx, edi (the transceiver), then the call
	{0x007ca97b, {0x8b, 0xcf}, 2},
};

typedef void(__fastcall *StepFn)(void *transceiver, void *edx);
typedef void(__fastcall *CheckFn)(void *transceiver, void *edx, int pc, const BYTE *cmd);
typedef void(__fastcall *SegmentFn)(void *transceiver8, void *edx, DWORD time);
typedef void(__fastcall *RunnerFn)(void *transceiver, void *edx);

bool g_disabled = false;  // part of the game's code was not as expected: the hooks only pass on
StepFn g_mp_step_next = nullptr;
CheckFn g_check_next = nullptr;
SegmentFn g_segment_next = nullptr;
RunnerFn g_runner_next = nullptr;

// --- state: all on the game's main thread (the message pump, the segment runner and the
// transceiver's step) ---
const void *g_transceiver = nullptr;
DWORD g_segments = 0;  // segments started in this game
DWORD g_last_reports = 0;
bool g_hello_sent = false;
features::Hello g_hellos[kMaxPcs];
bool g_decided = false;
unsigned g_on = 0;
unsigned g_game = 0;
int g_runner_depth = 0;

DWORD Read(const void *p, DWORD off)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(p) + off);
}

// What this PC knows: every feature of this version, but endless_turns only on the 2008 game
// (EndlessTurnsAvailable).
unsigned Knows()
{
	return features::kKnown & ~(EndlessTurnsAvailable() ? 0u : 1u << features::kEndlessTurns);
}

void NewGame(const void *transceiver)
{
	// a new game: the game's own simultaneous turns until this game's decision
	SetEndlessTurns(false);
	g_transceiver = transceiver;
	g_segments = 0;
	g_last_reports = 0;
	g_hello_sent = false;
	for (features::Hello &h : g_hellos)
		h = features::Hello();
	g_decided = false;
	g_on = 0;
}

bool Pcs(const BYTE *t, int *me, int *pcs)
{
	*me = static_cast<int>(Read(t, kMe));
	*pcs = static_cast<int>((Read(t, kReportsEnd) - Read(t, kReports)) / 4);
	return *pcs > 0 && *pcs <= kMaxPcs && *me >= 0 && *me < *pcs;
}

DWORD Reports(const BYTE *t, int pc)
{
	return reinterpret_cast<const DWORD *>(Read(t, kReports))[pc];
}

// A world-checksum message with check number -1 and our value, to the other PCs (as
// Undo End Turn sends its own; the game ignores check -1).
void SendValue(const BYTE *transceiver, DWORD value)
{
	typedef void *(__cdecl * NewFn)(unsigned size);
	typedef void(__fastcall * SendFn)(void *net, void *edx, void *cmd);
	typedef void(__fastcall * StrongReleaseFn)(void *cmd, void *edx, DWORD mask);
	BYTE *cmd = static_cast<BYTE *>(reinterpret_cast<NewFn>(kGameNew)(0x2c));
	if (!cmd)
		return;
	memset(cmd, 0, 0x2c);
	*reinterpret_cast<DWORD *>(cmd) = kCrcCmdVftable;
	*reinterpret_cast<LONG *>(cmd + 0x0c) = -1;
	*reinterpret_cast<DWORD *>(cmd + 0x10) = value;
	*reinterpret_cast<DWORD *>(cmd + 4) += 1;
	void *net = reinterpret_cast<void *>(Read(transceiver, kNet));
	(*reinterpret_cast<SendFn *const *>(net))[7](net, nullptr, cmd);
	*reinterpret_cast<DWORD *>(cmd + 4) -= 1;
	if ((*reinterpret_cast<DWORD *>(cmd + 4) & 0xfffff) == 0)
		reinterpret_cast<StrongReleaseFn>(kReleaseStrong)(cmd, nullptr, 0xfffff);
}

// What this PC wants, as a host (h5-together.ini, read at each game's start), and the
// computer bonus it wants set ([rules] ai_cheat, when the rules are on and it is set).
unsigned Wants(unsigned *ai_bonus)
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together", L".ini", path, MAX_PATH);
	unsigned wants = 0;
	*ai_bonus = 0;
	// the value the F2 panel shows: the host's choice, else its own MMH5.5 file's (0.3.4-test2
	// read only a saved choice: a host whose file already said "Off" asked for nothing)
	const int ai = HostComputerBonus();
	if (ai >= 0 && ai <= 6) {
		wants |= 1u << features::kAiBonus;
		*ai_bonus = static_cast<unsigned>(ai);
	}
	if (GetPrivateProfileIntW(L"coop", L"script_rhythm", 1, path) != 0)
		wants |= 1u << features::kScriptRhythm;
	if (GetPrivateProfileIntW(L"ai", L"parallel_turns", 1, path) != 0)
		wants |= 1u << features::kParallelAi;
	if (EndlessTurnsAvailable() && GetPrivateProfileIntW(L"coop", L"endless_turns", 1, path) != 0)
		wants |= 1u << features::kEndlessTurns;
	return wants;
}

std::string Names(unsigned bits)
{
	std::string out;
	for (int f = 0; f < features::kFeatureCount; ++f)
		if (bits & (1u << f)) {
			if (!out.empty())
				out += " ";
			out += features::Name(f);
		}
	return out.empty() ? "none" : out;
}

// After this PC's own step (its segment report went out): the hello.
void __fastcall MpStepHook(void *self, void *edx)
{
	g_mp_step_next(self, edx);
	if (g_disabled)
		return;
	const BYTE *t = static_cast<const BYTE *>(self);
	int me = 0, pcs = 0;
	if (!Pcs(t, &me, &pcs))
		return;
	const DWORD reports = Reports(t, me);
	if (t != g_transceiver || reports < g_last_reports)
		NewGame(t);
	g_last_reports = reports;
	if (!g_hello_sent && reports >= features::kSendAt && reports <= features::kSendBy) {
		g_hello_sent = true;
		unsigned ai_bonus = 0;
		const unsigned wants = me == 0 ? Wants(&ai_bonus) : 0;
		const uint32_t value = features::EncodeHello(Knows(), wants, ai_bonus);
		g_hellos[me] = features::DecodeHello(value);
		SendValue(t, value);
		if (me == 0)
			hooklog::Write("co-op features: hello sent (PC 0 of %d, the host; knows %s; wants %s%s)", pcs,
			               Names(Knows()).c_str(), Names(wants).c_str(),
			               (wants & (1u << features::kAiBonus)) ? (" = " + std::to_string(ai_bonus)).c_str() : "");
		else
			hooklog::Write("co-op features: hello sent (PC %d of %d; knows %s)", me, pcs, Names(Knows()).c_str());
	}
}

void __fastcall CheckHook(void *self, void *edx, int pc, const BYTE *cmd)
{
	if (!g_disabled && cmd && *reinterpret_cast<const LONG *>(cmd + 0x0c) == -1) {
		const features::Hello h = features::DecodeHello(*reinterpret_cast<const DWORD *>(cmd + 0x10));
		if (h.valid) {
			const BYTE *t = static_cast<const BYTE *>(self);
			if (t != g_transceiver)
				NewGame(t);
			int me = 0, pcs = 0;
			if (!Pcs(t, &me, &pcs) || pc < 0 || pc >= pcs)
				return;
			const DWORD sender_reports = Reports(t, pc);
			// counted only if it came by its sender's report kSendBy: the same hellos count on every PC
			if (sender_reports <= features::kSendBy && !g_decided) {
				g_hellos[pc] = h;
				hooklog::Write("co-op features: hello from PC %d (version %u; knows %s%s%s)", pc, h.version, Names(h.knows).c_str(),
				               pc == 0 ? "; the host wants " : "", pc == 0 ? Names(h.wants).c_str() : "");
			}
			else
				hooklog::Write("co-op features: hello from PC %d after its report %lu: too late, not counted", pc, sender_reports);
			return;  // ours: the game never sees it
		}
	}
	g_check_next(self, edx, pc, cmd);
}

void SetComputerBonus(unsigned bonus);

// Testing (the robot; never for players): [debug] script_at=<segment> and script=<a line of
// Lua>, the same on every PC of a robot game: that line runs at the start of that segment on
// every PC (the same point of the game), e.g. to take a player's heroes and towns away so that
// it loses (a defeated player watching: docs/plans/backlog.md).
void DebugScriptAt(DWORD segment)
{
	static int at = -2;
	static char line[512];
	if (at == -2) {
		wchar_t path[MAX_PATH];
		hooklog::PathNextToExe(L"h5-together", L".ini", path, MAX_PATH);
		at = static_cast<int>(GetPrivateProfileIntW(L"debug", L"script_at", -1, path));
		wchar_t wide[512] = L"";
		GetPrivateProfileStringW(L"debug", L"script", L"", wide, 512, path);
		WideCharToMultiByte(CP_UTF8, 0, wide, -1, line, sizeof(line), nullptr, nullptr);
	}
	if (at < 0 || !line[0] || segment != static_cast<DWORD>(at))
		return;
	const bool ok = RunGameScript(line);
	hooklog::Write("co-op features: testing: segment %lu: the script line %s: %s", segment, ok ? "ran" : "FAILED", line);
}

// The start of each segment's commands (every PC runs the same segments in the same
// order): the decision at kDecideAt.
void __fastcall SegmentHook(void *transceiver8, void *edx, DWORD time)
{
	if (!g_disabled) {
		const BYTE *t = static_cast<const BYTE *>(transceiver8) - 8;
		if (t != g_transceiver)
			NewGame(t);
		++g_segments;
		if (g_segments == 1)
			JoinTimeGameStarted();  // (the game's own time limit again, on every PC)
		DebugScriptAt(g_segments);
		int me = 0, pcs = 0;
		if (g_segments == features::kDecideAt && !g_decided && Pcs(t, &me, &pcs)) {
			g_decided = true;
			g_on = features::Decide(g_hellos, pcs);
			++g_game;
			std::string missing;
			for (int pc = 0; pc < pcs; ++pc)
				if (!g_hellos[pc].valid)
					missing += (missing.empty() ? "" : ", ") + std::to_string(pc);
			hooklog::Write("co-op features: this game (%d PCs): on: %s%s%s", pcs, Names(g_on).c_str(),
			               missing.empty() ? "" : "; no hello from PC ", missing.c_str());
			if (g_on & (1u << features::kAiBonus))
				SetComputerBonus(g_hellos[0].ai_bonus);
			SetEndlessTurns((g_on & (1u << features::kEndlessTurns)) != 0);
		}
	}
	g_segment_next(transceiver8, edx, time);
}

// ai_bonus: H55_AICheatMode in the running game, on every PC at the same point (the start
// of segment kDecideAt). Logged with what the script says back, so that every PC's log
// shows the same value.
void SetComputerBonus(unsigned bonus)
{
	char line[160];
	_snprintf_s(line, sizeof(line), _TRUNCATE,
	            "H55_AICheatMode = %u; print(\"H5 Together: H55_AICheatMode = \"..tostring(H55_AICheatMode))", bonus);
	const bool ok = RunGameScript(line);
	hooklog::Write("co-op features: the host's computer bonus: H55_AICheatMode = %u (%s) set in the game on every PC%s", bonus,
	               bonus == 0 ? "off" : bonus == 4 ? "100%" : "MMH5.5's 0-6 scale", ok ? "" : ": FAILED here (the script did not run)");
}

void __fastcall RunnerHook(void *transceiver, void *edx)
{
	++g_runner_depth;
	g_runner_next(transceiver, edx);
	--g_runner_depth;
}

bool CodeAsExpected()
{
	for (const Code &c : kCode)
		if (memcmp(reinterpret_cast<const void *>(c.address), c.bytes, c.size) != 0) {
			hooklog::Write("co-op features: the game's code at %08lx is not as expected: off", c.address);
			return false;
		}
	return true;
}

}  // namespace

bool CoopFeatureOn(int feature)
{
	return g_decided && feature >= 0 && feature < features::kFeatureCount && (g_on & (1u << feature)) != 0;
}

bool InMultiplayerSegment()
{
	return g_runner_depth > 0;
}

unsigned CoopFeaturesGame()
{
	return g_game;
}

void ApplyCoopFeaturesFromSettings()
{
	// Every PC takes part (its hello), whatever its own settings: only the host's count.
	if (!CodeAsExpected()) {
		g_disabled = true;
		return;
	}
	g_mp_step_next = reinterpret_cast<StepFn>(hookpatch::PatchPointer(kMpStepCell, kMpStep, &MpStepHook));
	g_check_next = reinterpret_cast<CheckFn>(hookpatch::RedirectCall(kCheckCall, kCheckHandler, &CheckHook));
	g_segment_next = reinterpret_cast<SegmentFn>(hookpatch::RedirectCall(kSegmentCall, kSegmentTime, &SegmentHook));
	g_runner_next = reinterpret_cast<RunnerFn>(hookpatch::RedirectCalls(kRunnerCalls, 2, kRunner, &RunnerHook));
	if (!g_mp_step_next || !g_check_next || !g_segment_next || !g_runner_next) {
		hooklog::Write("co-op features: part of the game's code is not as expected (step %d, messages %d, segments %d, runner %d): "
		               "off (the hooks that went in only pass on)",
		               g_mp_step_next != nullptr, g_check_next != nullptr, g_segment_next != nullptr, g_runner_next != nullptr);
		g_disabled = true;
		// (the runner's depth is still counted if its hook went in: InMultiplayerSegment stays right)
		return;
	}
	unsigned ai_bonus = 0;
	const unsigned wants = Wants(&ai_bonus);
	hooklog::Write("co-op features: ready (knows %s; as host, this PC wants %s%s: [coop] script_rhythm, [ai] parallel_turns, "
	               "[rules] ai_cheat, [coop] endless_turns)",
	               Names(Knows()).c_str(), Names(wants).c_str(),
	               (wants & (1u << features::kAiBonus)) ? (" = " + std::to_string(ai_bonus)).c_str() : "");
}

}  // namespace mods
