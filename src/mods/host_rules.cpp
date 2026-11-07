// Mod: game rules chosen by the host for every player, with no file to swap
// (docs/plans/host-rules.md). Step 1: MMH5.5's own switches of
// scripts/H55-Settings.lua (governors, town conversion, town gate, mine guards,
// AI bonus, mixed neutral stacks, prank artifacts, random map extras...).
// Step 2: the start rules, the game mode (Default / Savage / Nightmare / Random)
// and ToE's start bonuses (each player keeps the bonus picked with the lobby's
// arrows), served in MMH5.5's start script scripts/H55-AdvMap.lua; the lobby's
// bonus tooltips then describe ToE's bonuses (host_rules_bonus.cpp), and its
// bonus icons show ToE's pictures (host_rules_icons.cpp serves them).
// Step 3: the class skill lock (class_skills): the game's class table, edited in
// memory when the game starts (host_rules_classes.cpp), kept in saves.
//
// How a game gets its rules:
//   1. Lobby hello (every PC with this mod, any map). Each client's DLL sends the
//      host, now and then, waiting-room requests the game ignores (as the faction
//      mod's hello, rmg_factions.cpp: CMPWaitChangeRequest, type unknown to the
//      host's handler 0x0071f3d0): "I apply the host's rules, version 4" (type
//      'OR'), and five hashes (types 'N', 'S', 'X', 'A', 'C' in the top byte:
//      H55-Settings.lua without the rules' values, the whole of it, MMH5.5's
//      stats paks, which 55u.dll reads itself, the "StartingBonus" part of
//      H55-AdvMap.lua, and the class table in memory). The host logs (and the
//      panel shows) a player whose files differ from its own; the start rules
//      are used only when every PC has the same "StartingBonus" part as the
//      host, which our edits know, and class_skills only when every PC has the
//      host's class table and can edit it.
//   2. Lobby display (host -> clients). The host sends each client that said
//      hello its rules (a short text, "L3|1|0,1,..."), 4 bytes at a time, as the
//      game's "lock buttons" commands (CMPWaitLockButtonsUICommand, 0x007111d0,
//      sent to one PC like the game's own, transport slot 8) for rows that do not
//      exist (0x4f52xxxx): a client without this mod ignores them (0x0071ef10
//      checks the row), ours puts them together and shows the rules read only.
//      (A hero-name command, which holds a string, never reached the client: the
//      game does not send that class over the network.)
//   3. The start (binding). When the host starts (0x0071feb0) and every human
//      player said hello, the start message (NMP::CMPStart) carries one more
//      chunk, 0x4f, after the game's own fields (its serializer, vtable slot 3
//      0x007208a0, through the IBinSaver: a chunk with an unknown number is
//      skipped by the game's reader, 0x00cb6370): "OHR3;governance=0;...", the
//      value of every rule (the host's file, with its choices). The host keeps
//      the same values. If a player lacks the mod (no hello), no chunk is sent
//      and every PC keeps its own file: the game as without the mod (logged).
//      If a PC's start script differs, the start rules go as "0" (MMH5.5's way).
//   4. Each PC, when its game starts (NUI::CRunMPGame, slot 7 0x0071d660),
//      takes those values for this game. A single-player start
//      (NUI::CStartSingleAdventureMap, slot 7 0x007e1030) drops them.
//   5. When the map's scripts read scripts/H55-Settings.lua (advmap-startup.lua
//      -> H55-Core.lua, on every PC), our wrap of the game's "open file"
//      (NVFS::CWinVFS slot 7, stacked with the other mods) serves the file with
//      those values: only the numbers change (host_rules_logic.cpp). Logged.
//      When they read scripts/H55-AdvMap.lua and a start rule is not MMH5.5's
//      way, the file is served with three checked edits (PatchStartScript); if
//      MMH5.5's text is not as expected, it is served as it is (logged).
//   6. One rule is in no file: map_scripts, MMH5.5's scripts on maps
//      without a map script (a random map straight from the generator). In a
//      multiplayer game the world starts the map's scripts only for a map with
//      one (NWorld::CWorld slot 128, 0x009941a0 -> 0x00992d00, given the map's
//      NDb::SScript by the world's creation 0x00c9fa80). Our wrap of that slot,
//      while a multiplayer game starts with the host's rules: map_scripts=1 and
//      no script -> an empty script (as MMH5.5's Mapmixer adds: the game then
//      runs scripts/advmap-startup.lua, which loads MMH5.5's H55-Core.lua, and
//      no map file); map_scripts=0 and no script -> nothing, as in the original
//      game (RC19k's 55u.dll would run them: its change of the jump at
//      0x00992d2a). The default (the host's "file" value) is what this PC's
//      MMH5.5 does by itself: that jump's bytes. Logged.
//   7. class_skills (host_rules_classes.cpp): when the game starts (4.), each
//      PC points the class table's skill lists at unlocked copies, and puts
//      MMH5.5's back at the next start, in a lobby, or when a save without the
//      rule is read.
// The add-on's version (0.3.0 on): the same hello carries each client's H5 Together
// version and build ('V', 'B'), and the host sends every client that said its own the
// list of every player's (itself first; host_rules_logic.h). The panel's line in the
// waiting room says when they differ, or when a player (or the host) has an older
// H5 Together or none. A PC with [rules] enabled=0 says nothing, so it shows as "none".
// Saves: the scripts' variables are part of the save (docs/plans/host-rules.md,
// "Saves"), so a loaded game keeps the rules it started with (and its scripts);
// class_skills is kept in one more chunk of the saved world (step 7).
//
// The panel (host_rules_panel.cpp): the host chooses in the waiting room; the
// clients see the host's rules read-only; in a game, everyone sees this game's.
//
// Settings, h5-together.ini [rules]:
//   enabled=1      0 = take no part (no hello, no rules served), as without the DLL
//   key=F2         the panel's key (lobby and adventure map); none = no key
//   button=1       our "Rules..." button in the waiting room (0 = the key only)
//   <rule>=        the host's choices (written by the panel); empty = MMH5.5's file
//                  (game_mode=, toe_bonuses=: empty or 0 = MMH5.5's way)
//   trace=0        1 = log every script file the game opens (research), and print
//                  each player's start resources and minor artifacts to the
//                  game's console (a line added to H55-AdvMap.lua; testing)
//   trace_skills=0 1 = log every new skill a level-up offers (testing)
//   class_skills_single=0  1 = a single-player game uses this PC's class_skills
//                  and class_chance (testing saves; the other rules stay the file's)
//   class_chance=  the chance of the skills a class does not list, with
//                  class_skills=1 (empty = 2, vanilla's; testing; travels with the rules)
//   test_version=  testing: this PC says it has that H5 Together version in the lobby
//                  ("0.2.9"), or "old" = it says none, as a PC before 0.3.0
#include "host_rules.h"
#include "host_rules_internal.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "../hook/game_build.h"
#include "../hook/log.h"
#include "../hook/overlay.h"
#include "../hook/patch.h"
#include "../hook/version.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace mods {
namespace {

using rules::kRuleCount;
using rules::kRules;
using rules::Values;

// --- the game (vanilla 3.1 layout, also MMH55_Utility) -------------------------------------
const DWORD kWinVfsOpenSlot = 0x00e6651c;  // NVFS::CWinVFS vftable 0x00e66500, slot 7: OpenFile
const DWORD kWinVfsOpen = 0x009e0020;      // __thiscall(CWinVFS *, const nstl::string *path), ret 4
const DWORD kMemoryStreamVftable = 0x00e0a980;
const DWORD kMemoryStreamCtor = 0x004034d0;
const DWORD kStreamSetSize = 0x008c7610;
const DWORD kGameNew = 0x00878fd0;

const DWORD kHostUpdateSlot = 0x00e3a5dc;     // CMPUbiComWaitHostLogic slot 0
const DWORD kHostUpdate = 0x00720080;         // __thiscall(logic)
const DWORD kHostRequestSlot = 0x00e3a5f0;    // slot 5: a message from a PC
const DWORD kHostRequest = 0x0071f3d0;        // __thiscall(logic, int sender, message *), ret 8
const DWORD kClientUpdateSlot = 0x00e3a544;   // CMPUbiComWaitClientLogic slot 0
const DWORD kClientUpdate = 0x0071df50;
const DWORD kClientMessageSlot = 0x00e3a558;  // slot 5: a message from the host
const DWORD kClientMessage = 0x0071ef10;      // __thiscall(logic, int sender, message *), ret 8
const DWORD kHostStartCall = 0x007200da;      // in the host logic's update
const DWORD kHostStart = 0x0071feb0;          // __thiscall(logic): builds and sends NMP::CMPStart
const DWORD kStartSerializeSlot = 0x00e3a4e8; // NMP::CMPStart (vftable 0x00e3a4dc) slot 3
const DWORD kStartSerialize = 0x007208a0;     // __thiscall(message, IBinSaver *), ret 4, returns 0
const DWORD kRunMpSlot = 0x00e3a498;          // NUI::CRunMPGame (vftable 0x00e3a47c) slot 7
const DWORD kRunMp = 0x0071d660;              // __thiscall(this): every PC's world starts
const DWORD kRunSingleSlot = 0x00e454dc;      // NUI::CStartSingleAdventureMap (vftable 0x00e454c0) slot 7
const DWORD kRunSingle = 0x007e1030;
const DWORD kWorldScriptsSlot = 0x00e6280c;   // NWorld::CWorld (vftable 0x00e6260c) slot 128
const DWORD kWorldScripts = 0x009941a0;       // __thiscall(world, const NDb::SScript *map_script), ret 4
const DWORD kScriptStart = 0x00992d00;        // what it calls: starts the world's scripts
const DWORD kNoScriptJumpAt = 0x00992d2a;     // in it: "no map script: return" (jz, 6 bytes)
// The bytes our empty script relies on (vanilla 3.1, also MMH55_Utility): the
// slot's code (the world's script host +0x1d0, then 0x00992d00), the null test
// before the jump, and the only reads of the script: its file name, a string at
// +0x44..+0x48 (RC19k's 55u.dll changes that read, below, not what it reads).
const BYTE kWorldScriptsCode[] = {0x8b, 0x89, 0xd0, 0x01, 0x00, 0x00, 0x85, 0xc9, 0x74, 0x05, 0xe9, 0x51, 0xeb, 0xff, 0xff};
const DWORD kNullTestAt = 0x00992d26;
const BYTE kNullTest[] = {0x39, 0x5c, 0x24, 0x34};  // cmp [esp+0x34], ebx (ebx = 0)
const DWORD kNameReadAt = 0x00992db1;
const BYTE kNameRead[] = {0x8b, 0x44, 0x24, 0x3c, 0x8b, 0x48, 0x48, 0x8b, 0x50, 0x44};  // mov eax,[esp+0x3c]; [eax+0x48]; [eax+0x44]
// MMH5.5 RC19k's 55u.dll (with its 6 nops at the jump) replaces the first 7 of
// those bytes with a jump to its own code, which skips the name when there is no
// script and otherwise reads it as the game does: xor eax,eax; cmp ss:[esp+0x3c],eax;
// jz +0f; mov eax,ss:[esp+0x3c]; mov ecx,ds:[eax+0x48]; then back to 8b 50 44.
const BYTE kNameReadRc19kTail[] = {0x90, 0x90, 0x8b, 0x50, 0x44};
const BYTE kNameReadRc19kCode[] = {0x33, 0xc0, 0x36, 0x39, 0x44, 0x24, 0x3c, 0x74, 0x0f,
                                   0x36, 0x8b, 0x44, 0x24, 0x3c, 0x3e, 0x8b, 0x48, 0x48};

const DWORD kRequestVftable = 0x00e39838;      // NUI::CMPWaitChangeRequest: +0xc row, +0x10 type, +0x14 byte
const DWORD kLockCommandVftable = 0x00e397d0;  // NUI::CMPWaitLockButtonsUICommand: +0xc row, +0x10 mask
const DWORD kNewLockCommand = 0x007111d0;      // __fastcall(int row, const DWORD *mask): new command, count 0
const DWORD kReleaseObject = 0x008c61d0;       // __thiscall(object, 0xfffff): when its count reached 0
const DWORD kStartInfoVftable = 0x00e92744;
const DWORD kSlotSize = 0x8c;
const BYTE kChunk = 0x4f;          // our chunk in the start message (the game's are 2..7 inside 3)
const int kChunkSize = 512;        // fixed: a reader copies this many bytes (a new size needs a new chunk number)

const char kSettingsPath[] = "scripts/h55-settings.lua";
const char kStartScriptPath[] = "scripts/h55-advmap.lua";

struct GameString {
	const char *begin;
	const char *end;
	const char *cap;
};
struct GameWString {
	const wchar_t *begin;
	const wchar_t *end;
	const wchar_t *cap;
};
struct GameStream {  // CMemoryStream (see original_music.cpp)
	void **vftable;
	char *begin;
	char *cap;
	char *end;
	char *pos;
	DWORD flags;  // bit 0: broken
};

typedef GameStream *(__fastcall *OpenFn)(void *vfs, void *edx, const GameString *path);
typedef GameStream *(__fastcall *CtorFn)(GameStream *self, void *edx);
typedef void(__fastcall *SetSizeFn)(GameStream *self, void *edx, int size);
typedef void(__fastcall *DeleteFn)(GameStream *self, void *edx, int flags);
typedef void *(__cdecl *NewFn)(size_t size);
typedef void(__fastcall *LogicFn)(void *logic, void *edx);
typedef int(__fastcall *MessageFn)(void *logic, void *edx, int sender, BYTE *message);
typedef int(__fastcall *SerializeFn)(BYTE *message, void *edx, void *saver);
typedef void(__fastcall *RunFn)(void *self, void *edx);
typedef BYTE *(__fastcall *NewLockCommandFn)(int row, const DWORD *mask);
typedef void(__fastcall *ReleaseFn)(void *object, void *edx, int mask);
typedef void(__fastcall *SendToFn)(void *net, void *edx, int pc, void *message);
typedef char(__fastcall *IsReadingFn)(void *saver, void *edx);
typedef void(__fastcall *DataChunkFn)(void *saver, void *edx, int id, void *data, int size, int number);
typedef void(__fastcall *WorldScriptsFn)(void *world, void *edx, const void *map_script);

// An empty map script (NDb::SScript as 0x00992d00 reads it: its file name only,
// a string at +0x44..+0x48): the world then runs scripts/advmap-startup.lua
// and createAdvmapAliases(), and no map file (its name is empty), as for a map
// from MMH5.5's Mapmixer (whose map.lua holds only a comment).
struct EmptyScript {
	BYTE head[0x44];
	const char *name_begin, *name_end, *name_cap;
	BYTE tail[0x40];
};
const char g_empty_name[4] = {0, 0, 0, 0};
EmptyScript g_empty_script;  // zero, its name set at install

OpenFn g_open_next = nullptr;
LogicFn g_host_update_next = nullptr;
MessageFn g_host_request_next = nullptr;
LogicFn g_client_update_next = nullptr;
MessageFn g_client_message_next = nullptr;
LogicFn g_host_start_next = nullptr;
SerializeFn g_serialize_next = nullptr;
RunFn g_run_mp_next = nullptr;
RunFn g_run_single_next = nullptr;
WorldScriptsFn g_world_scripts_next = nullptr;  // null: map_scripts cannot be applied (the code is not as expected)

bool g_enabled = true;
bool g_trace = false;
bool g_class_single = false;  // testing: single player uses this PC's class_skills
int g_key = VK_F2;
bool g_button = true;

// --- state (under g_lock) --------------------------------------------------------------------
CRITICAL_SECTION g_lock;
struct Lock {
	Lock() { EnterCriticalSection(&g_lock); }
	~Lock() { LeaveCriticalSection(&g_lock); }
};

void *volatile g_vfs = nullptr;
bool g_file_known = false;  // this PC's settings file, as installed
std::string g_file;
Values g_file_values = rules::NoValues();
uint32_t g_hash_rest = 0, g_hash_whole = 0, g_hash_stats = 0;
bool g_start_known = false;      // this PC's start script (H55-AdvMap.lua) was looked at
uint32_t g_start_hash = 0;       // its "StartingBonus" part (0 = not found)
bool g_start_patchable = false;  // our edits fit it
std::string g_start_problem;     // why not
bool g_start_missing = false;    // why not: there is no such file
Values g_chosen = rules::NoValues();  // host: [rules] <key>= (valid values only)

const int kHashes = 5;  // rest, whole, stats, start script, class table
struct Pc {
	int sender;
	BYTE version;  // 0 = no hello yet
	bool has[kHashes];
	uint32_t hash[kHashes];
	bool told[kHashes];  // the difference was logged
	std::string sent;  // the last lobby text sent to it
	BYTE seq;          // of the last text's pieces
	DWORD hello_at;    // when its last hello came (a client says it every ~4 s)
	std::string name;  // its player's name then (another PC may get its number later)
	bool has_addon_version = false, has_addon_build = false;  // its H5 Together (0.3.0 on: 'V', 'B')
	uint32_t addon_version = 0, addon_build = 0;
	std::string sent_versions;  // the last version list sent to it
};
std::vector<Pc> g_pcs;
const void *g_pcs_logic = nullptr;  // the host lobby they belong to
DWORD g_host_seen = 0, g_client_seen = 0;  // the last update of each lobby logic (tick)
bool g_will_apply = false;
bool g_start_ok = false;   // host: every PC can take the start rules (the same start script, our edits fit)
std::string g_start_why;   // host: why not (for the log)
rules::Why g_start_why_code;  // the same for the panel
bool g_class_ok = false;   // host: every PC can take class_skills (the host's class table, editable)
std::string g_class_why;   // host: why not (for the log)
rules::Why g_class_why_code;
std::string g_map_path;   // host: the lobby's map
int g_map_script = -1;    // host: does it have a map script (-1 unknown)
std::vector<std::string> g_missing;
std::vector<rules::Differ> g_differ;
// rules::Differ::what for the log (the panel has them in the player's words)
const char *const kDifferWhat[3] = {"H55-Settings.lua (besides the rules)", "H55-Settings.lua", "MMH5.5's stats files"};

bool g_lobby_known = false;  // client: what the host sent in the lobby
Values g_lobby_values = rules::NoValues();
int g_lobby_flags = 0;
rules::LobbyAssembler g_assembler;  // the host's lobby text, piece by piece

// H5 Together's versions in the lobby
rules::Peer g_mine;                 // this PC's version and build (or [rules] test_version's)
bool g_say_version = true;          // false with [rules] test_version=old (as a PC before 0.3.0)
std::vector<rules::Peer> g_peers;   // host: every player's, itself first
std::vector<std::pair<std::string, DWORD>> g_seen;  // host: when each player was first seen in this lobby
std::vector<rules::Peer> g_host_list;  // client: the host's list
bool g_host_list_known = false;     // client: it came
bool g_host_list_unreadable = false;  // client: a list of a later format came
DWORD g_client_lobby_at = 0;        // client: when this lobby was first seen
const char kNotLogged[] = "-";
std::string g_versions_logged = kNotLogged;  // the differences logged last (in this lobby)
std::string VersionList(const std::vector<rules::Peer> &peers);
void LogVersions(const std::vector<rules::Peer> &differ, int players);

std::string g_start_text;  // host: the chunk of its start message ("" = none)
bool g_armed = false;      // this PC's next multiplayer game gets these rules
Values g_armed_values = rules::NoValues();
bool g_serving = false;  // this game's rules (served when the scripts read the file)
Values g_serving_values = rules::NoValues();
bool g_in_game = false;  // a game started since the last lobby
LONG g_served = 0;
volatile LONG g_mp_world = 0;  // a multiplayer game's world is being made (inside RunMpHook)

// --- small helpers ---------------------------------------------------------------------------
bool Readable(const void *p, size_t n)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (!p || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
		return false;
	return static_cast<const BYTE *>(p) + n <= static_cast<const BYTE *>(mbi.BaseAddress) + mbi.RegionSize;
}

DWORD Read(const void *base, DWORD offset)
{
	const BYTE *p = static_cast<const BYTE *>(base) + offset;
	return Readable(p, 4) ? *reinterpret_cast<const DWORD *>(p) : 0;
}

std::wstring IniPath()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together", L".ini", path, MAX_PATH);
	return path;
}

std::string Narrow(const std::wstring &w, UINT cp = CP_UTF8)
{
	const int n = WideCharToMultiByte(cp, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string s(n > 0 ? n : 0, '\0');
	if (n > 0)
		WideCharToMultiByte(cp, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
	return s;
}

std::string ValueList(const Values &v)
{
	std::string s;
	for (int r = 0; r < kRuleCount; ++r)
		if (!v[r].empty())
			s += std::string(s.empty() ? "" : " ") + kRules[r].key + "=" + v[r];
	return s.empty() ? "(none)" : s;
}

// "scripts\H55-Settings.lua" or "/scripts/h55-settings.lua" -> "scripts/h55-settings.lua"
std::string NormalizePath(const char *b, const char *e)
{
	while (b < e && (*b == '/' || *b == '\\'))
		++b;
	std::string s(b, e);
	for (char &c : s)
		c = c == '\\' ? '/' : (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	return s;
}

bool CodeIs(DWORD at, const BYTE *bytes, size_t n)
{
	return Readable(reinterpret_cast<const void *>(at), n) && memcmp(reinterpret_cast<const void *>(at), bytes, n) == 0;
}

// How the world's script start reads the script's name: 1 = the game's own code,
// 2 = through MMH5.5 RC19k's 55u.dll (the same reads when there is a script),
// 0 = something else (then an empty script is not given).
int NameReadForm()
{
	if (CodeIs(kNameReadAt, kNameRead, sizeof(kNameRead)))
		return 1;
	const BYTE *n = reinterpret_cast<const BYTE *>(kNameReadAt);
	if (!Readable(n, 10) || n[0] != 0xe9 || !CodeIs(kNameReadAt + 5, kNameReadRc19kTail, sizeof(kNameReadRc19kTail)))
		return 0;
	const DWORD target = kNameReadAt + 5 + *reinterpret_cast<const DWORD *>(n + 1);
	return CodeIs(target, kNameReadRc19kCode, sizeof(kNameReadRc19kCode)) ? 2 : 0;
}

// --- the rule map_scripts ----------------------------------------------------------------------
// What this PC's MMH5.5 does by itself on a map without a map script, in a
// multiplayer game: the "no map script" jump's bytes (55u.dll changes them when
// the game starts, long before a lobby). 1 = its scripts run (RC19k), 0 = not
// (RC19d, the original game), -1 = unreadable. Logged once, with where a changed
// jump goes.
int OwnMapScripts()
{
	const BYTE *code = reinterpret_cast<const BYTE *>(kNoScriptJumpAt);
	if (!Readable(code, 6))
		return -1;
	unsigned char now[6];
	memcpy(now, code, sizeof(now));
	const int own = rules::OwnMapScripts(now);
	static LONG logged = 0;
	if (InterlockedExchange(&logged, 1) == 0) {
		std::string where;
		DWORD target = 0;
		if (now[0] == 0x0f && (now[1] & 0xf0) == 0x80)
			target = kNoScriptJumpAt + 6 + *reinterpret_cast<const DWORD *>(now + 2);
		else if (now[0] == 0xe9 || now[0] == 0xe8)
			target = kNoScriptJumpAt + 5 + *reinterpret_cast<const DWORD *>(now + 1);
		if (own && target) {
			char module[MAX_PATH] = "?";
			HMODULE m = nullptr;
			if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			                       reinterpret_cast<LPCSTR>(target), &m))
				GetModuleFileNameA(m, module, MAX_PATH);
			const char *base = strrchr(module, '\\');
			char text[160];
			_snprintf_s(text, sizeof(text), _TRUNCATE, "; it goes to 0x%08x (%s):", target, base ? base + 1 : module);
			where = text;
			const BYTE *t = reinterpret_cast<const BYTE *>(target);
			for (int i = 0; i < 32 && Readable(t + i, 1); ++i) {
				_snprintf_s(text, sizeof(text), _TRUNCATE, " %02x", t[i]);
				where += text;
			}
		}
		hooklog::Write("rules: map scripts: the game's \"no map script\" jump (0x%08x) is %02x %02x %02x %02x %02x %02x: %s%s",
		               kNoScriptJumpAt, now[0], now[1], now[2], now[3], now[4], now[5],
		               own ? "changed (MMH5.5 RC19k: its scripts run on maps without a map script, in multiplayer too)"
		                   : "the original (MMH5.5's scripts do not run on maps without a map script in multiplayer)",
		               where.c_str());
	}
	return own;
}

// --- the files ---------------------------------------------------------------------------------
// A stream with this content instead of the one the game opened (as edge_sky.cpp).
GameStream *WithContent(GameStream *s, const std::string &data)
{
	const SetSizeFn set_size = reinterpret_cast<SetSizeFn>(kStreamSetSize);
	const auto ok = [&data](const GameStream *m) {
		return !(m->flags & 1) && m->begin && static_cast<size_t>(m->end - m->begin) == data.size();
	};
	if (s->vftable == reinterpret_cast<void **>(kMemoryStreamVftable)) {
		set_size(s, nullptr, static_cast<int>(data.size()));
		if (ok(s))
			memcpy(s->begin, data.data(), data.size());
		return s;
	}
	GameStream *m = static_cast<GameStream *>(reinterpret_cast<NewFn>(kGameNew)(sizeof(GameStream)));
	if (!m)
		return s;
	reinterpret_cast<CtorFn>(kMemoryStreamCtor)(m, nullptr);
	set_size(m, nullptr, static_cast<int>(data.size()));
	GameStream *unused = m;
	if (ok(m)) {
		memcpy(m->begin, data.data(), data.size());
		unused = s;
	}
	reinterpret_cast<DeleteFn>(unused->vftable[0])(unused, nullptr, 1);
	return unused == s ? m : s;
}

// A file of the game's data as installed (below our wrap: never with our values).
bool ReadInstalled(const char *path, std::string *out)
{
	void *vfs = g_vfs;
	if (!vfs || !g_open_next)
		return false;
	const size_t n = strlen(path);
	const GameString name = {path, path + n, path + n};
	GameStream *s = g_open_next(vfs, nullptr, &name);
	if (!s)
		return false;
	const bool ok = !(s->flags & 1) && s->begin && s->end > s->begin;
	if (ok)
		out->assign(s->begin, s->end);
	reinterpret_cast<DeleteFn>(s->vftable[0])(s, nullptr, 1);
	return ok;
}

// MMH5.5's stats files, as 55u.dll looks for them next to the exe (its own
// list: UserMODs\MMH55-CustomStats.h5u, data\MMH55-Stats.pak, ...Stats1..9.pak):
// one hash of the names and bytes of those that exist.
uint32_t StatsHash()
{
	wchar_t exe[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
	while (n > 0 && exe[n - 1] != L'\\')
		--n;
	exe[n] = 0;
	std::vector<std::wstring> names = {L"..\\UserMODs\\MMH55-CustomStats.h5u", L"..\\data\\MMH55-Stats.pak"};
	for (int i = 1; i <= 9; ++i)
		names.push_back(L"..\\data\\MMH55-Stats" + std::to_wstring(i) + L".pak");
	uint32_t hash = 2166136261u;
	std::string found;
	for (const std::wstring &name : names) {
		HANDLE f = CreateFileW((std::wstring(exe) + name).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		                       OPEN_EXISTING, 0, nullptr);
		if (f == INVALID_HANDLE_VALUE)
			continue;
		const std::string narrow = Narrow(name);
		hash = rules::Fnv1a(narrow.data(), narrow.size(), hash);
		std::vector<char> buf(65536);
		DWORD got = 0;
		while (ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) && got)
			hash = rules::Fnv1a(buf.data(), got, hash);
		CloseHandle(f);
		found += (found.empty() ? "" : ", ") + narrow;
	}
	hooklog::Write("rules: MMH5.5's stats files here: %s (hash %08x)", found.empty() ? "none" : found.c_str(), hash);
	return hash;
}

// This PC's settings file and its hashes, once (from the lobby, when the game's
// file system is up).
void LoadFiles()
{
	bool start_known;
	{
		Lock lock;
		if (g_file_known && g_start_known)
			return;
		start_known = g_start_known;
	}
	if (!start_known) {
		// MMH5.5's start script: its "StartingBonus" part's hash, and whether our edits fit it
		std::string lua, served, why;
		const bool read = ReadInstalled(kStartScriptPath, &lua);
		const uint32_t hash = read ? rules::StartBlockHash(lua) : 0;
		const bool fits = read && rules::PatchStartScript(lua, 4, true, false, &served, &why);
		if (!read)
			why = "the file was not found";
		if (read || g_vfs) {
			hooklog::Write("rules: this PC's scripts/H55-AdvMap.lua: %u bytes, its StartingBonus part's hash %08x; the start "
			               "rules (game mode, ToE start bonuses) %s%s",
			               static_cast<unsigned>(lua.size()), hash, fits ? "can be served" : "can NOT be served: ",
			               fits ? "" : why.c_str());
			Lock lock;
			g_start_known = true;
			g_start_hash = hash;
			g_start_patchable = fits;
			g_start_problem = fits ? std::string() : why;
			g_start_missing = !read;
		}
	}
	{
		Lock lock;
		if (g_file_known)
			return;
	}
	std::string file;
	if (!ReadInstalled(kSettingsPath, &file))
		return;
	Values values = rules::ParseSettings(file);
	for (int r = 0; r < kRuleCount; ++r)
		if (rules::IsStartRule(r))
			values[r] = rules::kStartRuleOff;  // not in that file: "MMH5.5's way" is this PC's default
	// map_scripts is not in the file either: its "file" value is what this PC's MMH5.5 does by itself
	const int own = OwnMapScripts();
	if (own >= 0)
		values[rules::kMapScripts] = own ? "1" : "0";
	// nor the class rules: MMH5.5's lock, and vanilla's rare-skill chance (the table is read and logged here)
	values[rules::kClassSkills] = rules::kClassLocked;
	values[rules::kClassChance] = rules::kClassChanceVanilla;
	rules::ClassTableInfo();
	const uint32_t rest = rules::HashWithoutRuleValues(file);
	const uint32_t whole = rules::Fnv1a(file.data(), file.size());
	const uint32_t stats = StatsHash();
	int missing = 0;
	for (int r = 0; r < kRuleCount; ++r)
		missing += kRules[r].var && values[r].empty() ? 1 : 0;
	hooklog::Write("rules: this PC's scripts/H55-Settings.lua: %u bytes, hash %08x (without the rules' values %08x); %s%s",
	               static_cast<unsigned>(file.size()), whole, rest, ValueList(values).c_str(),
	               missing ? " (some rules are not in the file: MMH5.5's scripts use their own defaults)" : "");
	Lock lock;
	g_file = file;
	g_file_values = values;
	g_hash_rest = rest;
	g_hash_whole = whole;
	g_hash_stats = stats;
	g_file_known = true;
}

// The host's values: its file's, with its choices.
Values Effective()
{
	Values v = g_file_values;
	for (int r = 0; r < kRuleCount; ++r)
		if (!g_chosen[r].empty())
			v[r] = g_chosen[r];
	return v;
}

}  // namespace

int HostComputerBonus()
{
	if (!g_enabled)
		return -1;
	const int r = rules::RuleIndex("ai_cheat");
	if (r < 0)
		return -1;
	std::string value;
	{
		Lock lock;
		if (!g_file_known && g_chosen[r].empty())
			return -1;
		value = Effective()[r];
	}
	if (value.empty())
		return -1;
	const int bonus = atoi(value.c_str());
	return bonus >= 0 && bonus <= 6 ? bonus : -1;
}

namespace {

// MMH5.5's start script, when the scripts read it (dofile from H55-Core.lua, every
// PC): with this game's start rules (game mode, ToE start bonuses), or as it is.
GameStream *ServeStartScript(GameStream *s)
{
	int mode = 0;
	bool toe = false;
	bool serving;
	{
		Lock lock;
		serving = g_serving;
		if (serving) {
			mode = atoi(g_serving_values[rules::kGameModeRule].c_str());
			toe = g_serving_values[rules::kToeBonusRule] == "1";
		}
	}
	const int used = rules::ModeUsed(mode, toe);
	if (used == 0 && !toe && !g_trace) {
		hooklog::Write("rules: the scripts read scripts/H55-AdvMap.lua: served as it is (%s)",
		               serving ? "this game's game mode and start bonuses are MMH5.5's way" : "this game uses the files' rules");
		return s;
	}
	const std::string file(s->begin, s->end);
	std::string served, why;
	if (!rules::PatchStartScript(file, mode, toe, g_trace, &served, &why)) {
		hooklog::Write("rules: the scripts read scripts/H55-AdvMap.lua: REFUSED, served as it is (MMH5.5's way): %s", why.c_str());
		return s;
	}
	if (served != file)
		s = WithContent(s, served);
	const bool ok = !(s->flags & 1) && static_cast<size_t>(s->end - s->begin) == served.size() &&
	                memcmp(s->begin, served.data(), served.size()) == 0;
	static const char *const kModes[5] = {"the bonus arrows (MMH5.5)", "Default", "Savage", "Nightmare", "Random"};
	hooklog::Write("rules: the scripts read scripts/H55-AdvMap.lua: served with game mode %s, %s%s; %u -> %u bytes, hash %08x%s",
	               kModes[used], toe ? "each human keeps the ToE start bonus" : "MMH5.5's start bonus (the ToE bonus taken back)",
	               g_trace ? ", the trace line" : "", static_cast<unsigned>(file.size()), static_cast<unsigned>(served.size()),
	               rules::Fnv1a(served.data(), served.size()), ok ? "" : " - FAILED: the game got the file as installed");
	return s;
}

GameStream *__fastcall OpenFileHook(void *vfs, void *edx, const GameString *path)
{
	g_vfs = vfs;
	GameStream *s = g_open_next(vfs, edx, path);
	if (!path || !path->begin || path->end <= path->begin)
		return s;
	const size_t length = static_cast<size_t>(path->end - path->begin);
	if (g_enabled && length >= 8 && length <= 300 &&
	    (_strnicmp(path->end - 4, ".xdb", 4) == 0 || _strnicmp(path->end - 4, ".dds", 4) == 0))
		return static_cast<GameStream *>(rules::ServeBonusPicture(path->begin, path->end, s));  // the lobby's bonus icons
	if (length < 8 || length > 300 || _strnicmp(path->end - 4, ".lua", 4) != 0)
		return s;  // (cheap test first: the game opens thousands of files)
	const std::string key = NormalizePath(path->begin, path->end);
	if (g_trace) {
		static LONG traced = 0;
		if (InterlockedIncrement(&traced) <= 200)
			hooklog::Write("rules: the game opens %s: %s", key.c_str(), s && !(s->flags & 1) ? "found" : "not found");
	}
	if ((key != kSettingsPath && key != kStartScriptPath) || !s || (s->flags & 1) || !s->begin || s->end < s->begin ||
	    s->pos != s->begin)
		return s;
	if (key == kStartScriptPath)
		return ServeStartScript(s);
	Values values;
	{
		Lock lock;
		if (!g_serving) {
			hooklog::Write("rules: the scripts read scripts/H55-Settings.lua: this game uses the file's rules");
			return s;
		}
		values = g_serving_values;
	}
	const std::string file(s->begin, s->end);
	std::vector<std::string> changes;
	const std::string served = rules::Substitute(file, values, &changes);
	std::string list;
	for (const std::string &c : changes)
		list += (list.empty() ? "" : ", ") + c;
	if (served != file)
		s = WithContent(s, served);
	const bool ok = !(s->flags & 1) && static_cast<size_t>(s->end - s->begin) == served.size() &&
	                memcmp(s->begin, served.data(), served.size()) == 0;
	if (InterlockedIncrement(&g_served) <= 50)
		hooklog::Write("rules: the scripts read scripts/H55-Settings.lua: served with the host's rules (%s; %s); %u -> %u bytes, "
		               "hash %08x%s",
		               list.empty() ? "no value differs from this PC's file" : list.c_str(), ValueList(values).c_str(),
		               static_cast<unsigned>(file.size()), static_cast<unsigned>(served.size()),
		               rules::Fnv1a(served.data(), served.size()), ok ? "" : " - FAILED: the game got the file as installed");
	return s;
}

// --- the lobby: who is who ------------------------------------------------------------------
int SlotCount(const BYTE *info)
{
	if (!info || Read(info, 0) != kStartInfoVftable)
		return 0;
	const DWORD b = Read(info, 0x10), e = Read(info, 0x14);
	const int n = e > b ? static_cast<int>((e - b) / kSlotSize) : 0;
	return n <= 8 ? n : 0;
}

const BYTE *SlotAt(const BYTE *info, int i)
{
	return reinterpret_cast<const BYTE *>(Read(info, 0x10) + i * kSlotSize);
}

std::string SlotName(const BYTE *slot)
{
	const GameWString *w = reinterpret_cast<const GameWString *>(slot + 0xc);
	if (!Readable(w, sizeof(*w)) || !w->begin || w->end <= w->begin || w->end - w->begin > 64 || !Readable(w->begin, (w->end - w->begin) * 2))
		return std::string();
	return Narrow(std::wstring(w->begin, w->end));
}

std::string PcName(const BYTE *info, int pc)
{
	for (int i = 0; i < SlotCount(info); ++i) {
		const BYTE *s = SlotAt(info, i);
		if (static_cast<int>(Read(s, 0x24)) == pc) {
			const std::string name = SlotName(s);
			if (!name.empty())
				return name;
		}
	}
	char text[24];
	_snprintf_s(text, sizeof(text), _TRUNCATE, "PC %d", pc);
	return text;
}

Pc *FindPc(int sender)
{
	for (Pc &p : g_pcs)
		if (p.sender == sender)
			return &p;
	return nullptr;
}

// A new host lobby: forget the last one's players.
void HostLobby(const void *logic)
{
	if (g_pcs_logic == logic)
		return;
	g_pcs_logic = logic;
	g_pcs.clear();
	g_peers.clear();
	g_seen.clear();
	g_versions_logged = kNotLogged;
	g_start_text.clear();
	g_in_game = false;
}

// The human players' PCs other than the host's (slot state 2 = human).
std::vector<int> HumanClients(const void *logic, const BYTE *info)
{
	std::vector<int> pcs;
	const int host_pc = static_cast<int>(Read(logic, 0x34));
	for (int i = 0; i < SlotCount(info); ++i) {
		const BYTE *s = SlotAt(info, i);
		const int pc = static_cast<int>(Read(s, 0x24));
		if (Read(s, 0) == 2 && pc != host_pc && pc != -1)
			pcs.push_back(pc);
	}
	return pcs;
}

// Are the start rules (game mode, ToE bonuses) other than MMH5.5's way in these values?
bool StartRulesChanged(const Values &v)
{
	for (int r = 0; r < kRuleCount; ++r)
		if (rules::IsStartRule(r) && !v[r].empty() && v[r] != rules::kStartRuleOff)
			return true;
	return false;
}

// Do these values unlock the class skills (class_skills other than MMH5.5's lock)?
bool ClassRuleChanged(const Values &v)
{
	return !v[rules::kClassSkills].empty() && v[rules::kClassSkills] != rules::kClassLocked;
}

// Host, under the lock: who lacks the mod, whose files differ, can every PC take
// the start rules and class_skills (for the log, the panel and the start).
void CheckPlayers(const void *logic, const BYTE *info, bool log)
{
	g_missing.clear();
	g_differ.clear();
	g_start_ok = g_start_known && g_start_patchable;
	g_start_why = !g_start_known ? "this PC's H55-AdvMap.lua was not read yet"
	              : !g_start_patchable ? "the host's H55-AdvMap.lua: " + g_start_problem
	                                   : std::string();
	g_start_why_code.code = !g_start_known ? rules::Why::kStartNotRead : !g_start_patchable ? rules::Why::kStartHost : rules::Why::kNone;
	g_start_why_code.player.clear();
	const rules::ClassInfo classes = rules::ClassTableInfo();
	g_class_ok = classes.editable;
	g_class_why = classes.editable ? std::string() : "the host's class table cannot be edited (" + classes.why + ")";
	g_class_why_code.code = classes.editable ? rules::Why::kNone : rules::Why::kClassHost;
	g_class_why_code.player.clear();
	const char *const *kWhat = kDifferWhat;
	for (int pc : HumanClients(logic, info)) {
		Pc *p = FindPc(pc);
		const std::string name = PcName(info, pc);
		// a hello of this version, fresh, from this player (not from a PC that left before it joined)
		if (!p || p->version != rules::kProtocol || GetTickCount() - p->hello_at > 15000 || p->name != name) {
			g_missing.push_back(name);
			continue;
		}
		// the start script: the same "StartingBonus" part as the host's (then our edits fit it there too)
		if (!p->has[3] || p->hash[3] != g_start_hash) {
			if (g_start_ok) {
				g_start_why = name + (p->has[3] ? "'s H55-AdvMap.lua is not the host's (its StartingBonus part differs)"
				                                : " has not sent its H55-AdvMap.lua's hash yet");
				g_start_why_code.code = p->has[3] ? rules::Why::kStartDiffers : rules::Why::kStartWaiting;
				g_start_why_code.player = name;
			}
			g_start_ok = false;
			if (log && p->has[3] && !p->told[3]) {
				p->told[3] = true;
				hooklog::Write("rules: CHECK: %s (PC %d) has another StartingBonus part in H55-AdvMap.lua than the host (%08x, "
				               "host %08x): the game mode and ToE start bonuses stay MMH5.5's way",
				               name.c_str(), pc, p->hash[3], g_start_hash);
			}
		}
		// the class table: the host's, and editable there (a PC that cannot edit its table sends 0)
		if (!p->has[4] || p->hash[4] == 0 || p->hash[4] != classes.hash) {
			if (g_class_ok) {
				g_class_why = name + (!p->has[4]        ? " has not sent its class table's hash yet"
				                      : p->hash[4] == 0 ? "'s class table cannot be edited"
				                                        : "'s class table is not the host's (another MMH5.5 version?)");
				g_class_why_code.code = !p->has[4]        ? rules::Why::kClassWaiting
				                        : p->hash[4] == 0 ? rules::Why::kClassCannot
				                                          : rules::Why::kClassDiffers;
				g_class_why_code.player = name;
			}
			g_class_ok = false;
			if (log && p->has[4] && !p->told[4]) {
				p->told[4] = true;
				hooklog::Write("rules: CHECK: %s (PC %d) has another class table than the host (%08x, host %08x): the class "
				               "skills stay MMH5.5's lock",
				               name.c_str(), pc, p->hash[4], classes.hash);
			}
		}
		const uint32_t mine[3] = {g_hash_rest, g_hash_whole, g_hash_stats};
		for (int k = 0; k < 3; ++k) {
			if (!g_file_known || !p->has[k] || p->hash[k] == mine[k])
				continue;
			if (k == 1 && p->has[0] && p->hash[0] == mine[0])
				continue;  // only the rules' values differ: the host's replace them
			g_differ.push_back({name, k});
			if (log && !p->told[k]) {
				p->told[k] = true;
				hooklog::Write("rules: CHECK: %s (PC %d) has another %s than the host (%08x, host %08x): the game may "
				               "not stay in step",
				               name.c_str(), pc, kWhat[k], p->hash[k], mine[k]);
			}
		}
	}
	g_will_apply = g_missing.empty();
}

// --- 1. the hello: clients ------------------------------------------------------------------
// In a lobby (host or client), the last game is over: MMH5.5's class lists back,
// unless a save's world was just read (a game being loaded from the lobby).
void ClassSkillsInLobby()
{
	if (rules::ClassSkillsNow().mode != rules::kClassLocked && !rules::WorldReadRecently(60000))
		rules::SetClassSkills(rules::kClassLocked, std::string(), false, "back in a lobby");
}

void __fastcall ClientUpdateHook(void *logic, void *edx)
{
	if (g_enabled)
		ClassSkillsInLobby();
	{
		Lock lock;
		g_client_seen = GetTickCount();
		g_host_seen = 0;
		static const void *last_logic = nullptr;
		if (g_in_game || logic != last_logic) {  // another lobby: what the last host sent is over
			last_logic = logic;
			g_in_game = false;
			g_lobby_known = false;
			g_assembler = rules::LobbyAssembler();
			g_host_list_known = false;
			g_host_list_unreadable = false;
			g_host_list.clear();
			g_client_lobby_at = GetTickCount();
			g_versions_logged = kNotLogged;
		}
	}
	static int step = 0;
	static DWORD next_at = 0;
	const DWORD now = GetTickCount();
	if (g_enabled && static_cast<LONG>(now - next_at) >= 0) {
		LoadFiles();
		// A pending request on the first row (as rmg_factions.cpp's hello): +0xc4 type, +0xc8 a byte.
		const DWORD rows = Read(logic, 0x38);
		BYTE *full = rows && Read(logic, 0x3c) > rows ? reinterpret_cast<BYTE *>(Read(reinterpret_cast<void *>(rows), 0)) - 8 : nullptr;
		if (full && Readable(full + 0xc4, 8) && *reinterpret_cast<DWORD *>(full + 0xc4) == 0) {
			uint32_t type = rules::kHelloType;
			uint8_t byte = rules::kProtocol;
			bool known, start_known;
			uint32_t rest, whole, stats, start;
			{
				Lock lock;
				known = g_file_known;
				start_known = g_start_known;
				rest = g_hash_rest, whole = g_hash_whole, stats = g_hash_stats, start = g_start_hash;
			}
			bool send = step == 0 || known;
			if (step == 1 && known)
				rules::HashRequest(rules::kMarkSettingsRest, rest, &type, &byte);
			else if (step == 2 && known)
				rules::HashRequest(rules::kMarkSettingsWhole, whole, &type, &byte);
			else if (step == 3)
				rules::HashRequest(rules::kMarkStats, stats, &type, &byte);
			else if (step == 4) {
				rules::HashRequest(rules::kMarkStartScript, start, &type, &byte);
				send = start_known;
			}
			else if (step == 5) {
				const rules::ClassInfo classes = rules::ClassTableInfo();
				rules::HashRequest(rules::kMarkClassTable, classes.editable ? classes.hash : 0, &type, &byte);
			}
			else if (step == 6) {  // H5 Together's version and build (0.3.0 on; a host before ignores them)
				rules::HashRequest(rules::kMarkVersion, rules::PackVersion(g_mine.version.c_str()), &type, &byte);
				send = g_say_version;
			}
			else if (step == 7) {
				rules::HashRequest(rules::kMarkBuild, g_mine.build, &type, &byte);
				send = g_say_version;
			}
			if (send) {
				*reinterpret_cast<DWORD *>(full + 0xc4) = type;
				full[0xc8] = byte;
			}
			static int logged = 0;
			if (step == 0 && logged++ < 1)
				hooklog::Write("rules: hello to the host (this PC applies the host's rules, version %u)", rules::kProtocol);
			step = (step + 1) % 8;
			if (step == 0)
				next_at = now + 4000;
		}
	}
	static DWORD tips_at = 0;
	if (g_enabled && now - tips_at >= 500) {
		tips_at = now;
		rules::BonusTips tips;
		{
			Lock lock;
			if (g_lobby_known && (g_lobby_flags & rules::kWillApply) && !(g_lobby_flags & rules::kNoStart))
				tips = rules::TipsFor(g_lobby_values);
		}
		rules::SyncBonusTips(logic, tips);
	}
	g_client_update_next(logic, edx);
}

// --- 2. the lobby display: clients -------------------------------------------------------------

int __fastcall ClientMessageHook(void *logic, void *edx, int sender, BYTE *message)
{
	if (message && Read(message, 0) == kLockCommandVftable && rules::IsLobbyRow(Read(message, 0xc))) {
		std::string text;
		if (g_assembler.Add(Read(message, 0xc), Read(message, 0x10), &text) && rules::IsVersionsText(text)) {
			std::vector<rules::Peer> peers;
			const bool read = rules::ReadVersionsText(text, &peers);
			Lock lock;
			if (!g_say_version) {
				// [rules] test_version=old: as a PC before 0.3.0, which never gets one
			}
			else if (read) {
				if (!g_host_list_known || peers != g_host_list)
					hooklog::Write("rules: versions: the host's list: %s", VersionList(peers).c_str());
				g_host_list = peers;
				g_host_list_known = true;
			}
			else if (!g_host_list_unreadable) {
				g_host_list_unreadable = true;
				hooklog::Write("rules: versions: the host sent a list this PC cannot read (\"%.40s...\"): it has another "
				               "(newer) H5 Together",
				               text.c_str());
			}
		}
		else if (!text.empty()) {
			Values values;
			int flags = 0;
			if (rules::ReadLobbyText(text, &flags, &values)) {
				Lock lock;
				const bool changed = !g_lobby_known || values != g_lobby_values || flags != g_lobby_flags;
				g_lobby_known = true;
				g_lobby_values = values;
				g_lobby_flags = flags;
				if (changed)
					hooklog::Write("rules: the host's rules (lobby): %s; %s", ValueList(values).c_str(),
					               (g_lobby_flags & rules::kWillApply) ? "every player has H5 Together's rules: they will be used"
					                                                   : "a player lacks H5 Together's rules: the files' rules will be used");
			}
			else
				hooklog::Write("rules: a lobby message from the host that this PC cannot read (\"%s\"): another version?",
				               text.c_str());
		}
		return 1;  // ours: the game never sees it (it would ignore the row anyway)
	}
	return g_client_message_next(logic, edx, sender, message);
}

// --- 1 and 2: the host ---------------------------------------------------------------------------
int __fastcall HostRequestHook(void *logic, void *edx, int sender, BYTE *message)
{
	if (message && Read(message, 0) == kRequestVftable) {
		const uint32_t type = Read(message, 0x10);
		const uint8_t byte = message[0x14];
		char mark = 0;
		uint32_t hash = 0;
		Lock lock;
		HostLobby(logic);
		if (type == rules::kHelloType || rules::ReadHashRequest(type, byte, &mark, &hash)) {
			Pc *p = FindPc(sender);
			if (!p) {
				g_pcs.push_back(Pc{sender, 0, {}, {}, {}, std::string(), 0, 0, std::string()});
				p = &g_pcs.back();
			}
			if (type == rules::kHelloType) {
				if (p->version != byte)
					hooklog::Write("rules: hello from PC %d (version %u%s)", sender, byte,
					               byte == rules::kProtocol ? "" : ", not this PC's: the files' rules unless it updates");
				p->version = byte;
				p->hello_at = GetTickCount();
				p->name = PcName(reinterpret_cast<const BYTE *>(Read(logic, 0x74)), sender);
			}
			else if (mark == rules::kMarkVersion) {
				if (!p->has_addon_version || p->addon_version != hash)
					hooklog::Write("rules: versions: PC %d has H5 Together %s", sender, rules::UnpackVersion(hash).c_str());
				p->has_addon_version = true;
				p->addon_version = hash;
			}
			else if (mark == rules::kMarkBuild) {
				p->has_addon_build = true;
				p->addon_build = hash;
			}
			else {
				const int k = mark == rules::kMarkSettingsRest    ? 0
				              : mark == rules::kMarkSettingsWhole ? 1
				              : mark == rules::kMarkStats         ? 2
				              : mark == rules::kMarkStartScript   ? 3
				                                                  : 4;
				if (!p->has[k] || p->hash[k] != hash)
					p->told[k] = false;
				p->has[k] = true;
				p->hash[k] = hash;
			}
		}
	}
	return g_host_request_next(logic, edx, sender, message);  // the game ignores these types
}

// Host, under the lock: every player's H5 Together, the host first. A player who
// joined less than 12 s ago and said nothing yet is "waiting" (its hello comes every 4 s).
std::vector<rules::Peer> VersionPeers(const void *logic, const BYTE *info)
{
	const DWORD now = GetTickCount();
	std::vector<rules::Peer> peers;
	rules::Peer host = g_mine;
	host.name = PcName(info, static_cast<int>(Read(logic, 0x34)));
	peers.push_back(host);
	std::vector<std::pair<std::string, DWORD>> seen;
	for (int pc : HumanClients(logic, info)) {
		rules::Peer peer;
		peer.name = PcName(info, pc);
		DWORD since = now;
		for (const auto &s : g_seen)
			if (s.first == peer.name)
				since = s.second;
		seen.push_back({peer.name, since});
		const Pc *p = FindPc(pc);
		const bool fresh = p && p->version != 0 && now - p->hello_at <= 15000 && p->name == peer.name;
		if (fresh && p->has_addon_version) {
			peer.kind = rules::Peer::kKnown;
			peer.version = rules::UnpackVersion(p->addon_version);
			peer.build = p->has_addon_build ? p->addon_build : 0;
		}
		else if (now - since < 12000)
			peer.kind = rules::Peer::kWaiting;
		else
			peer.kind = fresh ? rules::Peer::kOlder : rules::Peer::kNone;
		peers.push_back(peer);
	}
	g_seen = seen;
	return peers;
}

// "Bob 0.2.7 (build 6900a1b2), Ann: older, Eve: none"
std::string VersionList(const std::vector<rules::Peer> &peers)
{
	std::string s;
	for (const rules::Peer &p : peers) {
		char build[16];
		_snprintf_s(build, sizeof(build), _TRUNCATE, "%08x", p.build);
		s += (s.empty() ? "" : ", ") + p.name +
		     (p.kind == rules::Peer::kKnown    ? " " + p.version + " (build " + build + ")"
		      : p.kind == rules::Peer::kOlder  ? ": older than 0.3.0"
		      : p.kind == rules::Peer::kWaiting ? ": not known yet"
		                                        : ": none (or its game rules are off)");
	}
	return s.empty() ? "(nobody)" : s;
}

// Logs the differences when they change (host and client).
void LogVersions(const std::vector<rules::Peer> &differ, int players)
{
	const std::string now = differ.empty() ? std::string() : VersionList(differ);
	if (now == g_versions_logged)
		return;
	g_versions_logged = now;
	if (differ.empty())
		hooklog::Write("rules: versions: every player (%d) has this H5 Together, %s", players, g_mine.version.c_str());
	else
		hooklog::Write("rules: versions: NOT THE SAME H5 TOGETHER on every PC (this one: %s): %s", g_mine.version.c_str(),
		               now.c_str());
}

// Sends a client our lobby text, in pieces: "lock buttons" commands for rows
// that do not exist (a client without the mod ignores them).
void SendLobbyText(void *logic, int pc, const std::string &text, BYTE seq)
{
	void *net = reinterpret_cast<void *>(Read(logic, 0x10));
	if (!net || !Readable(net, 4))
		return;
	const SendToFn send = reinterpret_cast<SendToFn>(Read(reinterpret_cast<void *>(Read(net, 0)), 0x20));
	for (const auto &piece : rules::LobbyPieces(text, seq)) {
		const DWORD mask = piece.second;
		BYTE *command = reinterpret_cast<NewLockCommandFn>(kNewLockCommand)(static_cast<int>(piece.first), &mask);
		if (!command)
			return;
		LONG *count = reinterpret_cast<LONG *>(command + 4);
		++*count;
		send(net, nullptr, pc, command);
		if ((--*count & 0xfffff) == 0)
			reinterpret_cast<ReleaseFn>(kReleaseObject)(command, nullptr, 0xfffff);
	}
}

// The lobby's map (the host logic's map id, +0x80, its path at +0x14): does it
// name a map script? In a multiplayer game MMH5.5's scripts run only on maps
// that do (a random map straight from the generator does not). Read once per map.
void CheckMap(void *logic)
{
	const GameString *p = reinterpret_cast<const GameString *>(static_cast<BYTE *>(logic) + 0x80 + 0x14);
	std::string path;
	if (Readable(p, sizeof(*p)) && p->begin && p->end > p->begin && p->end - p->begin < 400 && Readable(p->begin, p->end - p->begin))
		path.assign(p->begin, p->end);
	{
		Lock lock;
		if (path == g_map_path)
			return;
	}
	std::string file = path.substr(0, path.find('#'));
	while (!file.empty() && (file[0] == '/' || file[0] == '\\'))
		file.erase(0, 1);
	std::string xdb;
	const int script = file.empty() || !ReadInstalled(file.c_str(), &xdb) ? -1 : rules::HasMapScript(xdb) ? 1 : 0;
	Lock lock;
	g_map_path = path;
	g_map_script = script;
	if (!path.empty())
		hooklog::Write(script == 1   ? "rules: the lobby's map %s has a map script: MMH5.5's scripts run on it"
		               : script == 0 ? "rules: the lobby's map %s has NO map script: in a multiplayer game MMH5.5's scripts run on "
		                               "it only with map_scripts=1 (else these rules change nothing there)"
		                             : "rules: the lobby's map %s could not be read",
		               path.c_str());
}

void __fastcall HostUpdateHook(void *logic, void *edx)
{
	const DWORD now = GetTickCount();
	static DWORD last = 0;
	if (g_enabled)
		ClassSkillsInLobby();
	if (g_enabled && now - last >= 500) {
		last = now;
		LoadFiles();
		CheckMap(logic);
		const BYTE *info = reinterpret_cast<const BYTE *>(Read(logic, 0x74));
		struct Send {
			int pc;
			std::string text;
			BYTE seq;
		};
		std::vector<Send> sends;
		rules::BonusTips tips;
		{
			Lock lock;
			HostLobby(logic);
			g_host_seen = now;
			g_client_seen = 0;
			if (g_in_game)
				g_in_game = false;
			CheckPlayers(logic, info, true);
			const Values values = Effective();
			if (g_will_apply && g_start_ok)
				tips = rules::TipsFor(values);
			g_peers = VersionPeers(logic, info);
			const std::string versions = rules::VersionsText(g_peers);
			if (g_peers.size() > 1 && g_say_version) {
				bool waiting = false;
				for (const rules::Peer &peer : g_peers)
					waiting = waiting || peer.kind == rules::Peer::kWaiting;
				if (!waiting)
					LogVersions(rules::Differing(std::vector<rules::Peer>(g_peers.begin() + 1, g_peers.end()), g_mine),
					            static_cast<int>(g_peers.size()));
			}
			for (Pc &p : g_pcs) {
				if (p.version != rules::kProtocol)
					continue;
				int flags = g_will_apply ? rules::kWillApply : 0;
				if (g_map_script == 0)
					flags |= rules::kNoScript;
				if (StartRulesChanged(values) && !g_start_ok)
					flags |= rules::kNoStart;
				if (ClassRuleChanged(values) && !g_class_ok)
					flags |= rules::kNoClasses;
				const uint32_t mine[3] = {g_hash_rest, g_hash_whole, g_hash_stats};
				if (g_file_known && ((p.has[0] && p.hash[0] != mine[0]) || (p.has[2] && p.hash[2] != mine[2])))
					flags |= rules::kDiffers;
				const std::string text = rules::LobbyText(flags, values);
				if (text != p.sent) {
					p.sent = text;
					sends.push_back({p.sender, text, ++p.seq});
				}
				// the version list, to a client that said its own (one before 0.3.0 never gets it)
				if (g_say_version && p.has_addon_version && versions != p.sent_versions) {
					p.sent_versions = versions;
					sends.push_back({p.sender, versions, ++p.seq});
				}
			}
		}
		for (const Send &s : sends) {
			SendLobbyText(logic, s.pc, s.text, s.seq);
			hooklog::Write("rules: the host's rules sent to PC %d for its lobby: %s", s.pc, s.text.c_str());
		}
		rules::SyncBonusTips(logic, tips);
	}
	else if (g_enabled) {
		Lock lock;
		g_host_seen = now;
	}
	g_host_update_next(logic, edx);
}

// --- 3. the start ------------------------------------------------------------------------------
void __fastcall HostStartHook(void *logic, void *edx)
{
	if (g_enabled) {
		LoadFiles();
		const BYTE *info = reinterpret_cast<const BYTE *>(Read(logic, 0x74));
		Lock lock;
		HostLobby(logic);
		CheckPlayers(logic, info, true);
		Values values = Effective();
		if (g_will_apply && StartRulesChanged(values) && !g_start_ok) {
			hooklog::Write("rules: START: the game mode and ToE start bonuses stay MMH5.5's way (the bonus arrows choose the "
			               "mode): %s",
			               g_start_why.c_str());
			for (int r = 0; r < kRuleCount; ++r)
				if (rules::IsStartRule(r))
					values[r] = rules::kStartRuleOff;
		}
		if (g_will_apply && ClassRuleChanged(values) && !g_class_ok) {
			hooklog::Write("rules: START: the class skills stay MMH5.5's lock: %s", g_class_why.c_str());
			values[rules::kClassSkills] = rules::kClassLocked;
		}
		if (!g_will_apply) {
			std::string names;
			for (const std::string &n : g_missing)
				names += (names.empty() ? "" : ", ") + n;
			g_start_text.clear();
			g_armed = false;
			hooklog::Write("rules: START: %s without H5 Together's rules: every PC uses its own H55-Settings.lua (the "
			               "host's choices are not used in this game)",
			               names.c_str());
		}
		else {
			g_start_text = rules::Encode(values);
			g_armed = true;
			g_armed_values = values;
			std::string differ;
			for (const rules::Differ &d : g_differ)
				differ += "; files differ: " + d.player + ": " + kDifferWhat[d.what];
			hooklog::Write("rules: START: every player has H5 Together's rules: this game uses %s (the host's choices: %s)%s",
			               ValueList(values).c_str(), ValueList(g_chosen).c_str(), differ.c_str());
		}
	}
	g_host_start_next(logic, edx);
}

// The start message's own fields, then ours (a chunk the game's reader skips).
int __fastcall StartSerializeHook(BYTE *message, void *edx, void *saver)
{
	const int r = g_serialize_next(message, edx, saver);
	if (!g_enabled || !saver || !Readable(saver, 4))
		return r;
	void **vt = *reinterpret_cast<void ***>(saver);
	const bool reading = reinterpret_cast<IsReadingFn>(vt[15])(saver, nullptr) != 0;
	const DataChunkFn chunk = reinterpret_cast<DataChunkFn>(vt[10]);
	char buf[kChunkSize];
	memset(buf, 0, sizeof(buf));
	if (!reading) {
		std::string text;
		{
			Lock lock;
			text = g_start_text;
		}
		if (text.empty() || text.size() >= sizeof(buf))
			return r;
		memcpy(buf, text.c_str(), text.size());
		chunk(saver, nullptr, kChunk, buf, kChunkSize, 1);
		static LONG logged = 0;
		if (InterlockedIncrement(&logged) <= 10)
			hooklog::Write("rules: the start message carries the host's rules (%s)", text.c_str());
		return r;
	}
	chunk(saver, nullptr, kChunk, buf, kChunkSize, 1);  // all zeros when the host sent none
	buf[kChunkSize - 1] = 0;
	Values values;
	Lock lock;
	if (buf[0] && rules::Decode(buf, &values)) {
		g_armed = true;
		g_armed_values = values;
		hooklog::Write("rules: the host's start message: this game uses %s", ValueList(values).c_str());
	}
	else {
		g_armed = false;
		hooklog::Write(buf[0] ? "rules: the host's start message has rules this PC cannot read (\"%.60s\"): the file's rules"
		                      : "rules: the host's start message has no rules%.0s: every PC uses its own H55-Settings.lua",
		               buf);
	}
	return r;
}

// --- 4. a game starts -------------------------------------------------------------------------
void __fastcall RunMpHook(void *self, void *edx)
{
	std::string classes, chance;
	{
		Lock lock;
		g_serving = g_armed;
		g_serving_values = g_armed ? g_armed_values : rules::NoValues();
		g_armed = false;
		g_in_game = true;
		g_served = 0;
		hooklog::Write("rules: a multiplayer game starts: %s",
		               g_serving ? ("the host's rules, " + ValueList(g_serving_values)).c_str() : "the file's rules");
		if (g_serving) {
			classes = g_serving_values[rules::kClassSkills];
			chance = g_serving_values[rules::kClassChance];
		}
	}
	// the class table before the world is made (a hero's level-ups may come with it). This is
	// always a new game: a multiplayer load goes through NMP::CMPLoad, not CRunMPGame (and a
	// save's world, when it is read, sets the class skills itself)
	if (g_enabled) {
		rules::SetClassSkills(classes, chance, false, "a multiplayer game starts");
		rules::ForgetWorldRead();
	}
	InterlockedExchange(&g_mp_world, 1);  // the world is made inside (0x00820830 -> 0x00c9fa80)
	g_run_mp_next(self, edx);
	InterlockedExchange(&g_mp_world, 0);
}

// Testing (the robot; never for players): [debug] script_at runs a line of script on every PC
// (coop_features.cpp), which needs the world's scripts. A multiplayer map without a map script
// has none (every map of the 2008 game), so with script_at set it gets the empty one, as
// map_scripts=1 gives (the same on every robot copy: they share the setting).
bool TestScriptsWanted()
{
	static int wanted = -1;
	if (wanted < 0)
		wanted = static_cast<int>(GetPrivateProfileIntW(L"debug", L"script_at", -1, IniPath().c_str())) >= 0 ? 1 : 0;
	return wanted == 1;
}

// --- 6. the world starts its scripts (map_scripts) ----------------------------------------------
void __fastcall WorldScriptsHook(void *world, void *edx, const void *map_script)
{
	const bool mp = g_mp_world != 0;
	std::string value;
	{
		Lock lock;
		if (mp && g_serving)
			value = g_serving_values[rules::kMapScripts];
	}
	rules::MapScriptAction action = rules::MapScriptActionFor(map_script != nullptr, value);
	if (!map_script && mp && action != rules::kEmptyScript && TestScriptsWanted()) {
		hooklog::Write("rules: the world starts its scripts: testing ([debug] script_at): an empty map script, so a line of script "
		               "can run");
		action = rules::kEmptyScript;
	}
	const int own = OwnMapScripts();
	static LONG logged = 0;
	const bool log = InterlockedIncrement(&logged) <= 30;
	if (action == rules::kEmptyScript && NameReadForm() == 0) {
		// checked at start-up; changed since (an unknown MMH5.5 patch): the empty script is not safe
		hooklog::Write("rules: the world starts its scripts: map_scripts=1, but the game's script start changed since "
		               "start-up: FAILED, no scripts on this map (every PC with the same MMH5.5 does the same)");
		action = rules::kNoScripts;
	}
	if (action == rules::kEmptyScript) {
		if (log)
			hooklog::Write("rules: the world starts its scripts: this map has no map script, and this game's rules say "
			               "map_scripts=1: it gets an empty one, so MMH5.5's scripts run (as on a map from MMH5.5's Mapmixer)");
		g_world_scripts_next(world, edx, &g_empty_script);
		return;
	}
	if (action == rules::kNoScripts) {
		if (log)
			hooklog::Write("rules: the world starts its scripts: this map has no map script, and this game's rules say "
			               "map_scripts=0: no scripts, as in the original game%s",
			               own == 1 ? " (this PC's MMH5.5 would run them)" : "");
		return;  // what the game itself does without a map script (0x00992d00 returns at its jump)
	}
	if (log) {
		if (map_script)
			hooklog::Write("rules: the world starts its scripts: the map has its own map script: the game's own way");
		else if (!mp)
			hooklog::Write("rules: the world starts its scripts: no map script, not a multiplayer game's start (single "
			               "player, or a random map being rebuilt): the game's own way (single player runs MMH5.5's scripts "
			               "from the adventure screen)");
		else
			hooklog::Write("rules: the world starts its scripts: no map script, no host's rules in this game: MMH5.5's own way "
			               "(%s)",
			               own == 1 ? "this PC's MMH5.5 runs its scripts" : "MMH5.5's scripts do not run");
	}
	g_world_scripts_next(world, edx, map_script);
}

void __fastcall RunSingleHook(void *self, void *edx)
{
	std::string classes, chance;
	{
		Lock lock;
		if (g_serving || g_armed)
			hooklog::Write("rules: a single-player game starts: the file's rules");
		g_serving = false;
		g_armed = false;
		g_start_text.clear();
		g_in_game = true;
		if (g_class_single) {  // testing: this PC's choices for the class rules
			classes = g_chosen[rules::kClassSkills];
			chance = g_chosen[rules::kClassChance];
		}
	}
	// a new game (a single-player load does not come here: robot run 20261102-011559)
	if (g_enabled) {
		rules::SetClassSkills(classes, chance, false,
		                      g_class_single ? "a single-player game starts (class_skills_single=1: this PC's choice)"
		                                     : "a single-player game starts");
		rules::ForgetWorldRead();
	}
	g_run_single_next(self, edx);
}

void ReadChoices()
{
	const std::wstring ini = IniPath();
	Values chosen = rules::NoValues();
	for (int r = 0; r < kRuleCount; ++r) {
		wchar_t text[32] = L"";
		const std::wstring key(kRules[r].key, kRules[r].key + strlen(kRules[r].key));
		GetPrivateProfileStringW(L"rules", key.c_str(), L"", text, 32, ini.c_str());
		const std::string value = Narrow(text);
		if (value.empty())
			continue;
		if (rules::ValidValue(kRules[r], value))
			chosen[r] = value;
		else
			hooklog::Write("rules: [rules] %s=%s is not a value it can take: the file's", kRules[r].key, value.c_str());
	}
	Lock lock;
	g_chosen = chosen;
}

}  // namespace

// --- the panel's side -----------------------------------------------------------------------------
namespace rules {

View::Role LobbyRole()
{
	const DWORD now = GetTickCount();
	Lock lock;
	if (g_host_seen && now - g_host_seen < 1500)
		return View::kHost;
	if (g_client_seen && now - g_client_seen < 1500)
		return View::kClient;
	return g_in_game ? View::kInGame : View::kNoGame;
}

View GetView(View::Role role)
{
	if (role != View::kInGame)
		LoadFiles();
	View v;
	Lock lock;
	v.role = role;
	v.file_known = g_file_known;
	v.file = g_file_values;
	v.chosen = g_chosen;
	if (role == View::kHost || role == View::kSetup || role == View::kNoGame) {
		v.shown_known = true;
		v.shown = role == View::kNoGame ? g_file_values : Effective();
		if (role == View::kHost) {
			v.will_apply = g_will_apply;
			v.map_script = g_map_script;
			v.missing = g_missing;
			v.differ = g_differ;
			v.start_ok = g_start_ok;
			v.start_why = g_start_why_code;
			v.class_ok = g_class_ok;
			v.class_why = g_class_why_code;
		}
		else {  // the setup screen: only this PC's own start script and class table are known
			v.start_ok = !g_start_known || g_start_patchable;
			v.start_why.code = g_start_missing ? rules::Why::kStartMissing : rules::Why::kStartThisPc;
			const rules::ClassInfo classes = rules::ClassTableInfo();
			v.class_ok = !classes.read || classes.editable;
			v.class_why.code = rules::Why::kClassThisPc;
		}
	}
	else if (role == View::kClient) {
		v.shown_known = g_lobby_known;
		v.shown = g_lobby_values;
		v.will_apply = g_lobby_known && (g_lobby_flags & rules::kWillApply);
		v.this_differs = g_lobby_known && (g_lobby_flags & rules::kDiffers);
		v.map_script = !g_lobby_known ? -1 : (g_lobby_flags & rules::kNoScript) ? 0 : 1;
		v.start_ok = !(g_lobby_known && (g_lobby_flags & rules::kNoStart));
		v.class_ok = !(g_lobby_known && (g_lobby_flags & rules::kNoClasses));
	}
	else if (role == View::kInGame) {
		v.in_game_rules = g_serving;
		v.shown_known = true;
		v.shown = g_serving ? g_serving_values : g_file_values;
		// the class skills as the table is now (a loaded save sets them itself)
		const rules::ClassState classes = rules::ClassSkillsNow();
		v.shown[rules::kClassSkills] = classes.mode;
		v.shown[rules::kClassChance] = classes.mode == rules::kClassLocked ? v.shown[rules::kClassChance] : classes.chance;
		v.class_from_save = classes.from_save;
	}
	return v;
}

VersionsView GetVersions(View::Role role)
{
	VersionsView v;
	Lock lock;
	v.mine = g_mine.version;
	if (role == View::kHost && g_say_version) {
		v.players = static_cast<int>(g_peers.size());
		bool waiting = false;
		for (const Peer &p : g_peers)
			waiting = waiting || p.kind == Peer::kWaiting;
		if (g_peers.size() > 1)
			v.differ = Differing(std::vector<Peer>(g_peers.begin() + 1, g_peers.end()), g_mine);
		v.state = !v.differ.empty() ? VersionsView::kDiffer : g_peers.size() > 1 && !waiting ? VersionsView::kSame : VersionsView::kNothing;
	}
	else if (role == View::kClient && g_say_version) {
		if (g_host_list_known) {
			v.players = static_cast<int>(g_host_list.size());
			v.differ = Differing(g_host_list, g_mine);
			bool waiting = false;
			for (const Peer &p : g_host_list)
				waiting = waiting || p.kind == Peer::kWaiting;
			v.state = !v.differ.empty() ? VersionsView::kDiffer : !waiting ? VersionsView::kSame : VersionsView::kNothing;
		}
		else if (g_host_list_unreadable)
			v.state = VersionsView::kHostNewer;
		else if (g_client_lobby_at && GetTickCount() - g_client_lobby_at > 15000)
			v.state = g_lobby_known ? VersionsView::kHostOlder : VersionsView::kHostNone;
		if (v.state == VersionsView::kDiffer)
			LogVersions(v.differ, v.players);
		else if (v.state == VersionsView::kSame)
			LogVersions(v.differ, v.players);
		else if ((v.state == VersionsView::kHostOlder || v.state == VersionsView::kHostNone) && g_versions_logged != "host") {
			g_versions_logged = "host";
			hooklog::Write("rules: versions: no list from the host after 15 s: %s", v.state == VersionsView::kHostOlder
			                                                                            ? "it has an H5 Together older than 0.3.0"
			                                                                            : "it has no H5 Together (or its game rules are off)");
		}
	}
	return v;
}

void SetChoices(const Values &chosen)
{
	Values clean = NoValues();
	std::string list;
	{
		Lock lock;
		for (int r = 0; r < kRuleCount; ++r)
			if (static_cast<size_t>(r) < chosen.size() && ValidValue(kRules[r], chosen[r]) &&
			    !(g_file_known && chosen[r] == g_file_values[r]))
				clean[r] = chosen[r];
		g_chosen = clean;
		list = ValueList(clean);
	}
	const std::wstring ini = IniPath();
	for (int r = 0; r < kRuleCount; ++r) {
		const std::wstring key(kRules[r].key, kRules[r].key + strlen(kRules[r].key));
		const std::wstring value(clean[r].begin(), clean[r].end());
		WritePrivateProfileStringW(L"rules", key.c_str(), clean[r].empty() ? nullptr : value.c_str(), ini.c_str());
	}
	hooklog::Write("rules: the host's choices: %s (saved in h5-together.ini [rules])", list.c_str());
}

int PanelKey()
{
	return g_key;
}

bool PanelButton()
{
	return g_button;
}

}  // namespace rules

void InstallHostRulesEarly()
{
	// The panel is an overlay client: the Direct3D import must go in before the
	// game makes its device (as the other panels do).
	if (GetPrivateProfileIntW(L"rules", L"enabled", 1, IniPath().c_str()) != 0)
		overlay::InstallEarly();
}

void ApplyHostRulesFromSettings()
{
	InitializeCriticalSection(&g_lock);
	const std::wstring ini = IniPath();
	g_enabled = GetPrivateProfileIntW(L"rules", L"enabled", 1, ini.c_str()) != 0;
	g_trace = GetPrivateProfileIntW(L"rules", L"trace", 0, ini.c_str()) != 0;
	g_class_single = GetPrivateProfileIntW(L"rules", L"class_skills_single", 0, ini.c_str()) != 0;
	g_button = GetPrivateProfileIntW(L"rules", L"button", 1, ini.c_str()) != 0;
	{
		g_mine.kind = rules::Peer::kKnown;
		g_mine.version = rules::UnpackVersion(rules::PackVersion(OPEN_HOMM5_VERSION));
		const IMAGE_NT_HEADERS32 *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(
			reinterpret_cast<const BYTE *>(&__ImageBase) + __ImageBase.e_lfanew);
		g_mine.build = nt->FileHeader.TimeDateStamp;
		wchar_t test[32] = L"";
		GetPrivateProfileStringW(L"rules", L"test_version", L"", test, 32, ini.c_str());
		const std::string fake = Narrow(test);
		if (fake == "old") {
			g_say_version = false;
			hooklog::Write("rules: versions: test: this PC says no version in the lobby, as one before 0.3.0 ([rules] test_version=old)");
		}
		else if (!fake.empty() && rules::PackVersion(fake.c_str())) {
			g_mine.version = rules::UnpackVersion(rules::PackVersion(fake.c_str()));
			hooklog::Write("rules: versions: test: this PC says it has H5 Together %s ([rules] test_version)", g_mine.version.c_str());
		}
	}
	wchar_t key[16];
	GetPrivateProfileStringW(L"rules", L"key", L"F2", key, 16, ini.c_str());
	g_key = 0;
	if ((key[0] == L'F' || key[0] == L'f') && _wtoi(key + 1) >= 1 && _wtoi(key + 1) <= 12)
		g_key = VK_F1 + _wtoi(key + 1) - 1;
	if (!g_enabled) {
		hooklog::Write("rules: off (enabled=0): no hello, every game uses this PC's H55-Settings.lua");
		return;
	}
	// The 2008 game without MMH5.5: MMH5.5's rules have nothing to act on. The hellos stay
	// (the waiting room's versions line); no choice is read, so every game has none.
	if (GameHasMmh55())
		ReadChoices();
	else
		hooklog::Write("rules: the 2008 game without MMH5.5: MMH5.5's rules do not apply here (the host's [rules] choices are "
		               "not used; no F2 panel, no Rules... button); the waiting room's versions line stays");
	// class_skills: the world's serializer (saves keep the rule) and the testing trace
	rules::InstallClassSkills(GetPrivateProfileIntW(L"rules", L"trace_skills", 0, ini.c_str()) != 0);
	g_open_next = reinterpret_cast<OpenFn>(
		hookpatch::PatchPointer(kWinVfsOpenSlot, kWinVfsOpen, reinterpret_cast<const void *>(&OpenFileHook)));
	g_host_update_next = reinterpret_cast<LogicFn>(hookpatch::PatchPointer(kHostUpdateSlot, kHostUpdate, &HostUpdateHook));
	g_host_request_next = reinterpret_cast<MessageFn>(hookpatch::PatchPointer(kHostRequestSlot, kHostRequest, &HostRequestHook));
	g_client_update_next = reinterpret_cast<LogicFn>(hookpatch::PatchPointer(kClientUpdateSlot, kClientUpdate, &ClientUpdateHook));
	g_client_message_next =
		reinterpret_cast<MessageFn>(hookpatch::PatchPointer(kClientMessageSlot, kClientMessage, &ClientMessageHook));
	g_host_start_next = reinterpret_cast<LogicFn>(hookpatch::RedirectCall(kHostStartCall, kHostStart, &HostStartHook));
	g_serialize_next = reinterpret_cast<SerializeFn>(hookpatch::PatchPointer(kStartSerializeSlot, kStartSerialize, &StartSerializeHook));
	g_run_mp_next = reinterpret_cast<RunFn>(hookpatch::PatchPointer(kRunMpSlot, kRunMp, &RunMpHook));
	g_run_single_next = reinterpret_cast<RunFn>(hookpatch::PatchPointer(kRunSingleSlot, kRunSingle, &RunSingleHook));
	// map_scripts: the world's script start, with the bytes our empty script relies on
	// (kWorldScriptsCode ends with its jump to kScriptStart: e9 51 eb ff ff)
	const int name_read = NameReadForm();
	const bool script_code = CodeIs(kWorldScripts, kWorldScriptsCode, sizeof(kWorldScriptsCode)) &&
	                         CodeIs(kNullTestAt, kNullTest, sizeof(kNullTest)) && name_read != 0;
	if (script_code) {
		g_empty_script.name_begin = g_empty_name;
		g_empty_script.name_end = g_empty_name;
		g_empty_script.name_cap = g_empty_name;
		g_world_scripts_next = reinterpret_cast<WorldScriptsFn>(
			hookpatch::PatchPointer(kWorldScriptsSlot, kWorldScripts, reinterpret_cast<const void *>(&WorldScriptsHook)));
	}
	if (!g_world_scripts_next)
		hooklog::Write("rules: map_scripts: %s (slot 0x%08x holds 0x%08x; code %d%d%d)",
		               script_code ? "the world's script slot could not be wrapped" : "the world's script start is not as expected",
		               kWorldScriptsSlot, Read(reinterpret_cast<const void *>(kWorldScriptsSlot), 0),
		               CodeIs(kWorldScripts, kWorldScriptsCode, sizeof(kWorldScriptsCode)), CodeIs(kNullTestAt, kNullTest, sizeof(kNullTest)),
		               name_read);
	else
		hooklog::Write("rules: map_scripts: the world's script start is as expected (its name read: %s)",
		               name_read == 1 ? "the game's own" : "through MMH5.5 RC19k's 55u.dll, the same for a script");
	const bool all = g_open_next && g_host_update_next && g_host_request_next && g_client_update_next && g_client_message_next &&
	                 g_host_start_next && g_serialize_next && g_run_mp_next && g_run_single_next && g_world_scripts_next;
	if (!all) {
		// Whatever went in stays harmless: with g_enabled off the hooks only pass through (the world's
		// script hook acts only on this game's rules, which a PC without the hello never gets).
		g_enabled = false;
		hooklog::Write("rules: the game's code is not as expected (%d%d%d%d%d%d%d%d%d%d): off, every game uses this PC's "
		               "H55-Settings.lua",
		               !!g_open_next, !!g_host_update_next, !!g_host_request_next, !!g_client_update_next,
		               !!g_client_message_next, !!g_host_start_next, !!g_serialize_next, !!g_run_mp_next, !!g_run_single_next,
		               !!g_world_scripts_next);
		return;
	}
	const bool panel = rules::InstallPanel();
	std::string chosen;
	{
		Lock lock;
		chosen = ValueList(g_chosen);
	}
	hooklog::Write("rules: on: the host's rules for every player (version %u); this PC's choices as host: %s; panel %s",
	               rules::kProtocol, chosen.c_str(), panel ? "on" : "off (no overlay)");
}

}  // namespace mods
