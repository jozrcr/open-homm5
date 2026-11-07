// Mod: more time for a PC that joins a multiplayer game ([net] join_timeout_s).
//
// In plain words: the game gives up on a PC that sends nothing for net_timeout seconds (20),
// "timeout 0.18.0" on the joining PC. A PC that joins a random map game first builds the map
// itself (rmg_join.cpp: the game's own way), and while it builds, its game sends nothing:
// with a big map on a slower PC (the owner's 6-player Impossible map under Wine, 2026-11-06:
// about 15 s to build, the box 22 s after Join) the 20 seconds run out. The same limit drops a
// player whose game crashed in a running game, so it is not raised for good: while players
// join (the waiting room) the network managers get join_timeout_s (300 by default since 0.3.5's
// test7; 90 before), and at the first segment of the game (every PC at the same point) the
// game's own values again.
//
// How: the game makes its network manager with 0x0047ef60 (new 0x150, then the constructor
// 0x0047ee50 at 0x0047ef7b, __thiscall(manager, const DWORD settings[2], BYTE)), the settings
// being { net_timeout (whole seconds), 20.0f }; the constructor keeps them at +0x0c and +0x10.
// The call is wrapped: the new manager is remembered (with its vtable 0x00e131e4, checked
// before each write), and its +0x0c is set. Robot (LAN, the 6-player Impossible recipe map,
// 2026-11-06): the host makes this manager when it starts hosting, the joining PC only after
// it built the map; the join took 20.1 s of the host's 20 (Join at 31.3 s, the map built at
// 47.1 s, in at 51.4 s): the host's limit counts there.
//
// The LAN screen's manager (2026-11-07): the game also makes one with its own 60 s each time the
// LAN screen opens, and the joining PC talks to the host through it until the map is built
// (robot: that one at 14.0 s, Join at 31.1 s, the players' manager only at 45.9 s). So
// every manager made outside a game gets join_timeout_s if its own limit is lower, and every
// one of them gets its own limit back at the game's first segment.
//
// The joining PC's join stages (found 2026-11-07, the real cause of the laptop's timeouts): the
// waiting room's logic gives the join stages the console variables net_multiplayer_init_timeout
// (10 s, read at 0x0071e24c: mov ecx, [0x00f3bee8]), net_ubicom_init_timeout (10 s, 0x0071e3aa:
// mov eax, [0x00f3bee4]) and net_load_timeout (20 s, 0x0075386b: mov ecx, [0x00f3d484]), handed
// to the stage timer 0x00710380. A Linux PC under Proton builds the owner's 6-player Impossible
// builder map in 25-28 s, then needs a few seconds of handshake: the error came 30-33 s after
// Join on every hand try ("timeout 0.18.0"), while the robot's join made it in 29.1 s. With the
// three at 120 in the laptop's profile the hand join worked (29.2 s). So the three reads are
// pointed at our own value, join_timeout_s (not the variables: the game saves its variables into
// the player's profile when it exits).
//
// Kept active under Wine (the second half of the Linux join, 2026-11-07): the game's activation
// handler (0x00c3b3c0, from the main loop's WM_ACTIVATEAPP) marks the game inactive when it
// loses the focus and, in fullscreen, minimizes it, unless the console variable
// app_always_active (0x010d85c3; 0 in every profile) is set; its one read is
// "cmp byte ptr [0x010d85c3], 0" at 0x00c3b3dc. During the 25-28 s map build GNOME's "not
// responding" box takes the focus, and after the build the joining game barely ran its
// handshake. The laptop's tries: kept active with the game's 30 s, failed at 33 s (time); not
// kept active with 300 s, failed (the handshake never answered); kept active with 120 s,
// joined; the robot (always kept active) joined at 29.1 s with 30 s. So under Wine
// ([video] background=2, the default) that read points at our own 1; 1 = on Windows too,
// 0 = the game's own. The variable and the player's profile stay as they are.
#include "join_time.h"

#include <windows.h>

#include <string>

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

const DWORD kCtorCall = 0x0047ef7b;
const DWORD kCtor = 0x0047ee50;
const DWORD kManagerVftable = 0x00e131e4;
const DWORD kTimeoutOffset = 0x0c;
const DWORD kNetTimeoutVar = 0x00f27394;  // the console variable net_timeout (20)

typedef void *(__fastcall *CtorFn)(void *self, void *edx, const DWORD *settings, BYTE flag);
CtorFn g_ctor_next = nullptr;
DWORD g_join_s = 300;          // 0: off (the game's own)
struct Raised {
	void *manager;  // a network manager whose limit we raised
	DWORD game_s;   // its limit as the game made it
};
const int kRaised = 8;
Raised g_raised[kRaised];      // (a manager made again at the same address takes its place)
int g_made = 0;

// The join stages' time limits (the three reads, pointed at g_stage_s)
DWORD g_stage_s = 0;
struct StageRead {
	DWORD operand;   // the address of the variable in the instruction
	DWORD variable;  // the console variable it reads
	const char *name;
};
const StageRead kStageReads[] = {
	{0x0071e24e, 0x00f3bee8, "net_multiplayer_init_timeout"},
	{0x0071e3ab, 0x00f3bee4, "net_ubicom_init_timeout"},
	{0x0075386d, 0x00f3d484, "net_load_timeout"},
};

const StageRead kAlwaysActiveRead = {0x00c3b3de, 0x010d85c3, "app_always_active"};
BYTE g_always_active = 1;

bool UnderWine()
{
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

bool PointRead(const StageRead &r, DWORD value)
{
	DWORD *p = reinterpret_cast<DWORD *>(r.operand);
	if (*p == value)
		return true;
	if (*p != r.variable) {
		hooklog::Write("join time: the read of %s at %08lx is %08lx, not %08lx: left alone", r.name, r.operand, *p, r.variable);
		return false;
	}
	DWORD old;
	if (!VirtualProtect(p, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &old))
		return false;
	*p = value;
	VirtualProtect(p, sizeof(DWORD), old, &old);
	FlushInstructionCache(GetCurrentProcess(), p, sizeof(DWORD));
	return true;
}

bool IsManager(void *p)
{
	__try {
		return p && *static_cast<const DWORD *>(p) == kManagerVftable;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool SetLimit(void *manager, DWORD seconds)
{
	if (!IsManager(manager))
		return false;
	__try {
		*reinterpret_cast<DWORD *>(static_cast<BYTE *>(manager) + kTimeoutOffset) = seconds;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void Remember(void *manager, DWORD game_s)
{
	int slot = -1;
	for (int i = 0; i < kRaised && slot < 0; ++i)
		if (g_raised[i].manager == manager)
			slot = i;
	for (int i = 0; i < kRaised && slot < 0; ++i)
		if (!g_raised[i].manager || !IsManager(g_raised[i].manager))
			slot = i;
	if (slot < 0)
		slot = g_made % kRaised;
	g_raised[slot].manager = manager;
	g_raised[slot].game_s = game_s;
}

void *__fastcall CtorHook(void *self, void *edx, const DWORD *settings, BYTE flag)
{
	void *result = g_ctor_next(self, edx, settings, flag);
	const DWORD limit = settings ? settings[0] : 0;
	const bool players = limit == *reinterpret_cast<const DWORD *>(kNetTimeoutVar);
	++g_made;
	for (Raised &r : g_raised)  // a new manager where an old one was: not that one any more
		if (r.manager == self)
			r.manager = nullptr;
	bool raised = false;
	if (g_join_s > limit && SetLimit(self, g_join_s)) {
		Remember(self, limit);
		raised = true;
	}
	hooklog::Write("join time: the game made a network manager (#%d, its time limit %lu s%s%s)", g_made, limit,
	               players ? ", the players' one" : ", the LAN screen's (a joining PC's until its map is built)",
	               raised ? (": " + std::to_string(g_join_s) + " s while players join").c_str() : "");
	return result;
}

}  // namespace

void JoinTimeGameStarted()
{
	for (Raised &r : g_raised) {
		if (!r.manager)
			continue;
		const bool ok = SetLimit(r.manager, r.game_s);
		hooklog::Write("join time: the game starts: a network manager's time limit is the game's %lu s again%s", r.game_s,
		               ok ? "" : " (it is gone: nothing to do)");
		r.manager = nullptr;
	}
}

void ApplyJoinTimeFromSettings()
{
	wchar_t ini[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together", L".ini", ini, MAX_PATH);
	g_join_s = static_cast<DWORD>(GetPrivateProfileIntW(L"net", L"join_timeout_s", 300, ini));
	if (g_join_s > 600)
		g_join_s = 600;
	g_ctor_next = reinterpret_cast<CtorFn>(hookpatch::RedirectCall(kCtorCall, kCtor, reinterpret_cast<const void *>(&CtorHook)));
	if (g_join_s) {
		g_stage_s = g_join_s;
		int pointed = 0;
		for (const StageRead &r : kStageReads)
			pointed += PointRead(r, reinterpret_cast<DWORD>(&g_stage_s)) ? 1 : 0;
		hooklog::Write("join time: the join stages' time limits (net_multiplayer_init_timeout 10 s, net_ubicom_init_timeout "
		               "10 s, net_load_timeout 20 s) are %lu s: %d of 3 reads pointed at our value",
		               g_stage_s, pointed);
	}
	const int background = static_cast<int>(GetPrivateProfileIntW(L"video", L"background", 2, ini));
	const bool wine = UnderWine();
	if (background == 1 || (background == 2 && wine)) {
		const bool ok = PointRead(kAlwaysActiveRead, reinterpret_cast<DWORD>(&g_always_active));
		hooklog::Write(ok ? "background: the game keeps running when another window has the focus (%s; app_always_active "
		                    "read as 1, the profile untouched): a long map build no longer stalls a join"
		                  : "background: the game's code is not as expected (%s): the game's own behaviour",
		               wine ? "Linux/Wine" : "[video] background=1");
	}
	else
		hooklog::Write("background: the game's own (it pauses and, in fullscreen, minimizes itself without the focus; %s)",
		               wine ? "[video] background=0" : "Windows: only needed under Linux/Wine");
	hooklog::Write(g_ctor_next ? (g_join_s ? "join time: on: %lu s for a PC that joins (the game's 20 s again once the game runs)"
	                                       : "join time: off: the game's own time limit (join_timeout_s=0)%.0lu")
	                           : "join time: the game's code is not as expected: off%.0lu",
	               g_join_s);
}

}  // namespace mods
