// Mod: a start terrain that matches each player's faction on random maps
// (docs/plans/faction-terrain.md, option B(a): the map is rebuilt at the start
// with the factions picked in the lobby).
//
// What the game does (robot research, 2026-10-25):
//   - A random map is a map package (.h5m in the game's Maps folder) whose files
//     sit at Maps/RMG/<guid>/. The host made it on the random map screen; each
//     client rebuilt it when it joined (CRMGConnector 0x007011d0: the settings
//     from the map's sRMGProps (0x00701090), the generator's Generate (slot 1)
//     and Write (slot 4), which writes Maps/<name>.N.h5m).
//   - The generator gives each start zone its player's faction (sRMGProps
//     PlayersInfo, desc +0x254 +0x3c), so the town, ground, decorations and
//     dwellings follow it. Lobby factions only change the player, or the town
//     when it is a random town ("Random towns"): the ground stays.
//   - At the start the host sends NMP::CMPStart: the lobby setup (start info,
//     the factions at slot +0x18, 1 = random) and the map's id, a CDBID (a type
//     string, a byte, a dword and the path string at +0x14). Each client
//     resolves that id when it reads the message (0x00ca0c70, from 0x005a14a8),
//     then every PC creates the world from it (0x00820830, from 0x0071d67b). The
//     map's description was loaded in the lobby and stays cached by path; its
//     ground files are read again at the start.
//
// What this mod does (open-homm5.ini [rmg] faction_terrain=1 on the host):
//   1. Lobby (host only): on random maps, every start slot may pick any of the 8
//      factions (the host's start info decides the arrows of every PC: its lock
//      commands, 0x0071e7b0).
//   2. Each client's DLL says hello to the host in the lobby: a waiting-room
//      request (CMPWaitChangeRequest, 0x00711580) with a type the game does not
//      use, which a host without our DLL ignores (0x0071f3d0, default case).
//   3. Host start (0x007200da): factions left on "Random" get the faction the map
//      was made with. If a faction then differs and every human client said
//      hello, the start message names another map: Maps/RMG/<guid>-OH1-<factions>/,
//      one hex digit per player (0x00720019, after the message is built). If not
//      every client can follow, the map stays as it is (and on fixed-town maps the
//      changed factions go back to the map's).
//   4. Every PC that sees such a name (any DLL with this mod, whatever its
//      setting) rebuilds the map from the original's settings with those
//      factions, before the name is first looked up: a client when it reads the
//      start message, the host when its world starts. Same inputs, same
//      generator: the same map on every PC. A package with that name that already
//      opens (a game played before) is used as it is.
//
// Settings, open-homm5.ini [rmg]:
//   faction_terrain=0   1 = as the host, offer it (see above); off by default
//   trace_factions=0    research: log the lobby setups, the start data, the
//                       generator's calls and the map files opened
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "rmg_factions.h"

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

// --- the game's code -------------------------------------------------------------------
const DWORD kRunGameCall = 0x0071d67b;    // in 0x0071d660: call 0x00820830 (ecx = the start data)
const DWORD kRunGame = 0x00820830;        // __fastcall(start data, edx, one stack argument), ret 4
const DWORD kCtorCalls[] = {0x00ca0c45, 0x00ca0cbc};
const DWORD kCtor = 0x00ca0240;           // CPlayersStartInfo: __thiscall(SAdvMapDesc *), ret 4
const DWORD kHostStartCall = 0x007200da;  // in the host logic's update 0x00720080
const DWORD kHostStart = 0x0071feb0;      // __thiscall(host lobby logic)
const DWORD kStartBuiltCall = 0x00720019; // in 0x0071feb0: the message's data from the setup
const DWORD kStartBuilt = 0x0059fbc0;     // __thiscall(data, const data *), ret 4
const DWORD kStartReadCall = 0x005a14a8;  // in the message's reader 0x005a1430
const DWORD kStartRead = 0x00ca0c70;      // __thiscall(data, stream), ret 4: the start info from the map
const DWORD kHostUpdateSlot = 0x00e3a5dc; // CMPUbiComWaitHostLogic, slot 0
const DWORD kHostUpdate = 0x00720080;     // __thiscall(logic)
const DWORD kClientUpdateSlot = 0x00e3a544; // CMPUbiComWaitClientLogic, slot 0
const DWORD kClientUpdate = 0x0071df50;
const DWORD kHostRequestSlot = 0x00e3a5f0;  // host logic slot 5: a message from a PC
const DWORD kHostRequest = 0x0071f3d0;    // __thiscall(logic, int sender, message *), ret 8
const DWORD kLockElements = 0x0071e7b0;   // __fastcall(host logic): sends each PC its arrow locks
const DWORD kGenerateSlot = 0x00e55f2c;   // NRMG::CRandomMapGenerator, slot 1 (research trace)
const DWORD kGenerate = 0x0090e7e0;
const DWORD kWriteSlot = 0x00e55f38;      // slot 4 (research trace)
const DWORD kWrite = 0x00908da0;
const DWORD kGameVfs = 0x00fd63c4;        // the game's file system (combiner)

const DWORD kGameNew = 0x00878fd0;        // cdecl void *(size)
const DWORD kGameDelete = 0x00879240;     // cdecl void (void *)
const DWORD kDbGet = 0x009b0f60;          // __fastcall(const CDBID *): the resource (or 0)
const DWORD kParamsCtor = 0x007016c0;     // __fastcall(params): the generator's settings, 0xac bytes
const DWORD kParamsDtor = 0x007017c0;
const DWORD kParamsFromProps = 0x00701090; // __stdcall(params, const sRMGProps *): false if the template is missing
const DWORD kNewGenerator = 0x0090c6c0;   // __fastcall(log, progress, x): a new generator
const DWORD kRelease = 0x00434460;        // __fastcall(CPtr *): release
const DWORD kCdbidDtor = 0x0040daf0;      // __fastcall(CDBID *)
const DWORD kPathOf = 0x00bf69e0;         // __fastcall(string *out, const CDBID *): the path without '/' and #xpointer
const DWORD kWideToNarrow = 0x00401f70;   // __fastcall(string *out, const wstring *)
const DWORD kReplaceChar = 0x0047b140;    // __fastcall(string *, char from, char to), ret 4
const DWORD kStrCopy = 0x00401c60;        // __thiscall(string *out, const string *), ret 4
const DWORD kAppendSep = 0x009eb3d0;      // __fastcall(string *, char): a '\' or '/' unless the last is one
const DWORD kAppendText = 0x00484200;     // __thiscall(string *, const char *), ret 4
const DWORD kAppendRange = 0x004091d0;    // __thiscall(string *, const char *, const char *), ret 8
const DWORD kSlashes = 0x009eb6b0;        // __fastcall(string *dst, const string *src): '\' -> '/'
const DWORD kGameRoot = 0x00fe71a0;       // string: the game's folder
const DWORD kRmgFolder = 0x00fe07e0;      // string begin, end: the random maps' folder under data

const DWORD kStartInfoVftable = 0x00e92744;
const DWORD kRequestVftable = 0x00e39838; // NUI::CMPWaitChangeRequest: +0xc row, +0x10 type, +0x14 byte
const DWORD kSlotSize = 0x8c;
const DWORD kRmgProps = 0x254;            // SAdvMapDesc: sRMGProps (the map writer's field list, 0x00a0c141)
const DWORD kHelloType = 0x4f48;          // 'OH': a waiting-room request type the game does not use
const BYTE kProtocol = 1;                 // the "-OH1-" in the names
const int kMaxPlayers = 8;
const DWORD kAllRaces[] = {3, 8, 7, 4, 6, 5, 9, 10};  // the game's own order (random-town maps)

struct GameString {  // nstl::string (narrow), or nstl::wstring with wide characters
	char *begin;
	char *end;
	char *cap;
};
struct Cdbid {  // NDb CDBID, 0x20 bytes
	GameString type;
	BYTE flag;
	BYTE pad[3];
	DWORD hash;
	GameString path;
};

typedef void *(__cdecl *NewFn)(size_t);
typedef void(__cdecl *DeleteFn)(void *);
typedef void *(__fastcall *DbGetFn)(const void *id);
typedef void *(__fastcall *ThisFn)(void *self);
typedef char(__stdcall *ParamsFromPropsFn)(void *params, const void *props);
typedef BYTE *(__fastcall *NewGeneratorFn)(void *log, void *progress, void *x);
typedef void(__fastcall *ReleaseFn)(void *holder);
typedef GameString *(__fastcall *PathOfFn)(GameString *out, const void *id);
typedef GameString *(__fastcall *WideToNarrowFn)(GameString *out, const void *wide);
typedef void(__fastcall *ReplaceCharFn)(GameString *s, int from, int to);
typedef GameString *(__fastcall *StrCopyFn)(GameString *out, void *edx, const GameString *src);
typedef void(__fastcall *AppendSepFn)(GameString *s, int c);
typedef void(__fastcall *AppendTextFn)(GameString *s, void *edx, const char *text);
typedef void(__fastcall *AppendRangeFn)(GameString *s, void *edx, const char *b, const char *e);
typedef void(__fastcall *SlashesFn)(GameString *dst, const GameString *src);
typedef void *(__fastcall *GenerateFn)(void *gen, void *edx, void *out, BYTE *params, int *status);
typedef char(__fastcall *WriteFn)(void *gen, void *edx, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6,
                                  DWORD a7, DWORD a8, DWORD a9);
typedef DWORD(__fastcall *VersionFn)(void *gen, void *edx);
typedef void *(__fastcall *OpenFn)(void *vfs, void *edx, const GameString *path);

typedef void(__fastcall *RunGameFn)(void *start, void *edx, void *arg);
typedef void *(__fastcall *CtorFn)(void *self, void *edx, const BYTE *desc);
typedef void(__fastcall *LogicFn)(void *logic, void *edx);
typedef void *(__fastcall *StartBuiltFn)(BYTE *data, void *edx, const void *src);
typedef void(__fastcall *StartReadFn)(BYTE *data, void *edx, void *stream);
typedef int(__fastcall *HostRequestFn)(void *logic, void *edx, int sender, BYTE *message);
RunGameFn g_run_game_next = nullptr;
CtorFn g_ctor_next = nullptr;
LogicFn g_host_start_next = nullptr;
StartBuiltFn g_start_built_next = nullptr;
StartReadFn g_start_read_next = nullptr;
LogicFn g_host_update_next = nullptr;
LogicFn g_client_update_next = nullptr;
HostRequestFn g_host_request_next = nullptr;
GenerateFn g_generate_next = nullptr;
WriteFn g_write_next = nullptr;
OpenFn g_open_next = nullptr;

bool g_host = false;   // faction_terrain=1
bool g_trace = false;  // trace_factions=1

// --- small helpers -----------------------------------------------------------------------
bool Readable(const void *p, size_t n)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (!p || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
	    (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
		return false;
	return static_cast<const BYTE *>(p) + n <= static_cast<const BYTE *>(mbi.BaseAddress) + mbi.RegionSize;
}

DWORD Read(const void *base, DWORD offset)
{
	const BYTE *p = static_cast<const BYTE *>(base) + offset;
	return Readable(p, 4) ? *reinterpret_cast<const DWORD *>(p) : 0;
}

std::string Text(const GameString *s)
{
	if (!s || !Readable(s, sizeof(*s)) || !s->begin || s->end < s->begin || s->end - s->begin > 1024 ||
	    !Readable(s->begin, s->end - s->begin))
		return std::string();
	return std::string(s->begin, s->end);
}

// A game string holding text, with the game's allocator (it frees it itself).
bool SetText(GameString *s, const std::string &text)
{
	char *buf = static_cast<char *>(reinterpret_cast<NewFn>(kGameNew)(text.size() + 1));
	if (!buf)
		return false;
	memcpy(buf, text.c_str(), text.size() + 1);
	if (s->begin)
		reinterpret_cast<DeleteFn>(kGameDelete)(s->begin);
	s->begin = buf;
	s->end = buf + text.size();
	s->cap = buf + text.size() + 1;
	return true;
}

void FreeText(GameString *s)
{
	if (s->begin)
		reinterpret_cast<DeleteFn>(kGameDelete)(s->begin);
	s->begin = s->end = s->cap = nullptr;
}

size_t FindNoCase(const std::string &text, const char *part)
{
	const size_t n = strlen(part);
	for (size_t i = 0; i + n <= text.size(); ++i)
		if (_strnicmp(text.c_str() + i, part, n) == 0)
			return i;
	return std::string::npos;
}

// The folder name after /Maps/RMG/ in a map path, and where it starts.
bool MapFolder(const std::string &path, size_t *at, std::string *folder)
{
	const size_t i = FindNoCase(path, "maps/rmg/");
	if (i == std::string::npos)
		return false;
	const size_t start = i + 9;
	const size_t slash = path.find('/', start);
	if (slash == std::string::npos || slash == start)
		return false;
	*at = start;
	*folder = path.substr(start, slash - start);
	return true;
}

// "<guid>-OH1-<8 hex digits>": the original guid and the factions of players 1..8 (0 = none).
bool ParseOurs(const std::string &folder, std::string *guid, DWORD races[kMaxPlayers])
{
	char marker[8];
	_snprintf_s(marker, sizeof(marker), _TRUNCATE, "-OH%u-", kProtocol);
	const size_t m = folder.size() >= kMaxPlayers + 5 ? folder.size() - kMaxPlayers - 5 : std::string::npos;
	if (m == std::string::npos || m == 0 || folder.compare(m, 5, marker) != 0)
		return false;
	for (int i = 0; i < kMaxPlayers; ++i) {
		const char c = folder[m + 5 + i];
		const DWORD v = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
		if (v != 0 && (v < 3 || v > 10))
			return false;
		races[i] = v;
	}
	*guid = folder.substr(0, m);
	return true;
}

// --- the map's random map settings (desc +0x254) -------------------------------------------
struct MapInfo {
	bool rmg = false;
	bool random_towns = false;
	int players = 0;
	DWORD made[kMaxPlayers] = {};  // the faction each player's start zone was made for
	std::string guid;
};

bool ReadMap(const BYTE *desc, MapInfo *m)
{
	const BYTE *p = desc + kRmgProps;
	if (!desc || !Readable(p, 0x64))
		return false;
	m->rmg = p[4] != 0;
	m->random_towns = p[0x54] != 0;
	m->guid = Text(reinterpret_cast<const GameString *>(p + 0x10));
	const DWORD b = Read(p, 0x3c), e = Read(p, 0x40);
	m->players = e > b && (e - b) % 16 == 0 ? static_cast<int>((e - b) / 16) : 0;
	if (m->players > kMaxPlayers)
		m->players = kMaxPlayers;
	for (int i = 0; i < m->players; ++i)
		m->made[i] = Read(reinterpret_cast<void *>(b + 16 * i), 4);
	return true;
}

std::string RaceList(const DWORD *races, int n)
{
	std::string s;
	for (int i = 0; i < n; ++i) {
		char one[8];
		_snprintf_s(one, sizeof(one), _TRUNCATE, "%s%lu", i ? "," : "", races[i]);
		s += one;
	}
	return s;
}

// Start infos as the game built them, with their map (a small ring, like rmg_teams).
const int kRing = 64;
struct InfoEntry {
	const void *info;
	MapInfo map;
	bool widened;
};
InfoEntry g_ring[kRing];
int g_ring_next = 0;
bool g_lobby_rmg = false;  // the last lobby setup the game built is a random map's

InfoEntry *FindInfo(const void *info)
{
	for (int k = 1; k <= kRing; ++k) {
		InfoEntry &e = g_ring[(g_ring_next - k + kRing) % kRing];
		if (e.info == info)
			return &e;
	}
	return nullptr;
}

int Slots(const BYTE *info)
{
	if (!info || Read(info, 0) != kStartInfoVftable)
		return 0;
	const DWORD b = Read(info, 0x10), e = Read(info, 0x14);
	const int n = e > b ? static_cast<int>((e - b) / kSlotSize) : 0;
	return n <= kMaxPlayers ? n : 0;
}

BYTE *Slot(const BYTE *info, int i)
{
	return reinterpret_cast<BYTE *>(Read(info, 0x10) + i * kSlotSize);
}

// --- research trace (trace_factions=1) -----------------------------------------------------
void TextAt(DWORD p, char *out, size_t size)
{
	out[0] = 0;
	const BYTE *b = reinterpret_cast<const BYTE *>(p);
	if (!Readable(b, 2))
		return;
	const bool wide = Readable(b, 4) && b[1] == 0 && b[0] >= 0x20 && b[0] < 0x7f;
	const size_t step = wide ? 2 : 1;
	size_t n = 0;
	for (size_t i = 0; n + 1 < size && Readable(b + i, step); i += step) {
		if (b[i] < 0x20 || b[i] >= 0x7f || (wide && b[i + 1]))
			break;
		out[n++] = static_cast<char>(b[i]);
	}
	out[n] = 0;
	if (n < 3)
		out[0] = 0;
}

void LogDwords(const char *what, const void *p, int count)
{
	char line[1024];
	int len = _snprintf_s(line, sizeof(line), _TRUNCATE, "rmg factions: %s at %p:", what, p);
	for (int i = 0; i < count && len > 0 && len < static_cast<int>(sizeof(line)) - 80; ++i) {
		const DWORD v = Read(p, 4 * i);
		char text[64];
		TextAt(v, text, sizeof(text));
		len += _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, text[0] ? " %08lx\"%s\"" : " %08lx", v, text);
	}
	hooklog::Write("%s", line);
}

void LogSlots(const char *when, const BYTE *info)
{
	char line[1024];
	int len = _snprintf_s(line, sizeof(line), _TRUNCATE, "rmg factions: %s:", when);
	for (int i = 0; i < Slots(info); ++i) {
		const BYTE *s = Slot(info, i);
		len += _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " [state %lu player %lu pc %ld race %lu allowed",
		                   Read(s, 0), Read(s, 4), static_cast<LONG>(Read(s, 0x24)), Read(s, 0x18));
		const DWORD lb = Read(s, 0x50), le = Read(s, 0x54);
		for (DWORD q = lb; q < le && q < lb + 16 * 4; q += 4)
			len += _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " %lu", Read(reinterpret_cast<void *>(q), 0));
		len += _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, "]");
	}
	hooklog::Write("%s", line);
}

void *__fastcall GenerateHook(void *gen, void *edx, void *out, BYTE *params, int *status)
{
	hooklog::Write("rmg factions: generate: seed %lu players %lu size %lu", Read(params, 0x80), Read(params, 0x18),
	               Read(params, 0x10));
	const DWORD b = Read(params, 0x54), e = Read(params, 0x58);
	if (e > b && e - b <= 8 * 0x20)
		LogDwords("generate: factions", reinterpret_cast<void *>(b), static_cast<int>((e - b) / 4));
	const DWORD t0 = GetTickCount();
	void *r = g_generate_next(gen, edx, out, params, status);
	hooklog::Write("rmg factions: generate done in %lu ms, status %d", GetTickCount() - t0, status ? *status : -1);
	return r;
}

char __fastcall WriteHook(void *gen, void *edx, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7,
                          DWORD a8, DWORD a9)
{
	hooklog::Write("rmg factions: write: folder %s, file %s, from %s",
	               Text(reinterpret_cast<const GameString *>(a1)).c_str(), Text(reinterpret_cast<const GameString *>(a2)).c_str(),
	               Text(reinterpret_cast<const GameString *>(a3)).c_str());
	const DWORD t0 = GetTickCount();
	const char r = g_write_next(gen, edx, a1, a2, a3, a4, a5, a6, a7, a8, a9);
	hooklog::Write("rmg factions: write done in %lu ms: %d", GetTickCount() - t0, r);
	return r;
}

void *__fastcall OpenHook(void *vfs, void *edx, const GameString *path)
{
	void *s = g_open_next(vfs, edx, path);
	const std::string text = Text(path);
	if (text.find("map.xdb") != std::string::npos || text.find("Terrain.bin") != std::string::npos)
		hooklog::Write("rmg factions: file system opens %s: %s", text.c_str(), s ? "found" : "not found");
	return s;
}

// Research: where a crash happens (the exception, the code address and the
// game addresses found on the stack). Logs the first three faults only.
LONG CALLBACK FaultLogger(EXCEPTION_POINTERS *info)
{
	static LONG count = 0;
	const DWORD code = info->ExceptionRecord->ExceptionCode;
	const DWORD eip = info->ContextRecord->Eip;
	const bool in_game = eip >= 0x00401000 && eip < 0x00e00000;
	if (!((code == EXCEPTION_ACCESS_VIOLATION && in_game) || code == EXCEPTION_PRIV_INSTRUCTION ||
	      code == EXCEPTION_ILLEGAL_INSTRUCTION) ||
	    InterlockedIncrement(&count) > 3)
		return EXCEPTION_CONTINUE_SEARCH;
	const CONTEXT *c = info->ContextRecord;
	char line[600];
	int len = _snprintf_s(line, sizeof(line), _TRUNCATE, "rmg factions: FAULT %08lx at %08lx (eax %08lx ecx %08lx esi %08lx edi %08lx); stack:",
	                      code, c->Eip, c->Eax, c->Ecx, c->Esi, c->Edi);
	const DWORD *sp = reinterpret_cast<const DWORD *>(c->Esp);
	for (int i = 0, found = 0; i < 400 && found < 14 && Readable(sp + i, 4); ++i)
		if (sp[i] >= 0x00401000 && sp[i] < 0x00e00000) {
			len += _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " %08lx", sp[i]);
			++found;
		}
	hooklog::Write("%s", line);
	return EXCEPTION_CONTINUE_SEARCH;
}

void InstallOpenTrace()
{
	static LONG done = 0;
	if (!g_trace || InterlockedExchange(&done, 1))
		return;
	void *vfs = *reinterpret_cast<void **>(kGameVfs);
	if (!vfs || !Readable(vfs, 4))
		return;
	const DWORD slot = Read(vfs, 0) + 7 * 4;
	g_open_next = reinterpret_cast<OpenFn>(hookpatch::PatchPointer(slot, Read(reinterpret_cast<void *>(slot), 0), &OpenHook));
	hooklog::Write("rmg factions: file system trace: %s", g_open_next ? "on" : "FAILED");
}

// --- 1. the lobby (host): every faction on random maps ----------------------------------------
void *__fastcall CtorHook(void *self, void *edx, const BYTE *desc)
{
	void *result = g_ctor_next(self, edx, desc);
	InstallOpenTrace();
	InfoEntry &e = g_ring[g_ring_next];
	g_ring_next = (g_ring_next + 1) % kRing;
	e.info = self;
	e.map = MapInfo();
	e.widened = false;
	if (desc)
		ReadMap(desc, &e.map);
	g_lobby_rmg = e.map.rmg;
	if (g_trace)
		hooklog::Write("rmg factions: lobby setup %p: random map %d, random towns %d, made for %s, guid %s", self,
		               e.map.rmg, e.map.random_towns, RaceList(e.map.made, e.map.players).c_str(), e.map.guid.c_str());
	if (g_trace)
		LogSlots("lobby setup", static_cast<const BYTE *>(self));
	return result;
}

// Every playing slot of a random map may take any faction. Its allowed list is a
// game vector of ints at slot +0x50 (begin, end, cap; 0x00c9e9f0 fills it from the
// player's towns: one faction for a fixed town).
int Widen(const BYTE *info)
{
	int widened = 0;
	for (int i = 0; i < Slots(info); ++i) {
		BYTE *s = Slot(info, i);
		DWORD *list = reinterpret_cast<DWORD *>(s + 0x50);
		if (list[1] >= list[0] && (list[1] - list[0]) / 4 >= _countof(kAllRaces))
			continue;
		DWORD *items = static_cast<DWORD *>(reinterpret_cast<NewFn>(kGameNew)(sizeof(kAllRaces)));
		if (!items)
			continue;
		memcpy(items, kAllRaces, sizeof(kAllRaces));
		if (list[0])
			reinterpret_cast<DeleteFn>(kGameDelete)(reinterpret_cast<void *>(list[0]));
		list[0] = reinterpret_cast<DWORD>(items);
		list[1] = list[2] = reinterpret_cast<DWORD>(items + _countof(kAllRaces));
		++widened;
	}
	return widened;
}

void __fastcall HostUpdateHook(void *logic, void *edx)
{
	const BYTE *info = reinterpret_cast<const BYTE *>(Read(logic, 0x74));
	InfoEntry *e = info ? FindInfo(info) : nullptr;
	if (e && e->map.rmg && !e->widened) {
		e->widened = true;
		const int n = Widen(info);
		hooklog::Write("rmg factions: random map in the lobby: every faction allowed (%d start slot(s) were locked)", n);
		if (n)
			reinterpret_cast<ThisFn>(kLockElements)(logic);  // tell every PC its arrows
	}
	g_host_update_next(logic, edx);
}

// --- 2. the hello ---------------------------------------------------------------------------
const int kMaxSenders = 16;
struct Hello {
	int sender;
	BYTE version;
};
Hello g_hellos[kMaxSenders];
int g_hello_count = 0;
const void *g_hello_logic = nullptr;  // the host lobby they belong to

void __fastcall ClientUpdateHook(void *logic, void *edx)
{
	static DWORD last = 0;
	const DWORD now = GetTickCount();
	if (g_lobby_rmg && now - last >= 3000) {
		// A pending request on the first row (as the row's arrows record one:
		// +0xc4 type, +0xc8 a byte); this update sends it to the host (0x0071ded0).
		const DWORD rows = Read(logic, 0x38);
		BYTE *full = rows && Read(logic, 0x3c) > rows ? reinterpret_cast<BYTE *>(Read(reinterpret_cast<void *>(rows), 0)) - 8 : nullptr;
		if (full && Readable(full + 0xc4, 8) && *reinterpret_cast<DWORD *>(full + 0xc4) == 0) {
			*reinterpret_cast<DWORD *>(full + 0xc4) = kHelloType;
			full[0xc8] = kProtocol;
			last = now;
			static int logged = 0;
			if (logged++ < 1)
				hooklog::Write("rmg factions: hello to the host (this PC can rebuild a random map with the lobby's factions)");
		}
	}
	g_client_update_next(logic, edx);
}

int __fastcall HostRequestHook(void *logic, void *edx, int sender, BYTE *message)
{
	if (g_trace && message)
		hooklog::Write("rmg factions: message from PC %d: vftable %08lx, +0c %08lx +10 %08lx +14 %08lx", sender,
		               Read(message, 0), Read(message, 0xc), Read(message, 0x10), Read(message, 0x14));
	if (message && Read(message, 0) == kRequestVftable && Read(message, 0x10) == kHelloType) {
		if (g_hello_logic != logic) {
			g_hello_logic = logic;
			g_hello_count = 0;
		}
		int i = 0;
		while (i < g_hello_count && g_hellos[i].sender != sender)
			++i;
		if (i == g_hello_count && g_hello_count < kMaxSenders) {
			g_hellos[g_hello_count++] = {sender, message[0x14]};
			hooklog::Write("rmg factions: hello from PC %d (version %u)", sender, message[0x14]);
		}
	}
	return g_host_request_next(logic, edx, sender, message);  // the game ignores the type
}

bool SaidHello(const void *logic, int sender)
{
	if (g_hello_logic != logic)
		return false;
	for (int i = 0; i < g_hello_count; ++i)
		if (g_hellos[i].sender == sender)
			return g_hellos[i].version == kProtocol;
	return false;
}

// --- 3. the host's start ----------------------------------------------------------------------
std::string g_pending_path;    // the map the start message must name, if any
bool g_in_host_start = false;  // the host's start runs (it writes the start message)

// A slot's faction, changed the way the lobby's arrows change it (SetRace,
// start info slot 0x38 = 0x00c9df40: it also clears the slot's hero choice),
// stepping through its allowed list.
bool SetSlotRace(BYTE *info, int index, DWORD target)
{
	typedef void(__fastcall * SetRaceFn)(void *info, void *edx, int slot, int right);
	const DWORD *race = reinterpret_cast<const DWORD *>(Slot(info, index) + 0x18);
	const SetRaceFn set = reinterpret_cast<SetRaceFn>(Read(reinterpret_cast<void *>(Read(info, 0)), 0x38));
	for (int k = 0; k < 12 && *race != target; ++k)
		set(info, nullptr, index, 1);
	if (*race == target)
		return true;
	hooklog::Write("rmg factions: player %lu cannot take faction %lu in the lobby's list", Read(Slot(info, index), 4), target);
	return false;
}

void __fastcall HostStartHook(void *logic, void *edx)
{
	g_pending_path.clear();
	BYTE *info = reinterpret_cast<BYTE *>(Read(logic, 0x74));
	Cdbid *id = reinterpret_cast<Cdbid *>(static_cast<BYTE *>(logic) + 0x80);
	const BYTE *desc = static_cast<const BYTE *>(reinterpret_cast<DbGetFn>(kDbGet)(id));
	MapInfo map;
	if (Slots(info) && desc && ReadMap(desc, &map) && map.rmg && map.players > 0) {
		DWORD final_races[kMaxPlayers] = {};
		for (int i = 0; i < map.players; ++i)
			final_races[i] = map.made[i];
		bool changed = false, all_hello = true;
		const int host_pc = static_cast<int>(Read(logic, 0x34));
		for (int i = 0; i < Slots(info); ++i) {
			BYTE *s = Slot(info, i);
			const DWORD state = Read(s, 0), player = Read(s, 4);
			if ((state != 1 && state != 2) || player < 1 || player > static_cast<DWORD>(map.players))
				continue;
			const DWORD *race = reinterpret_cast<const DWORD *>(s + 0x18);
			if (*race == 1)  // "Random": the faction the map was made with (the owner's choice)
				SetSlotRace(info, i, map.made[player - 1]);
			final_races[player - 1] = *race;
			changed = changed || *race != map.made[player - 1];
			const int pc = static_cast<int>(Read(s, 0x24));
			if (state == 2 && pc != host_pc && pc != -1 && !SaidHello(logic, pc)) {
				all_hello = false;
				hooklog::Write("rmg factions: PC %d (player %lu) did not say hello: it cannot rebuild the map", pc, player);
			}
		}
		hooklog::Write("rmg factions: start: made for %s, factions %s", RaceList(map.made, map.players).c_str(),
		               RaceList(final_races, map.players).c_str());
		size_t at = 0;
		std::string folder;
		const std::string path = Text(&id->path);
		if (!changed) {
			hooklog::Write("rmg factions: every faction is the one the map was made for: the map stays");
		}
		else if (!all_hello || !MapFolder(path, &at, &folder) || folder != map.guid) {
			hooklog::Write("rmg factions: not every PC can rebuild the map (or its path %s is not as expected): it stays as it is",
			               path.c_str());
			if (!map.random_towns) {  // a fixed town of another faction: keep the map's factions
				for (int i = 0; i < Slots(info); ++i) {
					const DWORD player = Read(Slot(info, i), 4);
					if (player >= 1 && player <= static_cast<DWORD>(map.players))
						SetSlotRace(info, i, map.made[player - 1]);
				}
				hooklog::Write("rmg factions: fixed towns: the factions go back to the map's");
			}
		}
		else {
			char code[32];
			int n = _snprintf_s(code, sizeof(code), _TRUNCATE, "-OH%u-", kProtocol);
			for (int i = 0; i < kMaxPlayers; ++i)
				n += _snprintf_s(code + n, sizeof(code) - n, _TRUNCATE, "%lX", i < map.players ? final_races[i] : 0);
			g_pending_path = path.substr(0, at) + folder + code + path.substr(at + folder.size());
			hooklog::Write("rmg factions: the start names the rebuilt map %s", g_pending_path.c_str());
		}
	}
	g_in_host_start = true;
	g_host_start_next(logic, edx);
	g_in_host_start = false;
	g_pending_path.clear();
}

// The map id of the message: another path, in the form a client reads (an empty
// type, flag and hash; 0x00bf6880 copies these fields).
void *__fastcall StartBuiltHook(BYTE *data, void *edx, const void *src)
{
	void *r = g_start_built_next(data, edx, src);
	if (!g_pending_path.empty()) {
		Cdbid *id = reinterpret_cast<Cdbid *>(data + 4);
		if (SetText(&id->path, g_pending_path)) {
			if (id->type.begin) {
				id->type.end = id->type.begin;
				*id->type.begin = 0;
			}
			id->flag = 0;
			id->hash = 0;
		}
	}
	return r;
}

// --- 4. the rebuild -----------------------------------------------------------------------------
std::string g_built;  // the last rebuilt map path (lower case)

std::string Lower(std::string s)
{
	for (char &c : s)
		c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
	return s;
}

// Does the game's file system already have that map (a game played before)?
bool MapExists(const std::string &path)
{
	void *vfs = *reinterpret_cast<void **>(kGameVfs);
	if (!vfs || !Readable(vfs, 4))
		return false;
	std::string file = path.substr(0, path.find('#'));
	while (!file.empty() && file[0] == '/')
		file.erase(0, 1);
	char buf[512];
	if (file.size() >= sizeof(buf))
		return false;
	memcpy(buf, file.c_str(), file.size() + 1);
	GameString s = {buf, buf + file.size(), buf + file.size() + 1};
	const OpenFn open = reinterpret_cast<OpenFn>(Read(reinterpret_cast<void *>(Read(vfs, 0)), 7 * 4));
	void *stream = open(vfs, nullptr, &s);
	if (!stream)
		return false;
	typedef void(__fastcall * DeleteStreamFn)(void *self, void *edx, int flags);
	reinterpret_cast<DeleteStreamFn>(Read(reinterpret_cast<void *>(Read(stream, 0)), 0))(stream, nullptr, 1);
	return true;
}

// Builds the map that `path` names, as a joining client builds a random map
// (0x007011d0), with the factions in its name. False when it is not one of ours
// or the build failed (then nothing was written).
bool Rebuild(const std::string &path, bool *ours)
{
	*ours = false;
	size_t at = 0;
	std::string folder, guid;
	DWORD races[kMaxPlayers];
	if (!MapFolder(path, &at, &folder) || !ParseOurs(folder, &guid, races))
		return false;
	*ours = true;
	if (Lower(path) == g_built)
		return true;
	if (MapExists(path)) {
		hooklog::Write("rmg factions: %s is already there (a game played before): using it", path.c_str());
		g_built = Lower(path);
		return true;
	}
	// The original map's description (cached since the lobby).
	Cdbid original = {};
	const std::string original_path = path.substr(0, at) + guid + path.substr(at + folder.size());
	if (!SetText(&original.type, "") || !SetText(&original.path, original_path))
		return false;
	const BYTE *desc = static_cast<const BYTE *>(reinterpret_cast<DbGetFn>(kDbGet)(&original));
	FreeText(&original.type);
	FreeText(&original.path);
	MapInfo map;
	if (!desc || !ReadMap(desc, &map) || !map.rmg || map.guid != guid) {
		hooklog::Write("rmg factions: rebuild: the original map %s is not here or not a random map", original_path.c_str());
		return false;
	}
	const DWORD t0 = GetTickCount();
	// The robot runs two copies of the game from one folder, and the generator
	// writes data\RMGTemp\CurrentMap: one rebuild at a time.
	HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\open-homm5-rmg-rebuild");
	if (mutex)
		WaitForSingleObject(mutex, 120000);
	bool ok = false;
	BYTE params[0xb0] = {};
	reinterpret_cast<ThisFn>(kParamsCtor)(params);
	const BYTE *props = desc + kRmgProps;
	BYTE *gen = nullptr;
	if (!reinterpret_cast<ParamsFromPropsFn>(kParamsFromProps)(params, props)) {
		hooklog::Write("rmg factions: rebuild: the map's template is missing");
	}
	else {
		// The factions (16-byte entries, the faction at +4) and the new guid.
		const DWORD b = Read(params, 0x54), e = Read(params, 0x58);
		const int n = e > b ? static_cast<int>((e - b) / 16) : 0;
		for (int i = 0; i < n && i < kMaxPlayers; ++i)
			if (races[i])
				*reinterpret_cast<DWORD *>(b + 16 * i + 4) = races[i];
		SetText(reinterpret_cast<GameString *>(params), folder);
		gen = reinterpret_cast<NewGeneratorFn>(kNewGenerator)(nullptr, nullptr, nullptr);
	}
	if (gen) {
		const DWORD vbase = Read(reinterpret_cast<void *>(Read(gen, 4)), 4);
		*reinterpret_cast<DWORD *>(gen + vbase + 8) += 1;  // the reference the connector takes
		void **vt = *reinterpret_cast<void ***>(gen);
		const DWORD version = reinterpret_cast<VersionFn>(vt[7])(gen, nullptr);
		Cdbid result = {};  // Generate builds it in place
		int status = -1;
		if (version != Read(props, 8)) {
			hooklog::Write("rmg factions: rebuild: the map was made by generator version %lu, this one is %lu", Read(props, 8),
			               version);
		}
		else {
			reinterpret_cast<GenerateFn>(vt[1])(gen, nullptr, &result, params, &status);
			if (status != 0 || result.path.begin == result.path.end) {
				hooklog::Write("rmg factions: rebuild: the generator failed (%d)", status);
			}
			else {
				// Where and under which names the connector writes (0x00701334..0x00701451).
				const GameString *mapname = reinterpret_cast<const GameString *>(props + 0x48);
				GameString name, from, dir;
				reinterpret_cast<WideToNarrowFn>(kWideToNarrow)(&name, mapname);
				static const char kBad[] = " .:<>\"/\\|*?";
				for (const char *c = kBad; *c; ++c)
					reinterpret_cast<ReplaceCharFn>(kReplaceChar)(&name, *c, '_');
				reinterpret_cast<StrCopyFn>(kStrCopy)(&dir, nullptr, reinterpret_cast<const GameString *>(kGameRoot));
				reinterpret_cast<AppendSepFn>(kAppendSep)(&dir, '\\');
				reinterpret_cast<AppendTextFn>(kAppendText)(&dir, nullptr, "data");
				reinterpret_cast<AppendSepFn>(kAppendSep)(&dir, '\\');
				reinterpret_cast<AppendRangeFn>(kAppendRange)(&dir, nullptr, reinterpret_cast<const char *>(Read(reinterpret_cast<void *>(kRmgFolder), 0)),
				                                               reinterpret_cast<const char *>(Read(reinterpret_cast<void *>(kRmgFolder), 4)));
				reinterpret_cast<SlashesFn>(kSlashes)(&dir, &dir);
				reinterpret_cast<PathOfFn>(kPathOf)(&from, &result);
				ok = reinterpret_cast<WriteFn>(vt[4])(gen, nullptr, reinterpret_cast<DWORD>(&dir), reinterpret_cast<DWORD>(&name),
				                                      reinterpret_cast<DWORD>(&from), reinterpret_cast<DWORD>(mapname),
				                                      reinterpret_cast<DWORD>(&result), 1, 0, 0, 0) != 0;
				if (!ok)
					hooklog::Write("rmg factions: rebuild: writing the map failed");
				FreeText(&from);
				FreeText(&name);
				FreeText(&dir);
			}
			reinterpret_cast<ThisFn>(kCdbidDtor)(&result);
		}
		reinterpret_cast<ReleaseFn>(kRelease)(&gen);
	}
	reinterpret_cast<ThisFn>(kParamsDtor)(params);
	if (mutex) {
		ReleaseMutex(mutex);
		CloseHandle(mutex);
	}
	if (ok) {
		g_built = Lower(path);
		hooklog::Write("rmg factions: rebuilt the map for factions %s in %lu ms: %s", RaceList(races, map.players).c_str(),
		               GetTickCount() - t0, path.c_str());
	}
	return ok;
}

// A failed rebuild: this PC loads the original map (its world will differ from
// the others', which the game's checks report) rather than a missing one.
void Restore(Cdbid *id, const std::string &path)
{
	size_t at = 0;
	std::string folder, guid;
	DWORD races[kMaxPlayers];
	if (MapFolder(path, &at, &folder) && ParseOurs(folder, &guid, races)) {
		SetText(&id->path, path.substr(0, at) + guid + path.substr(at + folder.size()));
		hooklog::Write("rmg factions: REBUILD FAILED: this PC starts on the original map; the game will not be in step");
	}
}

void RebuildFor(Cdbid *id)
{
	const std::string path = Text(&id->path);
	bool ours = false;
	if (!Rebuild(path, &ours) && ours)
		Restore(id, path);
}

// A client reads the start message: build the map before the game looks it up.
// The same code writes the message on the host, inside its start: not then.
void __fastcall StartReadHook(BYTE *data, void *edx, void *stream)
{
	if (!g_in_host_start)
		RebuildFor(reinterpret_cast<Cdbid *>(data + 4));
	g_start_read_next(data, edx, stream);
}

void __fastcall RunGameHook(void *start, void *edx, void *arg)
{
	if (g_trace) {
		hooklog::Write("rmg factions: the game starts on %s",
		               Text(reinterpret_cast<const GameString *>(static_cast<BYTE *>(start) + 0x24)).c_str());
		LogSlots("start info at the start", reinterpret_cast<const BYTE *>(Read(start, 0xc)));
	}
	RebuildFor(reinterpret_cast<Cdbid *>(static_cast<BYTE *>(start) + 0x10));  // the host
	g_run_game_next(start, edx, arg);
}

// The game's functions this mod calls must be the ones it knows.
bool CodeAsExpected()
{
	struct Expect {
		DWORD at;
		BYTE bytes[8];
	};
	static const Expect kCode[] = {
		{kParamsFromProps, {0x83, 0xec, 0x0c, 0x55, 0x57, 0x8b, 0x7c, 0x24}},
		{kNewGenerator, {0x53, 0x56, 0x57, 0x68, 0xf0, 0x00, 0x00, 0x00}},
		{kPathOf, {0x6a, 0xff, 0x68, 0x8c, 0xe0, 0xd9, 0x00, 0x64}},
		{kReplaceChar, {0x8b, 0x01, 0x3b, 0x41, 0x04, 0x74, 0x18, 0x53}},
		{kAppendSep, {0x8b, 0x41, 0x04, 0x39, 0x01, 0x74, 0x11, 0x8a}},
		{kStrCopy, {0x56, 0x8b, 0xf1, 0x33, 0xc0, 0x89, 0x06, 0x89}},
		{kAppendText, {0x56, 0x8b, 0x74, 0x24, 0x08, 0x8b, 0xc6, 0x57}},
		{kRunGameCall - 4, {0x8b, 0xd6, 0x8b, 0xcf, 0xe8, 0xb0, 0x31, 0x10}},
	};
	for (const Expect &e : kCode)
		if (memcmp(reinterpret_cast<const void *>(e.at), e.bytes, sizeof(e.bytes)) != 0) {
			hooklog::Write("rmg factions: the game's code at %08lx is not as expected", e.at);
			return false;
		}
	return true;
}

}  // namespace

void ApplyRmgFactionsFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	g_host = GetPrivateProfileIntW(L"rmg", L"faction_terrain", 0, path) != 0;
	g_trace = GetPrivateProfileIntW(L"rmg", L"trace_factions", 0, path) != 0;
	if (!CodeAsExpected()) {
		hooklog::Write("rmg factions: off (this PC cannot rebuild random maps with the lobby's factions)");
		return;
	}
	// Every PC: follow a host that names a rebuilt map, and say hello in the lobby.
	g_ctor_next = reinterpret_cast<CtorFn>(hookpatch::RedirectCalls(kCtorCalls, 2, kCtor, &CtorHook));
	g_start_read_next = reinterpret_cast<StartReadFn>(hookpatch::RedirectCall(kStartReadCall, kStartRead, &StartReadHook));
	g_run_game_next = reinterpret_cast<RunGameFn>(hookpatch::RedirectCall(kRunGameCall, kRunGame, &RunGameHook));
	if (!g_ctor_next || !g_start_read_next || !g_run_game_next) {
		hooklog::Write("rmg factions: the game's start code is not as expected: off");
		return;
	}
	g_client_update_next = reinterpret_cast<LogicFn>(hookpatch::PatchPointer(kClientUpdateSlot, kClientUpdate, &ClientUpdateHook));
	if (!g_client_update_next)
		hooklog::Write("rmg factions: the waiting room is not as expected: no hello (a host cannot rebuild maps with this PC)");
	if (g_trace) {
		AddVectoredExceptionHandler(0, &FaultLogger);
		g_generate_next = reinterpret_cast<GenerateFn>(hookpatch::PatchPointer(kGenerateSlot, kGenerate, &GenerateHook));
		g_write_next = reinterpret_cast<WriteFn>(hookpatch::PatchPointer(kWriteSlot, kWrite, &WriteHook));
	}
	if (g_host) {
		g_host_update_next = reinterpret_cast<LogicFn>(hookpatch::PatchPointer(kHostUpdateSlot, kHostUpdate, &HostUpdateHook));
		g_host_request_next = reinterpret_cast<HostRequestFn>(hookpatch::PatchPointer(kHostRequestSlot, kHostRequest, &HostRequestHook));
		g_host_start_next = reinterpret_cast<LogicFn>(hookpatch::RedirectCall(kHostStartCall, kHostStart, &HostStartHook));
		g_start_built_next = reinterpret_cast<StartBuiltFn>(hookpatch::RedirectCall(kStartBuiltCall, kStartBuilt, &StartBuiltHook));
		if (!g_host_update_next || !g_host_request_next || !g_host_start_next || !g_start_built_next) {
			hooklog::Write("rmg factions: the host's lobby code is not as expected: hosting with faction terrain off");
			g_host = false;
		}
	}
	hooklog::Write("rmg factions: %s; this PC rebuilds random maps when a host asks for it%s",
	               g_host ? "faction_terrain=1: as the host, random maps are rebuilt with the lobby's factions"
	                      : "faction_terrain=0",
	               g_trace ? " (trace on)" : "");
}

}  // namespace mods
