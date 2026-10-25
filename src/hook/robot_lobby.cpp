// Robot lobby helpers: choosing the map on the multiplayer "create a game" screen.
//
//   step=16|map|<part of the map file path, e.g. A1M4/>
//
// The screen (NUI::CMPCreateScreen) keeps its map list in a NUI::CMapsFormer at
// +0x17c. The map used by "create" is the row highlighted in the list widget
// (IScrollableContainer at former+0x34: slot 5 returns the highlighted row,
// slot 4 highlights one, slot 7 scrolls to it). The map entries are a vector of
// pointers at former+0x18..0x1c; each holds its list row at +0x90 and a filter
// mask at +0x94 (0x00704df0, 0x00705130), and the map file path at +0x3c (narrow
// string, e.g. "/Maps/Multiplayer/A1M4/map.xdb#xpointer(/AdvMapDesc)"; found by
// listing the text each entry points to, the displayed names are elsewhere).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "robot.h"

namespace robot {
namespace {

bool Readable(const void *p, size_t n)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (!p || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
	    (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
		return false;
	return static_cast<const BYTE *>(p) + n <= static_cast<const BYTE *>(mbi.BaseAddress) + mbi.RegionSize;
}

// Text at p (narrow or UTF-16, printable ASCII, at least 4 characters), or false.
bool TextAt(const void *p, char *out, size_t size)
{
	if (!Readable(p, 128))
		return false;
	const BYTE *b = static_cast<const BYTE *>(p);
	size_t n = 0;
	const bool wide = b[1] == 0 && b[0] >= 0x20 && b[0] < 0x7f;
	const size_t step = wide ? 2 : 1;
	for (size_t i = 0; i + step <= 128 && n + 1 < size; i += step) {
		const BYTE c = b[i];
		if (wide && b[i + 1] != 0)
			break;
		if (c < 0x20 || c >= 0x7f)
			break;
		out[n++] = static_cast<char>(c);
	}
	out[n] = 0;
	return n >= 4;
}

bool ContainsNoCase(const char *text, const char *part)
{
	const size_t n = strlen(part);
	for (; *text; ++text) {
		if (_strnicmp(text, part, n) == 0)
			return true;
	}
	return false;
}

typedef void *(__fastcall *GetSelectedFn)(void *container, void *edx);
typedef void(__fastcall *SelectFn)(void *container, void *edx, void *row);

}  // namespace

bool PickMap(const char *wanted)
{
	BYTE *screen = static_cast<BYTE *>(FindScreen("NUI::CMPCreateScreen"));
	if (!screen)
		return false;
	bool picked = false;
	__try {
		BYTE *former = *reinterpret_cast<BYTE **>(screen + 0x17c);
		void *container = *reinterpret_cast<void **>(former + 0x34);
		BYTE **begin = *reinterpret_cast<BYTE ***>(former + 0x18);
		BYTE **end = *reinterpret_cast<BYTE ***>(former + 0x1c);
		void **vt = *static_cast<void ***>(container);
		hooklog::Write("robot: map list: %d maps", static_cast<int>(end - begin));
		for (BYTE **it = begin; it != end; ++it) {
			BYTE *entry = *it;
			char path[128];
			if (!TextAt(*reinterpret_cast<void **>(entry + 0x3c), path, sizeof(path)))
				lstrcpynA(path, "?", sizeof(path));
			const bool match = !picked && wanted[0] && ContainsNoCase(path, wanted);
			hooklog::Write("robot: map %d%s: %s", static_cast<int>(it - begin), match ? " (picked)" : "", path);
			if (match) {
				void *row = *reinterpret_cast<void **>(entry + 0x90);
				reinterpret_cast<SelectFn>(vt[4])(container, nullptr, row);
				reinterpret_cast<SelectFn>(vt[7])(container, nullptr, row);
				picked = reinterpret_cast<GetSelectedFn>(vt[5])(container, nullptr) == row;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("robot: map list crashed (caught)");
		return false;
	}
	if (picked)
		PostGameMessage("mapselected", 0);  // let the screen show the map's description
	else
		hooklog::Write("robot: no map matching \"%s\"", wanted);
	return picked;
}

// "off:text | off:text ..." for every pointer in the first `size` bytes of an
// object that leads to text (for finding names in structures we don't know).
void DescribeStrings(const BYTE *object, size_t size, char *out, size_t out_size)
{
	out[0] = 0;
	for (size_t off = 0; off + 4 <= size; off += 4) {
		char text[96];
		if (TextAt(*reinterpret_cast<void *const *>(object + off), text, sizeof(text))) {
			char item[128];
			_snprintf_s(item, sizeof(item), _TRUNCATE, "%s%02x:%s", out[0] ? " | " : "", static_cast<unsigned>(off),
			            text);
			strncat_s(out, out_size, item, _TRUNCATE);
		}
	}
}

// The single-player load screen (NUI::CLoadScreen<SHeaderInfo>, opened by the
// main menu's "single load_game"; UI/SaveLoad): list widget at +0x17c
// (IScrollableContainer: slot 4 highlight, slot 5 highlighted row, slot 7 scroll
// to), save entries at +0x18c..+0x190 (each with its row at +0xa8). The "click"
// message makes the highlighted row the selected save (0x00800a40), "load" loads
// it (0x00809840; from the main menu without asking).
bool PickSave(const char *wanted)
{
	BYTE *screen = static_cast<BYTE *>(FindScreen(".?AV?$CLoadScreen@USHeaderInfo@@@NUI@@"));
	if (!screen)
		return false;
	bool picked = false;
	__try {
		void *container = *reinterpret_cast<void **>(screen + 0x17c);
		BYTE **begin = *reinterpret_cast<BYTE ***>(screen + 0x18c);
		BYTE **end = *reinterpret_cast<BYTE ***>(screen + 0x190);
		void **vt = *static_cast<void ***>(container);
		hooklog::Write("robot: save list: %d saves", static_cast<int>(end - begin));
		for (BYTE **it = begin; it != end; ++it) {
			char text[1024];
			DescribeStrings(*it, 0xa8, text, sizeof(text));
			const bool match = !picked && wanted[0] && ContainsNoCase(text, wanted);
			hooklog::Write("robot: save %d%s: %s", static_cast<int>(it - begin), match ? " (picked)" : "", text);
			if (match) {
				void *row = *reinterpret_cast<void **>(*it + 0xa8);
				reinterpret_cast<SelectFn>(vt[4])(container, nullptr, row);
				reinterpret_cast<SelectFn>(vt[7])(container, nullptr, row);
				picked = reinterpret_cast<GetSelectedFn>(vt[5])(container, nullptr) == row;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("robot: save list crashed (caught)");
		return false;
	}
	if (picked)
		PostGameMessage("click", 0);  // the screen takes the highlighted row as its selection
	else
		hooklog::Write("robot: no save matching \"%s\"", wanted);
	return picked;
}

// Checkboxes of the multiplayer setup screen's options panel
// (NUI::CMPCreateOptions, filled in by 0x0070862c): IButton pointers at
// +0x18 ghostmode, +0x1c quick_combat, +0x20 realtime, +0x3c simultaneus.
// IButton (a window interface): slot 2 next state (a click), slot 3
// SetState(int), slot 5 state. When the game is created (0x00708f10) each
// option is on when its state is 0; "realtime" becomes the game option
// "autosave_enabled" (the lobby's autosave).
bool SetCreateOption(const char *spec)
{
	struct Option {
		const char *name;
		DWORD offset;
	};
	const Option options[] = {{"ghostmode", 0x18}, {"quick_combat", 0x1c}, {"realtime", 0x20}, {"simultaneus", 0x3c}};
	const char *space = strchr(spec, ' ');
	const int state = space ? atoi(space + 1) : 0;
	const Option *option = nullptr;
	for (const Option &o : options) {
		if (space && strncmp(spec, o.name, static_cast<size_t>(space - spec)) == 0 && !o.name[space - spec])
			option = &o;
	}
	if (!option) {
		hooklog::Write("robot: unknown option \"%s\" (ghostmode, quick_combat, realtime, simultaneus)", spec);
		return false;
	}
	BYTE *panel = static_cast<BYTE *>(FindScreen("NUI::CMPCreateOptions"));
	if (!panel)
		return false;
	typedef void(__fastcall *SetStateFn)(void *self, void *edx, int state);
	typedef int(__fastcall *GetStateFn)(void *self, void *edx);
	__try {
		void *button = *reinterpret_cast<void **>(panel + option->offset);
		void **vt = *static_cast<void ***>(button);
		const int before = reinterpret_cast<GetStateFn>(vt[5])(button, nullptr);
		reinterpret_cast<SetStateFn>(vt[3])(button, nullptr, state);
		const int after = reinterpret_cast<GetStateFn>(vt[5])(button, nullptr);
		hooklog::Write("robot: option %s: state %d -> %d", option->name, before, after);
		return after == state;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("robot: setting option %s crashed (caught)", option->name);
		return false;
	}
}

// Sliders of the same options panel (ISlider pointers): +0x24 timelimitscroll,
// +0x28 difficultyscroll, +0x34 animationspeedscroll, +0x40
// combatturnspeedscroll. ISlider slot 10 SetPosition(step), slot 11 position
// (both forwarded to the CWindowSlider inside, 0x0048f370 / 0x0048f2e0). The
// game reads animation speed = position + 1, 1 to 4 (0x00708f10).
bool SetCreateSlider(const char *spec)
{
	struct Slider {
		const char *name;
		DWORD offset;
	};
	const Slider sliders[] = {{"timelimit", 0x24}, {"difficulty", 0x28}, {"animationspeed", 0x34}, {"combatturnspeed", 0x40}};
	const char *space = strchr(spec, ' ');
	const Slider *slider = nullptr;
	for (const Slider &s : sliders) {
		if (space && strncmp(spec, s.name, static_cast<size_t>(space - spec)) == 0 && !s.name[space - spec])
			slider = &s;
	}
	if (!slider) {
		hooklog::Write("robot: unknown slider \"%s\" (timelimit, difficulty, animationspeed, combatturnspeed)", spec);
		return false;
	}
	const int position = atoi(space + 1);
	BYTE *panel = static_cast<BYTE *>(FindScreen("NUI::CMPCreateOptions"));
	if (!panel)
		return false;
	typedef void(__fastcall *SetFn)(void *self, void *edx, int position);
	typedef int(__fastcall *GetFn)(void *self, void *edx);
	__try {
		void *s = *reinterpret_cast<void **>(panel + slider->offset);
		void **vt = *static_cast<void ***>(s);
		const int before = reinterpret_cast<GetFn>(vt[11])(s, nullptr);
		reinterpret_cast<SetFn>(vt[10])(s, nullptr, position);
		const int after = reinterpret_cast<GetFn>(vt[11])(s, nullptr);
		hooklog::Write("robot: slider %s: position %d -> %d", slider->name, before, after);
		return after == position;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("robot: setting slider %s crashed (caught)", slider->name);
		return false;
	}
}

// The LAN lobby (NUI::CLobbyScreen) shows the games it has heard of in a
// NUI::CGamesView2 at +0x1dc, whose list widget (CWindowListCtrl, through its
// IListControl part, vftable 0x00e94870) is at view+8. "join" uses the
// highlighted row (0x006fb9a0). IListControl, as used here:
//   +0x20     the rows: a circular list; node[0] = next, node[2] = the row (IWindow)
//   slot 9    the highlighted row (IListControlItem), or 0
//   slot 10   highlight the row whose data is `data`, and scroll to it (0x00cba6a0)
// A row's data (IListControlItem slot 1) is the game's NUI::CGameData.
bool PickGame(int index)
{
	BYTE *lobby = static_cast<BYTE *>(FindScreen("NUI::CLobbyScreen"));
	const void *window_type = TypeDescriptorOf("IWindow");
	const void *item_type = TypeDescriptorOf("IListControlItem");
	if (!lobby || !window_type || !item_type) {
		hooklog::Write("robot: games list: lobby %p, IWindow %p, IListControlItem %p", lobby, window_type, item_type);
		return false;
	}
	typedef void *(__cdecl *DynamicCastFn)(void *object, LONG vf_delta, const void *from, const void *to, BOOL is_ref);
	typedef void *(__fastcall *GetFn)(void *self, void *edx);
	typedef void(__fastcall *SelectFn)(void *self, void *edx, void *data);
	const DynamicCastFn cast = reinterpret_cast<DynamicCastFn>(0x00d119fc);  // the game's __RTDynamicCast
	bool picked = false;
	int count = 0;
	__try {
		BYTE *view = *reinterpret_cast<BYTE **>(lobby + 0x1dc);
		BYTE *list = *reinterpret_cast<BYTE **>(view + 8);
		void **vt = *reinterpret_cast<void ***>(list);
		const DWORD *head = *reinterpret_cast<const DWORD **>(list + 0x20);
		for (const DWORD *node = reinterpret_cast<const DWORD *>(head[0]); node != head && count < 1000;
		     node = reinterpret_cast<const DWORD *>(node[0]), ++count) {
			void *item = cast(reinterpret_cast<void *>(node[2]), 0, window_type, item_type, FALSE);
			void *data = item ? reinterpret_cast<GetFn>((*static_cast<void ***>(item))[1])(item, nullptr) : nullptr;
			hooklog::Write("robot: game %d: row %08lx, data %p", count, node[2], data);
			if (count == index && data) {
				reinterpret_cast<SelectFn>(vt[10])(list, nullptr, data);
				picked = reinterpret_cast<GetFn>(vt[9])(list, nullptr) != nullptr;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("robot: games list crashed (caught)");
		return false;
	}
	hooklog::Write("robot: games list: %d games, game %d %s", count, index, picked ? "highlighted" : "NOT highlighted");
	return picked;
}

// Research: why the host's "Start game" stays disabled. The waiting room
// (NUI::CMPWaitScreen) asks its logic object (at +0x1b4, e.g.
// CMPUbiComWaitHostLogic) slot 6 "can start?" (0x0071d590), which needs
//   [logic+0x74] slot 19 () true                      (game info ready?)
//   no row in state 0 or 4 (human not ready,          (rows: logic+0x38..0x3c,
//     open slot)                                       state = *row->slot14())
//   at least 2 rows in state 3 (human ready) whose info
//     ([logic+0x74] slot 8 (index)) +0x24 != -1
// so a LAN game needs two humans, both ready.
// This logs all of that.
void LogWaitSlots()
{
	BYTE *screen = static_cast<BYTE *>(FindScreen("NUI::CMPWaitScreen"));
	if (!screen)
		return;
	typedef bool(__fastcall *BoolFn)(void *self, void *edx);
	typedef DWORD *(__fastcall *PtrFn)(void *self, void *edx);
	typedef BYTE *(__fastcall *InfoFn)(void *self, void *edx, int index);
	__try {
		BYTE *logic = *reinterpret_cast<BYTE **>(screen + 0x1b4);
		void *info = *reinterpret_cast<void **>(logic + 0x74);
		void **info_vt = *static_cast<void ***>(info);
		void **logic_vt = *reinterpret_cast<void ***>(logic);
		const bool can_start = reinterpret_cast<BoolFn>(logic_vt[6])(logic, nullptr);
		const bool info_ok = reinterpret_cast<BoolFn>(info_vt[19])(info, nullptr);
		void **begin = *reinterpret_cast<void ***>(logic + 0x38);
		void **end = *reinterpret_cast<void ***>(logic + 0x3c);
		char line[512];
		_snprintf_s(line, sizeof(line), _TRUNCATE, "robot: waiting room: can start %d, game info ready %d, slots:",
		            can_start, info_ok);
		for (void **it = begin; it != end && it - begin < 16; ++it) {
			void **slot_vt = *static_cast<void ***>(*it);
			// The row's own copy (slot 14, also on clients): [0] state, [1] colour, [0xf] team.
			const DWORD *row_state = reinterpret_cast<PtrFn>(slot_vt[14])(*it, nullptr);
			const BYTE *slot_info = reinterpret_cast<InfoFn>(info_vt[8])(info, nullptr, static_cast<int>(it - begin));
			char item[80];
			_snprintf_s(item, sizeof(item), _TRUNCATE, " [state %lu, colour %lu, team %lu, +24 %ld]", row_state[0],
			            row_state[1], row_state[0xf], slot_info ? *reinterpret_cast<const LONG *>(slot_info + 0x24) : -99);
			strncat_s(line, sizeof(line), item, _TRUNCATE);
		}
		hooklog::Write("%s", line);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("robot: waiting room: reading the slots crashed (caught)");
	}
}

// Press a player row's arrow in the waiting room: "<row> left|right", or
// "<row> team left|right" for the team arrows.
// The arrows (UI/MPWait/PlayersList/Item/OpenCloseLeft|Right, TeamColorLeft|Right)
// only record a pending change on the row (CMPWaitItem, 0x00713820 / 0x00713840,
// team: 0x00713860 / 0x00713880):
//   +0xc4 = 1 (change the state) or 2 (the team), +0xc8 = 0 left / 1 right
// on the full row object; the logic's list holds a pointer to its part at +8.
// Row states (+0x28 of that part, seen in the robot's screenshots): 0 human not
// ready, 1 computer, 2 closed, 3 human ready, 4 open slot. The right arrow on an
// empty slot cycles open -> computer -> closed -> open; on your own row it
// toggles ready.
bool PressSlotArrow(const char *spec)
{
	const int row = atoi(spec);
	const bool right = strstr(spec, "right") != nullptr;
	const bool team = strstr(spec, "team") != nullptr;
	BYTE *screen = static_cast<BYTE *>(FindScreen("NUI::CMPWaitScreen"));
	if (!screen)
		return false;
	__try {
		BYTE *logic = *reinterpret_cast<BYTE **>(screen + 0x1b4);
		BYTE **begin = *reinterpret_cast<BYTE ***>(logic + 0x38);
		BYTE **end = *reinterpret_cast<BYTE ***>(logic + 0x3c);
		if (row < 0 || row >= end - begin) {
			hooklog::Write("robot: waiting room: no row %d", row);
			return false;
		}
		BYTE *full = begin[row] - 8;
		*reinterpret_cast<DWORD *>(full + 0xc4) = team ? 2 : 1;
		*(full + 0xc8) = right ? 1 : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("robot: waiting room: pressing an arrow crashed (caught)");
		return false;
	}
	hooklog::Write("robot: waiting room: row %d %s arrow %s pressed", row, team ? "team" : "state", right ? "right" : "left");
	return true;
}

}  // namespace robot
