// Mod: teams on maps without teams of their own, such as random maps (map
// generator plan, step 2: docs/plans/map-generator.md).
//
// Teams live in the game setup, NWorld::CPlayersStartInfo (vtable 0x00e92744):
// a vector of slots at +0x10/+0x14, 0x8c bytes each, with +0 the state (0 open,
// 1 computer, 2 human, 3 closed), +4 the player's number on the map (0 =
// PLAYER_1) and +0x1c the team. A map without teams (CustomTeams false, every
// random map) puts every slot on its own team (constructor 0x00ca0240).
//
// When the host starts the game (0x007200da calls 0x0071feb0 with the host's
// lobby logic), it sends its setup (logic +0x74) to every PC in NMP::CMPStart,
// and every PC creates its players from it (0x00c9fa80, team = slot +0x1c). So
// changing the teams in the host's setup just before that is enough: every PC
// gets the same teams, players without our DLL too, and nothing has to agree.
//
// Settings, open-homm5.ini [rmg] (only the host's count):
//   teams=              empty: the map's own teams (the game's behaviour)
//   teams=humans_vs_ai  all humans on one team, all computer players on the other
//   teams=pairs         players 1+2, 3+4, 5+6, 7+8 (our team templates put
//                       zones 1+2, 3+4, ... together; player N starts in zone N)
//   teams=1,1,2,2,3,3   a team for each player number, in map order
//   trace_teams=1       logs the teams each PC starts with (the default)
// Maps with teams of their own keep them. A choice that leaves fewer than two
// teams is not applied (the game would end at once).
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "rmg_teams.h"

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

const DWORD kStartInfoVftable = 0x00e92744;  // NWorld::CPlayersStartInfo
const DWORD kHostStartCall = 0x007200da;      // in 0x00720080: call 0x0071feb0 (ecx = host lobby logic)
const DWORD kHostStart = 0x0071feb0;
const DWORD kGameStartCall = 0x008208a8;      // in 0x00820830: call 0x00c9fa80 (edx = the setup's holder)
const DWORD kGameStart = 0x00c9fa80;
const DWORD kSlotSize = 0x8c;
const int kMaxSlots = 8;

enum Mode { kOff, kHumansVsAi, kPairs, kList };
Mode g_mode = kOff;
int g_list[kMaxSlots];  // kList: the team (1..8) of each player number
bool g_trace = true;

typedef void(__fastcall *HostStartFn)(void *logic, void *edx);
typedef DWORD(__fastcall *GameStartFn)(void *out, void *holder, int flag);
HostStartFn g_host_start_next = nullptr;
GameStartFn g_game_start_next = nullptr;

DWORD Read(const void *base, DWORD offset)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(base) + offset);
}

bool IsStartInfo(const BYTE *info)
{
	return info && !IsBadReadPtr(info, 0x20) && Read(info, 0) == kStartInfoVftable;
}

int Slots(const BYTE *info)
{
	const int n = static_cast<int>((Read(info, 0x14) - Read(info, 0x10)) / kSlotSize);
	return n >= 0 && n <= kMaxSlots ? n : 0;
}

BYTE *Slot(const BYTE *info, int i)
{
	return reinterpret_cast<BYTE *>(Read(info, 0x10) + i * kSlotSize);
}

bool Plays(const BYTE *slot)
{
	const DWORD state = Read(slot, 0);
	return state == 1 || state == 2;
}

void LogSlots(const char *when, const BYTE *info)
{
	char line[256] = "";
	for (int i = 0; i < Slots(info); ++i) {
		const BYTE *s = Slot(info, i);
		const DWORD state = Read(s, 0);
		if (state == 3)
			continue;
		char one[40];
		wsprintfA(one, " player%lu:%s/team%lu", Read(s, 4) + 1,
		          state == 2 ? "human" : state == 1 ? "computer" : "open", Read(s, 0x1c));
		lstrcatA(line, one);
	}
	hooklog::Write("rmg teams: %s:%s", when, line);
}

// The team each playing slot should get (1..8), or 0 when the setting has none.
int Wanted(const BYTE *slot)
{
	const DWORD id = Read(slot, 4);
	switch (g_mode) {
	case kHumansVsAi:
		return Read(slot, 0) == 2 ? 1 : 2;
	case kPairs:
		return id < kMaxSlots ? static_cast<int>(id / 2) + 1 : 0;
	case kList:
		return id < kMaxSlots ? g_list[id] : 0;
	default:
		return 0;
	}
}

void Apply(const BYTE *info)
{
	const int n = Slots(info);
	// Only maps without teams of their own: every playing slot on its own team.
	for (int i = 0; i < n; ++i)
		for (int j = i + 1; j < n; ++j)
			if (Plays(Slot(info, i)) && Plays(Slot(info, j)) && Read(Slot(info, i), 0x1c) == Read(Slot(info, j), 0x1c)) {
				hooklog::Write("rmg teams: this map has teams of its own: keeping them");
				return;
			}
	int wanted[kMaxSlots] = {};
	for (int i = 0; i < n; ++i) {
		if (!Plays(Slot(info, i)))
			continue;
		wanted[i] = Wanted(Slot(info, i));
		if (wanted[i] <= 0) {
			hooklog::Write("rmg teams: no team for player %lu in the setting: keeping the map's teams",
			               Read(Slot(info, i), 4) + 1);
			return;
		}
	}
	// Number the teams 0, 1, 2... in slot order, as the game's constructor does.
	int order[kMaxSlots + 1];
	int teams = 0;
	for (int i = 0; i < n; ++i) {
		if (!wanted[i])
			continue;
		bool known = false;
		for (int k = 0; k < teams; ++k)
			known = known || order[k] == wanted[i];
		if (!known)
			order[teams++] = wanted[i];
	}
	if (teams < 2) {
		hooklog::Write("rmg teams: the setting leaves only one team: keeping the map's teams");
		return;
	}
	for (int i = 0; i < n; ++i) {
		if (!wanted[i])
			continue;
		for (int k = 0; k < teams; ++k)
			if (order[k] == wanted[i])
				*reinterpret_cast<DWORD *>(Slot(info, i) + 0x1c) = k;
	}
	// Closed or open slots get teams of their own after the others.
	int next = teams;
	for (int i = 0; i < n; ++i)
		if (!wanted[i])
			*reinterpret_cast<DWORD *>(Slot(info, i) + 0x1c) = next++;
	hooklog::Write("rmg teams: %d teams set on the host", teams);
}

void __fastcall HostStartHook(void *logic, void *edx)
{
	const BYTE *info = logic ? reinterpret_cast<const BYTE *>(Read(logic, 0x74)) : nullptr;
	if (IsStartInfo(info)) {
		if (g_trace)
			LogSlots("host setup before the start", info);
		if (g_mode != kOff)
			Apply(info);
	}
	g_host_start_next(logic, edx);
}

DWORD __fastcall GameStartHook(void *out, void *holder, int flag)
{
	const BYTE *info = holder && !IsBadReadPtr(holder, 4) ? reinterpret_cast<const BYTE *>(Read(holder, 0)) : nullptr;
	if (IsStartInfo(info))
		LogSlots("this PC starts with", info);
	return g_game_start_next(out, holder, flag);
}

bool ParseList(const wchar_t *text)
{
	int count = 0;
	const wchar_t *p = text;
	while (*p && count < kMaxSlots) {
		wchar_t *end;
		const long team = wcstol(p, &end, 10);
		if (end == p || team < 1 || team > kMaxSlots)
			return false;
		g_list[count++] = static_cast<int>(team);
		p = end;
		while (*p == L',' || *p == L' ')
			++p;
	}
	return count >= 2 && !*p;
}

}  // namespace

void ApplyRmgTeamsFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	wchar_t text[64];
	GetPrivateProfileStringW(L"rmg", L"teams", L"", text, 64, path);
	g_trace = GetPrivateProfileIntW(L"rmg", L"trace_teams", 1, path) != 0;
	if (!lstrcmpiW(text, L"humans_vs_ai"))
		g_mode = kHumansVsAi;
	else if (!lstrcmpiW(text, L"pairs"))
		g_mode = kPairs;
	else if (text[0] && ParseList(text))
		g_mode = kList;
	else if (text[0])
		hooklog::Write("rmg teams: teams=%ls is not humans_vs_ai, pairs or a list like 1,1,2,2: off", text);
	if (g_mode == kOff && !g_trace)
		return;
	g_host_start_next = reinterpret_cast<HostStartFn>(hookpatch::RedirectCall(kHostStartCall, kHostStart, &HostStartHook));
	if (g_trace)
		g_game_start_next = reinterpret_cast<GameStartFn>(hookpatch::RedirectCall(kGameStartCall, kGameStart, &GameStartHook));
	if (!g_host_start_next || (g_trace && !g_game_start_next)) {
		hooklog::Write("rmg teams: the game start code is not as expected: off");
		g_mode = kOff;
		return;
	}
	if (g_mode != kOff)
		hooklog::Write("rmg teams: when this PC hosts a map without teams: teams=%ls", text);
}

}  // namespace mods
