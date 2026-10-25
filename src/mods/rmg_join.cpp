// Mod: joining a multiplayer random map game, the client side of recipe
// templates (in-game builder plan, step 3: docs/plans/in-game-generator.md).
//
// How a client builds the host's random map (static reading of the reference
// exe, 2026-10-25):
//   - The host advertises its game info (0x194 bytes): +0x104 is the "random
//     map" flag, +0x108 the map's sRMGProps (as in map.xdb: version, seed,
//     guid, InitialParams). InitialParams' Template, at sRMGProps +0x28, is a
//     database link (an NDb pointer and a flag).
//   - The link travels by name, "/RMG/Templates/<file>.xdb#xpointer(/RMGTemplate)".
//     When a PC reads it (0x004a6a50), the game's database loads that template
//     at once (0x009b0f60), through the file system: for a recipe name our Open
//     hook (rmg_templates.cpp) builds the template and registers the name. When
//     the file is missing, the link stays empty.
//   - Joining (0x006f7960) creates CCreateRMGConnectorCmd (0x007014e0), which
//     holds a copy of the game info at +0x10. Its Execute (vtable cell
//     0x00e38734 -> 0x00701530) runs NUI::CRMGConnector (0x007011d0): it lists
//     all templates (generator slot 2, which our List hook extends), looks for
//     the map's template in that list (0x00701670) and only then builds the map.
//
// What this file adds:
//   1. Before Execute, it reads the template's name from the game info copy and
//      registers it if it is a recipe, so the list has it. (The Open hook
//      already did, when the game info was read; this covers a template the
//      database had loaded before, or the listing's limit of 64.)
//   2. The guard. With the template missing, the connector used the empty link
//      without a check (0x007012b1: call 0x004a69d0, then [eax+0xc]) and the
//      game crashed. That call is redirected: an empty link gives a stand-in
//      whose name matches no template, so the connector takes its own "template
//      not found" way (code 1) and the lobby shows the game's RMG_MAP_FAIL
//      message. The bytes around the call are checked first.
#include <windows.h>
#include <string.h>

#include <string>

#include "rmg_join.h"
#include "rmg_recipe.h"
#include "rmg_templates.h"

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

const DWORD kExecuteSlot = 0x00e38734;  // CCreateRMGConnectorCmd vtable 0x00e38718, slot 7
const DWORD kExecute = 0x00701530;      // thiscall (cmd), no arguments
const DWORD kLookupCall = 0x007012b1;   // in 0x007011d0: call 0x004a69d0, ecx = the template link
const DWORD kResolveLink = 0x004a69d0;  // thiscall (link): the template, or 0
const DWORD kGameInfoInCmd = 0x10;
const DWORD kRmgFlag = 0x104;
const DWORD kRmgProps = 0x108;

struct GameString {  // nstl::string
	const char *begin;
	const char *end;
	const char *cap;
};
// NDb's database id (0x20 bytes), at +0xc of every database object: the path,
// then a "key made" flag, a hash and the key (the path in a canonical case).
struct DbId {
	GameString path;
	BYTE key_made;
	BYTE pad[3];
	DWORD hash;
	GameString key;
};
static_assert(sizeof(DbId) == 0x20, "NDb database id");

// What the guard gives the connector instead of a missing template: a database
// object whose id has an empty key, which no listed template matches.
struct StandIn {
	BYTE head[0xc];
	DbId id;
	BYTE tail[0x80];  // the connector reads nothing else of it on its "not found" way
};
char g_nothing[1];
StandIn g_stand_in;

typedef void(__fastcall *ExecuteFn)(void *cmd, void *edx);
typedef void *(__fastcall *ResolveFn)(void *link, void *edx);
ExecuteFn g_execute_next = nullptr;
ResolveFn g_resolve_next = nullptr;

bool ReadString(const GameString *s, std::string *out)
{
	if (IsBadReadPtr(s, sizeof(GameString)) || !s->begin || s->end <= s->begin)
		return false;
	const size_t n = static_cast<size_t>(s->end - s->begin);
	if (n > 1024 || IsBadReadPtr(s->begin, n))
		return false;
	out->assign(s->begin, n);
	return true;
}

void *__fastcall ResolveHook(void *link, void *edx)
{
	void *t = g_resolve_next(link, edx);
	if (t)
		return t;
	static LONG logged = 0;
	if (InterlockedIncrement(&logged) <= 5)
		hooklog::Write("rmg join: the map's template is missing on this PC: the game says the map can't be made, "
		               "instead of crashing");
	return &g_stand_in;
}

void NoteJoin(const BYTE *cmd)
{
	const BYTE *info = cmd + kGameInfoInCmd;
	if (IsBadReadPtr(info, kRmgProps + kRmgPropsTemplateLink + 8) || !info[kRmgFlag])
		return;
	std::string reference, recipe;
	if (!TemplateLinkReference(info + kRmgProps + kRmgPropsTemplateLink, &reference)) {
		hooklog::Write("rmg join: joining a random map game whose template this PC does not have");
		return;
	}
	if (!rmg::RecipeOfReference(reference, &recipe)) {
		hooklog::Write("rmg join: joining a random map game made from %s", reference.c_str());
		return;
	}
	const bool added = RegisterRecipe(recipe);
	hooklog::Write("rmg join: joining a random map game made from the recipe %s%s", recipe.c_str(),
	               added ? " (now in the template list)" : "");
}

void __fastcall ExecuteHook(void *cmd, void *edx)
{
	try {
		if (cmd)
			NoteJoin(static_cast<const BYTE *>(cmd));
	} catch (...) {  // out of memory: the game goes on as without us
	}
	g_execute_next(cmd, edx);
}

bool InstallGuard()
{
	static const BYTE before[] = {0x8d, 0x4d, 0x28};  // lea ecx, [ebp+0x28]: sRMGProps' template link
	static const BYTE after[] = {0x8b, 0x54, 0x24, 0x24, 0x8b, 0x4c, 0x24, 0x20, 0x83, 0xc0, 0x0c};  // ...; add eax, 0xc
	if (memcmp(reinterpret_cast<const void *>(kLookupCall - sizeof(before)), before, sizeof(before)) != 0 ||
	    memcmp(reinterpret_cast<const void *>(kLookupCall + 5), after, sizeof(after)) != 0)
		return false;
	g_stand_in.id.path = {g_nothing, g_nothing, g_nothing + 1};
	g_stand_in.id.key = g_stand_in.id.path;
	g_stand_in.id.key_made = 1;  // nothing to compute: the key stays empty
	g_stand_in.id.hash = 0x0bad0bad;
	g_resolve_next = reinterpret_cast<ResolveFn>(hookpatch::RedirectCall(kLookupCall, kResolveLink, &ResolveHook));
	return g_resolve_next != nullptr;
}

}  // namespace

bool TemplateLinkReference(const void *link, std::string *reference)
{
	if (!link || IsBadReadPtr(link, 4))
		return false;
	const BYTE *object = *static_cast<const BYTE *const *>(link);
	if (!object || IsBadReadPtr(object, 0xc + sizeof(DbId)))
		return false;
	const DbId *id = reinterpret_cast<const DbId *>(object + 0xc);
	// A loaded template's id holds its path first. A link read from a map file
	// (the host's own map, robot run 2026-10-25) holds "RMGTemplate" (the
	// xpointer's name) there, and the file in the key. Take the one naming a file.
	std::string path, key;
	const bool has_path = ReadString(&id->path, &path), has_key = ReadString(&id->key, &key);
	auto names_file = [](const std::string &s) {
		for (size_t i = 0; i + 4 <= s.size(); i++)
			if (s[i] == '.' && (s[i + 1] | 0x20) == 'x' && (s[i + 2] | 0x20) == 'd' && (s[i + 3] | 0x20) == 'b')
				return true;
		return false;
	};
	if (has_path && (names_file(path) || !has_key || !names_file(key)))
		*reference = path;
	else if (has_key)
		*reference = key;
	else
		return false;
	return true;
}

void ApplyRmgJoin(bool recipes_served)
{
	if (InstallGuard())
		hooklog::Write("rmg join: a missing random map template now gives the game's \"can't make the map\" message, "
		               "not a crash");
	else
		hooklog::Write("rmg join: the connector code is not as expected: no guard against a missing template");
	if (!recipes_served)
		return;
	g_execute_next = reinterpret_cast<ExecuteFn>(hookpatch::PatchPointer(kExecuteSlot, kExecute, &ExecuteHook));
	if (!g_execute_next)
		hooklog::Write("rmg join: the connector command is not as expected: a joined game's recipe is registered only "
		               "when the game opens it");
}

}  // namespace mods
