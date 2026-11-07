// Mod: undo End Turn while the other humans still play ([coop] undo_end_turn: the
// host's choice, off by default; docs/plans/turn-features.md, part 1).
//
// In plain words: with simultaneous turns, a player who pressed End Turn too early
// gets a round button "Resume my turn" over the game's own hourglass (the End Turn
// button), for as long as another human still plays. A click undoes the End Turn on
// every PC, at the same moment of the game.
//
// 1. The game's turns (players.md, turn-features.md): End Turn sets the player's
//    "pressed End Turn" (+0x178, IPlayer slot 0x50); with simultaneous turns the
//    world's turn manager (0x00998520, every world tick) gives state 1 to each player
//    at state 0 who pressed it, as long as some player at state 0 has not; when none
//    is left, the next turn starts (0x00998060) in the same tick. The undo puts
//    "pressed" and the state back to 0 with the player's own setters (slots 0x50 and
//    0x48): the game's banner and player list follow by themselves (robot, 2026-11-04).
// 2. Who takes part, decided once per game (the hero looks' rule, hero_skins.cpp 5):
//    each PC's DLL sends a hello after its own segment report kSendAt, which counts if
//    it came by its sender's report kSendBy; the host's hello says whether the host
//    allows the undo. At the start of segment kDecideAt every PC decides the same:
//    on only if every PC sent its hello and the host allows it (undo::Decide).
// 3. A click (or the robot's "panel|turn request"): this PC sends "undo player P"
//    after its next segment report R. Every PC, the sender too, takes it in at the
//    start of segment R + kDelay (the segment runner's call 0x007c9a67, before that
//    segment's commands) and decides there, on the same world, whether it is still
//    allowed (undo::Allowed); a request that comes after the last other human ended
//    is refused the same way everywhere.
// 4. The button: an overlay client on the adventure screen, a disc over the hourglass at
//    the centre of the game's wheel (an arrow turning back around a small hourglass; its
//    name at the left of the wheel a few seconds, and under the mouse), shown only while
//    this PC's player could undo; the overlay holds back the game's clicks under it
//    (overlay::BlockClicksIn), so the click does not reach the hourglass.
//
// Robot steps: "panel|turn request" (as a click on the button), "panel|turn
// force_request" (a request even when the button would not show: to test that every
// PC refuses it the same way), "panel|turn states" (every player's state in the log).
#include "turn_undo.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "rmg_builder_widgets.h"
#include "turn_undo_logic.h"

#include "../hook/log.h"
#include "../hook/overlay.h"
#include "../hook/patch.h"
#include "../hook/robot.h"
#include "../hook/texts.h"
#include "../hook/third_party/imgui/imgui.h"

namespace mods {
namespace {

namespace ui = builder_ui;

const char kName[] = "turn_undo";
const char kNameAcross[] = "turn_undo across";  // (more click zones: one per name)
const char kNameDown[] = "turn_undo down";
const char kAdventureScreen[] = ".?AVCAdventureScreen@NUI@@";

// --- the game (vanilla 3.1 addresses, the same in MMH55_Utility) -----------------------------
const DWORD kUpdateCall = 0x0060895e;      // in 0x00608930: call 0x006082f0 (the adventure controller's frame)
const DWORD kUpdate = 0x006082f0;          // __thiscall, 5 stack arguments
const DWORD kControllerWorld = 0x18, kControllerLocal = 0x3c;  // NWorld world, this PC's IPlayer *
const DWORD kMpStepCell = 0x00e43f1c + 0x10;  // NWorld::CMultiPlayerTransceiver slot 4 (its own step: reports)
const DWORD kMpStep = 0x007ca990;
const DWORD kCheckCall = 0x007ca8d1;       // in the message pump 0x007ca7c0: call 0x007c9410 (a checksum came)
const DWORD kCheckHandler = 0x007c9410;    // __thiscall(int pc, CCrcTestCmd *), ret 8; ignores check -1
const DWORD kSegmentCall = 0x007c9a67;     // in the segment runner 0x007c98e0, before a segment's commands
const DWORD kSegmentTime = 0x007cc680;     // __thiscall(transceiver + 8, DWORD game time), ret 4
const DWORD kCrcCmdVftable = 0x00e43db0;   // NWorld::CCrcTestCmd, 0x2c bytes
const DWORD kGameNew = 0x00878fd0;
const DWORD kReleaseStrong = 0x008c61d0;   // __thiscall(object, mask)
// The transceiver (as the co-op guard and hero looks read it): the network, the
// players' segment reports (one per PC), this PC's index (the host is 0)
const DWORD kNet = 0x34, kReports = 0x3c, kReportsEnd = 0x40, kMe = 0x54;
const int kMaxPcs = 8;
// The world: its players (vector of IPlayer *), simultaneous turns (byte)
const DWORD kPlayers = 0x24, kPlayersEnd = 0x28, kSimultaneous = 0x90;
// IPlayer slots (byte offsets in its vtable 0x00e1a09c)
const DWORD kNumber = 0x2c, kState = 0x44, kSetState = 0x48, kPressed = 0x4c, kSetPressed = 0x50, kInGame = 0x6c,
            kHuman = 0x184;

struct Code {
	DWORD address;
	BYTE bytes[12];
	int size;
};
// The functions behind the slots above (players.md) and the hooked code (as hero looks
// checks it), checked before anything is changed.
const Code kCode[] = {
	{0x00720e90, {0x8b, 0x41, 0x78, 0xc3}, 4},                            // slot 0x2c: the number at +0x78
	{0x00500c10, {0x8b, 0x81, 0x34, 0x06, 0x00, 0x00}, 6},                // slot 0x44: the state at +0x634
	{0x00500c20, {0x8b, 0x44, 0x24, 0x04, 0x39, 0x81, 0x34, 0x06}, 8},   // slot 0x48: set it
	{0x00500c40, {0x8a, 0x81, 0x78, 0x01, 0x00, 0x00}, 6},                // slot 0x4c: pressed at +0x178
	{0x00500c50, {0x8a, 0x44, 0x24, 0x04, 0x38, 0x81, 0x78, 0x01}, 8},   // slot 0x50: set it
	{0x00500d60, {0x33, 0xc0, 0x39, 0x41, 0x74, 0x0f, 0x94, 0xc0, 0xc3}, 9},  // slot 0x184: human
	{kMpStep, {0x56, 0x8b, 0xf1, 0xe8, 0xf8, 0x1c, 0x21, 0x00}, 8},
	{kCheckHandler, {0x83, 0xec, 0x60, 0x33, 0xc0, 0x39, 0x44, 0x24}, 8},
	{0x007c9a5f, {0x8d, 0x4e, 0x08, 0x53, 0x89, 0x4c, 0x24, 0x20}, 8},    // lea ecx,[esi+8]; push ebx; ... (then the call)
	{kSegmentTime, {0x53, 0x55, 0x8b, 0x6c, 0x24, 0x0c, 0x57, 0x55, 0x8b, 0xd9}, 10},
};

// --- layout: the game's 1024x768 screens, centred and scaled with the height ---------------
// The button: over the hourglass at the centre of the game's wheel at the bottom right (the
// owner's idea, 2026-11-05), round like it. The wheel (the game's EoTButton, 202x220 at
// 823,549 of its 1024x768 layout) keeps to the screen's bottom-right corner, scaled with the
// height: the hourglass's centre 122.7 left of the right edge and 113.2 over the bottom, its
// dark disc 28 around, the gold ring 39.5 (the robot's 1008x729 screenshot and the owner's
// 2560x1440 one agree within a pixel). The wheel's six small buttons start 50 from the centre.
const float kRefH = 768.0f;
const float kDialRight = 122.7f, kDialBottom = 113.2f, kDialR = 39.5f, kBlockHalf = 33.0f, kBlockBar = 22.0f;
const float kLabelFont = 14.0f, kLabelRight = 114.0f;  // the name: left of the wheel's horse button (108)
const DWORD kLabelMs = 4000;

typedef void(__fastcall *UpdateFn)(void *controller, void *edx, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e);
typedef void(__fastcall *StepFn)(void *transceiver, void *edx);
typedef void(__fastcall *CheckFn)(void *transceiver, void *edx, int pc, const BYTE *cmd);
typedef void(__fastcall *SegmentFn)(void *transceiver8, void *edx, DWORD time);
typedef int(__fastcall *GetFn)(void *self, void *edx);
typedef void(__fastcall *SetFn)(void *self, void *edx, int value);

UpdateFn g_update_next = nullptr;
bool g_disabled = false;  // part of the game's code was not as expected: the hooks only pass on
StepFn g_mp_step_next = nullptr;
CheckFn g_check_next = nullptr;
SegmentFn g_segment_next = nullptr;

// --- state: all on the game's main thread (its frame, the message pump, the segment
// runner and the transceiver's step), except the overlay's draw, which reads g_shown ---
void *g_controller = nullptr;     // the adventure controller (world, this PC's player)
const void *g_transceiver = nullptr;
DWORD g_segments = 0;             // segments started in this game
DWORD g_last_reports = 0;
bool g_hello_sent = false;
unsigned g_hellos[kMaxPcs];       // each PC's hello: 0 none, else its version
bool g_host_allows = false;
bool g_decided = false, g_on = false;
int g_request = 0;                // a click: the player to undo, sent after this PC's next report
struct Pending {
	DWORD at;
	int player;
	int from_pc;
};
std::vector<Pending> g_pending;
DWORD g_requested_at = 0;         // this PC's last request counts at this segment (no button until then)
// the button
bool g_shown = false, g_down = false, g_hovered = false;
float g_back_w = 1024.0f, g_back_h = 768.0f;

DWORD Read(const void *p, DWORD off)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(p) + off);
}

int Get(void *player, DWORD slot)
{
	return reinterpret_cast<GetFn>(Read(reinterpret_cast<void *>(Read(player, 0)), slot))(player, nullptr);
}

void Set(void *player, DWORD slot, int value)
{
	reinterpret_cast<SetFn>(Read(reinterpret_cast<void *>(Read(player, 0)), slot))(player, nullptr, value);
}

// The game's "is this object being destroyed" test (players.md).
bool Dying(const void *object)
{
	const BYTE *o = static_cast<const BYTE *>(object);
	const DWORD vbtable = Read(o, 4);
	const LONG base = *reinterpret_cast<const LONG *>(vbtable + 4);
	return (o[base + 0xb] & 0x80) != 0;
}

void *World()
{
	return g_controller ? reinterpret_cast<void *>(Read(g_controller, kControllerWorld)) : nullptr;
}

// The players as undo::Allowed sees them, and their IPlayer pointers (same order).
int ReadPlayers(void *world, undo::Player *out, void **pointers, int max)
{
	int n = 0;
	void **p = reinterpret_cast<void **>(Read(world, kPlayers));
	void **end = reinterpret_cast<void **>(Read(world, kPlayersEnd));
	for (; p && p < end && n < max; ++p) {
		if (!*p || Dying(*p))
			continue;
		undo::Player &u = out[n];
		u.number = Get(*p, kNumber);
		u.human = (Get(*p, kHuman) & 0xff) != 0;
		u.in_game = Get(*p, kInGame) == 1;
		u.state = Get(*p, kState);
		u.pressed = (Get(*p, kPressed) & 0xff) != 0;
		pointers[n++] = *p;
	}
	return n;
}

bool Simultaneous(void *world)
{
	return *(static_cast<const BYTE *>(world) + kSimultaneous) != 0;
}

// "1h:0/1 2h:1/0 3ai:..." : number, human or computer, state / pressed End Turn.
std::string States(void *world)
{
	undo::Player players[8];
	void *pointers[8];
	const int n = ReadPlayers(world, players, pointers, 8);
	std::string out;
	for (int i = 0; i < n; ++i) {
		if (!players[i].in_game)
			continue;
		char item[32];
		_snprintf_s(item, sizeof(item), _TRUNCATE, "%s%d%s:%d/%d", out.empty() ? "" : " ", players[i].number,
		            players[i].human ? "h" : "ai", players[i].state, players[i].pressed ? 1 : 0);
		out += item;
	}
	return out;
}

// May `number` undo its End Turn on this world now?
bool AllowedNow(void *world, int number, const char **why)
{
	undo::Player players[8];
	void *pointers[8];
	const int n = ReadPlayers(world, players, pointers, 8);
	return undo::Allowed(players, n, Simultaneous(world), number, why);
}

// The undo, at the start of a segment on every PC: done if still allowed.
void Apply(void *world, int number, int from_pc)
{
	undo::Player players[8];
	void *pointers[8];
	const int n = ReadPlayers(world, players, pointers, 8);
	const std::string before = States(world);
	const char *why = "";
	if (!undo::Allowed(players, n, Simultaneous(world), number, &why)) {
		hooklog::Write("turn undo: segment %lu: PC %d's request for player %d refused: %s (%s)", g_segments, from_pc, number, why,
		               before.c_str());
		return;
	}
	for (int i = 0; i < n; ++i)
		if (players[i].number == number) {
			Set(pointers[i], kSetPressed, 0);
			Set(pointers[i], kSetState, 0);
		}
	hooklog::Write("turn undo: segment %lu: player %d plays again (PC %d asked; before: %s; after: %s)", g_segments, number,
	               from_pc, before.c_str(), States(world).c_str());
}

int LocalNumber()
{
	if (!g_controller)
		return 0;
	void *local = reinterpret_cast<void *>(Read(g_controller, kControllerLocal));
	return local && !Dying(local) ? Get(local, kNumber) : 0;
}

// --- the game's hooks --------------------------------------------------------------------------
void NewGame(const void *transceiver)
{
	g_transceiver = transceiver;
	g_segments = 0;
	g_last_reports = 0;
	g_hello_sent = false;
	memset(g_hellos, 0, sizeof(g_hellos));
	g_host_allows = false;
	g_decided = false;
	g_on = false;
	g_request = 0;
	g_pending.clear();
	g_requested_at = 0;
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
// hero looks and the co-op guard send theirs; the game ignores check -1).
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

bool HostSetting()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together", L".ini", path, MAX_PATH);
	return GetPrivateProfileIntW(L"coop", L"undo_end_turn", 1, path) != 0;
}

void __fastcall UpdateHook(void *controller, void *edx, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e)
{
	g_controller = controller;
	g_update_next(controller, edx, a, b, c, d, e);
}

// After this PC's own step (its segment report went out): the hello, and a request.
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
	if (!g_hello_sent && reports >= undo::kSendAt && reports <= undo::kSendBy) {
		g_hello_sent = true;
		const bool allow = me == 0 && HostSetting();
		g_hellos[me] = undo::kVersion;
		if (me == 0)
			g_host_allows = allow;
		SendValue(t, undo::EncodeHello(allow));
		hooklog::Write("turn undo: hello sent (PC %d of %d%s)", me, pcs, me == 0 ? (allow ? ", the host allows the undo" : ", the host does not allow the undo ([coop] undo_end_turn=0)") : "");
	}
	if (g_request && g_on) {
		const DWORD at = undo::RequestCountsAt(reports);
		SendValue(t, undo::EncodeRequest(g_request));
		g_pending.push_back({at, g_request, me});
		g_requested_at = at;
		hooklog::Write("turn undo: request sent for player %d (after this PC's report %lu; it counts at segment %lu)", g_request,
		               reports, at);
	}
	g_request = 0;
}

void __fastcall CheckHook(void *self, void *edx, int pc, const BYTE *cmd)
{
	if (!g_disabled && cmd && *reinterpret_cast<const LONG *>(cmd + 0x0c) == -1) {
		const undo::Message m = undo::Decode(*reinterpret_cast<const DWORD *>(cmd + 0x10));
		if (m.kind != undo::Message::kNone) {
			const BYTE *t = static_cast<const BYTE *>(self);
			int me = 0, pcs = 0;
			if (t != g_transceiver)
				NewGame(t);
			if (!Pcs(t, &me, &pcs) || pc < 0 || pc >= pcs)
				return;
			const DWORD sender_reports = Reports(t, pc);
			if (m.kind == undo::Message::kHello) {
				// counted only if it came by its sender's report kSendBy: the same hellos count on every PC
				if (sender_reports <= undo::kSendBy && !g_decided) {
					g_hellos[pc] = m.version;
					if (pc == 0)
						g_host_allows = m.allow;
					hooklog::Write("turn undo: hello from PC %d (version %u%s)", pc, m.version,
					               pc == 0 ? (m.allow ? ", the host allows the undo" : ", the host does not allow the undo") : "");
				}
				else
					hooklog::Write("turn undo: hello from PC %d after its report %lu: too late, not counted", pc, sender_reports);
			}
			else if (g_on) {
				const DWORD at = undo::RequestCountsAt(sender_reports);
				g_pending.push_back({at, m.player, pc});
				hooklog::Write("turn undo: PC %d asks to undo player %d's End Turn (counts at segment %lu)", pc, m.player, at);
			}
			return;  // ours: the game never sees it
		}
	}
	g_check_next(self, edx, pc, cmd);
}

void SafeApply(void *world, int number, int from_pc)
{
	__try {
		Apply(world, number, from_pc);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("turn undo: crashed reading the players (caught)");
	}
}

void RobotCommand(const char *text);

// The start of each segment's commands (every PC runs the same segments in the same
// order): the decision at kDecideAt, then the requests that count at this segment.
void __fastcall SegmentHook(void *transceiver8, void *edx, DWORD time)
{
	if (g_disabled) {
		g_segment_next(transceiver8, edx, time);
		return;
	}
	const BYTE *t = static_cast<const BYTE *>(transceiver8) - 8;
	if (t != g_transceiver)
		NewGame(t);
	++g_segments;
	char command[64];
	while (robot::TakePanelCommand(command, sizeof(command), "turn "))
		RobotCommand(command);
	int me = 0, pcs = 0;
	if (g_segments == undo::kDecideAt && !g_decided && Pcs(t, &me, &pcs)) {
		g_decided = true;
		g_on = undo::Decide(g_hellos, pcs, g_host_allows);
		if (g_on)
			hooklog::Write("turn undo: on for this game (all %d PCs know it, the host allows it)", pcs);
		else {
			int missing = -1;
			for (int pc = 0; pc < pcs && missing < 0; ++pc)
				if (!g_hellos[pc])
					missing = pc;
			hooklog::Write("turn undo: off for this game (%s)", !g_host_allows ? "the host does not allow it" : missing >= 0 ? "a PC sent no hello: no add-on, or an older one" : "a game of one PC");
		}
	}
	void *world = World();
	for (size_t i = 0; i < g_pending.size();) {
		if (g_pending[i].at > g_segments) {
			++i;
			continue;
		}
		if (g_pending[i].at < g_segments)
			hooklog::Write("turn undo: a request for segment %lu taken at segment %lu (late!)", g_pending[i].at, g_segments);
		if (world)
			SafeApply(world, g_pending[i].player, g_pending[i].from_pc);
		g_pending.erase(g_pending.begin() + i);
	}
	g_segment_next(transceiver8, edx, time);
}

// --- the button -----------------------------------------------------------------------------------
// May this PC's player undo now (the button shows)?
bool CanUndoHere()
{
	if (!g_on || !g_controller || (g_requested_at && g_segments < g_requested_at))
		return false;
	void *world = World();
	const int me = LocalNumber();
	const char *why;
	return world && me > 0 && AllowedNow(world, me, &why);
}

bool SafeCanUndoHere()
{
	__try {
		return CanUndoHere();
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void Clicked(const char *how)
{
	const int me = LocalNumber();
	if (me <= 0)
		return;
	g_request = me;  // sent after this PC's next segment report
	hooklog::Write("turn undo: %s: undo player %d's End Turn", how, me);
}

void RobotCommand(const char *text)
{
	if (strcmp(text, "request") == 0) {
		if (SafeCanUndoHere())
			Clicked("robot");
		else
			hooklog::Write("turn undo: robot: no button now (%s)", g_on ? "the undo is not allowed now" : "the undo is off in this game");
	}
	else if (strcmp(text, "force_request") == 0) {
		// a request even when the button would not show: every PC must refuse it the same way
		if (g_on)
			Clicked("robot (forced, to test a refusal)");
		else
			hooklog::Write("turn undo: robot: the undo is off in this game");
	}
	else if (strcmp(text, "states") == 0) {
		void *world = World();
		hooklog::Write("turn undo: segment %lu: %s", g_segments, world ? States(world).c_str() : "(no world)");
	}
	else
		hooklog::Write("turn undo: robot command \"%s\" not known", text);
}

// The hourglass's centre on screen, and the button's radius.
ImVec2 DialCentre(float w, float h, float *radius)
{
	const float k = h / kRefH;
	*radius = kDialR * k;
	return ImVec2(w - kDialRight * k, h - kDialBottom * k);
}

bool g_was_shown = false;
DWORD g_shown_since = 0;  // the label shows a few seconds after the button comes

bool Wanted()
{
	char top[96] = "";
	overlay::TopScreen(top, sizeof(top));
	g_shown = strcmp(top, kAdventureScreen) == 0 && SafeCanUndoHere();
	if (g_shown && !g_was_shown)
		g_shown_since = GetTickCount();
	g_was_shown = g_shown;
	if (!g_shown) {
		g_down = g_hovered = false;
		return false;
	}
	float r = 0;
	const ImVec2 c = DialCentre(g_back_w, g_back_h, &r);
	float x, y;
	HWND window = overlay::GameWindow();
	const bool front = window && GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) == GetAncestor(window, GA_ROOTOWNER);
	g_hovered = front && overlay::CursorInBackBuffer(g_back_w, g_back_h, &x, &y) && (x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= r * r;
	const bool down = front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (down && !g_down && g_hovered)
		g_down = true;
	else if (!down && g_down) {
		g_down = false;
		if (g_hovered)
			Clicked("its button");
	}
	return true;
}

void Prepare(const overlay::Frame &frame)
{
	ui::LoadPictures(frame.device);
	overlay::WantFont(floorf(kLabelFont * frame.height / kRefH + 0.5f));
}

ImU32 Rgba(int r, int g, int b, float a)
{
	return IM_COL32(r, g, b, static_cast<int>(a * 255.0f + 0.5f));
}

// A small hourglass, in the game's colours: wooden ends, the glass, the sand mostly run down.
void Hourglass(ImDrawList *draw, ImVec2 o, float k, float a)
{
	const float hh = 11.0f * k, hw = 7.0f * k, neck = 1.2f * k;
	const ImU32 glass = Rgba(196, 206, 204, 0.85f * a), sand = Rgba(226, 186, 112, a), wood = Rgba(150, 96, 52, a);
	draw->AddTriangleFilled(ImVec2(o.x - hw * 0.8f, o.y + hh - 1.5f * k), ImVec2(o.x + hw * 0.8f, o.y + hh - 1.5f * k),
	                        ImVec2(o.x, o.y + 2.5f * k), sand);
	draw->AddTriangleFilled(ImVec2(o.x - hw * 0.45f, o.y - 4.5f * k), ImVec2(o.x + hw * 0.45f, o.y - 4.5f * k), ImVec2(o.x, o.y - 0.5f * k),
	                        sand);
	const ImVec2 left[] = {ImVec2(o.x - hw, o.y - hh), ImVec2(o.x - neck, o.y), ImVec2(o.x - hw, o.y + hh)};
	const ImVec2 right[] = {ImVec2(o.x + hw, o.y - hh), ImVec2(o.x + neck, o.y), ImVec2(o.x + hw, o.y + hh)};
	draw->AddPolyline(left, 3, glass, 0, 1.4f * k);
	draw->AddPolyline(right, 3, glass, 0, 1.4f * k);
	draw->AddLine(ImVec2(o.x - hw - 2.0f * k, o.y - hh), ImVec2(o.x + hw + 2.0f * k, o.y - hh), wood, 2.6f * k);
	draw->AddLine(ImVec2(o.x - hw - 2.0f * k, o.y + hh), ImVec2(o.x + hw + 2.0f * k, o.y + hh), wood, 2.6f * k);
}

// An arrow turning back (counter-clockwise) around the hourglass: "back to your turn".
void TurnBackArrow(ImDrawList *draw, ImVec2 o, float radius, float k, ImU32 ink)
{
	const float pi = 3.14159265f;
	const float a0 = -pi / 2 + 0.55f, a1 = -pi / 2 + 2 * pi - 0.3f;  // open at the top; the head at the right of the gap
	const float head_len = 8.0f * k, head_half = 6.0f * k;
	draw->PathArcTo(o, radius, a0 + 0.1f, a1, 48);
	draw->PathStroke(ink, 0, 3.2f * k);
	const ImVec2 base(o.x + radius * cosf(a0), o.y + radius * sinf(a0));
	const ImVec2 along(sinf(a0), -cosf(a0));   // counter-clockwise, along the circle
	const ImVec2 out(cosf(a0), sinf(a0));      // away from the centre
	draw->AddTriangleFilled(ImVec2(base.x + along.x * head_len, base.y + along.y * head_len),
	                        ImVec2(base.x + out.x * head_half, base.y + out.y * head_half),
	                        ImVec2(base.x - out.x * head_half, base.y - out.y * head_half), ink);
}

void Draw(const overlay::Frame &frame)
{
	g_back_w = frame.width;
	g_back_h = frame.height;
	if (!g_shown)
		return;
	const float k = frame.height / kRefH;
	float r = 0;
	const ImVec2 c = DialCentre(frame.width, frame.height, &r);
	// the game's clicks under the round button do not reach the hourglass: a square inside the
	// circle and two bars across it (never over the wheel's six small buttons around)
	const float s = kBlockHalf * k, bar = kBlockBar * k;
	overlay::BlockClicksIn(kName, c.x - s, c.y - s, c.x + s, c.y + s, frame.width, frame.height);
	overlay::BlockClicksIn(kNameAcross, c.x - r, c.y - bar, c.x + r, c.y + bar, frame.width, frame.height);
	overlay::BlockClicksIn(kNameDown, c.x - bar, c.y - r, c.x + bar, c.y + r, frame.width, frame.height);
	ImDrawList *draw = ImGui::GetForegroundDrawList();
	const bool pressed = g_down && g_hovered;
	const ImVec2 o(c.x, c.y + (pressed ? k : 0.0f));
	// the disc over the game's hourglass and its ring: the game's dark teal, a gold rim
	draw->AddCircleFilled(c, r, IM_COL32(4, 16, 21, 240), 64);
	draw->AddCircleFilled(c, r * 0.72f, pressed ? IM_COL32(6, 26, 33, 255) : IM_COL32(9, 34, 43, 255), 64);
	draw->AddCircle(c, r - 1.2f * k, IM_COL32(120, 84, 40, 255), 64, 2.4f * k);
	if (g_hovered && !pressed)
		draw->AddCircle(c, r - 1.2f * k, IM_COL32(255, 222, 150, 150), 64, 4.0f * k);
	Hourglass(draw, o, k, 1.0f);
	const ImU32 ink = pressed ? IM_COL32(200, 160, 90, 255) : g_hovered ? IM_COL32(255, 232, 170, 255) : IM_COL32(236, 196, 116, 255);
	TurnBackArrow(draw, o, r * 0.86f, k, ink);
	// its name, at the left of the wheel: a few seconds when it comes, and under the mouse
	const DWORD age = GetTickCount() - g_shown_since;
	const float a = g_hovered ? 1.0f : age < kLabelMs ? 1.0f : age < kLabelMs + 600 ? 1.0f - (age - kLabelMs) / 600.0f : 0.0f;
	if (a > 0.0f) {
		const float px = floorf(kLabelFont * k + 0.5f);
		ImFont *font = overlay::Font(px);
		const char *text = texts::Get("turn.undo_button");
		const ImVec2 size = font->CalcTextSizeA(px, 10000.0f, 0.0f, text);
		const float right = floorf(c.x - kLabelRight * k), pad_x = 9.0f * k, pad_y = 5.0f * k;
		const ImVec2 p0(floorf(right - size.x - 2 * pad_x), floorf(c.y - size.y / 2 - pad_y));
		const ImVec2 p1(right, floorf(c.y + size.y / 2 + pad_y));
		draw->AddRectFilled(p0, p1, Rgba(14, 11, 8, 0.82f * a), 4.0f * k);
		draw->AddRect(p0, p1, Rgba(150, 112, 60, 0.9f * a), 4.0f * k, 0, 1.2f * k);
		draw->AddText(font, px, ImVec2(p0.x + pad_x + 1, p0.y + pad_y + 1), Rgba(0, 0, 0, 0.8f * a), text);
		draw->AddText(font, px, ImVec2(p0.x + pad_x, p0.y + pad_y), Rgba(255, 236, 190, a), text);
	}
	static bool told = false;
	if (!told) {
		told = true;
		hooklog::Write("turn undo: the button is shown over the hourglass (the first time in this run)");
	}
}

bool CodeAsExpected()
{
	for (const Code &c : kCode)
		if (memcmp(reinterpret_cast<const void *>(c.address), c.bytes, c.size) != 0) {
			hooklog::Write("turn undo: the game's code at %08lx is not as expected: off", c.address);
			return false;
		}
	return true;
}

}  // namespace

void ApplyTurnUndoFromSettings()
{
	// Every PC takes part (its hello, others' requests), whatever its own setting: only
	// the host's [coop] undo_end_turn decides, at each game's start.
	if (!CodeAsExpected())
		return;
	g_update_next = reinterpret_cast<UpdateFn>(hookpatch::RedirectCall(kUpdateCall, kUpdate, &UpdateHook));
	g_mp_step_next = reinterpret_cast<StepFn>(hookpatch::PatchPointer(kMpStepCell, kMpStep, &MpStepHook));
	g_check_next = reinterpret_cast<CheckFn>(hookpatch::RedirectCall(kCheckCall, kCheckHandler, &CheckHook));
	g_segment_next = reinterpret_cast<SegmentFn>(hookpatch::RedirectCall(kSegmentCall, kSegmentTime, &SegmentHook));
	if (!g_update_next || !g_mp_step_next || !g_check_next || !g_segment_next) {
		hooklog::Write("turn undo: part of the game's code is not as expected (frame %d, step %d, messages %d, segments %d): "
		               "off (the hooks that went in only pass on)",
		               g_update_next != nullptr, g_mp_step_next != nullptr, g_check_next != nullptr, g_segment_next != nullptr);
		g_disabled = true;
		return;
	}
	const overlay::Client client = {kName, &Wanted, &Prepare, &Draw};
	if (!overlay::AddClient(client))
		hooklog::Write("turn undo: no overlay in this game: no button (the requests of others still count)");
	else
		overlay::EnableModalInput();  // (its click zones: the game does not get the button's clicks)
	hooklog::Write("turn undo: ready (the host's [coop] undo_end_turn decides at each game's start; this PC's: %d)",
	               HostSetting() ? 1 : 0);
}

}  // namespace mods
