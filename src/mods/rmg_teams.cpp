// Mod: teams on maps without teams of their own, such as random maps (map
// generator plan, step 2: docs/plans/map-generator.md).
//
// Teams live in the game setup, NWorld::CPlayersStartInfo (vtable 0x00e92744):
// a vector of slots at +0x10/+0x14, 0x8c bytes each, with +0 the state (0 open,
// 1 computer, 2 human, 3 closed), +4 the player's number on the map (1 =
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
//
//   lobby_teams=1       on such maps, the lobby's team arrows pick each
//                       player's team number (1, 2, 3...) instead of moving
//                       the player to another start (the game's arrows; then
//                       they no longer pick a colour). Off by default.
// How (only the host decides; clients need nothing):
//   - the setup constructor (0x00ca0240, called at 0x00ca0c45 and 0x00ca0cbc)
//     tells whether the map has teams (SAdvMapDesc +0x100, CustomTeams); we
//     remember the setups of maps without;
//   - for those, CanChangeTeam (vtable slot 0x00e92770) allows any playing slot,
//     and SetTeam (slot 0x00e92774, thiscall (slot, bool right), ret 8) moves
//     the slot's team up or down instead of swapping slots;
//   - the host tells every lobby a row's team only when its colour changed
//     (0x0071e67d); a small patch sends it when only the team changed too
//     (never the case in the game itself, where a team change moves the colour);
//   - at the start, teams are numbered 0, 1, 2... as the game's constructor does.
//
//   recipe_teams=1      (the default) maps made from a recipe template
//                       (OH-X1-..., rmg_recipe.cpp) get the recipe's teams:
//                       with 3 teams of 2, players 1+2, 3+4, 5+6 (player N
//                       starts in zone N). Only when teams= is empty and the
//                       lobby arrows (lobby_teams=1) left every player alone.
// How: the setup constructor gets the map's description (NDb::SAdvMapDesc);
// its sRMGProps (+0x254) names the template the map was made from (+0x28, a
// database link, read by rmg_join.cpp). We remember the recipe's teams per
// setup, as for lobby_teams, and the host start applies them.
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "rmg_join.h"
#include "rmg_recipe.h"
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

const DWORD kCtorCalls[] = {0x00ca0c45, 0x00ca0cbc};
const DWORD kCtor = 0x00ca0240;               // thiscall (SAdvMapDesc *), ret 4, returns this
const DWORD kCanChangeTeamSlot = 0x00e92770;
const DWORD kCanChangeTeam = 0x00c9d740;      // thiscall (int slot), ret 4
const DWORD kSetTeamSlot = 0x00e92774;
const DWORD kSetTeam = 0x00c9e660;            // thiscall (int slot, bool right), ret 8
const DWORD kTeamCommandCheck = 0x0071e67d;   // cmp [esp+0x1c], eax; je 0x0071e6aa
const DWORD kTeamCommandSend = 0x0071e683;
const DWORD kTeamCommandSkip = 0x0071e6aa;

enum Mode { kOff, kHumansVsAi, kPairs, kList, kRecipe };
Mode g_mode = kOff;
int g_list[kMaxSlots];  // kList: the team (1..8) of each player number
bool g_trace = true;
bool g_lobby = false;
bool g_recipe_teams = true;

// Setups of maps without teams of their own, as built (a small ring: a setup
// lives for one lobby or one game, and a reused address is overwritten).
const int kFfaRing = 64;
struct FfaEntry {
	const void *info;
	bool ffa;
	char recipe[24];  // the recipe the map was made from, or ""
	int teams;        // its teams...
	int per_team;     // ...of this many players (0: not a recipe map)
};
FfaEntry g_ffa[kFfaRing];
int g_ffa_next = 0;

typedef void *(__fastcall *CtorFn)(void *self, void *edx, const BYTE *desc);
typedef bool(__fastcall *CanChangeTeamFn)(void *self, void *edx, int slot);
typedef void(__fastcall *SetTeamFn)(void *self, void *edx, int slot, int right);
CtorFn g_ctor_next = nullptr;
CanChangeTeamFn g_can_change_next = nullptr;
SetTeamFn g_set_team_next = nullptr;

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
		wsprintfA(one, " player%lu:%s/team%lu", Read(s, 4),
		          state == 2 ? "human" : state == 1 ? "computer" : "open", Read(s, 0x1c));
		lstrcatA(line, one);
	}
	hooklog::Write("rmg teams: %s:%s", when, line);
}

// The team each playing slot should get (1..8), or 0 when the setting has none.
// kRecipe: the recipe's teams of per_team players, in player order.
int Wanted(const BYTE *slot, Mode mode, int teams, int per_team)
{
	const DWORD id = Read(slot, 4) - 1;  // 0 = PLAYER_1 (the slot holds 1..8)
	switch (mode) {
	case kHumansVsAi:
		return Read(slot, 0) == 2 ? 1 : 2;
	case kPairs:
		return id < kMaxSlots ? static_cast<int>(id / 2) + 1 : 0;
	case kList:
		return id < kMaxSlots ? g_list[id] : 0;
	case kRecipe: {
		rmg::Choices c;
		c.teams = teams;
		c.per_team = per_team;
		return id < kMaxSlots ? rmg::RecipeTeamOf(c, static_cast<int>(id) + 1) + 1 : 0;  // 0: no start zone for it
	}
	default:
		return 0;
	}
}

void Apply(const BYTE *info, Mode mode, int teams_wanted = 0, int per_team = 0)
{
	const int n = Slots(info);
	// Only maps without teams of their own: every playing slot on its own team.
	for (int i = 0; i < n; ++i)
		for (int j = i + 1; j < n; ++j)
			if (Plays(Slot(info, i)) && Plays(Slot(info, j)) && Read(Slot(info, i), 0x1c) == Read(Slot(info, j), 0x1c)) {
				hooklog::Write("rmg teams: teams already set (by the map or in the lobby): keeping them");
				return;
			}
	int wanted[kMaxSlots] = {};
	for (int i = 0; i < n; ++i) {
		if (!Plays(Slot(info, i)))
			continue;
		wanted[i] = Wanted(Slot(info, i), mode, teams_wanted, per_team);
		if (wanted[i] <= 0) {
			hooklog::Write("rmg teams: no team for player %lu in the setting: keeping the map's teams",
			               Read(Slot(info, i), 4));
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

// --- team choice in the lobby (lobby_teams=1), recipe teams (recipe_teams=1) ----------
const FfaEntry *Find(const void *info)
{
	for (int k = 1; k <= kFfaRing; ++k) {
		const FfaEntry &e = g_ffa[(g_ffa_next - k + kFfaRing) % kFfaRing];
		if (e.info == info)
			return &e;
	}
	return nullptr;
}

bool IsFfa(const void *info)
{
	const FfaEntry *e = Find(info);
	return e && e->ffa;
}

// The recipe a map was made from: its description's sRMGProps names the
// template (a database link, loaded with the map).
void NoteRecipe(FfaEntry *e, const BYTE *desc)
{
	e->recipe[0] = 0;
	e->per_team = 0;
	std::string reference, recipe;
	rmg::Choices c;
	try {
		const bool random_map = desc && !IsBadReadPtr(desc + kAdvMapDescRmgProps, 8) && desc[kAdvMapDescRmgProps + 4];
		const bool linked = desc && TemplateLinkReference(desc + kAdvMapDescRmgProps + kRmgPropsTemplateLink, &reference);
		static LONG logged = 0;
		if (g_trace && random_map && InterlockedIncrement(&logged) <= 8)
			hooklog::Write("rmg teams: setup %p is for a random map made from %s", e->info,
			               linked ? reference.c_str() : "(a template link this PC can't read)");
		if (!linked || !rmg::RecipeOfReference(reference, &recipe) || !rmg::DecodeRecipe(recipe, &c))
			return;
	} catch (...) {
		return;
	}
	lstrcpynA(e->recipe, recipe.c_str(), sizeof(e->recipe));
	e->teams = c.teams;
	e->per_team = c.per_team;
}

void *__fastcall CtorHook(void *self, void *edx, const BYTE *desc)
{
	void *result = g_ctor_next(self, edx, desc);
	FfaEntry &e = g_ffa[g_ffa_next];
	e.info = self;
	e.ffa = desc && !IsBadReadPtr(desc + 0x100, 1) && desc[0x100] == 0;
	NoteRecipe(&e, desc);
	g_ffa_next = (g_ffa_next + 1) % kFfaRing;
	return result;
}

bool __fastcall CanChangeTeamHook(void *self, void *edx, int slot)
{
	const BYTE *info = static_cast<const BYTE *>(self);
	if (!IsFfa(info))
		return g_can_change_next(self, edx, slot);
	return slot >= 0 && slot < Slots(info) && Plays(Slot(info, slot));
}

void __fastcall SetTeamHook(void *self, void *edx, int slot, int right)
{
	const BYTE *info = static_cast<const BYTE *>(self);
	if (!IsFfa(info)) {
		g_set_team_next(self, edx, slot, right);
		return;
	}
	const int n = Slots(info);
	if (slot < 0 || slot >= n || !Plays(Slot(info, slot)))
		return;
	DWORD *team = reinterpret_cast<DWORD *>(Slot(info, slot) + 0x1c);
	*team = (right & 0xff) ? (*team + 1) % n : (*team + n - 1) % n;  // only the low byte is the bool
}

// Teams numbered 0, 1, 2... in slot order (the lobby may leave gaps).
void Normalize(const BYTE *info)
{
	const int n = Slots(info);
	DWORD seen[kMaxSlots];
	int teams = 0;
	for (int i = 0; i < n; ++i) {
		DWORD *team = reinterpret_cast<DWORD *>(Slot(info, i) + 0x1c);
		int k = 0;
		while (k < teams && seen[k] != *team)
			++k;
		if (k == teams)
			seen[teams++] = *team;
		*team = k;
	}
}

void PutJump(BYTE *code, DWORD at, DWORD target)
{
	code[0] = 0xe9;
	*reinterpret_cast<LONG *>(code + 1) = static_cast<LONG>(target - (at + 5));
}

// At 0x0071e67d the host sends a row's team to the lobbies only when its colour
// changed. A jump to a cave that also sends it when only the team changed: the
// old row copy is at [esp+0x18] (colour +4, team +0x3c), the new slot copy at
// [esp+0x58] (colour +8 = eax, team +0x1c).
bool PatchTeamCommand()
{
	static const BYTE expected[] = {0x39, 0x44, 0x24, 0x1c, 0x74, 0x27};
	if (memcmp(reinterpret_cast<const void *>(kTeamCommandCheck), expected, sizeof(expected)) != 0)
		return false;
	BYTE *cave = static_cast<BYTE *>(VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
	if (!cave)
		return false;
	BYTE code[26] = {
		0x39, 0x44, 0x24, 0x1c,  // cmp [esp+0x1c], eax   old colour vs new
		0x75, 0x0f,              // jne send
		0x8b, 0x4c, 0x24, 0x74,  // mov ecx, [esp+0x74]   new team
		0x3b, 0x4c, 0x24, 0x54,  // cmp ecx, [esp+0x54]   old team
		0x75, 0x05,              // jne send
	};
	const DWORD base = reinterpret_cast<DWORD>(cave);
	PutJump(code + 16, base + 16, kTeamCommandSkip);
	PutJump(code + 21, base + 21, kTeamCommandSend);  // send:
	memcpy(cave, code, sizeof(code));
	FlushInstructionCache(GetCurrentProcess(), cave, sizeof(code));
	BYTE patch[6];
	PutJump(patch, kTeamCommandCheck, base);
	patch[5] = 0x90;
	BYTE *site = reinterpret_cast<BYTE *>(kTeamCommandCheck);
	DWORD old;
	if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &old))
		return false;
	memcpy(site, patch, sizeof(patch));
	VirtualProtect(site, sizeof(patch), old, &old);
	FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
	return true;
}

void __fastcall HostStartHook(void *logic, void *edx)
{
	const BYTE *info = logic ? reinterpret_cast<const BYTE *>(Read(logic, 0x74)) : nullptr;
	if (IsStartInfo(info)) {
		if (g_trace)
			LogSlots("host setup before the start", info);
		if (g_lobby && IsFfa(info))
			Normalize(info);
		const FfaEntry *e = Find(info);
		if (g_trace && g_ctor_next)  // (without the constructor hook we know no setup)
			hooklog::Write("rmg teams: host setup %p: %s", info,
			               !e ? "not one we saw built" : e->recipe[0] ? e->recipe : "not from a recipe");
		if (g_mode != kOff) {
			Apply(info, g_mode);
		} else if (g_recipe_teams && e && e->ffa && e->per_team > 1) {
			hooklog::Write("rmg teams: a map made from the recipe %s: %d teams of %d, in player order", e->recipe, e->teams,
			               e->per_team);
			Apply(info, kRecipe, e->teams, e->per_team);
		}
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
	g_lobby = GetPrivateProfileIntW(L"rmg", L"lobby_teams", 0, path) != 0;
	g_recipe_teams = GetPrivateProfileIntW(L"rmg", L"recipe_teams", 1, path) != 0;
	if (g_lobby || g_recipe_teams) {  // both need to know each setup's map
		g_ctor_next = reinterpret_cast<CtorFn>(hookpatch::RedirectCalls(kCtorCalls, 2, kCtor, &CtorHook));
		if (!g_ctor_next) {
			hooklog::Write("rmg teams: the setup code is not as expected: lobby team choice and recipe teams off");
			g_lobby = g_recipe_teams = false;
		}
	}
	if (g_lobby) {
		g_can_change_next =
			reinterpret_cast<CanChangeTeamFn>(hookpatch::PatchPointer(kCanChangeTeamSlot, kCanChangeTeam, &CanChangeTeamHook));
		g_set_team_next = reinterpret_cast<SetTeamFn>(hookpatch::PatchPointer(kSetTeamSlot, kSetTeam, &SetTeamHook));
		if (!g_can_change_next || !g_set_team_next || !PatchTeamCommand()) {
			hooklog::Write("rmg teams: the lobby code is not as expected: lobby team choice off");
			g_lobby = false;
		} else {
			hooklog::Write("rmg teams: in the lobby, the team arrows pick teams on maps without teams of their own");
		}
	}
	if (g_mode == kOff && !g_trace && !g_lobby && !g_recipe_teams)
		return;
	g_host_start_next = reinterpret_cast<HostStartFn>(hookpatch::RedirectCall(kHostStartCall, kHostStart, &HostStartHook));
	if (g_trace)
		g_game_start_next = reinterpret_cast<GameStartFn>(hookpatch::RedirectCall(kGameStartCall, kGameStart, &GameStartHook));
	if (!g_host_start_next || (g_trace && !g_game_start_next)) {
		hooklog::Write("rmg teams: the game start code is not as expected: off");
		g_mode = kOff;
		g_recipe_teams = false;
		return;
	}
	if (g_mode != kOff)
		hooklog::Write("rmg teams: when this PC hosts a map without teams: teams=%ls", text);
	else if (g_recipe_teams)
		hooklog::Write("rmg teams: when this PC hosts a map made from a recipe (OH-X1-...): the recipe's teams");
}

}  // namespace mods
