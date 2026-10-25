// The hang watchdog (see watchdog.h and docs/subsystems/hangs.md).
//
// Why: on 2026-10-25 the owner's game froze during an AI turn (the host's main
// thread at 100% of a core for minutes, the window "not responding") and
// nothing told us where. This writes it down the next time.
//
// Progress: the game's main loop (0x00c3b490) calls PeekMessageA once per
// frame, in every screen (menus, the map, battles, alt-tabbed, minimized), then
// GetMessageA for each waiting message. Windows calls a window "not responding"
// when its thread stops taking messages, so that is exactly what we count: the
// exe's two import entries go through us (data only, as hookpatch::PatchImport
// does for other imports); each call from the main thread is one beat.
//
// The look, when there was no beat for hang_seconds (default 20 s): 20 times in
// 2 s, the main thread is suspended for a moment (well under a millisecond):
// its registers (GetThreadContext: EIP, ESP) and a copy of its stack (64 KB
// from ESP, ReadProcessMemory) are taken, and it runs again. While it is
// suspended this thread does nothing else: no log line, no memory allocation,
// nothing that could wait for a lock the main thread holds (it would never be
// resumed). Afterwards: where EIP was (a histogram), and the return addresses
// found on the stack (values that point just after a call instruction in code)
// that were there in at least half of the looks, innermost first. Addresses
// are shown as module+offset, for the exe also as its own address (the vanilla
// 3.1 layout, the same in MMH55_Utility) with the name of the function where we
// know it, for system DLLs with the nearest exported function, and for our DLL
// as an offset to look up in the build's map file. Then the mods' own state
// (the last AI steps, the lockstep), and the CPU time of the busiest threads:
// a spinning main thread uses ~2000 ms in those 2 s, a blocked one ~0.
// Once per freeze: when the beats come back, a line says so, and the next
// freeze gets a new look. A long load also stops the beats; it gets one look
// (cheap) and the "moves again" line shows it was only slow.
#include "watchdog.h"

#include <windows.h>
#include <tlhelp32.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "game_build.h"
#include "log.h"
#include "patch.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace watchdog {
namespace {

const int kSamples = 20;
const DWORD kSampleGapMs = 100;
const DWORD kStackBytes = 64 * 1024;
DWORD g_poll_ms = 1000;  // how often the watching thread looks at the beats

bool g_hang_test = false;
DWORD g_stall_ms = 20000;
volatile LONG g_main_id = 0;  // the watched thread (the first one to take the exe's messages)
volatile LONG g_beats = 0;
DWORD g_exe_base = 0, g_dll_base = 0;
char g_exe_name[64] = "exe";
bool g_vanilla_layout = false;  // the exe has the vanilla 3.1 layout (addresses as in our docs)

// --- the mods' state and our function names --------------------------------------------
const int kReporters = 8;
Reporter g_reporters[kReporters];
volatile LONG g_reporter_count = 0;

struct Named {
	DWORD address;
	const char *name;
};
const int kNamed = 48;
Named g_named[kNamed];
volatile LONG g_named_count = 0;

// --- the exe's functions we know (vanilla 3.1 addresses; sizes from Ghidra) ------------
struct Known {
	DWORD address, size;
	const char *name;
};
const Known kKnown[] = {
	{0x00c3b490, 138, "main loop: window messages"},
	{0x00597d70, 3056, "adventure screen update"},
	{0x00608930, 55, "adventure controller update (caller)"},
	{0x006082f0, 1410, "adventure controller update (AI manager)"},
	{0x00604360, 61, "send a command"},
	{0x0086dd40, 1034, "AI turn driver"},
	{0x00867440, 218, "AI stage INIT_DAY"},
	{0x0086d750, 1508, "AI begin of day"},
	{0x0086b400, 480, "AI stage MOVE_HERO"},
	{0x00867520, 198, "AI stage COLLECT_PASSABLE_OBJECTS"},
	{0x0086c760, 620, "AI stage BUILD_HERO_CHOICES"},
	{0x008667d0, 62, "AI stage ESTIMATE_CHOICES"},
	{0x0086c6b0, 174, "AI stage EXECUTE_BEST_CHOICE"},
	{0x0086d4e0, 575, "AI stage MANAGE_TOWNS"},
	{0x0086b0a0, 846, "AI stage HIRE_HEROES"},
	{0x00866700, 21, "AI stage LAST_WAIT"},
	{0x00868eb0, 189, "AI stage RETURN_TURN"},
	{0x0086c570, 314, "AI stage START_CHOOSE_DESTINATION"},
	{0x0086c010, 1371, "AI stage CHOOSE_DESTINATION"},
	{0x00869ad0, 2170, "AI stage MANAGE_HEROES"},
	{0x00868ca0, 335, "AI stage STEP_REARRANGE_TOWN_HEROES"},
	{0x0086a350, 1781, "AI stage STEP_ORDER_CARAVANS"},
	{0x00c08890, 840, "pathfinder search loop"},
	{0x0098e1c0, 430, "world: is the player busy (AI gate)"},
	{0x0098d480, 129, "world: the player whose turn it is"},
	{0x007ca7c0, 456, "multiplayer message pump"},
	{0x007c98e0, 1177, "multiplayer segment runner"},
	{0x007c9810, 202, "multiplayer send"},
	{0x007ca990, 95, "multiplayer own step"},
	{0x007c8dd0, 298, "multiplayer pacing"},
	{0x00475d80, 83, "segment timer step"},
	{0x0047cbd0, 87, "network thread loop"},
	{0x009dfef0, 295, "file system: open a file"},
	{0x007f9e10, 793, "autosave request"},
	{0x008256a0, 266, "multiplayer save"},
	{0x00c9f4a0, 770, "setup: hero pool"},
};

// --- small helpers ------------------------------------------------------------------------
// Reads memory without risking an exception (the kernel copies it).
bool Read(DWORD address, void *out, DWORD size)
{
	SIZE_T got = 0;
	return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), out, size, &got) &&
	       got == size;
}

DWORD ReadDword(DWORD address, DWORD fallback = 0)
{
	DWORD v;
	return Read(address, &v, sizeof(v)) ? v : fallback;
}

// The executable parts of the loaded modules, sorted by address.
struct Code {
	DWORD begin, end, base;
};
const int kCode = 512;
Code g_code[kCode];
int g_code_count = 0;

void FindCode()
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	DWORD p = reinterpret_cast<DWORD>(si.lpMinimumApplicationAddress);
	const DWORD top = reinterpret_cast<DWORD>(si.lpMaximumApplicationAddress);
	g_code_count = 0;
	MEMORY_BASIC_INFORMATION m;
	while (p < top && VirtualQuery(reinterpret_cast<const void *>(p), &m, sizeof(m)) == sizeof(m)) {
		const DWORD begin = reinterpret_cast<DWORD>(m.BaseAddress);
		const DWORD end = begin + static_cast<DWORD>(m.RegionSize);
		if (m.State == MEM_COMMIT && m.Type == MEM_IMAGE && (m.Protect & 0xf0) && !(m.Protect & PAGE_GUARD) &&
		    g_code_count < kCode)
			g_code[g_code_count++] = {begin, end, reinterpret_cast<DWORD>(m.AllocationBase)};
		if (end <= p)
			break;  // the top of the address space
		p = end;
	}
}

const Code *CodeAt(DWORD a)
{
	int lo = 0, hi = g_code_count - 1;
	while (lo <= hi) {
		const int mid = (lo + hi) / 2;
		if (a < g_code[mid].begin)
			hi = mid - 1;
		else if (a >= g_code[mid].end)
			lo = mid + 1;
		else
			return &g_code[mid];
	}
	return nullptr;
}

// The length of "call r/m32" (FF /2) whose ModRM byte is p[1].
int CallLength(const BYTE *p)
{
	const int mod = p[1] >> 6, rm = p[1] & 7;
	if (mod == 3)
		return 2;
	int n = 2;
	if (rm == 4) {
		++n;  // SIB
		if (mod == 0 && (p[2] & 7) == 5)
			n += 4;
	}
	else if (mod == 0 && rm == 5) {
		n += 4;
	}
	if (mod == 1)
		n += 1;
	else if (mod == 2)
		n += 4;
	return n;
}

// Does `ret` come right after a call instruction (so it can be a return address)?
bool AfterCall(DWORD ret)
{
	BYTE b[9];  // b[8 - k] is the byte at ret - k; b[8] is the byte at ret
	if (ret < 8 || !Read(ret - 8, b, sizeof(b)))
		return false;
	if (b[3] == 0xe8) {
		DWORD rel;
		memcpy(&rel, b + 4, 4);
		if (CodeAt(ret + rel))
			return true;
	}
	for (int len = 2; len <= 7; ++len) {
		const BYTE *p = b + 8 - len;
		if (p[0] == 0xff && ((p[1] >> 3) & 7) == 2 && CallLength(p) == len)
			return true;
	}
	return false;
}

// --- names ---------------------------------------------------------------------------------
struct Module {
	DWORD base;
	char name[48];
	DWORD exports, exports_size;  // the export directory (RVA, size), 0 = none
};
const int kModules = 64;
Module g_modules[kModules];
int g_module_count = 0;

const Module *ModuleAt(DWORD base)
{
	for (int i = 0; i < g_module_count; ++i)
		if (g_modules[i].base == base)
			return &g_modules[i];
	if (g_module_count >= kModules)
		return nullptr;
	Module &m = g_modules[g_module_count++];
	memset(&m, 0, sizeof(m));
	m.base = base;
	const DWORD nt = base + ReadDword(base + 0x3c);
	if (ReadDword(nt) == IMAGE_NT_SIGNATURE) {
		const DWORD dir = nt + 4 + sizeof(IMAGE_FILE_HEADER) + offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory);
		m.exports = ReadDword(dir);
		m.exports_size = ReadDword(dir + 4);
	}
	if (base == g_exe_base) {
		lstrcpynA(m.name, g_exe_name, sizeof(m.name));
	}
	else if (base == g_dll_base) {
		lstrcpynA(m.name, "dinput8.dll (open-homm5)", sizeof(m.name));
	}
	else {
		char name[48] = "";
		const DWORD name_rva = m.exports ? ReadDword(base + m.exports + 12) : 0;
		if (!name_rva || !Read(base + name_rva, name, sizeof(name) - 1))
			name[0] = 0;
		name[sizeof(name) - 1] = 0;
		if (name[0])
			lstrcpynA(m.name, name, sizeof(m.name));
		else
			wsprintfA(m.name, "module@%08lx", base);
	}
	return &m;
}

// The exported function of a module nearest below rva (within 2 KB: further away
// it is most likely an internal function of its own), or nullptr.
const char *NearestExport(const Module &m, DWORD rva, DWORD *offset, char *buffer, int size)
{
	if (!m.exports)
		return nullptr;
	IMAGE_EXPORT_DIRECTORY d;
	if (!Read(m.base + m.exports, &d, sizeof(d)) || d.NumberOfNames > 20000 || d.NumberOfFunctions > 20000)
		return nullptr;
	DWORD best = 0, best_name = 0;
	for (DWORD i = 0; i < d.NumberOfNames; ++i) {
		WORD ordinal;
		if (!Read(m.base + d.AddressOfNameOrdinals + 2 * i, &ordinal, 2) || ordinal >= d.NumberOfFunctions)
			continue;
		const DWORD f = ReadDword(m.base + d.AddressOfFunctions + 4 * ordinal);
		if (f > rva || f <= best || (f >= m.exports && f < m.exports + m.exports_size))
			continue;  // above, not better, or a forwarder
		best = f;
		best_name = ReadDword(m.base + d.AddressOfNames + 4 * i);
	}
	if (!best || rva - best > 0x800 || !best_name || !Read(m.base + best_name, buffer, size - 1))
		return nullptr;
	buffer[size - 1] = 0;
	*offset = rva - best;
	return buffer;
}

// "MMH55_Utility_64.exe+0x46e9d0 (0x0086e9d0, AI turn driver+0x90)", "KERNELBASE.dll+0x1234
// (SleepEx+0x5d)", "dinput8.dll (open-homm5)+0x4321 (after PumpHook+0x21)".
void Where(DWORD a, char *out, int size)
{
	const Code *c = CodeAt(a);
	const Module *m = c ? ModuleAt(c->base) : nullptr;
	if (!m) {
		_snprintf_s(out, size, _TRUNCATE, "0x%08lx (not in a module's code)", a);
		return;
	}
	const DWORD rva = a - m->base;
	if (m->base == g_exe_base) {
		const char *name = nullptr;
		DWORD off = 0;
		for (const Known &k : kKnown)
			if (g_vanilla_layout && a >= k.address && a < k.address + k.size) {
				name = k.name;
				off = a - k.address;
			}
		if (name)
			_snprintf_s(out, size, _TRUNCATE, "%s+0x%lx (0x%08lx, %s+0x%lx)", m->name, rva, a, name, off);
		else
			_snprintf_s(out, size, _TRUNCATE, "%s+0x%lx (0x%08lx)", m->name, rva, a);
		return;
	}
	if (m->base == g_dll_base) {
		const Named *best = nullptr;
		for (LONG i = 0; i < g_named_count; ++i)
			if (g_named[i].address <= a && a - g_named[i].address < 0x4000 && (!best || g_named[i].address > best->address))
				best = &g_named[i];
		if (best)
			_snprintf_s(out, size, _TRUNCATE, "%s+0x%lx (after %s+0x%lx)", m->name, rva, best->name, a - best->address);
		else
			_snprintf_s(out, size, _TRUNCATE, "%s+0x%lx", m->name, rva);
		return;
	}
	char name[64];
	DWORD off = 0;
	if (NearestExport(*m, rva, &off, name, sizeof(name)))
		_snprintf_s(out, size, _TRUNCATE, "%s+0x%lx (%s+0x%lx)", m->name, rva, name, off);
	else
		_snprintf_s(out, size, _TRUNCATE, "%s+0x%lx", m->name, rva);
}

// A coarser place, to add up the EIPs of one loop (its instructions differ):
// the known exe function, or the 256-byte block of the exe or our DLL, or the
// module and its nearest export.
void Place(DWORD a, char *out, int size)
{
	const Code *c = CodeAt(a);
	const Module *m = c ? ModuleAt(c->base) : nullptr;
	if (!m) {
		lstrcpynA(out, "outside the modules' code", size);
		return;
	}
	if (m->base == g_exe_base) {
		for (const Known &k : kKnown)
			if (g_vanilla_layout && a >= k.address && a < k.address + k.size) {
				_snprintf_s(out, size, _TRUNCATE, "%s (0x%08lx)", k.name, k.address);
				return;
			}
	}
	if (m->base == g_exe_base || m->base == g_dll_base) {
		_snprintf_s(out, size, _TRUNCATE, "%s+0x%lx..%lx", m->name, (a - m->base) & ~0xffu, ((a - m->base) & ~0xffu) + 0xff);
		return;
	}
	char name[64];
	DWORD off = 0;
	if (NearestExport(*m, a - m->base, &off, name, sizeof(name)))
		_snprintf_s(out, size, _TRUNCATE, "%s!%s", m->name, name);
	else
		lstrcpynA(out, m->name, size);
}

// --- the threads' CPU time ----------------------------------------------------------------
struct ThreadTime {
	DWORD id;
	ULONGLONG ticks;  // kernel + user, 100 ns
};
const int kThreads = 128;

int ThreadTimes(ThreadTime *out)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return 0;
	int n = 0;
	THREADENTRY32 te;
	te.dwSize = sizeof(te);
	const DWORD pid = GetCurrentProcessId();
	for (BOOL ok = Thread32First(snap, &te); ok && n < kThreads; ok = Thread32Next(snap, &te)) {
		if (te.th32OwnerProcessID != pid)
			continue;
		HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
		if (!h)
			continue;
		FILETIME c, e, k, u;
		if (GetThreadTimes(h, &c, &e, &k, &u)) {
			out[n].id = te.th32ThreadID;
			out[n].ticks = (static_cast<ULONGLONG>(k.dwHighDateTime) << 32 | k.dwLowDateTime) +
			               (static_cast<ULONGLONG>(u.dwHighDateTime) << 32 | u.dwLowDateTime);
			++n;
		}
		CloseHandle(h);
	}
	CloseHandle(snap);
	return n;
}

DWORD CpuMs(const ThreadTime *before, int nb, const ThreadTime &after)
{
	for (int i = 0; i < nb; ++i)
		if (before[i].id == after.id)
			return static_cast<DWORD>((after.ticks - before[i].ticks) / 10000);
	return static_cast<DWORD>(after.ticks / 10000);  // a new thread: all of its time
}

// --- the samples ----------------------------------------------------------------------------
BYTE g_stack[kStackBytes];  // one sample's copy of the stack (static: never allocated)

struct Eip {
	DWORD address;
	int count;
};
const int kEips = 64;
Eip g_eips[kEips];
int g_eip_count = 0;

struct Candidate {
	DWORD address;     // 0 = free
	DWORD slot;        // the lowest stack address it was found at
	int samples;       // in how many samples
	int last_sample;
	signed char call;  // -1 not checked yet, 0 no, 1 comes after a call
};
const int kCandidates = 2048;  // a power of 2
Candidate g_candidates[kCandidates];
int g_candidate_count = 0;

Candidate *CandidateFor(DWORD a)
{
	DWORD h = (a * 2654435761u) & (kCandidates - 1);
	for (int probe = 0; probe < kCandidates; ++probe, h = (h + 1) & (kCandidates - 1)) {
		Candidate &c = g_candidates[h];
		if (c.address == a)
			return &c;
		if (!c.address) {
			if (g_candidate_count >= kCandidates * 3 / 4)
				return nullptr;
			++g_candidate_count;
			c = {a, 0xffffffff, 0, -1, -1};
			return &c;
		}
	}
	return nullptr;
}

void AddEip(DWORD eip)
{
	for (int i = 0; i < g_eip_count; ++i)
		if (g_eips[i].address == eip) {
			++g_eips[i].count;
			return;
		}
	if (g_eip_count < kEips)
		g_eips[g_eip_count++] = {eip, 1};
}

// One look at the thread. Between SuspendThread and ResumeThread: only kernel
// calls into our own static memory (see the top of the file).
bool TakeSample(HANDLE thread, DWORD *eip, DWORD *esp, DWORD *bytes)
{
	if (SuspendThread(thread) == static_cast<DWORD>(-1))
		return false;
	CONTEXT ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;  // (also makes sure it is really suspended)
	bool ok = GetThreadContext(thread, &ctx) != 0;
	*bytes = 0;
	if (ok) {
		*eip = ctx.Eip;
		*esp = ctx.Esp;
		MEMORY_BASIC_INFORMATION m;
		if (VirtualQuery(reinterpret_cast<const void *>(ctx.Esp), &m, sizeof(m)) == sizeof(m) && m.State == MEM_COMMIT &&
		    !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
			const DWORD end = reinterpret_cast<DWORD>(m.BaseAddress) + static_cast<DWORD>(m.RegionSize);
			DWORD n = end - ctx.Esp;
			n = (n < kStackBytes ? n : kStackBytes) & ~3u;
			SIZE_T got = 0;
			if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(ctx.Esp), g_stack, n, &got))
				*bytes = static_cast<DWORD>(got) & ~3u;
		}
	}
	ResumeThread(thread);
	return ok;
}

void ScanStack(DWORD esp, DWORD bytes, int sample)
{
	const DWORD *v = reinterpret_cast<const DWORD *>(g_stack);
	for (DWORD i = 0; i < bytes / 4; ++i) {
		if (!CodeAt(v[i]))
			continue;
		Candidate *c = CandidateFor(v[i]);
		if (!c)
			continue;
		if (c->call < 0)
			c->call = AfterCall(v[i]) ? 1 : 0;
		if (!c->call)
			continue;
		const DWORD slot = esp + 4 * i;
		if (slot < c->slot)
			c->slot = slot;
		if (c->last_sample != sample) {
			c->last_sample = sample;
			++c->samples;
		}
	}
}

// --- the report ----------------------------------------------------------------------------
volatile LONG g_reports = 0;
#ifdef WATCHDOG_TEST
TestResult g_test = {};
DWORD g_test_expect = 0;
DWORD g_test_lo = 0, g_test_hi = 0;
#endif

void Report(DWORD stalled_ms)
{
	const DWORD id = static_cast<DWORD>(g_main_id);
	hooklog::Write("watchdog: the game's main thread (%lu) has not taken its window messages for %lu s (Windows calls "
	               "that \"not responding\"): looking where it is, %d times in %lu s; nothing is stopped or changed",
	               id, stalled_ms / 1000, kSamples, kSamples * kSampleGapMs / 1000);
	HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
	if (!thread) {
		hooklog::Write("watchdog: cannot open the main thread (error %lu): no look", GetLastError());
		return;
	}
	FindCode();
	g_eip_count = 0;
	memset(g_candidates, 0, sizeof(g_candidates));
	g_candidate_count = 0;
	static ThreadTime before[kThreads], after[kThreads];
	const int nb = ThreadTimes(before);
	const LONG beats = g_beats;
	const DWORD t0 = GetTickCount();
	int samples = 0;
	bool moved = false;
	for (int i = 0; i < kSamples; ++i) {
		if (g_beats != beats) {
			moved = true;
			break;
		}
		DWORD eip = 0, esp = 0, bytes = 0;
		if (TakeSample(thread, &eip, &esp, &bytes)) {
			AddEip(eip);
			ScanStack(esp, bytes, samples);
			++samples;
		}
		Sleep(kSampleGapMs);
	}
	const DWORD window = GetTickCount() - t0;
	const int na = ThreadTimes(after);
	CloseHandle(thread);
	InterlockedIncrement(&g_reports);

	// the main thread's CPU time: busy (a loop) or waiting (blocked)?
	DWORD main_ms = 0;
	for (int i = 0; i < na; ++i)
		if (after[i].id == id)
			main_ms = CpuMs(before, nb, after[i]);
	if (moved)
		hooklog::Write("watchdog: the main thread took its messages again during the look (%d samples): a long load or "
		               "a long step, not a hang", samples);
	if (!samples && !moved)
		hooklog::Write("watchdog: the main thread could not be looked at (it ended, or SuspendThread / GetThreadContext "
		               "failed)");
	hooklog::Write("watchdog: the main thread used %lu ms of CPU in those %lu ms: %s", main_ms, window,
	               main_ms * 2 > window ? "busy (it is running: an endless or very long loop)"
	                                    : "mostly waiting (blocked: on a lock, a wait or the system)");

	// where EIP was: by place (a loop's instructions together), then exactly
	char where[200];
	struct Group {
		char place[96];
		int count;
	};
	static Group groups[16];
	int group_count = 0;
	for (int i = 0; i < g_eip_count; ++i) {
		char place[96];
		Place(g_eips[i].address, place, sizeof(place));
		int g = 0;
		while (g < group_count && strcmp(groups[g].place, place) != 0)
			++g;
		if (g == group_count) {
			if (group_count == 16)
				continue;
			lstrcpynA(groups[group_count].place, place, sizeof(groups[0].place));
			groups[group_count++].count = 0;
		}
		groups[g].count += g_eips[i].count;
	}
	{
		char line[900] = "";
		size_t used = 0;
		for (int shown = 0; shown < group_count; ++shown) {
			int best = 0;
			for (int g = 1; g < group_count; ++g)
				if (groups[g].count > groups[best].count)
					best = g;
			if (groups[best].count <= 0)
				break;
			const int n = _snprintf_s(line + used, sizeof(line) - used, _TRUNCATE, "%s%s %d", used ? "; " : "",
			                          groups[best].place, groups[best].count);
			groups[best].count = -1;
			if (n < 0)
				break;
			used += static_cast<size_t>(n);
		}
		hooklog::Write("watchdog: EIP by place (of %d samples): %s", samples, line);
	}
#ifdef WATCHDOG_TEST
	g_test.eip_in_range = 0;
	for (int i = 0; i < g_eip_count; ++i)
		if (g_eips[i].address >= g_test_lo && g_eips[i].address < g_test_hi)
			g_test.eip_in_range += g_eips[i].count;
#endif
	for (int shown = 0; shown < 8; ++shown) {
		int best = -1;
		for (int i = 0; i < g_eip_count; ++i)
			if (g_eips[i].count > 0 && (best < 0 || g_eips[i].count > g_eips[best].count))
				best = i;
		if (best < 0)
			break;
		Where(g_eips[best].address, where, sizeof(where));
		hooklog::Write("watchdog: EIP %2d/%d at %s", g_eips[best].count, samples, where);
#ifdef WATCHDOG_TEST
		if (shown == 0) {
			g_test.top_eip = g_eips[best].address;
			g_test.top_count = g_eips[best].count;
		}
#endif
		g_eips[best].count = -g_eips[best].count;  // shown
	}

	// the return addresses that stayed on the stack (in half of the samples or
	// more), innermost (lowest stack address) first
	static const Candidate *kept[kCandidates];
	int hits = 0;
	for (const Candidate &c : g_candidates)
		if (c.address && c.call == 1 && samples > 0 && c.samples * 2 >= samples)
			kept[hits++] = &c;
	for (int i = 1; i < hits; ++i)  // insertion sort by stack address (a few dozen)
		for (int j = i; j > 0 && kept[j]->slot < kept[j - 1]->slot; --j) {
			const Candidate *t = kept[j];
			kept[j] = kept[j - 1];
			kept[j - 1] = t;
		}
	for (int i = 0; i < hits && i < 28; ++i) {
		Where(kept[i]->address, where, sizeof(where));
		hooklog::Write("watchdog: stack %2d/%d at %08lx: %s", kept[i]->samples, samples, kept[i]->slot, where);
	}
#ifdef WATCHDOG_TEST
	for (int i = 0; i < hits; ++i)
		g_test.found_return = g_test.found_return || kept[i]->address == g_test_expect;
#endif
	if (hits > 28)
		hooklog::Write("watchdog: (%d more return addresses further up the stack)", hits - 28);
	if (!hits)
		hooklog::Write("watchdog: no return address stayed on the stack in half of the samples");

	// the mods' own state
	for (LONG i = 0; i < g_reporter_count; ++i)
		g_reporters[i]();

	// the busiest threads in those 2 s (the main thread is one of them when it spins)
	char line[600] = "";
	size_t used = 0;
	bool taken[kThreads] = {};
	for (int shown = 0; shown < 5; ++shown) {
		int best = -1;
		DWORD best_ms = 0;
		for (int i = 0; i < na; ++i) {
			const DWORD ms = CpuMs(before, nb, after[i]);
			if (!taken[i] && after[i].id != GetCurrentThreadId() && (best < 0 || ms > best_ms)) {
				best = i;
				best_ms = ms;
			}
		}
		if (best < 0)
			break;
		taken[best] = true;
		const int n = _snprintf_s(line + used, sizeof(line) - used, _TRUNCATE, "%s%lu%s %lu ms", shown ? ", " : "",
		                          after[best].id, after[best].id == id ? " (main)" : "", best_ms);
		if (n < 0)
			break;
		used += static_cast<size_t>(n);
	}
	hooklog::Write("watchdog: the busiest threads in those %lu ms (thread, CPU time): %s", window, line);
	const DWORD stamp = ReadDword(g_dll_base + ReadDword(g_dll_base + 0x3c) + 8);
	hooklog::Write("watchdog: our DLL is at %08lx (link time stamp %08lx): its offsets are in that build's "
	               "build\\hook\\dinput8.map (tools/hang/symbolize.py reads this report with it); end of the look",
	               g_dll_base, stamp);
#ifdef WATCHDOG_TEST
	g_test.reports = g_reports;
	g_test.samples = samples;
	g_test.busy_ms = main_ms;
	g_test.window_ms = window;
	g_test.stack_hits = hits;
#endif
}

DWORD WINAPI WatchThread(void *)
{
	LONG seen = g_beats;
	DWORD since = GetTickCount();
	bool reported = false;
	bool armed = false;  // the main thread took its messages at least once
	for (;;) {
		Sleep(g_poll_ms);
		const LONG beats = g_beats;
		const DWORD now = GetTickCount();
		if (beats != seen) {
			if (reported)
				hooklog::Write("watchdog: the main thread takes its messages again, after %lu s", (now - since) / 1000);
			seen = beats;
			since = now;
			reported = false;
			armed = true;
			continue;
		}
		if (!armed || reported || now - since < g_stall_ms)
			continue;
		reported = true;
		Report(now - since);
	}
}

// --- the heartbeat: the exe's message calls -------------------------------------------------
void Beat()
{
	g_beats = g_beats + 1;  // only the watched thread writes it
}

bool OurWindowInFront()
{
	const HWND front = GetForegroundWindow();
	DWORD pid = 0;
	if (front)
		GetWindowThreadProcessId(front, &pid);
	return pid == GetCurrentProcessId();
}

// [debug] hang_test=1: F12 freezes the main thread on purpose (a busy loop, as in
// the owner's freeze), long enough for one look.
__declspec(noinline) void HangTestSpin(DWORD ms)
{
	const DWORD start = GetTickCount();
	volatile DWORD spins = 0;
	while (GetTickCount() - start < ms)
		spins = spins + 1;
}

void HangTest(const char *why)
{
	const DWORD ms = g_stall_ms + 5000 > 25000 ? g_stall_ms + 5000 : 25000;
	hooklog::Write("watchdog: hang test (%s): the main thread now spins for %lu s on purpose", why, ms / 1000);
	HangTestSpin(ms);
	hooklog::Write("watchdog: hang test over");
}

void HangTestKey()
{
	static bool was_down = false;
	const bool down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0 && OurWindowInFront();
	if (down && !was_down)
		HangTest("F12, [debug] hang_test=1");
	was_down = down;
}

DWORD g_hang_test_at_ms = 0;  // [debug] hang_test_at (robot tests): once, this long after the first beat
DWORD g_first_beat = 0;

void OnMessages()
{
	if (g_main_id != static_cast<LONG>(GetCurrentThreadId()))
		return;  // another of the exe's threads (a short-lived one takes messages at start-up)
	Beat();
	if (!g_first_beat)
		g_first_beat = GetTickCount() | 1;
	if (g_hang_test)
		HangTestKey();
	if (g_hang_test_at_ms && GetTickCount() - g_first_beat >= g_hang_test_at_ms) {
		g_hang_test_at_ms = 0;
		HangTest("[debug] hang_test_at");
	}
}

typedef BOOL(WINAPI *PeekFn)(LPMSG msg, HWND window, UINT first, UINT last, UINT remove);
typedef BOOL(WINAPI *GetFn)(LPMSG msg, HWND window, UINT first, UINT last);
PeekFn g_peek_next = nullptr;
GetFn g_get_next = nullptr;

BOOL WINAPI PeekHook(LPMSG msg, HWND window, UINT first, UINT last, UINT remove)
{
	OnMessages();
	return g_peek_next(msg, window, first, last, remove);
}

BOOL WINAPI GetHook(LPMSG msg, HWND window, UINT first, UINT last)
{
	OnMessages();
	return g_get_next(msg, window, first, last);
}

void Common()
{
	g_exe_base = reinterpret_cast<DWORD>(GetModuleHandleW(nullptr));
	g_dll_base = reinterpret_cast<DWORD>(&__ImageBase);
	wchar_t path[MAX_PATH];
	const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
	DWORD start = n;
	while (start > 0 && path[start - 1] != L'\\')
		--start;
	if (n && !WideCharToMultiByte(CP_UTF8, 0, path + start, -1, g_exe_name, sizeof(g_exe_name), nullptr, nullptr))
		lstrcpynA(g_exe_name, "exe", sizeof(g_exe_name));
	NameFunction(&WatchThread, "watchdog thread");
	NameFunction(&HangTestSpin, "watchdog hang test");
	NameFunction(&PeekHook, "watchdog PeekMessageA hook");
	NameFunction(&GetHook, "watchdog GetMessageA hook");
}

bool StartThread()
{
	HANDLE t = CreateThread(nullptr, 0, &WatchThread, nullptr, 0, nullptr);
	if (!t)
		return false;
	CloseHandle(t);
	return true;
}

}  // namespace

void AddReporter(Reporter reporter)
{
	const LONG i = InterlockedIncrement(&g_reporter_count) - 1;
	if (i < kReporters)
		g_reporters[i] = reporter;
	else
		InterlockedDecrement(&g_reporter_count);
}

void NameFunction(const void *function, const char *name)
{
	const LONG i = InterlockedIncrement(&g_named_count) - 1;
	if (i < kNamed)
		g_named[i] = {reinterpret_cast<DWORD>(function), name};
	else
		InterlockedDecrement(&g_named_count);
}

void Install()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	if (!GetPrivateProfileIntW(L"debug", L"hang_watchdog", 1, path)) {
		hooklog::Write("watchdog: off ([debug] hang_watchdog=0)");
		return;
	}
	const DWORD seconds = GetPrivateProfileIntW(L"debug", L"hang_seconds", 20, path);
	if (seconds < 5 || seconds > 600)
		hooklog::Write("watchdog: hang_seconds must be 5..600, not %lu: using 20", seconds);
	else
		g_stall_ms = seconds * 1000;
	g_hang_test = GetPrivateProfileIntW(L"debug", L"hang_test", 0, path) != 0;
	g_hang_test_at_ms = GetPrivateProfileIntW(L"debug", L"hang_test_at", 0, path) * 1000;
	Common();
	// The thread that makes the DirectInput object (we are called from there) is the
	// game's main thread, the one that runs its frames and the AI. (Not "the first
	// thread to take the exe's messages": a short-lived start-up thread does that.)
	g_main_id = static_cast<LONG>(GetCurrentThreadId());
	const GameBuild build = DetectGameBuild();
	g_vanilla_layout = build == GameBuild::Vanilla31 || build == GameBuild::Mmh55Utility;
	g_peek_next = reinterpret_cast<PeekFn>(hookpatch::PatchImport("USER32.dll", "PeekMessageA", reinterpret_cast<void *>(&PeekHook)));
	g_get_next = reinterpret_cast<GetFn>(hookpatch::PatchImport("USER32.dll", "GetMessageA", reinterpret_cast<void *>(&GetHook)));
	if (!g_peek_next || !g_get_next) {
		hooklog::Write("watchdog: the exe does not take its messages as expected (PeekMessageA, GetMessageA): off");
		return;
	}
	if (!StartThread()) {
		hooklog::Write("watchdog: cannot start its thread (error %lu): off", GetLastError());
		return;
	}
	hooklog::Write("watchdog: on: if the game stops taking its window messages for %lu s, open-homm5.log gets where it "
	               "is stuck (once per freeze; it only looks)%s%s",
	               g_stall_ms / 1000, g_hang_test ? "; hang test on: F12 freezes the game on purpose" : "",
	               g_hang_test_at_ms ? "; hang test on: the game freezes on purpose once (hang_test_at)" : "");
}

#ifdef WATCHDOG_TEST
void StartForTest(DWORD thread, DWORD stall_ms)
{
	Common();
	g_stall_ms = stall_ms;
	g_poll_ms = 200;
	g_main_id = static_cast<LONG>(thread);
	StartThread();
}

void BeatForTest()
{
	Beat();
}

TestResult LastResultForTest()
{
	return g_test;
}

void ExpectReturnForTest(DWORD address)
{
	g_test_expect = address;
}

void ExpectEipForTest(DWORD from, DWORD to)
{
	g_test_lo = from;
	g_test_hi = to;
}
#endif

}  // namespace watchdog
