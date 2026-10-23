// Robot-mode trace of the hero pathfinder (trace_paths=1 in open-homm5-robot.ini):
// how many path searches each AI step runs, from where, how long they take, how
// many nodes they visit and how many repeat an earlier search of the same step.
// See docs/subsystems/ai-turn.md, "Late game".
//
//   0x00c08be0  query   __thiscall(const DWORD *start, const BYTE *settings), ret 8
//               prepares one search from `start` (packed x | y << 10 | floor << 20)
//               with a settings block (built by 0x00c076d0) and runs it.
//               16 call sites; the AI's own pathfinder is the global 0x00fe8618.
//   0x00c08890  search  __thiscall(int limit), ret 4: the Dijkstra-like loop.
//               Called by the query (0x00c08e13) and directly from 3 more places.
//   0x00c07f80  expand  __thiscall(6 arguments), ret 0x18: once per visited node
//               (one call site, 0x00c08b2d).
// Every call site is patched (call rel32) only if it still calls the expected
// function (or a hook over it, such as the fast paths mod, which is then called
// in turn), so other builds are left alone.
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <string.h>

#include "game_build.h"
#include "log.h"
#include "patch.h"
#include "robot.h"
#include "../mods/fast_paths.h"

#pragma intrinsic(_ReturnAddress)

namespace robot {
namespace {

const DWORD kQuery = 0x00c08be0;
const DWORD kSearch = 0x00c08890;
const DWORD kExpand = 0x00c07f80;
const DWORD kQueryCalls[] = {0x0041eb28, 0x00867851, 0x0086c27c, 0x0086c3e4, 0x0086cf95, 0x008724eb,
                             0x009c20b3, 0x00a798b3, 0x00a79eb9, 0x00a7bf64, 0x00c56ad9, 0x00caacfa,
                             0x00caaf1b, 0x00ce4ba2, 0x00ce623f, 0x00cebb22};
const DWORD kSearchCalls[] = {0x00ce4c26, 0x00cea542, 0x00cebb82};  // not the query's own (0x00c08e13)
const DWORD kExpandCall = 0x00c08b2d;

// pathfinder fields (the settings block is copied to this+0x20 by the query)
const DWORD kOffTarget = 0x3c;    // packed position, or out of the map = no target
const DWORD kOffLimit = 0x40;     // passed to the search
const DWORD kOffExhausted = 0x90; // byte: 1 when the search emptied its heap
const DWORD kSettingsBytes = 0x70;

typedef void(__fastcall *QueryFn)(void *self, void *edx, const DWORD *start, const BYTE *settings);
typedef void(__fastcall *SearchFn)(void *self, void *edx, int limit);
typedef void(__fastcall *ExpandFn)(void *self, void *edx, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f);

struct Site {
	DWORD call;
	LONG count;
	LONG repeats;
	LONG exhausted;
	LONGLONG ticks;
	LONGLONG nodes;
	LONGLONG tiles;
	LONGLONG same;
};
const int kMaxSites = 32;
Site g_sites[kMaxSites];
int g_site_count = 0;

// keys of the searches seen in this AI step (open addressing; 0 = empty)
const int kKeySlots = 8192;
DWORD g_keys[kKeySlots];
int g_key_count = 0;

LONGLONG g_nodes = 0;  // expand calls so far
// what the patched calls went to before (the game's function, or a mod's hook)
QueryFn g_query_next = nullptr;
SearchFn g_search_next = nullptr;
ExpandFn g_expand_next = nullptr;
// per tile (2 floors, 2 layers, 1024 x 1024): the search that last expanded it and
// a hash of the node it expanded, to count tiles and repeat expansions of
// identical node data ("same")
const DWORD kTiles = 2 * 2 * 1024 * 1024;
WORD *g_tile_search = nullptr;
DWORD *g_tile_hash = nullptr;
WORD g_search_id = 0;
LONGLONG g_tiles = 0;      // distinct tiles expanded
LONGLONG g_same = 0;       // expansions of a tile with the same node data as its previous one
LARGE_INTEGER g_freq;
LONG g_details_left = 3000;  // detail lines left for the run
LONGLONG g_detail_min_nodes = 2000;  // searches with fewer nodes get no detail line
bool g_installed = false;

Site &SiteFor(DWORD call)
{
	for (int i = 0; i < g_site_count; ++i)
		if (g_sites[i].call == call)
			return g_sites[i];
	if (g_site_count < kMaxSites) {
		g_sites[g_site_count].call = call;
		return g_sites[g_site_count++];
	}
	return g_sites[kMaxSites - 1];
}

DWORD Hash(DWORD h, const BYTE *p, DWORD n)
{
	for (DWORD i = 0; i < n; ++i)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

// true if this key was already seen in this step
bool SeenBefore(DWORD key)
{
	if (key == 0)
		key = 1;
	if (g_key_count >= kKeySlots / 2)
		return false;
	for (int i = static_cast<int>(key % kKeySlots);; i = (i + 1) % kKeySlots) {
		if (g_keys[i] == key)
			return true;
		if (g_keys[i] == 0) {
			g_keys[i] = key;
			++g_key_count;
			return false;
		}
	}
}

DWORD Field(const void *base, DWORD offset)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(base) + offset);
}

// Fingerprint of a search's result: every node the search reached, in both
// layers. Grids are vectors of 16-byte floor descriptors {?, rows, width,
// height}; rows[y] points to the row (64-byte nodes, or 1-byte marks). A node
// was reached in this search when its mark equals the search's mark (this+0xd8).
//   nodes: this+0xa8 (layer 0), this+0xb4 (layer 1); marks: this+0xc0, this+0xcc
DWORD ResultHash(const void *self, LONG *reached, DWORD fields[16], DWORD flag_bits[32])
{
	DWORD h = 2166136261u;
	*reached = 0;
	for (int i = 0; i < 16; ++i)
		fields[i] = 2166136261u;
	for (int i = 0; i < 32; ++i)
		flag_bits[i] = 2166136261u;
	__try {
		const BYTE mark = *(static_cast<const BYTE *>(self) + 0xd8);
		for (int layer = 0; layer < 2; ++layer) {
			const DWORD nodes_begin = Field(self, layer ? 0xb4 : 0xa8), nodes_end = Field(self, layer ? 0xb8 : 0xac);
			const DWORD marks_begin = Field(self, layer ? 0xcc : 0xc0);
			for (DWORD f = 0; nodes_begin && marks_begin && f < (nodes_end - nodes_begin) / 16 && f < 16; ++f) {
				const BYTE *nf = reinterpret_cast<const BYTE *>(nodes_begin + f * 16);
				const BYTE *mf = reinterpret_cast<const BYTE *>(marks_begin + f * 16);
				const DWORD *node_rows = reinterpret_cast<const DWORD *>(Field(nf, 4));
				const DWORD *mark_rows = reinterpret_cast<const DWORD *>(Field(mf, 4));
				const DWORD w = Field(nf, 8), hgt = Field(nf, 12);
				for (DWORD y = 0; node_rows && mark_rows && y < hgt; ++y) {
					const BYTE *marks = reinterpret_cast<const BYTE *>(mark_rows[y]);
					const BYTE *row = reinterpret_cast<const BYTE *>(node_rows[y]);
					for (DWORD x = 0; x < w; ++x)
						if (marks[x] == mark) {
							++*reached;
							h = Hash(h, reinterpret_cast<const BYTE *>(&x), 4);
							h = Hash(h, reinterpret_cast<const BYTE *>(&y), 4);
							h = Hash(h, row + x * 64, 64);
							for (int i = 0; i < 16; ++i)
								fields[i] = Hash(fields[i], row + x * 64 + i * 4, 4);
							// node flags (dword 1) bit by bit, with the tile, to find bits that differ
							const DWORD flags = *reinterpret_cast<const DWORD *>(row + x * 64 + 4);
							const DWORD tile = (y << 10 | x) * 2;
							for (int i = 0; i < 32; ++i) {
								const DWORD v = tile | ((flags >> i) & 1);
								flag_bits[i] = Hash(flag_bits[i], reinterpret_cast<const BYTE *>(&v), 4);
							}
						}
				}
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
	return h;
}

// More of a search's output, which callers may read besides the grids: the
// list of newly reached tiles (vector of grid node pointers, object at
// pf+0xdc: begin +0, end +4; filled by relax in order of first reach) and what
// is left in the open list (pf+0x10..0x14, 64-byte nodes in heap order).
// Hashed in order, by position for the list and by every compared dword for
// the open list (dwords 7, 9, 13 never read are left out).
void OutputHashes(const void *self, LONG *listed, DWORD *list_hash, LONG *open, DWORD *open_hash)
{
	*list_hash = *open_hash = 2166136261u;
	__try {
		const DWORD list = Field(self, 0xdc);
		if (list) {
			const DWORD *begin = reinterpret_cast<const DWORD *>(Field(reinterpret_cast<const void *>(list), 0));
			const DWORD *end = reinterpret_cast<const DWORD *>(Field(reinterpret_cast<const void *>(list), 4));
			for (const DWORD *p = begin; p && p < end; ++p, ++*listed)
				*list_hash = Hash(*list_hash, reinterpret_cast<const BYTE *>(*p), 4);  // the node's position
		}
		const DWORD *first = reinterpret_cast<const DWORD *>(Field(self, 0x10));
		const DWORD *last = reinterpret_cast<const DWORD *>(Field(self, 0x14));
		for (const DWORD *n = first; n && n < last; n += 16, ++*open)
			for (int i = 0; i < 16; ++i)
				if (i != 7 && i != 9 && i != 13) {
					const DWORD v = i == 1 ? n[i] & ~0x7c00u : n[i];  // flag bits 10-14 are never initialized
					*open_hash = Hash(*open_hash, reinterpret_cast<const BYTE *>(&v), 4);
				}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

// Once per call site: the vtables of the objects the search calls back
// (this+4 object pool, this+0xc valuation, this+0xa4, this+0x70 world), to
// find out which classes they are (analysis/classes.txt).
void LogCallbacks(DWORD call, const void *self)
{
	static DWORD logged[kMaxSites];
	static int count = 0;
	for (int i = 0; i < count; ++i)
		if (logged[i] == call)
			return;
	if (count < kMaxSites)
		logged[count++] = call;
	DWORD vtables[4] = {};
	const DWORD offsets[4] = {0x04, 0x0c, 0xa4, 0x70};
	__try {
		for (int i = 0; i < 4; ++i) {
			const DWORD object = Field(self, offsets[i]);
			vtables[i] = object ? *reinterpret_cast<const DWORD *>(object) : 0;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
	hooklog::Write("path site=%08lx callbacks: +04 %08lx, +0c %08lx, +a4 %08lx, +70 %08lx", call, vtables[0], vtables[1],
	               vtables[2], vtables[3]);
}

void Record(DWORD call, DWORD key, const void *self, DWORD start, LONGLONG ticks, LONGLONG nodes, LONGLONG tiles,
            LONGLONG same)
{
	Site &s = SiteFor(call);
	++s.count;
	s.ticks += ticks;
	s.nodes += nodes;
	s.tiles += tiles;
	s.same += same;
	const bool repeat = SeenBefore(key);
	if (repeat)
		++s.repeats;
	const bool exhausted = *(static_cast<const BYTE *>(self) + kOffExhausted) != 0;
	if (exhausted)
		++s.exhausted;
	if (g_details_left > 0 && nodes >= g_detail_min_nodes) {
		--g_details_left;
		LONG reached = 0;
		DWORD fields[16], flag_bits[32];
		const DWORD result = ResultHash(self, &reached, fields, flag_bits);
		char per_field[16 * 5 + 32 * 3 + 8];
		for (int i = 0; i < 16; ++i)
			_snprintf_s(per_field + i * 5, 6, _TRUNCATE, "%04lx ", fields[i] & 0xffff);
		memcpy(per_field + 80, "bits=", 5);
		for (int i = 0; i < 32; ++i)
			_snprintf_s(per_field + 85 + i * 2, 3, _TRUNCATE, "%02lx", flag_bits[i] & 0xff);
		LONG listed = 0, open = 0;
		DWORD list_hash = 0, open_hash = 0;
		OutputHashes(self, &listed, &list_hash, &open, &open_hash);
		hooklog::Write("path site=%08lx pf=%p start=%lu,%lu,%lu target=%08lx limit=%ld nodes=%lld tiles=%lld same=%lld "
		               "us=%.0f reached=%ld result=%08lx%s%s fields=%s list=%ld/%08lx open=%ld/%08lx ends=%02x%02x",
		               call, self, start & 0x3ff, (start >> 10) & 0x3ff, (start >> 20) & 0xf, Field(self, kOffTarget),
		               static_cast<LONG>(Field(self, kOffLimit)), nodes, tiles, same, ticks * 1e6 / g_freq.QuadPart,
		               reached, result, exhausted ? " exhausted" : "", repeat ? " REPEAT" : "", per_field, listed,
		               list_hash, open, open_hash, static_cast<const BYTE *>(self)[0x90], static_cast<const BYTE *>(self)[0x91]);
		LogCallbacks(call, self);
	}
}

void __fastcall QueryHook(void *self, void *edx, const DWORD *start, const BYTE *settings)
{
	const DWORD call = reinterpret_cast<DWORD>(_ReturnAddress()) - 5;
	const DWORD start_pos = start ? *start : 0;
	DWORD key = Hash(2166136261u, reinterpret_cast<const BYTE *>(&self), sizeof(self));
	key = Hash(key, reinterpret_cast<const BYTE *>(&start_pos), 4);
	if (settings)
		key = Hash(key, settings, kSettingsBytes);
	const LONGLONG nodes0 = g_nodes, tiles0 = g_tiles, same0 = g_same;
	++g_search_id;
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	g_query_next(self, edx, start, settings);
	QueryPerformanceCounter(&t1);
	Record(call, key, self, start_pos, t1.QuadPart - t0.QuadPart, g_nodes - nodes0, g_tiles - tiles0, g_same - same0);
}

// direct searches (without the query): the start is whatever the caller set up
void __fastcall SearchHook(void *self, void *edx, int limit)
{
	const DWORD call = reinterpret_cast<DWORD>(_ReturnAddress()) - 5;
	DWORD key = Hash(2166136261u, reinterpret_cast<const BYTE *>(&self), sizeof(self));
	key = Hash(key, static_cast<const BYTE *>(self) + 0x20, kSettingsBytes);
	key = Hash(key, reinterpret_cast<const BYTE *>(&limit), 4);
	const LONGLONG nodes0 = g_nodes, tiles0 = g_tiles, same0 = g_same;
	++g_search_id;
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	g_search_next(self, edx, limit);
	QueryPerformanceCounter(&t1);
	Record(call, key, self, Field(self, 0x8c), t1.QuadPart - t0.QuadPart, g_nodes - nodes0, g_tiles - tiles0,
	       g_same - same0);
}

void __fastcall ExpandHook(void *self, void *edx, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f)
{
	++g_nodes;
	// a = the node being expanded (64 bytes: packed position, then flags; 0x40 = second layer)
	const DWORD *node = reinterpret_cast<const DWORD *>(a);
	if (g_tile_search && node) {
		const DWORD pos = node[0];
		const DWORD i = ((pos >> 20) & 1) << 21 | ((node[1] >> 6) & 1) << 20 | (pos & 0xfffff);
		const DWORD h = Hash(2166136261u, reinterpret_cast<const BYTE *>(node), 64);
		if (g_tile_search[i] != g_search_id) {
			g_tile_search[i] = g_search_id;
			++g_tiles;
		} else if (g_tile_hash[i] == h) {
			++g_same;
		}
		g_tile_hash[i] = h;
	}
	g_expand_next(self, edx, a, b, c, d, e, f);
}

}  // namespace

void InstallPathTrace(int level)
{
	if (level >= 2) {  // every search, also the small ones
		g_detail_min_nodes = 0;
		g_details_left = 200000;
	}
	if (g_installed || !IsHookable(DetectGameBuild()))
		return;
	g_installed = true;
	QueryPerformanceFrequency(&g_freq);
	g_tile_search = static_cast<WORD *>(VirtualAlloc(nullptr, kTiles * sizeof(WORD), MEM_COMMIT, PAGE_READWRITE));
	g_tile_hash = static_cast<DWORD *>(VirtualAlloc(nullptr, kTiles * sizeof(DWORD), MEM_COMMIT, PAGE_READWRITE));
	if (!g_tile_search || !g_tile_hash)
		g_tile_search = nullptr;
	// The trace is installed after the mods, so its hooks are the outer ones and
	// their return addresses are the game's call sites.
	g_query_next = reinterpret_cast<QueryFn>(
		hookpatch::RedirectCalls(kQueryCalls, sizeof(kQueryCalls) / sizeof(kQueryCalls[0]), kQuery, &QueryHook));
	g_search_next = reinterpret_cast<SearchFn>(
		hookpatch::RedirectCalls(kSearchCalls, sizeof(kSearchCalls) / sizeof(kSearchCalls[0]), kSearch, &SearchHook));
	g_expand_next = reinterpret_cast<ExpandFn>(hookpatch::RedirectCall(kExpandCall, kExpand, &ExpandHook));
	hooklog::Write("robot: path trace: query calls %s, search calls %s, expand call %s", g_query_next ? "patched" : "NOT as expected",
	               g_search_next ? "patched" : "NOT as expected", g_expand_next ? "patched" : "NOT as expected");
}

void PathTraceStep()
{
	if (!g_installed || g_site_count == 0)
		return;
	LONG count = 0, repeats = 0, exhausted = 0;
	LONGLONG ticks = 0, nodes = 0, tiles = 0, same = 0;
	char sites[600];
	size_t used = 0;
	sites[0] = 0;
	for (int i = 0; i < g_site_count; ++i) {
		const Site &s = g_sites[i];
		count += s.count;
		repeats += s.repeats;
		exhausted += s.exhausted;
		ticks += s.ticks;
		nodes += s.nodes;
		tiles += s.tiles;
		same += s.same;
		if (used + 64 < sizeof(sites))
			used += static_cast<size_t>(_snprintf_s(sites + used, sizeof(sites) - used, _TRUNCATE,
			                                        " %08lx:%ld/%ldr/%.0fms/%lldn", s.call, s.count, s.repeats,
			                                        s.ticks * 1e3 / g_freq.QuadPart, s.nodes));
	}
	LONGLONG hits = 0, misses = 0;
	mods::FastPathsStats(&hits, &misses);
	hooklog::Write("paths step searches=%ld repeats=%ld exhausted=%ld nodes=%lld tiles=%lld same=%lld ms=%.1f "
	               "remembered=%lld/%lld sites:%s",
	               count, repeats, exhausted, nodes, tiles, same, ticks * 1e3 / g_freq.QuadPart, hits, hits + misses,
	               sites);
	g_site_count = 0;
	memset(g_sites, 0, sizeof(g_sites));
	memset(g_keys, 0, sizeof(g_keys));
	g_key_count = 0;
}

}  // namespace robot
