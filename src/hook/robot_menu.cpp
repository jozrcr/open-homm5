// Robot menus: press main-menu buttons by sending their command strings to the
// menu pages, the way a button click does.
//
// Every main-menu page implements NUI::IBaseSubmenu (5 slots):
//   slot 0  SetOwner(owner, ?)                  __thiscall, ret 8 (called when the menu is built)
//   slot 1  bool OnCommand(?, const string *cmd, ?)   __thiscall, ret 0Ch
//           e.g. CMultiPlayerSubmenu (0x00699730): "hot_seat", "lan", "ubi_com"
//                CSinglePlayerSubmenu (0x0069f850): "campaign", "user_campaign", "load_game", ...
// We replace slot 0 in each page's vftable with a pass-through that remembers the
// page object, then call slot 1 ourselves. The command is a GameString (robot.h);
// the pages only read it.
#include <windows.h>
#include <string.h>

#include "game_build.h"
#include "log.h"
#include "robot.h"

namespace robot {
namespace {

struct Page {
	const char *name;     // short name used in the robot script
	DWORD vftable;        // main vftable of the page class
	void *object;         // learned at run time
	DWORD original_slot0;
};

Page g_pages[] = {
	{"main", 0x00e31840, nullptr, 0},            // NUI::CMainMenuSubmenu
	{"single", 0x00e31880, nullptr, 0},          // NUI::CSinglePlayerSubmenu
	{"multi", 0x00e318b8, nullptr, 0},           // NUI::CMultiPlayerSubmenu
	{"hotseat", 0x00e31594, nullptr, 0},         // NUI::CHotSeatSubmenu
	{"mission", 0x00e31a7c, nullptr, 0},         // NUI::CSelectMissionSubmenu
	{"campaign", 0x00e31d14, nullptr, 0},        // NUI::CSelectCampaignSubmenu
	{"replay", 0x00e3164c, nullptr, 0},          // NUI::CLoadReplaySubmenu
};

typedef void(__fastcall *SetOwnerFn)(void *self, void *edx, void *a, void *b);
typedef bool(__fastcall *OnCommandFn)(void *self, void *edx, void *a, GameString *command, void *c);

Page *PageOf(void *object)
{
	const DWORD vftable = *static_cast<DWORD *>(object);
	for (Page &p : g_pages) {
		if (p.vftable == vftable)
			return &p;
	}
	return nullptr;
}

void __fastcall SetOwnerHook(void *self, void *edx, void *a, void *b)
{
	Page *p = PageOf(self);
	if (!p)
		return;  // cannot happen: only installed in these vftables
	if (p->object != self) {
		p->object = self;
		hooklog::Write("robot: menu page \"%s\" at %p", p->name, self);
	}
	reinterpret_cast<SetOwnerFn>(p->original_slot0)(self, edx, a, b);
}

}  // namespace

void InstallMenuHooks()
{
	static bool done = false;
	if (done || !IsHookable(DetectGameBuild()))
		return;
	done = true;
	int hooked = 0;
	for (Page &p : g_pages) {
		DWORD *slot = reinterpret_cast<DWORD *>(p.vftable);
		DWORD old;
		if (!VirtualProtect(slot, sizeof(DWORD), PAGE_READWRITE, &old))
			continue;
		p.original_slot0 = *slot;
		*slot = reinterpret_cast<DWORD>(&SetOwnerHook);
		VirtualProtect(slot, sizeof(DWORD), old, &old);
		++hooked;
	}
	hooklog::Write("robot: menu page hooks installed (%d pages)", hooked);
}

bool MenuCommand(const char *page, const char *command)
{
	for (Page &p : g_pages) {
		if (strcmp(p.name, page) != 0)
			continue;
		if (!p.object) {
			hooklog::Write("robot: menu page \"%s\" not seen yet", page);
			return false;
		}
		const size_t n = strlen(command);
		GameString s = {command, command + n, command + n};
		const DWORD on_command = reinterpret_cast<DWORD *>(p.vftable)[1];
		bool handled = false;
		__try {
			handled = reinterpret_cast<OnCommandFn>(on_command)(p.object, nullptr, nullptr, &s, nullptr);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			hooklog::Write("robot: menu command \"%s %s\" crashed (caught)", page, command);
			return false;
		}
		hooklog::Write("robot: menu command \"%s %s\" -> %s", page, command, handled ? "handled" : "not handled");
		return handled;
	}
	hooklog::Write("robot: unknown menu page \"%s\"", page);
	return false;
}

}  // namespace robot
