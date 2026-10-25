// Robot UI: press buttons on game screens. Two kinds of buttons:
//   step=14|ui|NUI::CLobbyScreen create   a command sent to the screen object
//   step=20|msg|create                    a game message (see PostGameMessage)
//
// Screens whose buttons send commands implement IProgrammedReactionsAndChecks.
// Its only slot is  bool React(?, const string *command, ?)  (__thiscall, ret 0Ch),
// the same shape as the main-menu pages' OnCommand (robot_menu.cpp). E.g. the LAN
// lobby (0x006f7a00) understands "join", "create", "profile", "changename", ...
// The robot:
//   1. looks the class up in the exe's RTTI (the run-time type information MSVC
//      stores for every class with virtual functions): its vftables, and where the
//      IProgrammedReactionsAndChecks part sits inside the object;
//   2. finds the live screen object by scanning the game's memory for that pair
//      of vftables (a destroyed object no longer has them: destructors reset them);
//   3. calls React with the command.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>

#include "game_build.h"
#include "log.h"
#include "robot.h"

namespace robot {
namespace {

// MSVC RTTI records (32-bit): vftable[-1] points to the complete object locator.
struct TypeDescriptor {
	DWORD vftable;
	DWORD spare;
	char name[1];  // mangled, e.g. ".?AVCLobbyScreen@NUI@@"
};
struct BaseClassDescriptor {
	const TypeDescriptor *type;
	DWORD contained_bases;
	int mdisp, pdisp, vdisp;  // where this base sits in the object
	DWORD attributes;
};
struct ClassHierarchyDescriptor {
	DWORD signature, attributes, base_count;
	const BaseClassDescriptor *const *bases;
};
struct CompleteObjectLocator {
	DWORD signature;
	DWORD offset;  // of the sub-object this vftable belongs to
	DWORD cd_offset;
	const TypeDescriptor *type;
	const ClassHierarchyDescriptor *hierarchy;
};

const char kReactionsName[] = ".?AUIProgrammedReactionsAndChecks@@";

struct Screen {
	char name[96];
	DWORD vftable;            // main vftable (sub-object at offset 0)
	DWORD check_offset;       // a second sub-object, to recognise live objects
	DWORD check_vftable;
	bool has_reactions;       // takes button commands (IProgrammedReactionsAndChecks)
	DWORD reactions_offset;   // of IProgrammedReactionsAndChecks in the object
	DWORD reactions_vftable;
};
Screen g_cache[16];
int g_cache_count = 0;
bool g_quiet = false;  // routine checks (AutoCloseScreens): log only what they do

const BYTE *g_image = nullptr;
DWORD g_image_size = 0;

bool Readable(const void *p, size_t n)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
		return false;
	return static_cast<const BYTE *>(p) + n <= static_cast<const BYTE *>(mbi.BaseAddress) + mbi.RegionSize;
}

// "NUI::CLobbyScreen" -> ".?AVCLobbyScreen@NUI@@" (class) or ".?AU...@@" (struct)
void Mangle(const char *name, char kind, char *out, size_t size)
{
	char parts[8][64];
	int n = 0;
	for (const char *p = name; *p && n < 8;) {
		const char *end = strstr(p, "::");
		const size_t len = end ? static_cast<size_t>(end - p) : strlen(p);
		lstrcpynA(parts[n++], p, static_cast<int>(len < 63 ? len + 1 : 64));
		p += len + (end ? 2 : 0);
	}
	_snprintf_s(out, size, _TRUNCATE, ".?A%c", kind);
	for (int i = n - 1; i >= 0; --i) {
		strncat_s(out, size, parts[i], _TRUNCATE);
		strncat_s(out, size, "@", _TRUNCATE);
	}
	strncat_s(out, size, "@", _TRUNCATE);
}

// Every dword-aligned address in the exe image holding `value`, passed to `found`
// until it returns true.
template <typename F>
bool ForEachImageDword(DWORD value, F found)
{
	for (DWORD off = 0; off + 4 <= g_image_size; off += 4) {
		if (*reinterpret_cast<const DWORD *>(g_image + off) == value && found(g_image + off))
			return true;
	}
	return false;
}

const TypeDescriptor *FindType(const char *mangled)
{
	const size_t len = strlen(mangled) + 1;
	for (DWORD off = 8; off + len <= g_image_size; off += 4) {
		if (memcmp(g_image + off, mangled, len) == 0)
			return reinterpret_cast<const TypeDescriptor *>(g_image + off - 8);
	}
	return nullptr;
}

// The vftable of `type`'s sub-object at `offset`, or 0.
DWORD FindVftable(const TypeDescriptor *type, DWORD offset, const CompleteObjectLocator **col_out)
{
	DWORD vftable = 0;
	ForEachImageDword(reinterpret_cast<DWORD>(type), [&](const BYTE *p) {
		const CompleteObjectLocator *col = reinterpret_cast<const CompleteObjectLocator *>(p - 12);
		if (reinterpret_cast<const BYTE *>(col) < g_image || col->signature != 0 || col->offset != offset)
			return false;
		// the vftable starts right after the dword that points to this locator
		return ForEachImageDword(reinterpret_cast<DWORD>(col), [&](const BYTE *q) {
			vftable = reinterpret_cast<DWORD>(q) + 4;
			if (col_out)
				*col_out = col;
			return true;
		});
	});
	return vftable;
}

void EnsureImage()
{
	if (!g_image) {
		g_image = reinterpret_cast<const BYTE *>(GetModuleHandleW(nullptr));
		const IMAGE_NT_HEADERS32 *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(
			g_image + reinterpret_cast<const IMAGE_DOS_HEADER *>(g_image)->e_lfanew);
		g_image_size = nt->OptionalHeader.SizeOfImage;
		// stop at the first unreadable page (the scans read the whole range)
		DWORD readable = 0;
		while (readable < g_image_size && Readable(g_image + readable, 0x1000))
			readable += 0x1000;
		g_image_size = readable;
	}
}

// "NUI::CLobbyScreen" (a class or a struct) -> its type descriptor, or nullptr.
// Templates must be given mangled, e.g. ".?AV?$CLoadScreen@USHeaderInfo@@@NUI@@".
const TypeDescriptor *FindClass(const char *name)
{
	EnsureImage();
	if (strncmp(name, ".?A", 3) == 0)
		return FindType(name);
	char mangled[128];
	for (char kind : {'V', 'U'}) {
		Mangle(name, kind, mangled, sizeof(mangled));
		if (const TypeDescriptor *type = FindType(mangled))
			return type;
	}
	return nullptr;
}

const Screen *Lookup(const char *name)
{
	for (int i = 0; i < g_cache_count; ++i) {
		if (strcmp(g_cache[i].name, name) == 0)
			return &g_cache[i];
	}
	const TypeDescriptor *type = FindClass(name);
	if (!type) {
		hooklog::Write("robot: ui: no class \"%s\" in the game's type information", name);
		return nullptr;
	}
	const CompleteObjectLocator *col = nullptr;
	Screen s = {};
	lstrcpynA(s.name, name, sizeof(s.name));
	s.vftable = FindVftable(type, 0, &col);
	if (!s.vftable) {
		hooklog::Write("robot: ui: class \"%s\" has no vftable", name);
		return nullptr;
	}
	// Live objects are recognised by two vftables: the main one, and the one of the
	// sub-object furthest inside (or the main one again, for single inheritance).
	for (DWORD i = 0; i < col->hierarchy->base_count; ++i) {
		const BaseClassDescriptor *b = col->hierarchy->bases[i];
		if (!s.has_reactions && strcmp(b->type->name, kReactionsName) == 0) {
			s.has_reactions = true;
			s.reactions_offset = static_cast<DWORD>(b->mdisp);
		}
		if (b->mdisp > 0 && static_cast<DWORD>(b->mdisp) > s.check_offset) {
			if (const DWORD vt = FindVftable(type, static_cast<DWORD>(b->mdisp), nullptr)) {
				s.check_offset = static_cast<DWORD>(b->mdisp);
				s.check_vftable = vt;
			}
		}
	}
	if (!s.check_vftable)
		s.check_vftable = s.vftable;
	if (s.has_reactions)
		s.reactions_vftable = s.reactions_offset ? FindVftable(type, s.reactions_offset, nullptr) : s.vftable;
	hooklog::Write("robot: ui: %s vftable %08lx, check +%lu %08lx, commands %s +%lu (vftable %08lx)", name, s.vftable,
	               s.check_offset, s.check_vftable, s.has_reactions ? "at" : "none", s.reactions_offset,
	               s.reactions_vftable);
	Screen &slot = g_cache[g_cache_count < 16 ? g_cache_count++ : 15];
	slot = s;
	return &slot;
}

}  // namespace

void *FindLiveObject(DWORD vftable, DWORD second_offset, DWORD second_vftable, int *count)
{
	void *found = nullptr;
	int n = 0;
	SIZE_T scanned = 0;
	const DWORD start = GetTickCount();
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	for (BYTE *p = static_cast<BYTE *>(si.lpMinimumApplicationAddress); p < si.lpMaximumApplicationAddress;) {
		MEMORY_BASIC_INFORMATION mbi;
		if (!VirtualQuery(p, &mbi, sizeof(mbi)))
			break;
		BYTE *next = static_cast<BYTE *>(mbi.BaseAddress) + mbi.RegionSize;
		// plain read-write memory only: no guard pages, and no uncached or
		// write-combined memory (graphics buffers), which is very slow to read
		if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE) {
			scanned += mbi.RegionSize;
			__try {
				DWORD *d = static_cast<DWORD *>(mbi.BaseAddress);
				const size_t words = mbi.RegionSize / 4;
				const size_t second = second_offset / 4;
				for (size_t i = 0; i + second < words; ++i) {
					if (d[i] == vftable && d[i + second] == second_vftable) {
						found = &d[i];
						++n;
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
			}
		}
		p = next;
	}
	if (!g_quiet)
		hooklog::Write("robot: memory scan for vftable %08lx: %lu MB in %lu ms, %d found", vftable,
		               static_cast<unsigned long>(scanned >> 20), GetTickCount() - start, n);
	if (count)
		*count = n;
	return found;
}

namespace {

// The class name of an object with virtual functions, from its RTTI ("?" if none).
const char *ClassName(const void *object)
{
	if (!object)
		return "?";
	__try {
		const DWORD *vftable = *static_cast<const DWORD *const *>(object);
		const CompleteObjectLocator *col = reinterpret_cast<const CompleteObjectLocator *>(vftable[-1]);
		if (col->signature != 0 || !Readable(col->type, 16))
			return "?";
		return col->type->name;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return "?";
	}
}

}  // namespace

// Research helper: "<screen class> <hex offset> <hex offset> ..." follows pointers
// from the live screen object (object = *(object + offset), in turn) and logs
// each object's class and its first vftable slots.
void Peek(const char *spec)
{
	char screen[96] = {};
	const char *p = strchr(spec, ' ');
	const size_t len = p ? static_cast<size_t>(p - spec) : strlen(spec);
	lstrcpynA(screen, spec, static_cast<int>(len < sizeof(screen) ? len + 1 : sizeof(screen)));
	BYTE *object = static_cast<BYTE *>(FindScreen(screen));
	for (int depth = 0; object; ++depth) {
		__try {
			const DWORD *vt = *reinterpret_cast<const DWORD *const *>(object);
			hooklog::Write("robot: peek %d: %p %s vftable %p: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx "
			               "%08lx %08lx %08lx %08lx %08lx %08lx %08lx",
			               depth, object, ClassName(object), vt, vt[0], vt[1], vt[2], vt[3], vt[4], vt[5], vt[6], vt[7],
			               vt[8], vt[9], vt[10], vt[11], vt[12], vt[13], vt[14], vt[15]);
			if (!p)
				break;
			char *end = nullptr;
			const unsigned long offset = strtoul(p + 1, &end, 16);
			if (end == p + 1)
				break;
			p = *end ? end : nullptr;
			object = *reinterpret_cast<BYTE **>(object + offset);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			hooklog::Write("robot: peek %d: %p unreadable", depth, object);
			break;
		}
	}
}

// Other screens (e.g. the multiplayer game setup and waiting room) listen for
// "game messages" instead. A button with an ARSendGameMessage reaction
// (0x00b29970) posts one with 0x00493ed0, __fastcall(const GameString *name,
// int param, int extra) ret 4; the message is queued and delivered to the
// screens listening for that name. The names come from the UI data, e.g.
// UI/MPCreate/Menu/Create.(ARSendGameMessage).xdb: EventName "create".
bool PostGameMessage(const char *name, int param)
{
	if (!IsHookable(DetectGameBuild()))
		return false;
	typedef void(__fastcall * PostFn)(const GameString *name, int param, int extra);
	const size_t n = strlen(name);
	GameString str = {name, name + n, name + n};
	__try {
		reinterpret_cast<PostFn>(0x00493ed0)(&str, param, 0);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("robot: game message \"%s\" crashed (caught)", name);
		return false;
	}
	hooklog::Write("robot: game message \"%s\" (%d) posted", name, param);
	return true;
}

namespace {

const Screen *SafeLookup(const char *screen)
{
	if (!IsHookable(DetectGameBuild()))
		return nullptr;
	__try {
		return Lookup(screen);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("robot: ui: reading the type information of \"%s\" crashed (caught)", screen);
		return nullptr;
	}
}

}  // namespace

const void *TypeDescriptorOf(const char *name)
{
	if (!IsHookable(DetectGameBuild()))
		return nullptr;
	__try {
		return FindClass(name);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

void *FindScreen(const char *screen)
{
	const Screen *s = SafeLookup(screen);
	if (!s)
		return nullptr;
	int count = 0;
	void *object = FindLiveObject(s->vftable, s->check_offset, s->check_vftable, &count);
	if (!object && !g_quiet)
		hooklog::Write("robot: ui: no %s on screen", screen);
	return object;
}

bool UiCommand(const char *screen, const char *command)
{
	const Screen *s = SafeLookup(screen);
	if (!s)
		return false;
	if (!s->has_reactions) {
		hooklog::Write("robot: ui: %s does not take button commands (no IProgrammedReactionsAndChecks)", screen);
		return false;
	}
	BYTE *object = static_cast<BYTE *>(FindScreen(screen));
	if (!object)
		return false;
	const size_t n = strlen(command);
	GameString str = {command, command + n, command + n};
	void *reactions = object + s->reactions_offset;
	typedef bool(__fastcall * ReactFn)(void *self, void *edx, void *a, GameString *command, void *c);
	const ReactFn react = reinterpret_cast<ReactFn>(*reinterpret_cast<DWORD *>(s->reactions_vftable));
	bool handled = false;
	__try {
		handled = react(reactions, nullptr, nullptr, &str, nullptr);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("robot: ui: %s \"%s\" crashed (caught)", screen, command);
		return false;
	}
	hooklog::Write("robot: ui: %s at %p \"%s\" -> %s", screen, object, command, handled ? "handled" : "not handled");
	return handled;
}

// The random map screen (NUI::CRMGScreen). The custom game screen's "Générer
// une carte" button posts the game message "rmg_screen" (UI/HotSeat/Menu/
// RMGScreen.(UISSendGameMessage).xdb), but its observer
// (CGMMemFunObserver<NUI::CHSCreate>, notify 0x0077b090) ignores a message
// without the sender the button gives, so "msg|rmg_screen" does nothing. This
// calls what the observer would: the handler 0x0077a6c0 (__thiscall, one
// argument it does not read, ret 4) on the live NUI::CHSCreate. The handler sets
// the object's flag +0x180 and calls 0x00796be0, which queues the creation of the
// random map screen (a NUI::CCreateRMGScreen command, vtable 0x00e40e30). Without
// a CHSCreate (the multiplayer create screen), only 0x00796be0 is called.
bool OpenRmgScreen()
{
	if (!IsHookable(DetectGameBuild()))
		return false;
	static const BYTE kHandler[] = {0xc6, 0x81, 0x80, 0x01, 0x00, 0x00, 0x01, 0xe8, 0x14, 0xc5, 0x01, 0x00, 0xc2, 0x04, 0x00};
	static const BYTE kCreate[] = {0x6a, 0x0c, 0xe8, 0xe9, 0x23, 0x0e, 0x00, 0x33, 0xc9};
	if (memcmp(reinterpret_cast<const void *>(0x0077a6c0), kHandler, sizeof(kHandler)) != 0 ||
	    memcmp(reinterpret_cast<const void *>(0x00796be0), kCreate, sizeof(kCreate)) != 0) {
		hooklog::Write("robot: rmg: the game's code is not as expected");
		return false;
	}
	void *create_screen = FindScreen("NUI::CHSCreate");
	typedef void(__fastcall * HandlerFn)(void *self, void *edx, void *message);
	typedef void(__cdecl * CreateFn)();
	__try {
		if (create_screen)
			reinterpret_cast<HandlerFn>(0x0077a6c0)(create_screen, nullptr, nullptr);
		else
			reinterpret_cast<CreateFn>(0x00796be0)();
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("robot: rmg: opening the random map screen crashed (caught)");
		return false;
	}
	hooklog::Write("robot: rmg: random map screen requested (%s)",
	               create_screen ? "through NUI::CHSCreate's rmg_screen handler" : "directly");
	return true;
}

// Windows that wait for a click a robot game cannot give. Called by the
// end-turn helper when no AI has moved for a while, i.e. when the game seems
// to wait for a human:
//   - the combat results: its OK runs the reaction "Close" (UI/CombatResults/Ok,
//     handled by 0x00581120);
//   - a battle (NUI::CCombatArenaScreen) where the human player's hero is
//     attacked: the tactics phase waits for "start combat" and then every
//     turn of the human's army waits for a move. The robot posts the messages
//     of the battle's keys (profiles/default_profile/input_a2.cfg, sections
//     combat_screen_tactic and combat_screen), one per check:
//     "input_start_combat" (Enter in the tactics phase), then
//     "input_Combat_AutoCombat" (A, a toggle, sent once), so the computer
//     fights for the human. ("autoplace_army", the tactics phase's button
//     message, only places the units.)
//   - the retreat box (NUI::CRetreatBox, a two-option NUI::CCommonBox built in
//     code at 0x00570cf0): its left button posts "left" (flee), the right one
//     "right" (surrender), OK posts "confirm" (UI/CommonBoxes/Box2p, Ok.(UISDirectRunReaction)).
//     The robot flees: "left", then "confirm".
// Other common boxes post "left" for OK and "right" for cancel
// (UI/CommonBoxes/*_ARSendGameMessage).
// Returns true while a battle or a box is being handled, so the caller does
// not end the turn meanwhile.
bool AutoCloseScreens()
{
	g_quiet = true;
	UiCommand("NUI::CCombatResults", "Close");  // logs only when it found the window
	bool busy = false;
	static void *last_battle = nullptr;
	static int battle_step = 0;
	void *battle = FindScreen("NUI::CCombatArenaScreen");
	if (battle) {
		busy = true;
		if (battle != last_battle) {
			last_battle = battle;
			battle_step = 0;
		}
		// one message per check: the fight screen only takes auto-combat once
		// the tactics phase is over
		if (battle_step == 0) {
			PostGameMessage("input_start_combat", 0);
			hooklog::Write("robot: battle screen at %p: start combat (end of the tactics phase)", battle);
		} else if (battle_step == 1) {
			PostGameMessage("input_Combat_AutoCombat", 0);
			hooklog::Write("robot: battle screen at %p: auto-combat on", battle);
		}
		++battle_step;
	}
	if (FindScreen("NUI::CRetreatBox")) {
		busy = true;
		PostGameMessage("left", 0);
		PostGameMessage("confirm", 0);
		hooklog::Write("robot: retreat box: chose flee and confirmed");
	}
	g_quiet = false;
	return busy;
}

}  // namespace robot
