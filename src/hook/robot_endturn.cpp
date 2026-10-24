// Robot "human player": ends the passive human player's turn, the way the
// adventure screen's end-turn button does.
//
// NUI::CAdventureController::EndTurn is 0x00604e80 (__thiscall, no arguments,
// plain ret; called by the adventure screen UI at 0x005a5ab0 / 0x0058fa20). It
// builds an NWorld::CEndOfTurn command (constructor 0x00603160) and sends it.
// To call it we need the controller object: we learn its address by replacing
// slot 0 of its vftable (0x00e29a2c, "get local player": mov eax,[ecx+18h]; ret)
// with a pass-through that remembers `this`. Data patch only, no code patching.
//
// EndTurn itself only checks that a local player exists, so the robot calls it
// only when no AI has taken a step for a while: AI turns step every frame, the
// human's turn is the only quiet period.
#include <windows.h>

#include "game_build.h"
#include "log.h"
#include "robot.h"

namespace robot {
namespace {

const DWORD kControllerVftable = 0x00e29a2c;  // NUI::CAdventureController, main vftable
const DWORD kControllerSlot0 = 0x00819280;    // its slot 0 (shared, identical-code-folded)
const DWORD kEndTurn = 0x00604e80;

typedef void *(__fastcall *Slot0Fn)(void *self, void *edx);
typedef void(__fastcall *EndTurnFn)(void *self, void *edx);

void *volatile g_controller = nullptr;
bool g_installed = false;
volatile DWORD g_last_ai_step = 0;
DWORD g_last_end_turn = 0;
long g_end_turns = 0;
volatile long g_end_turns_since_ai = 0;  // end-turn attempts with no AI step since
const long kStuckAfter = 4;  // that many in a row: a battle or a box waits for the human
const long kStuckInBattleAfter = 36;  // checks (every few seconds) a battle may take

void *__fastcall ControllerSlot0Hook(void *self, void *edx)
{
	if (g_controller != self) {
		g_controller = self;
		hooklog::Write("robot: adventure controller at %p", self);
	}
	return reinterpret_cast<Slot0Fn>(kControllerSlot0)(self, edx);
}

bool CallEndTurn(void *controller)
{
	__try {
		reinterpret_cast<EndTurnFn>(kEndTurn)(controller, nullptr);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

}  // namespace

void OnAiStep()
{
	g_last_ai_step = GetTickCount();
	g_end_turns_since_ai = 0;
	PathTraceStep();
}

void InstallEndTurnHelper()
{
	if (g_installed || !IsHookable(DetectGameBuild()))
		return;
	g_installed = true;
	DWORD *slot = reinterpret_cast<DWORD *>(kControllerVftable);
	if (*slot != kControllerSlot0) {
		hooklog::Write("robot: controller vftable slot 0 is not what we expect: end-turn helper disabled");
		return;
	}
	DWORD old;
	if (VirtualProtect(slot, sizeof(DWORD), PAGE_READWRITE, &old)) {
		*slot = reinterpret_cast<DWORD>(&ControllerSlot0Hook);
		VirtualProtect(slot, sizeof(DWORD), old, &old);
		hooklog::Write("robot: end-turn helper ready (controller vftable %08lx)", kControllerVftable);
	}
}

void MaybeEndHumanTurn(DWORD every_ms, DWORD ai_idle_ms)
{
	void *controller = g_controller;
	if (!controller || !every_ms)
		return;
	const DWORD now = GetTickCount();
	if (now - g_last_end_turn < every_ms)
		return;
	if (g_last_ai_step && now - g_last_ai_step < ai_idle_ms)
		return;  // an AI is (or just was) playing: not the human's turn
	g_last_end_turn = now;
	// e.g. a battle's results window waiting for "OK", or a battle against the
	// human player's hero: not the time to end the turn
	static long battle_polls = 0;
	if (AutoCloseScreens()) {
		if (++battle_polls > kStuckInBattleAfter) {
			hooklog::Write("robot: stuck: a battle or a box has waited for %ld checks, closing the game", battle_polls);
			hooklog::Close();
			TerminateProcess(GetCurrentProcess(), 4);  // no game shutdown code: it can crash mid-turn
		}
		return;
	}
	battle_polls = 0;
	if (++g_end_turns_since_ai > kStuckAfter) {
		// The human's turn does not end and no AI plays: a battle (tactics
		// phase) or a box (the retreat box) waits for the robot's human player,
		// which cannot answer it. Waiting for the time limit would only waste
		// time, so the run ends here (exit code 4 = stuck).
		hooklog::Write("robot: stuck: %ld turn ends without an AI step (a battle or a box waits for the human player), "
		               "closing the game",
		               kStuckAfter);
		hooklog::Close();
		TerminateProcess(GetCurrentProcess(), 4);  // no game shutdown code: it can crash mid-turn
	}
	++g_end_turns;
	const bool ok = CallEndTurn(controller);
	hooklog::Write("robot: ending the human player's turn (#%ld)%s", g_end_turns, ok ? "" : " FAILED (crash caught)");
}

}  // namespace robot
