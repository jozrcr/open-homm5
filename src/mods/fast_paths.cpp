// Mod: faster AI pathfinding, with exactly the same results.
//
// Late in a game on a big map the AI thinks for seconds per move, nearly all of
// it in its hero pathfinder (docs/subsystems/ai-pathfinder.md). One search can
// expand the same tile over a hundred times, because the cost of a tile keeps
// improving (gains along the way make it more than a shortest-path search), and
// each time the expansion repeats checks that depend only on the tile, not on
// the path that led there. This mod remembers the answer of such checks until
// the search ends. The search itself does exactly the same steps, so every path
// and every AI decision stays the same; it is only faster. In co-op only the
// host runs the AI, so the other PCs are not affected either way.
//
// Checks remembered (per search; a new search starts at every call of the
// search loop 0x00c08890, from 0x00c08e13, 0x00ce4c26, 0x00cea542, 0x00cebb82):
//   0x008649f0  "can the hero go ashore here?", called by the node expansion
//               0x00c07f80 at 0x00c08000 for nodes on the boat layer.
//               __fastcall(world, ecx/edx = this+0x70 / this+0x38 of the
//               pathfinder, const Node *node, bool flag), ret 8. It looks at the
//               tile at node->position (its terrain and the objects on it) and
//               the flag; the two pointers are fixed during a search.
//
// Settings: open-homm5.ini [ai] fast_paths=1 (default 0 until it is checked
// against the original on more saves).
#include "fast_paths.h"

#include <windows.h>
#include <intrin.h>

#include "../hook/log.h"
#include "../hook/patch.h"

#pragma intrinsic(_ReturnAddress)

namespace mods {
namespace {

const DWORD kSearch = 0x00c08890;
const DWORD kSearchCalls[] = {0x00c08e13, 0x00ce4c26, 0x00cea542, 0x00cebb82};
const DWORD kGoAshore = 0x008649f0;
const DWORD kGoAshoreCall = 0x00c08000;

typedef void(__fastcall *SearchFn)(void *self, void *edx, int limit);
typedef bool(__fastcall *GoAshoreFn)(void *world, void *player, const DWORD *node, char flag);

// Remembered answers: open addressing, an entry belongs to the current search
// when its generation is g_generation (so a new search needs no clearing).
struct Entry {
	DWORD position;  // node[0], all 32 bits
	DWORD generation;
	bool flag;
	bool value;
};
const DWORD kSlotBits = 19;
const DWORD kSlots = 1u << kSlotBits;
const DWORD kMaxUsed = kSlots / 2;
Entry *g_table = nullptr;
DWORD g_generation = 1;
DWORD g_used = 0;  // entries of the current search
const void *g_world = nullptr;
const void *g_player = nullptr;
GoAshoreFn g_go_ashore = nullptr;
LONGLONG g_hits = 0;
LONGLONG g_misses = 0;

void NewSearch()
{
	++g_generation;
	g_used = 0;
}

void __fastcall SearchHook(void *self, void *edx, int limit)
{
	const DWORD site = reinterpret_cast<DWORD>(_ReturnAddress()) - 5;
	const SearchFn next = reinterpret_cast<SearchFn>(hookpatch::PreviousTarget(site, &SearchHook));
	NewSearch();
	next(self, edx, limit);
	NewSearch();
}

bool __fastcall GoAshoreHook(void *world, void *player, const DWORD *node, char flag)
{
	if (world != g_world || player != g_player) {
		g_world = world;
		g_player = player;
		NewSearch();
	}
	if (g_used >= kMaxUsed) {
		++g_misses;
		return g_go_ashore(world, player, node, flag);
	}
	const DWORD position = node[0];
	const bool f = flag != 0;
	for (DWORD i = ((position ^ (f ? 0x9e3779b9u : 0)) * 2654435761u) >> (32 - kSlotBits);; i = (i + 1) & (kSlots - 1)) {
		Entry &e = g_table[i];
		if (e.generation != g_generation) {
			++g_misses;
			++g_used;
			const bool value = g_go_ashore(world, player, node, flag);
			e.position = position;
			e.generation = g_generation;
			e.flag = f;
			e.value = value;
			return value;
		}
		if (e.position == position && e.flag == f) {
			++g_hits;
			return e.value;
		}
	}
}

}  // namespace

void ApplyFastPathsFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	if (!GetPrivateProfileIntW(L"ai", L"fast_paths", 0, path)) {
		hooklog::Write("fast paths: off");
		return;
	}
	g_table = static_cast<Entry *>(VirtualAlloc(nullptr, kSlots * sizeof(Entry), MEM_COMMIT, PAGE_READWRITE));
	if (!g_table) {
		hooklog::Write("fast paths: out of memory, off");
		return;
	}
	// The search hooks go first: without them the remembered answers could
	// outlive a search.
	int searches = 0;
	for (DWORD site : kSearchCalls)
		searches += hookpatch::RedirectCall(site, kSearch, &SearchHook) != 0;
	if (searches != sizeof(kSearchCalls) / sizeof(kSearchCalls[0])) {
		hooklog::Write("fast paths: only %d of 4 search calls are as expected: off", searches);
		return;
	}
	g_go_ashore = reinterpret_cast<GoAshoreFn>(hookpatch::RedirectCall(kGoAshoreCall, kGoAshore, &GoAshoreHook));
	hooklog::Write("fast paths: on (go-ashore check %s)", g_go_ashore ? "remembered per search" : "NOT as expected");
}

void FastPathsStats(LONGLONG *hits, LONGLONG *misses)
{
	*hits = g_hits;
	*misses = g_misses;
}

}  // namespace mods
