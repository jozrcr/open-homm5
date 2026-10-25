// Mod: quicker autosaves in multiplayer games (docs/subsystems/save.md).
//
// The owner's problem: in co-op games, the autosave at the start of each round
// freezes every PC for a long time.
//
// How the game autosaves (addresses in the vanilla 3.1 exe; checked with the
// robot, see save.md):
// 1. The trigger. When a new day begins, the frame update of
//    NUI::CAdventureController (0x006082f0, at 0x006087d7..0x00608865) reads
//    the autosave setting: in a multiplayer game the lobby's option (world
//    settings +0x38, from the game option "autosave_enabled", the setup
//    screen's checkbox named "realtime"), otherwise the console variable
//    autosave (0x00fd9e82). It goes on only on the PC whose player is the first
//    living human (0x00603ca0), so ONE PC asks per round. It calls the saves
//    creator (NUI::CSavesCreator at controller +0xb0), vtable slot 7 =
//    0x007f9e10 (cell 0x00e462e0), with the transceiver and the key
//    "autosavename" (0x00e299c0). A second trigger (0x00607d50, at
//    0x00607ee9..0x00607f1d) needs the console variable and world settings
//    +0x1c; it passes the same key. (Not seen in robot games.)
// 2. The request. In a multiplayer game 0x007f9e10 does not save itself: it
//    looks for the oldest of "Autosave 1..3" (each name lookup reads the
//    header of every save in the folder), builds an NMP::CMPSaveWorldCmd
//    (0x008236d0: a new GUID from CoCreateGuid, the name) and sends it through
//    the transceiver (slot 0).
// 3. The save. CMPSaveWorldCmd::Execute (0x008256a0, cell 0x00e487bc) runs on
//    every PC inside the lockstep's segment runner, so everyone waits for the
//    slowest PC. The save proper (0x00822950, called at 0x00825735) checks the
//    free disk space, generates the file name (0x006f8e70: it reads the header
//    of every save in the folder again), writes the header, then the world:
//    every object is serialized and fed to zlib (deflateInit_ called from the
//    save stream's constructor 0x00d10310 with level -1, zlib's default 6),
//    and the result goes to a memory-mapped file.
//
// Measured (save.md): the folder lookups are most of the freeze. The request
// reads every multiplayer save's header 4 times, each save on every PC twice.
// Reading one builds its game setup, whose constructor computes the hero pool
// (0x00c9f4a0, every hero against the map's objects): ~85 ms per big-map
// save. With the owner's 17 multiplayer saves in the folder, a round froze
// for ~10 s; zlib was ~0.1 s of it.
//
// What this mod does, settings in open-homm5.ini [save]:
//   trace_autosave=1   one log line per request (which PC asks, the folder
//                      lookups) and per save (which PC, how long, each phase,
//                      the zlib time and sizes, the file flushes).
//   compression=1      the zlib level of saves (0..9; -1 = the game's 6). Only
//                      the save stream's call of deflateInit_ changes (return
//                      address 0x00d10359); the format is the same, so the game
//                      loads the files as before. Applies to every save.
//   autosave_every=1   autosave only on days 1, 1+N, 1+2N... (7: the first day
//                      of each week). The day (world +0x48, CWorld slot 0xa8,
//                      0 on the first day) is part of the game world, so every
//                      PC with the mod decides the same.
//   fast_save_list=1   during the autosave request and the multiplayer saves,
//                      the folder's header reads skip the hero pool step
//                      (see "fast_save_list" below): ~10 s -> ~0.2 s a round.
// A save does not change the game world, so none of this can desync a game.
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "autosave.h"

#include "../hook/log.h"
#include "../hook/patch.h"

#pragma intrinsic(_ReturnAddress)

namespace mods {
namespace {

const DWORD kSenderSlot = 0x00e462e0;   // NUI::CSavesCreator (vtable 0x00e462c4), slot 7
const DWORD kSender = 0x007f9e10;       // __thiscall(ITransceiver *, const char *key), ret 8
const DWORD kExecuteSlot = 0x00e487bc;  // NMP::CMPSaveWorldCmd (vtable 0x00e487a0), slot 7
const DWORD kExecute = 0x008256a0;      // __thiscall(), returns al = 1
const DWORD kSaveCall = 0x00825735;     // in Execute: call 0x00822950, __fastcall(ecx, edx), returns al
const DWORD kSave = 0x00822950;
const DWORD kAutosaveKey = 0x00e299c0;        // "autosavename", pushed by both triggers
const DWORD kDayTriggerReturn = 0x00608868;   // after call [edx+0x1c] at 0x00608865
const DWORD kTurnTriggerReturn = 0x00607f20;  // after call [edx+0x1c] at 0x00607f1d
const DWORD kMpTransceiverVftable = 0x00e43f1c;  // NWorld::CMultiPlayerTransceiver
const DWORD kOwnStepSlot = kMpTransceiverVftable + 4 * 4;
const DWORD kOwnStep = 0x007ca990;
const DWORD kWorldVftable = 0x00e6260c;  // NWorld::CWorld
const DWORD kWorldDaySlot = kWorldVftable + 0xa8;
const DWORD kWorldDay = 0x00c61ab0;  // lea eax, [ecx+0x48]; ret
const DWORD kDeflateInitCall = 0x00d10353;  // call [deflateInit_] in the save stream (0x00d10310)
const DWORD kGuidCall = 0x008236de;         // call [CoCreateGuid] in 0x008236d0 (the request)
const BYTE kDeflateInitCallBytes[] = {0xff, 0x15, 0x68, 0xa7, 0xe0, 0x00};
const BYTE kGuidCallBytes[] = {0xff, 0x15, 0x3c, 0xa7, 0xe0, 0x00};
const BYTE kWorldDayBytes[] = {0x8d, 0x41, 0x48, 0xc3};
// fast_save_list: the hero pool step of the setup constructor 0x00ca0240.
const DWORD kHeroPoolArgs = 0x00ca0af2;  // mov edx, esi (the setup); mov ecx, edi (the map); call 0x00c9f4a0
const DWORD kHeroPoolCall = kHeroPoolArgs + 4;
const DWORD kHeroPool = 0x00c9f4a0;  // __fastcall(map, setup), plain ret; writes only globals
const DWORD kListingRead = 0x007f3207;  // in the folder scan 0x007f30d0: call 0x007e67b0 (one save's info)
const DWORD kListingReadReturn = kListingRead + 5;
const BYTE kHeroPoolArgsBytes[] = {0x8b, 0xd6, 0x8b, 0xcf, 0xe8, 0xa5, 0xe9, 0xff, 0xff};
const BYTE kHeroPoolBytes[] = {0x83, 0xec, 0x38, 0x53, 0x55, 0x56, 0x8b, 0xf2, 0x33, 0xdb, 0x3b, 0xf3, 0x8b, 0xe9};
const BYTE kListingReadBytes[] = {0xe8, 0xa4, 0x35, 0xff, 0xff};

bool g_trace = false;
int g_level = -1;  // -1 = leave the game's
int g_every = 1;
bool g_fast_list = false;
bool g_level_ok = false;  // the save stream's call site is the expected one
bool g_guid_ok = false;
bool g_day_ok = false;

// --- timing and the save streams' zlib work -----------------------------------
LONGLONG g_freq = 1;

LONGLONG Now()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

double Ms(LONGLONG ticks)
{
	return ticks * 1000.0 / g_freq;
}

struct Stats {
	LONGLONG deflate_ticks;  // inside deflate() of save streams
	LONGLONG file_ticks;     // inside FlushViewOfFile / UnmapViewOfFile during a save
	ULONGLONG bytes_in, bytes_out;
	LONG streams, file_calls;
	int level;  // the level of the last save stream (6 for the game's -1)
};
Stats g_stats;
// During the current save: the first stream opened, the last one closed, and
// the save proper (0x00822950).
LONGLONG g_first_init = 0, g_last_end = 0, g_save_begin = 0, g_save_end = 0;

// The zlib streams the save stream opened (z_stream pointers). Written on the
// main thread only; other threads' streams (the game has two other users of
// deflate) are only looked up, and a stale look-up only skews the statistics.
const int kStreams = 8;
void *volatile g_streams[kStreams];

bool Tracked(const void *strm)
{
	for (int i = 0; i < kStreams; ++i)
		if (g_streams[i] == strm)
			return true;
	return false;
}

// A save is running on this thread (Execute, or a local save in the sender).
volatile DWORD g_saving_thread = 0;

bool Saving()
{
	return g_saving_thread != 0 && g_saving_thread == GetCurrentThreadId();
}

typedef int(__cdecl *DeflateInitFn)(void *strm, int level, const char *version, int stream_size);
typedef int(__cdecl *DeflateFn)(void *strm, int flush);
typedef int(__cdecl *DeflateEndFn)(void *strm);
DeflateInitFn g_deflate_init = nullptr;
DeflateFn g_deflate = nullptr;
DeflateEndFn g_deflate_end = nullptr;

int __cdecl DeflateInitHook(void *strm, int level, const char *version, int stream_size)
{
	if (reinterpret_cast<DWORD>(_ReturnAddress()) != kDeflateInitCall + 6)
		return g_deflate_init(strm, level, version, stream_size);
	if (g_level_ok && g_level >= 0 && level == -1)
		level = g_level;
	const int result = g_deflate_init(strm, level, version, stream_size);
	if (g_trace && result == 0) {
		for (int i = 0; i < kStreams; ++i) {
			if (!g_streams[i]) {
				g_streams[i] = strm;
				break;
			}
		}
		g_stats.level = level < 0 ? 6 : level;
		if (Saving() && !g_first_init)
			g_first_init = Now();
	}
	return result;
}

int __cdecl DeflateHook(void *strm, int flush)
{
	if (!Tracked(strm))
		return g_deflate(strm, flush);
	const LONGLONG t0 = Now();
	const int result = g_deflate(strm, flush);
	g_stats.deflate_ticks += Now() - t0;
	return result;
}

int __cdecl DeflateEndHook(void *strm)
{
	if (strm && Tracked(strm)) {
		// z_stream (zlib 1.2.3): total_in at +0x08, total_out at +0x14
		g_stats.bytes_in += *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(strm) + 0x08);
		g_stats.bytes_out += *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(strm) + 0x14);
		g_stats.streams++;
		for (int i = 0; i < kStreams; ++i)
			if (g_streams[i] == strm)
				g_streams[i] = nullptr;
		const int result = g_deflate_end(strm);
		if (Saving())
			g_last_end = Now();
		return result;
	}
	return g_deflate_end(strm);
}

// The save file is a memory-mapped file: the time in these is the part of the
// writing that Windows does while the game waits.
typedef BOOL(WINAPI *FlushViewFn)(LPCVOID address, SIZE_T size);
typedef BOOL(WINAPI *UnmapViewFn)(LPCVOID address);
FlushViewFn g_flush_view = nullptr;
UnmapViewFn g_unmap_view = nullptr;

BOOL WINAPI FlushViewHook(LPCVOID address, SIZE_T size)
{
	if (!Saving())
		return g_flush_view(address, size);
	const LONGLONG t0 = Now();
	const BOOL result = g_flush_view(address, size);
	g_stats.file_ticks += Now() - t0;
	g_stats.file_calls++;
	return result;
}

BOOL WINAPI UnmapViewHook(LPCVOID address)
{
	if (!Saving())
		return g_unmap_view(address);
	const LONGLONG t0 = Now();
	const BOOL result = g_unmap_view(address);
	g_stats.file_ticks += Now() - t0;
	g_stats.file_calls++;
	return result;
}

// The request's GUID (its first 32 bits), to match a save with its request in
// the logs of the PCs.
typedef HRESULT(WINAPI *CoCreateGuidFn)(GUID *guid);
CoCreateGuidFn g_co_create_guid = nullptr;
volatile DWORD g_last_guid = 0;

HRESULT WINAPI CoCreateGuidHook(GUID *guid)
{
	const HRESULT result = g_co_create_guid(guid);
	if (g_guid_ok && reinterpret_cast<DWORD>(_ReturnAddress()) == kGuidCall + 6 && SUCCEEDED(result) && guid)
		g_last_guid = guid->Data1;
	return result;
}

// --- the game's state -----------------------------------------------------------
DWORD Read(const void *base, DWORD offset)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(base) + offset);
}

// The game's day number (1 = the first day; the world counts from 0 at +0x48,
// CWorld slot 0xa8), or -1.
int Day(const void *world)
{
	if (!g_day_ok || !world)
		return -1;
	__try {
		if (Read(world, 0) != kWorldVftable)
			return -1;
		return static_cast<int>(Read(world, 0x48)) + 1;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

bool IsMultiplayer(const void *transceiver)
{
	return transceiver && Read(transceiver, 0) == kMpTransceiverVftable;
}

// For the save lines, which run on PCs that did not ask: the world (noted when
// the adventure controller asks it for the day, every frame) and this PC's
// player (the multiplayer transceiver +0x54, noted at each own step, once per
// segment).
typedef const void *(__fastcall *WorldDayFn)(const void *world, void *edx);
WorldDayFn g_world_day_next = nullptr;
const void *volatile g_world = nullptr;

const void *__fastcall WorldDayHook(const void *world, void *edx)
{
	g_world = world;
	return g_world_day_next(world, edx);
}

typedef void(__fastcall *OwnStepFn)(void *transceiver, void *edx);
OwnStepFn g_own_step_next = nullptr;
const void *volatile g_transceiver = nullptr;
LONGLONG g_last_step = 0;   // this PC's last own step
LONGLONG g_pause_from = 0;  // the own step before a save, until the next one

int Me()
{
	const void *t = g_transceiver;
	return t && IsMultiplayer(t) ? static_cast<int>(Read(t, 0x54)) : -1;
}

// The freeze as the other PCs see it: this PC closes no segment while it saves.
void __fastcall OwnStepHook(void *transceiver, void *edx)
{
	g_transceiver = transceiver;
	g_last_step = Now();
	if (g_pause_from) {
		hooklog::Write("autosave: player %d closed no segment for %.0f ms around the save", Me(),
		               Ms(g_last_step - g_pause_from));
		g_pause_from = 0;
	}
	g_own_step_next(transceiver, edx);
}

// What this PC knows of the current round, for the save lines.
int g_round_day = -1;
int g_round_saves = 0;  // saves this PC ran since the round began
const int kOwnRequests = 8;
DWORD g_own_requests[kOwnRequests];  // the GUIDs this PC sent last
int g_own_next = 0;

void NewRound(int day)
{
	if (day != g_round_day) {
		g_round_day = day;
		g_round_saves = 0;
	}
}

void BeginSave()
{
	g_first_init = g_last_end = g_save_begin = g_save_end = 0;
	g_saving_thread = GetCurrentThreadId();
}

void EndSave()
{
	g_saving_thread = 0;
}

// --- fast_save_list: the saves folder's header reads ------------------------------
// While the autosave request or a multiplayer save runs, the game reads every
// save's info in the folder (0x007f30d0, one 0x007e67b0 per file). Each info
// holds a game setup, and the setup constructor (0x00ca0240) ends with the
// hero pool step 0x00c9f4a0 (call at 0x00ca0af6), which takes nearly all the
// time. That step writes nothing into the setup or the save: only the pool
// globals (0x010db28c, per faction) and the "setup the pool belongs to"
// (0x010daf84), which only the setup's own lobby hero choice reads (vtable
// slots 15 and 16: 0x00c9e140, 0x00c9e1e0, both check that pointer first). The
// listing throws these temporary setups away; the game's saves never read the
// pool. So during those reads, and only those (0x007f320c, the return address
// of the per-file read, is on the stack), the step is skipped.
volatile DWORD g_autosave_thread = 0;  // the request or a save runs on this thread
LONG g_pool_skipped = 0, g_pool_kept = 0;  // hero pool steps inside them

struct AutosaveScope {
	DWORD previous;
	AutosaveScope() : previous(g_autosave_thread) { g_autosave_thread = GetCurrentThreadId(); }
	~AutosaveScope() { g_autosave_thread = previous; }
};

// The return address of the folder scan's per-file read is on this thread's
// stack (between here and the stack's base, at most 16 KB up).
bool ListingReadOnStack()
{
	const DWORD *p = static_cast<const DWORD *>(_AddressOfReturnAddress());
	const DWORD *base = static_cast<const DWORD *>(reinterpret_cast<const NT_TIB *>(NtCurrentTeb())->StackBase);
	const DWORD *end = p + 4096 < base ? p + 4096 : base;
	for (; p < end; ++p)
		if (*p == kListingReadReturn)
			return true;
	return false;
}

typedef void(__fastcall *HeroPoolFn)(void *map, void *setup);
HeroPoolFn g_hero_pool_next = nullptr;

void __fastcall HeroPoolHook(void *map, void *setup)
{
	if (g_autosave_thread && g_autosave_thread == GetCurrentThreadId()) {
		if (ListingReadOnStack()) {
			++g_pool_skipped;
			return;
		}
		++g_pool_kept;
	}
	g_hero_pool_next(map, setup);
}

// ", 17 save headers read without the hero pool" (and how many with it).
void FormatPool(char *out, size_t size, LONG skipped_before, LONG kept_before)
{
	out[0] = 0;
	const LONG skipped = g_pool_skipped - skipped_before, kept = g_pool_kept - kept_before;
	if (skipped || kept)
		_snprintf_s(out, size, _TRUNCATE, "; %ld save headers read without the hero pool, %ld with it", skipped,
		            kept);
}

// "812 ms in all: 2 ms preparing, 20 ms until the first stream (disk check,
// file name, header), 700 ms serializing and compressing (deflate 420 ms,
// level 1, 43000 KB -> 5900 KB in 4 streams), 2 ms closing the file, 60 ms
// after (file flushes 1 ms, 9 calls)"
void FormatSave(char *out, size_t size, LONGLONG t0, LONGLONG t1, const Stats &before)
{
	const Stats &now = g_stats;
	const double total = Ms(t1 - t0);
	const double deflate = Ms(now.deflate_ticks - before.deflate_ticks);
	const ULONGLONG in = now.bytes_in - before.bytes_in, out_bytes = now.bytes_out - before.bytes_out;
	const double file = Ms(now.file_ticks - before.file_ticks);
	if (!g_first_init || !g_last_end) {
		_snprintf_s(out, size, _TRUNCATE, "%.0f ms in all (no compressed stream seen)", total);
		return;
	}
	// Without the save proper's times, the phases around the streams are
	// "before" and "after".
	const LONGLONG begin = g_save_begin ? g_save_begin : t0;
	const LONGLONG end = g_save_end ? g_save_end : t1;
	_snprintf_s(out, size, _TRUNCATE,
	            "%.0f ms in all: %.0f ms preparing, %.0f ms until the first stream (disk check, file name, header), "
	            "%.0f ms serializing and compressing (deflate %.0f ms, level %d, %llu KB -> %llu KB in %ld streams), "
	            "%.0f ms closing the file, %.0f ms after (file flushes %.0f ms, %ld calls)",
	            total, Ms(begin - t0), Ms(g_first_init - begin), Ms(g_last_end - g_first_init), deflate, now.level,
	            in / 1024, out_bytes / 1024, now.streams - before.streams, Ms(end - g_last_end), Ms(t1 - end), file,
	            now.file_calls - before.file_calls);
}

// --- the request (CSavesCreator slot 7) ------------------------------------------
typedef void(__fastcall *SenderFn)(void *creator, void *edx, void *transceiver, const char *key);
SenderFn g_sender_next = nullptr;

void __fastcall SenderHook(void *creator, void *edx, void *transceiver, const char *key)
{
	if (reinterpret_cast<DWORD>(key) != kAutosaveKey || !creator) {
		g_sender_next(creator, edx, transceiver, key);
		return;
	}
	// The two triggers (see the top): the day's start in the controller's
	// update, or 0x00607d50 (console variable autosave + world settings +0x1c).
	const DWORD from = reinterpret_cast<DWORD>(_ReturnAddress());
	const char *trigger = from == kDayTriggerReturn ? "day start" : from == kTurnTriggerReturn ? "turn start" : "other";
	const int day = Day(reinterpret_cast<const void *>(Read(creator, 0x0c)));
	const bool mp = IsMultiplayer(transceiver);
	const int me = mp ? static_cast<int>(Read(transceiver, 0x54)) : -1;
	NewRound(day);

	if (g_every > 1 && day >= 1 && (day - 1) % g_every != 0) {
		if (g_trace)
			hooklog::Write("autosave: day %d (%s): no autosave today (autosave_every=%d: days 1, %d, %d...)", day,
			               trigger, g_every, 1 + g_every, 1 + 2 * g_every);
		return;
	}

	g_last_guid = 0;
	const LONG skipped = g_pool_skipped, kept = g_pool_kept;
	const LONGLONG t0 = Now();
	{
		AutosaveScope scope;
		g_sender_next(creator, edx, transceiver, key);
	}
	const LONGLONG t1 = Now();
	char pool[96];
	FormatPool(pool, sizeof(pool), skipped, kept);
	if (mp && g_last_guid) {
		g_own_requests[g_own_next] = g_last_guid;
		g_own_next = (g_own_next + 1) % kOwnRequests;
		if (!g_pause_from)
			g_pause_from = g_last_step;
	}
	if (!g_trace)
		return;
	if (mp) {
		if (g_last_guid)
			hooklog::Write("autosave: day %d (%s): player %d asks every PC to save (request %08lx; finding the "
			               "oldest autosave and sending took %.0f ms%s)", day, trigger, me, g_last_guid, Ms(t1 - t0),
			               pool);
		else
			hooklog::Write("autosave: day %d (%s): player %d sent no request (%.0f ms%s)", day, trigger, me,
			               Ms(t1 - t0), pool);
		return;
	}
	// In single player the save is queued (0x007f9090) and runs later.
	hooklog::Write("autosave: day %d (%s): asked for the autosave (not a multiplayer game; %.0f ms%s)", day, trigger,
	               Ms(t1 - t0), pool);
}

// --- the save (CMPSaveWorldCmd::Execute) -------------------------------------------
typedef bool(__fastcall *ExecuteFn)(void *cmd, void *edx);
ExecuteFn g_execute_next = nullptr;
typedef bool(__fastcall *SaveFn)(void *a, void *b);
SaveFn g_save_next = nullptr;

// The save proper, inside Execute (0x00822950, ecx and edx: two locals).
bool __fastcall SaveHook(void *a, void *b)
{
	g_save_begin = Now();
	const bool result = g_save_next(a, b);
	g_save_end = Now();
	return result;
}

// The save's name (cmd +0x1c: a wide string, begin and end pointers) as UTF-8.
void SaveName(const void *cmd, char *out, int size)
{
	out[0] = 0;
	__try {
		const wchar_t *begin = reinterpret_cast<const wchar_t *>(Read(cmd, 0x1c));
		const wchar_t *end = reinterpret_cast<const wchar_t *>(Read(cmd, 0x20));
		if (!begin || end <= begin || end - begin > 120)
			return;
		const int n = WideCharToMultiByte(CP_UTF8, 0, begin, static_cast<int>(end - begin), out, size - 1, nullptr,
		                                  nullptr);
		out[n > 0 ? n : 0] = 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		out[0] = 0;
	}
}

bool __fastcall ExecuteHook(void *cmd, void *edx)
{
	if (!g_trace) {
		AutosaveScope scope;  // fast_save_list only
		return g_execute_next(cmd, edx);
	}
	const Stats before = g_stats;
	const LONG skipped = g_pool_skipped, kept = g_pool_kept;
	BeginSave();
	const LONGLONG t0 = Now();
	bool result;
	{
		AutosaveScope scope;
		result = g_execute_next(cmd, edx);
	}
	const LONGLONG t1 = Now();
	EndSave();
	char pool[96];
	FormatPool(pool, sizeof(pool), skipped, kept);

	if (!g_pause_from)
		g_pause_from = g_last_step;
	NewRound(Day(g_world));
	g_round_saves++;
	const DWORD guid = cmd ? Read(cmd, 0x0c) : 0;
	bool own = false;
	for (int i = 0; i < kOwnRequests; ++i)
		own = own || (guid && g_own_requests[i] == guid);
	char name[128];
	if (cmd)
		SaveName(cmd, name, sizeof(name));
	else
		name[0] = 0;
	char what[400];
	FormatSave(what, sizeof(what), t0, t1, before);
	hooklog::Write("autosave: day %d: player %d saved \"%s\" (save %d this round; request %08lx from %s): %s%s",
	               g_round_day, Me(), name, g_round_saves, guid, own ? "this PC" : "another PC", what, pool);
	return result;
}

// --- settings ----------------------------------------------------------------------
int ReadInt(const wchar_t *path, const wchar_t *key, int fallback)
{
	// GetPrivateProfileIntW gives no negative numbers (compression=-1)
	wchar_t text[32];
	GetPrivateProfileStringW(L"save", key, L"", text, 32, path);
	if (!text[0])
		return fallback;
	return _wtoi(text);
}

bool Matches(DWORD address, const BYTE *bytes, size_t size)
{
	return memcmp(reinterpret_cast<const void *>(address), bytes, size) == 0;
}

}  // namespace

void ApplyAutosaveFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	g_trace = ReadInt(path, L"trace_autosave", 1) != 0;
	g_level = ReadInt(path, L"compression", 1);
	g_every = ReadInt(path, L"autosave_every", 1);
	g_fast_list = ReadInt(path, L"fast_save_list", 1) != 0;
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	g_freq = freq.QuadPart ? freq.QuadPart : 1;

	if (g_level < -1 || g_level > 9) {
		hooklog::Write("autosave: compression must be -1 (the game's) or 0..9, not %d: keeping the game's", g_level);
		g_level = -1;
	}
	if (g_every < 1) {
		hooklog::Write("autosave: autosave_every must be 1 or more, not %d: using 1", g_every);
		g_every = 1;
	}

	// The save stream's zlib level, and the trace of its work.
	g_level_ok = Matches(kDeflateInitCall, kDeflateInitCallBytes, sizeof(kDeflateInitCallBytes));
	if ((g_level >= 0 || g_trace) && g_level_ok) {
		g_deflate_init = reinterpret_cast<DeflateInitFn>(
			hookpatch::PatchImport("zlib1.dll", "deflateInit_", reinterpret_cast<void *>(&DeflateInitHook)));
		if (!g_deflate_init)
			g_level_ok = false;
	}
	if (g_level >= 0) {
		if (g_level_ok)
			hooklog::Write("autosave: saves are compressed with zlib level %d (game: 6)", g_level);
		else
			hooklog::Write("autosave: the save code is not as expected: saves keep the game's compression");
	}
	if (g_trace && g_level_ok) {
		g_deflate = reinterpret_cast<DeflateFn>(
			hookpatch::PatchImport("zlib1.dll", "deflate", reinterpret_cast<void *>(&DeflateHook)));
		g_deflate_end = reinterpret_cast<DeflateEndFn>(
			hookpatch::PatchImport("zlib1.dll", "deflateEnd", reinterpret_cast<void *>(&DeflateEndHook)));
		if (!g_deflate || !g_deflate_end)
			hooklog::Write("autosave: zlib's imports are not as expected: the zlib times may be missing");
		g_flush_view = reinterpret_cast<FlushViewFn>(
			hookpatch::PatchImport("KERNEL32.dll", "FlushViewOfFile", reinterpret_cast<void *>(&FlushViewHook)));
		g_unmap_view = reinterpret_cast<UnmapViewFn>(
			hookpatch::PatchImport("KERNEL32.dll", "UnmapViewOfFile", reinterpret_cast<void *>(&UnmapViewHook)));
	}

	// The day number: autosave_every, and the trace.
	g_day_ok = Read(reinterpret_cast<const void *>(kWorldDaySlot), 0) == kWorldDay &&
	           Matches(kWorldDay, kWorldDayBytes, sizeof(kWorldDayBytes));
	if (g_every > 1 && !g_day_ok) {
		hooklog::Write("autosave: the world code is not as expected: autosave every day (as the game)");
		g_every = 1;
	}
	// fast_save_list: the hero pool step, skipped during the autosave's folder reads.
	if (g_fast_list) {
		if (Matches(kHeroPoolArgs, kHeroPoolArgsBytes, sizeof(kHeroPoolArgsBytes)) &&
		    Matches(kHeroPool, kHeroPoolBytes, sizeof(kHeroPoolBytes)) &&
		    Matches(kListingRead, kListingReadBytes, sizeof(kListingReadBytes)))
			g_hero_pool_next =
				reinterpret_cast<HeroPoolFn>(hookpatch::RedirectCall(kHeroPoolCall, kHeroPool, &HeroPoolHook));
		if (!g_hero_pool_next) {
			hooklog::Write("autosave: the save list code is not as expected: fast_save_list off");
			g_fast_list = false;
		}
	}
	if (g_trace || g_every > 1 || g_fast_list) {
		g_sender_next = reinterpret_cast<SenderFn>(hookpatch::PatchPointer(kSenderSlot, kSender, &SenderHook));
		if (!g_sender_next) {
			hooklog::Write("autosave: the saves code is not as expected: autosave every day (as the game), no trace");
			g_every = 1;
		}
	}
	if (g_trace || g_fast_list) {
		g_execute_next = reinterpret_cast<ExecuteFn>(hookpatch::PatchPointer(kExecuteSlot, kExecute, &ExecuteHook));
		if (!g_execute_next)
			hooklog::Write("autosave: the multiplayer save code is not as expected: no save lines");
	}
	if (g_fast_list)
		hooklog::Write("autosave: fast_save_list on: the autosave's look through the saves folder skips each "
		               "save's hero pool");
	if (g_trace) {
		g_guid_ok = Matches(kGuidCall, kGuidCallBytes, sizeof(kGuidCallBytes));
		if (g_guid_ok)
			g_co_create_guid = reinterpret_cast<CoCreateGuidFn>(
				hookpatch::PatchImport("ole32.dll", "CoCreateGuid", reinterpret_cast<void *>(&CoCreateGuidHook)));
		g_guid_ok = g_co_create_guid != nullptr;
		g_save_next = reinterpret_cast<SaveFn>(hookpatch::RedirectCall(kSaveCall, kSave, &SaveHook));
		g_own_step_next = reinterpret_cast<OwnStepFn>(hookpatch::PatchPointer(kOwnStepSlot, kOwnStep, &OwnStepHook));
		if (g_day_ok)
			g_world_day_next =
				reinterpret_cast<WorldDayFn>(hookpatch::PatchPointer(kWorldDaySlot, kWorldDay, &WorldDayHook));
	}
	if (g_every > 1)
		hooklog::Write("autosave: only on days 1, %d, %d... (autosave_every=%d)", 1 + g_every, 1 + 2 * g_every,
		               g_every);
	if (g_trace)
		hooklog::Write("autosave: trace on");
}

}  // namespace mods
