// The player list overlay: reading the game (docs/subsystems/players.md).
// Read only: nothing here writes to the game's memory or calls game code.
//
// Where things are (vanilla 3.1 exe, the same in MMH55_Utility):
//   the world (NWorld::CWorld, vtable 0x00e6260c): we note it when the game
//     asks it for the day (vtable slot 0xa8, 0x00c61ab0 "lea eax,[ecx+0x48]";
//     the adventure screen does every frame);
//     +0x24/+0x28  the players (vector of NWorld::IPlayer *), in turn order;
//     +0x48        the day, 0 on the first day;
//     +0x90        simultaneous turns on (byte; cleared when they break up).
//   a player (NWorld::IPlayer, the CPlayer subobject at +0x1c; vtable 0x00e1a09c):
//     +0x18/+0x1c  its heroes (vector of NWorld::IHero *);
//     +0x74        0 for a human (slot 0x184 "is human"), else the AI kind;
//     +0x78        the player number 1..8 (slot 0x2c);
//     +0x7c        1 while the player is in the game (slot 0x6c);
//     +0x140       its colour (slot 0x30, from the lobby slot +8);
//     +0x178       pressed End Turn (byte, slot 0x4c; set by CWorld slot 0xd0,
//                  the CEndOfTurn command; cleared when the next turn starts);
//     +0x1cc       its team (slot 0x14, from the lobby slot +0x1c; map scripts
//                  may change it: slot 0x18, CSetPlayerTeamCmd);
//     +0x1d0       its selected hero (slot 0xc; +0x1d4 is its selected town);
//     +0x1fc       its name (wide string, slot 0x38, from the lobby slot +0xc);
//     +0x634       0 while it is the player's turn (slot 0x44, set for every
//                  player by the world's "give turn to next players" 0x00998060).
//   a hero (NWorld::IHero, the CHero subobject at +0x1c; vtable 0x00e7e484):
//     +0x6c/+0x70  its name (wide string, slot 0x268).
//   the screens: a list at [0x00fe719c] (read by 0x008385a0), the last entry
//     (+8) is the screen on top; its RTTI class tells whether it is the
//     adventure map (NUI::CAdventureScreen).
// Every object is checked by its vtable before it is read.
#include "player_list_internal.h"

#include <stdio.h>
#include <string.h>

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace plist {
namespace {

const DWORD kWorldVftable = 0x00e6260c;  // NWorld::CWorld
const DWORD kWorldDaySlot = kWorldVftable + 0xa8;
const DWORD kWorldDay = 0x00c61ab0;  // lea eax, [ecx+0x48]; ret
const BYTE kWorldDayBytes[] = {0x8d, 0x41, 0x48, 0xc3};
const DWORD kPlayerVftable = 0x00e1a09c;  // NWorld::CPlayer's IPlayer
const DWORD kHeroVftable = 0x00e7e484;    // NWorld::CHero's IHero
const DWORD kTopScreenGetter = 0x008385a0;  // reads the screen list at [0x00fe719c]
const BYTE kTopScreenGetterBytes[] = {0x51, 0x33, 0xd2, 0x89, 0x14, 0x24, 0xa1, 0x9c, 0x71, 0xfe, 0x00};
const DWORD kScreenList = 0x00fe719c;
// The getters whose offsets we read (checked at install, so a different build
// turns the overlay off instead of showing nonsense).
struct Getter {
	DWORD slot;  // vtable cell
	DWORD function;
	BYTE bytes[8];
	int size;
};
const Getter kGetters[] = {
	{kPlayerVftable + 0x2c, 0x00720e90, {0x8b, 0x41, 0x78, 0xc3}, 4},                    // id: mov eax,[ecx+78h]
	{kPlayerVftable + 0x6c, 0x00c61a70, {0x8b, 0x41, 0x7c, 0xc3}, 4},                    // in game: [ecx+7Ch]
	{kPlayerVftable + 0x30, 0x00af8160, {0x8b, 0x81, 0x40, 0x01, 0x00, 0x00, 0xc3}, 7},  // colour: [ecx+140h]
	{kPlayerVftable + 0x14, 0x00500bc0, {0x8b, 0x81, 0xcc, 0x01, 0x00, 0x00, 0xc3}, 7},  // team: [ecx+1CCh]
	{kPlayerVftable + 0x4c, 0x00500c40, {0x8a, 0x81, 0x78, 0x01, 0x00, 0x00, 0xc3}, 7},  // ended: [ecx+178h]
	{kPlayerVftable + 0x44, 0x00500c10, {0x8b, 0x81, 0x34, 0x06, 0x00, 0x00, 0xc3}, 7},  // turn: [ecx+634h]
	{kPlayerVftable + 0x184, 0x00500d60, {0x33, 0xc0, 0x39, 0x41, 0x74, 0x0f, 0x94, 0xc0}, 8},  // human
	{kPlayerVftable + 0x0c, 0x004fcc40, {0x8b, 0x81, 0xd0, 0x01, 0x00, 0x00, 0x85, 0xc0}, 8},  // selected hero
	{kPlayerVftable + 0x38, 0x00500c00, {0x8d, 0x81, 0xfc, 0x01, 0x00, 0x00, 0xc3}, 7},  // name: lea eax,[ecx+1FCh]
	{kHeroVftable + 0x268, 0x00b4d4a0, {0x56, 0x8b, 0x74, 0x24, 0x08, 0x33, 0xc0}, 7},  // hero name (and below)
};
// In the hero name getter: mov eax,[ecx+70h]; mov ecx,[ecx+6Ch] (the string's end and begin).
const DWORD kHeroNameRead = 0x00b4d4af;
const BYTE kHeroNameReadBytes[] = {0x8b, 0x41, 0x70, 0x8b, 0x49, 0x6c};
// The portrait (only for the picture; the rest works without):
//   IHero slot 0x34 (0x00b5ae40) reads the hero's game data at +0xd0;
//   NDb::SAdvMapHeroShared (vtable 0x00e6d450) reads FaceTexture at +0x12c (its
//   serializer, slot 7 = 0x00a0c3b0, at 0x00a0c4a2: lea eax,[edi+12Ch] ... push "FaceTexture");
//   NDb::STexture (vtable 0x00e90318) reads DestName at +0x40 (serializer
//   0x00c7a680, at 0x00c7a68c: push "DestName" ... lea eax,[edi+40h]).
const DWORD kHeroSharedVftable = 0x00e6d450;
const DWORD kTextureVftable = 0x00e90318;
struct Bytes {
	DWORD address;
	BYTE bytes[16];
	int size;
};
const Bytes kFaceCode[] = {
	{kHeroVftable + 0x34, {0x40, 0xae, 0xb5, 0x00}, 4},  // the vtable cell: 0x00b5ae40
	{0x00b5ae40, {0x56, 0x8d, 0xb1, 0xd0, 0x00, 0x00, 0x00}, 7},
	{kHeroSharedVftable + 0x1c, {0xb0, 0xc3, 0xa0, 0x00}, 4},  // 0x00a0c3b0
	{0x00a0c4a2, {0x8d, 0x87, 0x2c, 0x01, 0x00, 0x00, 0x50, 0x50, 0x6a, 0x00, 0x68, 0x88, 0xd9, 0xe6, 0x00}, 15},
	{kTextureVftable + 0x1c, {0x80, 0xa6, 0xc7, 0x00}, 4},  // 0x00c7a680
	{0x00c7a68c, {0x68, 0x20, 0x9c, 0xe8, 0x00}, 5},
	{0x00c7a69c, {0x8d, 0x47, 0x40}, 3},
};

int g_trace = 0;
bool g_ok = false;
bool g_faces_ok = false;

typedef const void *(__fastcall *WorldDayFn)(const void *world, void *edx);
WorldDayFn g_world_day_next = nullptr;
const void *volatile g_world = nullptr;
volatile DWORD g_world_seen = 0;  // GetTickCount() when the world was last seen

const void *__fastcall WorldDayHook(const void *world, void *edx)
{
	g_world = world;
	g_world_seen = GetTickCount();
	return g_world_day_next(world, edx);
}

DWORD Read(const void *base, DWORD offset)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(base) + offset);
}

BYTE ReadByte(const void *base, DWORD offset)
{
	return *(static_cast<const BYTE *>(base) + offset);
}

const void *Ptr(const void *base, DWORD offset)
{
	return reinterpret_cast<const void *>(Read(base, offset));
}

// The game's own "is this object being destroyed" check (as in the getter
// 0x004fcc40): the virtual base's flags, bit 31 (+0xb & 0x80).
bool Dying(const void *object)
{
	const DWORD vbtable = Read(object, 4);
	const DWORD base = Read(reinterpret_cast<const void *>(vbtable), 4);
	return (ReadByte(object, base + 0xb) & 0x80) != 0;
}

// The class name from the object's RTTI (".?AVCAdventureScreen@NUI@@"), or "".
// The exe has no ASLR: its RTTI data lies between 0x00400000 and 0x01200000.
const char *ClassName(const void *object)
{
	__try {
		const DWORD vtable = Read(object, 0);
		if (vtable < 0x00401000 || vtable > 0x01200000)
			return "";
		const DWORD locator = Read(reinterpret_cast<const void *>(vtable), static_cast<DWORD>(-4));
		if (locator < 0x00401000 || locator > 0x01200000)
			return "";
		const DWORD type = Read(reinterpret_cast<const void *>(locator), 12);
		if (type < 0x00401000 || type > 0x01200000)
			return "";
		return reinterpret_cast<const char *>(type + 8);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return "";
	}
}

// A game wide string (begin, end, capacity) as UTF-8.
void WideToUtf8(const void *string, char *out, int size)
{
	out[0] = 0;
	const wchar_t *begin = reinterpret_cast<const wchar_t *>(Read(string, 0));
	const wchar_t *end = reinterpret_cast<const wchar_t *>(Read(string, 4));
	if (!begin || end <= begin || end - begin > 60)
		return;
	const int n = WideCharToMultiByte(CP_UTF8, 0, begin, static_cast<int>(end - begin), out, size - 1, nullptr, nullptr);
	out[n > 0 ? n : 0] = 0;
}

// The .dds file of a hero's portrait: hero +0xd0 (its game data, a CDBPtr
// whose first word is the object) -> FaceTexture (+0x12c, the same) ->
// DestName (+0x40, a narrow string: the file in the game's data).
void FacePath(const void *hero, char *out, int size)
{
	out[0] = 0;
	if (!g_faces_ok)
		return;
	const void *shared = Ptr(hero, 0xd0);
	if (!shared || Read(shared, 0) != kHeroSharedVftable)
		return;
	const void *texture = Ptr(shared, 0x12c);
	if (!texture || Read(texture, 0) != kTextureVftable)
		return;
	const char *begin = reinterpret_cast<const char *>(Read(texture, 0x40));
	const char *end = reinterpret_cast<const char *>(Read(texture, 0x44));
	if (!begin || end <= begin || end - begin >= size || end - begin < 5 || _strnicmp(end - 4, ".dds", 4) != 0)
		return;
	memcpy(out, begin, end - begin);
	out[end - begin] = 0;
}

// The screen on top: the adventure map?
bool OnAdventureMap(char *class_name, int size)
{
	class_name[0] = 0;
	const DWORD *sentinel = *reinterpret_cast<const DWORD *const *>(kScreenList);
	if (!sentinel || sentinel[0] == reinterpret_cast<DWORD>(sentinel))
		return false;
	const void *screen = Ptr(reinterpret_cast<const void *>(sentinel[1]), 8);
	if (!screen)
		return false;
	lstrcpynA(class_name, ClassName(screen), size);
	return strcmp(class_name, ".?AVCAdventureScreen@NUI@@") == 0;
}

// --- per-day bookkeeping: who has had the turn today ----------------------------
int g_day = -1;
bool g_had_turn[kMaxPlayers + 1];  // by player number: active at some point today
bool g_done[kMaxPlayers + 1];      // by player number: had the turn, and it is over

void NewDay(int day)
{
	if (day == g_day)
		return;
	g_day = day;
	memset(g_had_turn, 0, sizeof(g_had_turn));
	memset(g_done, 0, sizeof(g_done));
}

// --- the trace (research and checks) ------------------------------------------------
char g_last_line[kMaxPlayers][256];
char g_last_screen[128];

void Dump(const char *what, const void *base, int from, int bytes)
{
	char line[200];
	for (int off = from; off < from + bytes; off += 32) {
		int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "players: %s %+05x:", what, off);
		for (int i = 0; i < 32 && n > 0 && n < static_cast<int>(sizeof(line)) - 10; i += 4)
			n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " %08lx",
			                 Read(base, static_cast<DWORD>(off + i)));
		hooklog::Write("%s", line);
	}
}

// Research (trace_players=2): where a hero's game data (its portrait) is.
// A printable narrow string at p (8+ characters), or nullptr.
const char *Text(const void *p)
{
	__try {
		const char *s = static_cast<const char *>(p);
		int n = 0;
		while (n < 200 && s[n] >= 0x20 && s[n] < 0x7f)
			++n;
		return n >= 8 && s[n] == 0 ? s : nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

// Strings an object points to (directly, or as a string's begin pointer).
void LogStrings(const char *what, const void *object, int bytes)
{
	for (int off = 0; off < bytes; off += 4) {
		__try {
			const void *p = Ptr(object, static_cast<DWORD>(off));
			if (reinterpret_cast<DWORD>(p) < 0x10000)
				continue;
			if (const char *s = Text(p))
				hooklog::Write("players: %s %+05x -> \"%.120s\"", what, off, s);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
}

void DescribeShared(const void *shared)
{
	hooklog::Write("players: hero data %p %s", shared, ClassName(shared));
	Dump("shared", shared, -0x10, 0x220);
	LogStrings("shared", shared, 0x220);
	for (DWORD off = 0x12c; off <= 0x138; off += 4) {  // FaceTexture, FaceTextureSmall (CDBPtr, 8 bytes each)
		__try {
			const void *p = Ptr(shared, off);
			if (reinterpret_cast<DWORD>(p) < 0x10000)
				continue;
			hooklog::Write("players: shared %+05lx -> %p %s", off, p, ClassName(p));
			Dump("texture", p, -0x10, 0x90);
			LogStrings("texture", p, 0x90);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
}

const int kSeen = 64;
const void *g_seen[kSeen];
int g_seen_count = 0;

bool Seen(const void *p)
{
	for (int i = 0; i < g_seen_count; ++i)
		if (g_seen[i] == p)
			return true;
	if (g_seen_count < kSeen)
		g_seen[g_seen_count++] = p;
	return false;
}

void ScanForData(const char *what, const void *object, int from, int to, int depth)
{
	for (int off = from; off < to; off += 4) {
		__try {
			const void *p = Ptr(object, static_cast<DWORD>(off));
			if (reinterpret_cast<DWORD>(p) < 0x10000 || Seen(p))
				continue;
			const char *name = ClassName(p);
			if (!name[0]) {
				// a CDBPtr-like holder: its first word points to a game data object
				const void *q = Ptr(p, 0);
				const char *qn = reinterpret_cast<DWORD>(q) > 0x10000 ? ClassName(q) : "";
				if (qn[0] && strstr(qn, "@NDb@@")) {
					hooklog::Write("players: %s %+05x -> %p -> %p %s", what, off, p, q, qn);
					if (strstr(qn, "AdvMapHeroShared") && !Seen(q))
						DescribeShared(q);
				}
				continue;
			}
			hooklog::Write("players: %s %+05x -> %p %s", what, off, p, name);
			if (strstr(name, "AdvMapHeroShared"))
				DescribeShared(p);
			else if (depth > 0 && !strstr(name, "@NDb@@") && !strstr(name, "CWorld") && !strstr(name, "CPlayer@"))
				ScanForData(name, p, -0x40, 0x400, depth - 1);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
}

void ScanHero(const void *hero)
{
	g_seen_count = 0;
	ScanForData("hero", hero, -0x1c, 0x300, 1);
}

void TraceRaw(const void *world)
{
	static bool done = false;
	if (done)
		return;
	done = true;
	__try {
		Dump("world", world, 0, 0x200);
		const DWORD *begin = reinterpret_cast<const DWORD *>(Read(world, 0x24));
		const DWORD *end = reinterpret_cast<const DWORD *>(Read(world, 0x28));
		for (const DWORD *p = begin; p && p < end && p < begin + kMaxPlayers; ++p) {
			const void *player = reinterpret_cast<const void *>(*p);
			hooklog::Write("players: player %p %s", player, ClassName(player));
			Dump("player", player, -0x1c, 0x6bc);
			const void *hero = Ptr(player, 0x1d0);
			const DWORD *hb = reinterpret_cast<const DWORD *>(Read(player, 0x18));
			const DWORD *he = reinterpret_cast<const DWORD *>(Read(player, 0x1c));
			hooklog::Write("players: selected hero %p %s, %d heroes, first %p %s", hero, hero ? ClassName(hero) : "",
			               hb && he > hb ? static_cast<int>(he - hb) : 0, hb && he > hb ? reinterpret_cast<void *>(*hb) : nullptr,
			               hb && he > hb ? ClassName(reinterpret_cast<const void *>(*hb)) : "");
			if (hb && he > hb) {
				const void *h = reinterpret_cast<const void *>(*hb);
				Dump("hero", h, -0x1c, 0x300);
				ScanHero(h);
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("players: raw dump crashed (caught)");
	}
}

const char *StatusName(Status s)
{
	switch (s) {
	case kPlaying:
		return "playing";
	case kEnded:
		return "turn ended";
	case kThinking:
		return "thinking";
	default:
		return "waiting";
	}
}

void TraceRows(const Snapshot &s, const DWORD *turn, const BYTE *ended)
{
	for (int i = 0; i < s.count; ++i) {
		const Row &r = s.rows[i];
		char line[256];
		_snprintf_s(line, sizeof(line), _TRUNCATE,
		            "players: day %d%s: player %d \"%s\" (%s, colour %d, team %d) hero \"%s\": %s (turn=%lu ended=%u)",
		            s.day, s.simultaneous ? " (simultaneous turns)" : "", r.id, r.name, r.human ? "human" : "AI",
		            r.colour, r.team, r.hero, StatusName(r.status), turn[i], ended[i]);
		if (strcmp(line, g_last_line[i]) != 0) {
			lstrcpynA(g_last_line[i], line, sizeof(g_last_line[i]));
			hooklog::Write("%s", line);
		}
	}
}

bool ReadRows(const void *world, Snapshot *out, DWORD *turn, BYTE *ended)
{
	if (Read(world, 0) != kWorldVftable)
		return false;
	out->day = static_cast<int>(Read(world, 0x48)) + 1;
	out->simultaneous = ReadByte(world, 0x90) != 0;
	NewDay(out->day);
	const DWORD *begin = reinterpret_cast<const DWORD *>(Read(world, 0x24));
	const DWORD *end = reinterpret_cast<const DWORD *>(Read(world, 0x28));
	if (!begin || end < begin || end - begin > 16)
		return false;
	int computers = 0;
	for (const DWORD *p = begin; p < end && out->count < kMaxPlayers; ++p) {
		const void *player = reinterpret_cast<const void *>(*p);
		if (!player || Read(player, 0) != kPlayerVftable)
			continue;
		const int id = static_cast<int>(Read(player, 0x78));
		if (id < 1 || id > kMaxPlayers || Read(player, 0x7c) != 1)
			continue;  // the neutral player, or out of the game
		Row &r = out->rows[out->count];
		memset(&r, 0, sizeof(r));
		r.id = id;
		r.colour = static_cast<int>(Read(player, 0x140));
		r.team = static_cast<int>(Read(player, 0x1cc));
		r.human = Read(player, 0x74) == 0;
		WideToUtf8(static_cast<const BYTE *>(player) + 0x1fc, r.name, sizeof(r.name));
		if (!r.human)
			r.computer = ++computers;  // a nameless one is "Computer N" (player_list_draw.cpp)
		// The hero: the selected one, else the first of the player's heroes.
		const void *hero = Ptr(player, 0x1d0);
		if (!hero || Read(hero, 0) != kHeroVftable || Dying(hero)) {
			const DWORD *hb = reinterpret_cast<const DWORD *>(Read(player, 0x18));
			const DWORD *he = reinterpret_cast<const DWORD *>(Read(player, 0x1c));
			hero = hb && he > hb ? reinterpret_cast<const void *>(*hb) : nullptr;
		}
		if (hero && Read(hero, 0) == kHeroVftable && !Dying(hero)) {
			WideToUtf8(static_cast<const BYTE *>(hero) + 0x6c, r.hero, sizeof(r.hero));
			FacePath(hero, r.face, sizeof(r.face));
		}
		// The turn.
		const DWORD state = Read(player, 0x634);
		const bool pressed = ReadByte(player, 0x178) != 0;
		turn[out->count] = state;
		ended[out->count] = pressed ? 1 : 0;
		if (state == 0) {
			g_had_turn[id] = true;
			r.status = !r.human ? kThinking : pressed ? kEnded : kPlaying;
		}
		else {
			if (g_had_turn[id])
				g_done[id] = true;
			r.status = g_done[id] ? kEnded : kWaiting;
		}
		++out->count;
	}
	// Started in the middle of a day (a loaded game, or the overlay just
	// switched on): the players before the first one whose turn it is have
	// played already (the turn goes in player order; with simultaneous turns
	// the humans go first, together).
	int first_active = -1;
	for (int i = 0; i < out->count; ++i)
		if (turn[i] == 0 && first_active < 0)
			first_active = i;
	for (int i = 0; i < out->count && first_active >= 0; ++i) {
		Row &r = out->rows[i];
		if (r.status != kWaiting || g_had_turn[r.id])
			continue;
		const bool before = i < first_active;
		const bool human_before_ai = out->simultaneous && r.human && !out->rows[first_active].human;
		if (before || human_before_ai) {
			g_had_turn[r.id] = g_done[r.id] = true;
			r.status = kEnded;
		}
	}
	for (int i = 0; i < out->count; ++i) {
		if (out->rows[i].status == kEnded)
			++out->done;
		for (int j = 0; j < i; ++j)
			if (out->rows[j].team == out->rows[i].team)
				out->teams = true;
	}
	return out->count > 0;
}

}  // namespace

void SetTrace(int level)
{
	g_trace = level;
}

bool InstallGameHooks()
{
	if (g_ok)
		return true;
	bool ok = memcmp(reinterpret_cast<const void *>(kWorldDay), kWorldDayBytes, sizeof(kWorldDayBytes)) == 0 &&
	          memcmp(reinterpret_cast<const void *>(kTopScreenGetter), kTopScreenGetterBytes,
	                 sizeof(kTopScreenGetterBytes)) == 0;
	for (const Getter &g : kGetters)
		ok = ok && Read(reinterpret_cast<const void *>(g.slot), 0) == g.function &&
		     memcmp(reinterpret_cast<const void *>(g.function), g.bytes, g.size) == 0;
	ok = ok && memcmp(reinterpret_cast<const void *>(kHeroNameRead), kHeroNameReadBytes, sizeof(kHeroNameReadBytes)) == 0;
	if (!ok) {
		hooklog::Write("player list: the game's player data is not as expected: overlay off");
		return false;
	}
	g_faces_ok = true;
	for (const Bytes &b : kFaceCode)
		g_faces_ok = g_faces_ok && memcmp(reinterpret_cast<const void *>(b.address), b.bytes, b.size) == 0;
	if (!g_faces_ok)
		hooklog::Write("player list: the heroes' game data is not as expected: no portraits");
	g_world_day_next = reinterpret_cast<WorldDayFn>(hookpatch::PatchPointer(kWorldDaySlot, kWorldDay, &WorldDayHook));
	if (!g_world_day_next) {
		hooklog::Write("player list: the world's day code is not as expected: overlay off");
		return false;
	}
	g_ok = true;
	return true;
}

bool ReadSnapshot(Snapshot *out)
{
	memset(out, 0, sizeof(*out));
	const void *world = g_world;
	if (!g_ok || !world || GetTickCount() - g_world_seen > 3000)
		return false;
	char screen[128];
	bool on_map = false;
	__try {
		on_map = OnAdventureMap(screen, sizeof(screen));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
	if (g_trace && strcmp(screen, g_last_screen) != 0) {
		lstrcpynA(g_last_screen, screen, sizeof(g_last_screen));
		hooklog::Write("players: screen on top: %s", screen[0] ? screen : "(none)");
	}
	if (!on_map)
		return false;
	DWORD turn[kMaxPlayers] = {};
	BYTE ended[kMaxPlayers] = {};
	bool ok = false;
	__try {
		if (g_trace >= 2)
			TraceRaw(world);
		ok = ReadRows(world, out, turn, ended);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ok = false;
	}
	if (ok && g_trace)
		TraceRows(*out, turn, ended);
	return ok;
}

}  // namespace plist
}  // namespace mods
