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

const Screen *Lookup(const char *name)
{
	for (int i = 0; i < g_cache_count; ++i) {
		if (strcmp(g_cache[i].name, name) == 0)
			return &g_cache[i];
	}
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
	char mangled[128];
	const TypeDescriptor *type = nullptr;
	for (char kind : {'V', 'U'}) {
		Mangle(name, kind, mangled, sizeof(mangled));
		if ((type = FindType(mangled)) != nullptr)
			break;
	}
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

void *FindScreen(const char *screen)
{
	const Screen *s = SafeLookup(screen);
	if (!s)
		return nullptr;
	int count = 0;
	void *object = FindLiveObject(s->vftable, s->check_offset, s->check_vftable, &count);
	if (!object)
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

}  // namespace robot
