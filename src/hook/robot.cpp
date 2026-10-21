#include "robot.h"

#include <windows.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_build.h"
#include "log.h"

namespace robot {
namespace {

bool g_active = false;
char g_home[MAX_PATH] = {};
long g_quit_after_turns = 0;
DWORD g_quit_after_seconds = 0;
volatile LONG g_turns = 0;
wchar_t g_command[1024] = {};      // console command to run once the main menu is up
DWORD g_command_delay_ms = 10000;  // after the first message-loop call
DWORD g_first_peek = 0;
bool g_command_done = false;  // true once the first script step has run

// The robot script: "step=<seconds>|console|<text>", "step=<seconds>|menu|<page> <command>",
// "step=<seconds>|ui|<screen class> <command>", "step=<seconds>|msg|<name> [number]"
// (robot_ui.cpp), "step=<seconds>|map|<part of a map name>" (robot_lobby.cpp) or
// "step=<seconds>|shot|<label>" (screenshot of the next frame).
struct Step {
	DWORD at_ms;
	char kind;  // 'c' console, 'm' menu, 'u' ui, 'g' game message, 'p' pick map, 'j' pick game, 'k' peek,
	            // 'w' waiting-room slots, 'a' slot arrow, 'v' memory, 's' screenshot
	char text[512];
};
const int kMaxSteps = 32;
Step g_steps[kMaxSteps];
int g_step_count = 0;
int g_next_step = 0;
DWORD g_end_turn_ms = 0;     // 0 = never
bool g_trace_segments = false;
bool g_trace_mp = false;
DWORD g_segment_ms = 0;  // 0 = leave the game's 200 ms
DWORD g_press_enter_ms = 0;  // 0 = never
DWORD g_last_enter = 0;
wchar_t g_repeat_command[256] = {};  // console command run every repeat_every_seconds
DWORD g_repeat_ms = 0;
DWORD g_last_repeat = 0;
volatile DWORD g_last_peek_tick = 0;  // the main loop's last PeekMessageA call
HANDLE g_main_thread = nullptr;
char g_shot_dir[MAX_PATH] = {};  // screenshots (robot_screen.cpp) go here
DWORD g_shot_ms = 0;             // 0 = only "shot" steps
DWORD g_last_shot = 0;
bool g_net_log = false;  // robot_net.cpp
bool g_share_udp_ports = false;
char g_relay_ports[64] = {};
int g_net_log_packets = -1;  // -1 = robot_net.cpp's default

// --- running a console command the way WinMain does (0x00d112a7) ---------------
//   push L"mainmenu"; lea ecx,[str]; call 0x00456220   build a game wide string
//   lea ecx,[str];  call 0x00c125f0                     execute it as a console command
//   push str.begin; call 0x00879240                     free the string's buffer
struct GameWString {
	wchar_t *begin, *end, *capacity;
};
typedef GameWString *(__thiscall *WStringCtorFn)(GameWString *self, const wchar_t *text);  // ret 4
typedef void(__thiscall *ConsoleExecFn)(GameWString *command);                             // ret
typedef void(__cdecl *GameFreeFn)(void *block);
const DWORD kWStringCtor = 0x00456220;
const DWORD kConsoleExec = 0x00c125f0;
const DWORD kGameFree = 0x00879240;

bool RunConsoleCommand(const wchar_t *line)
{
	if (!IsHookable(DetectGameBuild()))
		return false;
	__try {
		GameWString s;
		reinterpret_cast<WStringCtorFn>(kWStringCtor)(&s, line);
		reinterpret_cast<ConsoleExecFn>(kConsoleExec)(&s);
		reinterpret_cast<GameFreeFn>(kGameFree)(s.begin);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// --- copying the game's own text output into our log ----------------------------
// The game prints console/debug messages through these imports. In robot mode we
// copy their text into open-homm5.log (and still pass every call through).
typedef void(WINAPI *OutputDebugStringAFn)(LPCSTR);
OutputDebugStringAFn g_real_OutputDebugStringA = nullptr;
typedef int(__cdecl *VfprintfFn)(FILE *, const char *, va_list);
typedef int(__cdecl *VfwprintfFn)(FILE *, const wchar_t *, va_list);
VfprintfFn g_crt_vfprintf = nullptr;    // from the game's msvcr71.dll: its FILE* are not ours
VfwprintfFn g_crt_vfwprintf = nullptr;

void LogGameText(const char *source, const char *text)
{
	char line[1024];
	lstrcpynA(line, text, sizeof(line));
	size_t n = strlen(line);
	while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
		line[--n] = 0;
	if (n)
		hooklog::Write("game-%s: %s", source, line);
}

void WINAPI RobotOutputDebugStringA(LPCSTR text)
{
	if (text)
		LogGameText("debug", text);
	g_real_OutputDebugStringA(text);
}

int __cdecl RobotPrintf(const char *fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	const int n = _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
	va_end(args);
	LogGameText("printf", buf);
	return n;
}

int __cdecl RobotFprintf(FILE *file, const char *fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	_vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
	va_end(args);
	LogGameText("fprintf", buf);
	va_start(args, fmt);
	const int n = g_crt_vfprintf ? g_crt_vfprintf(file, fmt, args) : -1;
	va_end(args);
	return n;
}

int __cdecl RobotFwprintf(FILE *file, const wchar_t *fmt, ...)
{
	wchar_t wbuf[1024];
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	_vsnwprintf_s(wbuf, 1024, _TRUNCATE, fmt, args);
	va_end(args);
	WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, sizeof(buf), nullptr, nullptr);
	LogGameText("fwprintf", buf);
	va_start(args, fmt);
	const int n = g_crt_vfwprintf ? g_crt_vfwprintf(file, fmt, args) : -1;
	va_end(args);
	return n;
}

// --- keeping the game "active" while it runs in the background -------------------
// The game stops reading input and sleeps 40 ms per frame when its window is not
// the active one (WinMain main loop). That would block the robot keyboard and
// distort AI timings, so in robot mode its window procedure never hears that it
// lost activation or focus.
// Every window class the game registers is wrapped (except helper windows); the
// original procedure is looked up by class name.
struct WrappedClass {
	char name[64];
	WNDPROC proc;
};
WrappedClass g_classes[8];
int g_class_count = 0;
HWND g_game_hwnd = nullptr;

WNDPROC OriginalProc(HWND hwnd)
{
	char name[64];
	GetClassNameA(hwnd, name, sizeof(name));
	for (int i = 0; i < g_class_count; ++i) {
		if (_stricmp(g_classes[i].name, name) == 0)
			return g_classes[i].proc;
	}
	return DefWindowProcA;
}

LRESULT CALLBACK RobotWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (!g_game_hwnd && GetParent(hwnd) == nullptr) {
		g_game_hwnd = hwnd;
		char name[64];
		GetClassNameA(hwnd, name, sizeof(name));
		hooklog::Write("robot: game window %p (class \"%s\"): keeping it active in the background", hwnd, name);
	}
	switch (msg) {
	case WM_ACTIVATEAPP:
		wp = TRUE;
		break;
	case WM_ACTIVATE:
		if (LOWORD(wp) == WA_INACTIVE)
			wp = MAKEWPARAM(WA_ACTIVE, HIWORD(wp));
		break;
	case WM_NCACTIVATE:
		wp = TRUE;
		break;
	case WM_KILLFOCUS:
		return 0;
	}
	return CallWindowProcA(OriginalProc(hwnd), hwnd, msg, wp, lp);
}

typedef ATOM(WINAPI *RegisterClassAFn)(const WNDCLASSA *);
typedef ATOM(WINAPI *RegisterClassExAFn)(const WNDCLASSEXA *);
RegisterClassAFn g_real_RegisterClassA = nullptr;
RegisterClassExAFn g_real_RegisterClassExA = nullptr;

// Returns true if this class should be wrapped, and remembers its original procedure.
bool Remember(const char *name, WNDPROC proc)
{
	if (!name || IS_INTRESOURCE(name)) {
		hooklog::Write("robot: window class registered by atom, not wrapped");
		return false;
	}
	// helper windows: the screen fade overlay runs on its own thread, the splash screen is short-lived
	if (strstr(name, "ScreenFade") || strstr(name, "Splash") || g_class_count == 8) {
		hooklog::Write("robot: window class \"%s\" left alone", name);
		return false;
	}
	lstrcpynA(g_classes[g_class_count].name, name, sizeof(g_classes[0].name));
	g_classes[g_class_count].proc = proc;
	++g_class_count;
	hooklog::Write("robot: wrapping window class \"%s\"", name);
	return true;
}

ATOM WINAPI RobotRegisterClassA(const WNDCLASSA *wc)
{
	if (wc && Remember(wc->lpszClassName, wc->lpfnWndProc)) {
		WNDCLASSA copy = *wc;
		copy.lpfnWndProc = &RobotWndProc;
		return g_real_RegisterClassA(&copy);
	}
	return g_real_RegisterClassA(wc);
}

ATOM WINAPI RobotRegisterClassExA(const WNDCLASSEXA *wc)
{
	if (wc && Remember(wc->lpszClassName, wc->lpfnWndProc)) {
		WNDCLASSEXA copy = *wc;
		copy.lpfnWndProc = &RobotWndProc;
		return g_real_RegisterClassExA(&copy);
	}
	return g_real_RegisterClassExA(wc);
}

typedef BOOL(WINAPI *PeekMessageAFn)(LPMSG, HWND, UINT, UINT, UINT);
PeekMessageAFn g_real_PeekMessageA = nullptr;

// The game's main loop pumps window messages every frame on its main thread,
// a safe moment to run console commands.
BOOL WINAPI RobotPeekMessageA(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT remove)
{
	const BOOL result = g_real_PeekMessageA(msg, hwnd, min, max, remove);
	g_last_peek_tick = GetTickCount();
	if (!g_main_thread)
		g_main_thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, GetCurrentThreadId());
	// The main loop (0x00c3b490) sets the game's "active" flags from queued
	// WM_ACTIVATEAPP messages (0x00c3b3c0); while inactive it sleeps 40 ms per
	// frame. Keep it active: flip deactivation messages and hold the flags at 1.
	if (result && msg && msg->message == WM_ACTIVATEAPP && msg->wParam == 0)
		msg->wParam = 1;
	if (IsHookable(DetectGameBuild())) {
		*reinterpret_cast<volatile BYTE *>(0x00f7a207) = 1;  // "app active" (read by 0x00c3b0a0)
		*reinterpret_cast<volatile BYTE *>(0x00f7a208) = 1;
	}
	static bool activated = false;
	if (g_game_hwnd && !activated) {  // tell the game once that it is active
		activated = true;
		SendMessageA(g_game_hwnd, WM_ACTIVATEAPP, TRUE, 0);
		SendMessageA(g_game_hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
		hooklog::Write("robot: sent activation to the game window");
	}
	// The robot script: steps run in order, each at its time after start-up.
	if (g_next_step < g_step_count) {
		const DWORD now = GetTickCount();
		if (!g_first_peek)
			g_first_peek = now;
		const Step &s = g_steps[g_next_step];
		if (now - g_first_peek >= s.at_ms) {
			++g_next_step;
			g_command_done = true;
			g_last_enter = now;
			if (s.kind == 's') {
				RequestScreenshot(s.text);
			}
			else if (s.kind == 'p') {  // "<part of a map name>"
				PickMap(s.text);
			}
			else if (s.kind == 'v') {  // "<hex address> ...": log game variables (dwords)
				char line[256] = "robot: memory:";
				for (const char *p = s.text; p && *p;) {
					char *end = nullptr;
					const DWORD address = strtoul(p, &end, 16);
					if (end == p)
						break;
					char item[40];
					__try {
						_snprintf_s(item, sizeof(item), _TRUNCATE, " [%08lx]=%ld", address,
						            *reinterpret_cast<const LONG *>(address));
					}
					__except (EXCEPTION_EXECUTE_HANDLER) {
						_snprintf_s(item, sizeof(item), _TRUNCATE, " [%08lx]=?", address);
					}
					strncat_s(line, sizeof(line), item, _TRUNCATE);
					p = end;
				}
				hooklog::Write("%s", line);
			}
			else if (s.kind == 'a') {  // "<row> left|right": a waiting-room state arrow
				PressSlotArrow(s.text);
			}
			else if (s.kind == 'w') {  // research: the waiting room's player slots
				LogWaitSlots();
			}
			else if (s.kind == 'j') {  // "<number>": a game in the LAN lobby's list
				PickGame(atoi(s.text));
			}
			else if (s.kind == 'k') {  // "<screen class> <hex offset> ..."
				Peek(s.text);
			}
			else if (s.kind == 'g') {  // "<message name> [number]"
				char name[96] = {};
				const char *space = strchr(s.text, ' ');
				const size_t len = space ? static_cast<size_t>(space - s.text) : strlen(s.text);
				lstrcpynA(name, s.text, static_cast<int>(len < sizeof(name) ? len + 1 : sizeof(name)));
				PostGameMessage(name, space ? atoi(space + 1) : 0);
			}
			else if (s.kind == 'u') {  // "<screen class> <command>"
				char screen[96] = {};
				const char *space = strchr(s.text, ' ');
				const size_t len = space ? static_cast<size_t>(space - s.text) : strlen(s.text);
				lstrcpynA(screen, s.text, static_cast<int>(len < sizeof(screen) ? len + 1 : sizeof(screen)));
				UiCommand(screen, space ? space + 1 : "");
			}
			else if (s.kind == 'm') {
				char page[32] = {};
				const char *space = strchr(s.text, ' ');
				const size_t len = space ? static_cast<size_t>(space - s.text) : strlen(s.text);
				lstrcpynA(page, s.text, static_cast<int>(len < sizeof(page) ? len + 1 : sizeof(page)));
				MenuCommand(page, space ? space + 1 : "");
			}
			else {
				wchar_t wide[512];
				MultiByteToWideChar(CP_ACP, 0, s.text, -1, wide, 512);
				hooklog::Write("robot: running console command: %s", s.text);
				hooklog::Write(RunConsoleCommand(wide) ? "robot: console command returned"
				                                       : "robot: console command FAILED (unknown build or crash caught)");
			}
		}
	}
	// End the passive human player's turn when no AI is playing.
	InstallEndTurnHelper();
	if (g_trace_segments)
		InstallSegmentTrace();
	if (g_trace_mp)
		InstallMpTrace();
	if (g_segment_ms)
		SetSegmentLength(g_segment_ms);
	if (g_command_done)
		MaybeEndHumanTurn(g_end_turn_ms, 3000);
	// Screenshots now and then.
	if (g_shot_ms) {
		const DWORD now = GetTickCount();
		if (now - g_last_shot >= g_shot_ms) {
			g_last_shot = now;
			RequestScreenshot("");
		}
	}
	// Repeat a console command now and then.
	if (g_command_done && g_repeat_ms && g_repeat_command[0]) {
		const DWORD now = GetTickCount();
		if (now - g_last_repeat >= g_repeat_ms) {
			g_last_repeat = now;
			const bool ok = RunConsoleCommand(g_repeat_command);
			hooklog::Write("robot: repeat command \"%ls\" %s", g_repeat_command, ok ? "ran" : "FAILED");
		}
	}
	// Press Enter now and then: ends a passive human player's turn, confirms dialogs.
	if (g_command_done && g_press_enter_ms) {
		const DWORD now = GetTickCount();
		if (now - g_last_enter >= g_press_enter_ms) {
			g_last_enter = now;
			const bool via_dinput = PressKey(0x1C);  // DIK_RETURN
			// ...and as window messages to the main game window ("H5"), which is
			// where the game seems to read its keyboard during play.
			if (g_game_hwnd) {
				const UINT scan = MapVirtualKeyA(VK_RETURN, MAPVK_VK_TO_VSC);
				PostMessageA(g_game_hwnd, WM_KEYDOWN, VK_RETURN, 1 | (scan << 16));
				PostMessageA(g_game_hwnd, WM_KEYUP, VK_RETURN, 1 | (scan << 16) | 0xC0000000);
			}
			hooklog::Write("robot: pressing Enter (DirectInput: %s, window message: %s)", via_dinput ? "yes" : "busy",
			               g_game_hwnd ? "yes" : "no window");
		}
	}
	return result;
}

// --- running several copies of the game at once --------------------------------
// WinMain refuses to start a second copy: CreateMutexA("NIVAL_H5"), then a
// message box if it already exists. Robot copies get their own mutex name.
typedef HANDLE(WINAPI *CreateMutexAFn)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
CreateMutexAFn g_real_CreateMutexA = nullptr;

HANDLE WINAPI RobotCreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name)
{
	if (name && strcmp(name, "NIVAL_H5") == 0) {
		char own[64];
		_snprintf_s(own, sizeof(own), _TRUNCATE, "NIVAL_H5-robot-%lu", GetCurrentProcessId());
		hooklog::Write("robot: single-copy mutex renamed to %s", own);
		return g_real_CreateMutexA(sa, owner, own);
	}
	return g_real_CreateMutexA(sa, owner, name);
}

typedef HRESULT(WINAPI *SHGetFolderPathAFn)(HWND, int, HANDLE, DWORD, LPSTR);
SHGetFolderPathAFn g_real_SHGetFolderPathA = nullptr;

// Replacement for SHGetFolderPathA: "My Documents" becomes the robot home.
HRESULT WINAPI RobotSHGetFolderPathA(HWND hwnd, int csidl, HANDLE token, DWORD flags, LPSTR path)
{
	if ((csidl & 0xff) == CSIDL_PERSONAL) {
		lstrcpynA(path, g_home, MAX_PATH);
		CreateDirectoryA(g_home, nullptr);
		hooklog::Write("robot: Documents -> %s", g_home);
		return S_OK;
	}
	return g_real_SHGetFolderPathA(hwnd, csidl, token, flags, path);
}

// One import address table entry: the loader wrote the function's address there.
void *PatchSlot(IMAGE_THUNK_DATA32 *slot, void *replacement)
{
	DWORD old;
	if (!VirtualProtect(&slot->u1.Function, sizeof(DWORD), PAGE_READWRITE, &old))
		return nullptr;
	void *original = reinterpret_cast<void *>(slot->u1.Function);
	slot->u1.Function = reinterpret_cast<DWORD>(replacement);
	VirtualProtect(&slot->u1.Function, sizeof(DWORD), old, &old);
	return original;
}

}  // namespace

// Replace one entry of the exe's import address table (the table of function
// pointers the loader filled in). Data, not code: nothing in the game is patched.
void *PatchImport(const char *dll, const char *function, void *replacement)
{
	BYTE *base = reinterpret_cast<BYTE *>(GetModuleHandleW(nullptr));
	const IMAGE_DOS_HEADER *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
	const IMAGE_NT_HEADERS32 *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	const IMAGE_IMPORT_DESCRIPTOR *imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress);
	for (; imp->Name; ++imp) {
		if (_stricmp(reinterpret_cast<const char *>(base + imp->Name), dll) != 0)
			continue;
		const IMAGE_THUNK_DATA32 *names = reinterpret_cast<const IMAGE_THUNK_DATA32 *>(base + imp->OriginalFirstThunk);
		IMAGE_THUNK_DATA32 *slots = reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + imp->FirstThunk);
		for (; names->u1.AddressOfData; ++names, ++slots) {
			if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG32)
				continue;
			const IMAGE_IMPORT_BY_NAME *by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
			if (strcmp(reinterpret_cast<const char *>(by_name->Name), function) == 0)
				return PatchSlot(slots, replacement);
		}
		// Imported by number (WSOCK32.dll): find the slot holding the function's address.
		HMODULE module = GetModuleHandleA(dll);
		const DWORD address = module ? reinterpret_cast<DWORD>(GetProcAddress(module, function)) : 0;
		for (slots = reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + imp->FirstThunk); address && slots->u1.Function;
		     ++slots) {
			if (slots->u1.Function == address)
				return PatchSlot(slots, replacement);
		}
	}
	return nullptr;
}

namespace {

// --- finding out where the game hangs ---------------------------------------------
// If the main loop stops calling PeekMessageA for a few seconds (a modal dialog, a
// blocking network call, ...), the watchdog pauses the main thread for a moment
// and logs where it is: its instruction pointer and the return addresses found on
// its stack (values just after a call instruction), as "module+offset".
bool AfterCall(const BYTE *p)
{
	return p[-5] == 0xE8 || (p[-6] == 0xFF && p[-5] == 0x15) || (p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0) ||
	       (p[-3] == 0xFF && (p[-2] & 0xF8) == 0x50) || (p[-6] == 0xFF && (p[-5] & 0xF8) == 0x90);
}

void DescribeAddress(DWORD a, char *out, size_t size)
{
	HMODULE m = nullptr;
	char name[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       reinterpret_cast<LPCSTR>(a), &m) &&
	    m) {
		GetModuleFileNameA(m, name, MAX_PATH);
		const char *base = strrchr(name, '\\');
		if (m == GetModuleHandleW(nullptr))
			_snprintf_s(out, size, _TRUNCATE, "%08lx", a);  // game addresses as they are (Ghidra)
		else
			_snprintf_s(out, size, _TRUNCATE, "%s+%lx", base ? base + 1 : name, a - reinterpret_cast<DWORD>(m));
		return;
	}
	_snprintf_s(out, size, _TRUNCATE, "%08lx?", a);
}

void ReportStall(DWORD stalled_ms, const char *what)
{
	if (!g_main_thread || SuspendThread(g_main_thread) == static_cast<DWORD>(-1))
		return;
	CONTEXT ctx = {};
	ctx.ContextFlags = CONTEXT_CONTROL;
	char line[1024];
	if (GetThreadContext(g_main_thread, &ctx)) {
		char where[128];
		DescribeAddress(ctx.Eip, where, sizeof(where));
		_snprintf_s(line, sizeof(line), _TRUNCATE, "robot: stall (%s for %lu ms); main thread at %s, called from", what,
		            stalled_ms, where);
		// the first few system frames, then only the game's own (the exe image)
		const BYTE *exe = reinterpret_cast<const BYTE *>(GetModuleHandleW(nullptr));
		const DWORD exe_end = reinterpret_cast<DWORD>(exe) + reinterpret_cast<const IMAGE_NT_HEADERS32 *>(
		                          exe + reinterpret_cast<const IMAGE_DOS_HEADER *>(exe)->e_lfanew)
		                          ->OptionalHeader.SizeOfImage;
		const DWORD *stack = reinterpret_cast<const DWORD *>(ctx.Esp);
		int found = 0, system = 0;
		for (int i = 0; i < 16384 && found < 24; ++i) {
			__try {
				const DWORD v = stack[i];
				const bool in_exe = v >= reinterpret_cast<DWORD>(exe) && v < exe_end;
				if (!in_exe && system >= 3)
					continue;
				MEMORY_BASIC_INFORMATION mbi;
				if (v > 0x10000 && VirtualQuery(reinterpret_cast<void *>(v), &mbi, sizeof(mbi)) && mbi.Type == MEM_IMAGE &&
				    (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE | PAGE_EXECUTE_READWRITE)) &&
				    AfterCall(reinterpret_cast<const BYTE *>(v))) {
					if (!in_exe)
						++system;
					DescribeAddress(v, where, sizeof(where));
					strncat_s(line, sizeof(line), " ", _TRUNCATE);
					strncat_s(line, sizeof(line), where, _TRUNCATE);
					++found;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				break;  // end of the stack
			}
		}
	}
	else {
		_snprintf_s(line, sizeof(line), _TRUNCATE, "robot: stall (%s for %lu ms), no thread context", what, stalled_ms);
	}
	// The frame loop (0x00838e10) only draws once the game-message queue (a list
	// at *0x00fd2d9c, see PostGameMessage) is empty: note what is queued.
	char queue[512] = "";
	if (IsHookable(DetectGameBuild())) {
		__try {
			const DWORD *head = *reinterpret_cast<DWORD *const *>(0x00fd2d9c);
			int n = 0;
			for (const DWORD *node = reinterpret_cast<const DWORD *>(head[0]); node != head && n < 10000;
			     node = reinterpret_cast<const DWORD *>(node[0])) {
				if (n < 3) {
					char item[160];
					_snprintf_s(item, sizeof(item), _TRUNCATE, " [%08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx]",
					            node[2], node[3], node[4], node[5], node[6], node[7], node[8], node[9]);
					strncat_s(queue, sizeof(queue), item, _TRUNCATE);
				}
				++n;
			}
			char count[48];
			_snprintf_s(count, sizeof(count), _TRUNCATE, "; %d game messages queued", n);
			strncat_s(line, sizeof(line), count, _TRUNCATE);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			strncat_s(line, sizeof(line), "; game-message queue unreadable", _TRUNCATE);
		}
	}
	ResumeThread(g_main_thread);
	hooklog::Write("%s%s", line, queue);
}

DWORD WINAPI Watchdog(LPVOID)
{
	const DWORD start = GetTickCount();
	DWORD reported_for = 0;  // the stall already reported (its last PeekMessage or frame tick)
	while (!g_quit_after_seconds || GetTickCount() - start < g_quit_after_seconds * 1000) {
		Sleep(500);
		// a stall: no PeekMessageA call, or no frame shown (a loop that pumps
		// messages while it waits for something), for 3 seconds
		const DWORD peek = g_last_peek_tick;
		const DWORD frame = LastFrameTick();
		const DWORD last = frame && frame < peek ? frame : peek;
		if (last && GetTickCount() - last > 3000 && reported_for != last) {
			reported_for = last;
			ReportStall(GetTickCount() - last, last == frame ? "no frame shown" : "no PeekMessageA call");
		}
	}
	hooklog::Write("robot: time limit of %lu s reached after %ld AI turns, closing the game", g_quit_after_seconds, g_turns);
	hooklog::Close();
	ExitProcess(2);
}

// Message boxes would wait for a click nobody makes: log them, and answer plain
// "OK" boxes (errors, notices) right away.
typedef int(WINAPI *MessageBoxAFn)(HWND, LPCSTR, LPCSTR, UINT);
MessageBoxAFn g_real_MessageBoxA = nullptr;

int WINAPI RobotMessageBoxA(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type)
{
	hooklog::Write("robot: message box \"%s\": %s", caption ? caption : "", text ? text : "");
	if ((type & MB_TYPEMASK) == MB_OK)
		return IDOK;
	return g_real_MessageBoxA(hwnd, text, caption, type);
}

bool ReadConfig(const wchar_t *path)
{
	FILE *f = nullptr;
	if (_wfopen_s(&f, path, L"r") != 0 || !f)
		return false;
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		char *eq = strchr(line, '=');
		if (!eq || line[0] == '#')
			continue;
		*eq = 0;
		char *value = eq + 1;
		value[strcspn(value, "\r\n")] = 0;
		if (strcmp(line, "home") == 0)
			lstrcpynA(g_home, value, MAX_PATH);
		else if (strcmp(line, "quit_after_ai_turns") == 0)
			g_quit_after_turns = atol(value);
		else if (strcmp(line, "quit_after_seconds") == 0)
			g_quit_after_seconds = static_cast<DWORD>(atol(value));
		else if (strcmp(line, "command") == 0)
			MultiByteToWideChar(CP_ACP, 0, value, -1, g_command, 1024);
		else if (strcmp(line, "step") == 0 && g_step_count < kMaxSteps) {
			// <seconds>|console|<text>   or   <seconds>|menu|<page> <command>
			char *kind = strchr(value, '|');
			char *text = kind ? strchr(kind + 1, '|') : nullptr;
			if (kind && text) {
				*kind++ = 0;
				*text++ = 0;
				Step &s = g_steps[g_step_count++];
				s.at_ms = static_cast<DWORD>(atol(value)) * 1000;
				s.kind = strcmp(kind, "menu") == 0   ? 'm'
				         : strcmp(kind, "shot") == 0 ? 's'
				         : strcmp(kind, "ui") == 0   ? 'u'
				         : strcmp(kind, "msg") == 0  ? 'g'
				         : strcmp(kind, "map") == 0  ? 'p'
				         : strcmp(kind, "peek") == 0 ? 'k'
				         : strcmp(kind, "game") == 0 ? 'j'
				         : strcmp(kind, "slots") == 0 ? 'w'
				         : strcmp(kind, "arrow") == 0 ? 'a'
				         : strcmp(kind, "mem") == 0 ? 'v'
				                                     : 'c';
				lstrcpynA(s.text, text, sizeof(s.text));
			}
		}
		else if (strcmp(line, "command_delay_seconds") == 0)
			g_command_delay_ms = static_cast<DWORD>(atol(value)) * 1000;
		else if (strcmp(line, "segment_ms") == 0)
			g_segment_ms = static_cast<DWORD>(atol(value));
		else if (strcmp(line, "trace_segments") == 0)
			g_trace_segments = atol(value) != 0;
		else if (strcmp(line, "trace_mp") == 0)
			g_trace_mp = atol(value) != 0;
		else if (strcmp(line, "end_turn_every_seconds") == 0)
			g_end_turn_ms = static_cast<DWORD>(atol(value)) * 1000;
		else if (strcmp(line, "press_enter_every_seconds") == 0)
			g_press_enter_ms = static_cast<DWORD>(atol(value)) * 1000;
		else if (strcmp(line, "repeat_command") == 0)
			MultiByteToWideChar(CP_ACP, 0, value, -1, g_repeat_command, 256);
		else if (strcmp(line, "repeat_every_seconds") == 0)
			g_repeat_ms = static_cast<DWORD>(atol(value)) * 1000;
		else if (strcmp(line, "screenshot_dir") == 0)
			lstrcpynA(g_shot_dir, value, MAX_PATH);
		else if (strcmp(line, "screenshot_every_seconds") == 0)
			g_shot_ms = static_cast<DWORD>(atol(value)) * 1000;
		else if (strcmp(line, "net_log") == 0)
			g_net_log = atol(value) != 0;
		else if (strcmp(line, "net_share_udp_ports") == 0)
			g_share_udp_ports = atol(value) != 0;
		else if (strcmp(line, "net_lan_relay_ports") == 0)
			lstrcpynA(g_relay_ports, value, sizeof(g_relay_ports));
		else if (strcmp(line, "net_log_packets") == 0)
			g_net_log_packets = atoi(value);
	}
	fclose(f);
	return true;
}

}  // namespace

void Init()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5-robot", L".ini", path, MAX_PATH);
	if (!ReadConfig(path))
		return;
	g_active = true;
	hooklog::Write("robot mode: home=%s quit_after_ai_turns=%ld quit_after_seconds=%lu", g_home, g_quit_after_turns,
	               g_quit_after_seconds);
	if (g_home[0]) {
		g_real_SHGetFolderPathA = reinterpret_cast<SHGetFolderPathAFn>(
			PatchImport("SHELL32.dll", "SHGetFolderPathA", reinterpret_cast<void *>(&RobotSHGetFolderPathA)));
		hooklog::Write(g_real_SHGetFolderPathA ? "robot: Documents redirect installed"
		                                       : "robot: could not redirect Documents (import not found)");
	}
	g_real_CreateMutexA = reinterpret_cast<CreateMutexAFn>(
		PatchImport("KERNEL32.dll", "CreateMutexA", reinterpret_cast<void *>(&RobotCreateMutexA)));
	// Keep the game window "active" in the background.
	g_real_RegisterClassA = reinterpret_cast<RegisterClassAFn>(
		PatchImport("USER32.dll", "RegisterClassA", reinterpret_cast<void *>(&RobotRegisterClassA)));
	g_real_RegisterClassExA = reinterpret_cast<RegisterClassExAFn>(
		PatchImport("USER32.dll", "RegisterClassExA", reinterpret_cast<void *>(&RobotRegisterClassExA)));

	// Capture the game's text output.
	g_real_OutputDebugStringA = reinterpret_cast<OutputDebugStringAFn>(
		PatchImport("KERNEL32.dll", "OutputDebugStringA", reinterpret_cast<void *>(&RobotOutputDebugStringA)));
	if (HMODULE crt = GetModuleHandleW(L"msvcr71.dll")) {
		g_crt_vfprintf = reinterpret_cast<VfprintfFn>(GetProcAddress(crt, "vfprintf"));
		g_crt_vfwprintf = reinterpret_cast<VfwprintfFn>(GetProcAddress(crt, "vfwprintf"));
	}
	PatchImport("MSVCR71.dll", "printf", reinterpret_cast<void *>(&RobotPrintf));
	if (g_crt_vfprintf)
		PatchImport("MSVCR71.dll", "fprintf", reinterpret_cast<void *>(&RobotFprintf));
	if (g_crt_vfwprintf)
		PatchImport("MSVCR71.dll", "fwprintf", reinterpret_cast<void *>(&RobotFwprintf));
	hooklog::Write("robot: capturing game text output (debug=%s, fprintf=%s, fwprintf=%s)",
	               g_real_OutputDebugStringA ? "yes" : "no", g_crt_vfprintf ? "yes" : "no",
	               g_crt_vfwprintf ? "yes" : "no");

	// "command=" (older form) becomes the first step of the script.
	if (g_command[0] && g_step_count < kMaxSteps) {
		for (int i = g_step_count; i > 0; --i)
			g_steps[i] = g_steps[i - 1];
		g_steps[0].at_ms = g_command_delay_ms;
		g_steps[0].kind = 'c';
		WideCharToMultiByte(CP_ACP, 0, g_command, -1, g_steps[0].text, sizeof(g_steps[0].text), nullptr, nullptr);
		++g_step_count;
	}
	// steps run in time order (stable: steps given the same time keep their order)
	for (int i = 1; i < g_step_count; ++i) {
		for (int j = i; j > 0 && g_steps[j - 1].at_ms > g_steps[j].at_ms; --j) {
			const Step t = g_steps[j];
			g_steps[j] = g_steps[j - 1];
			g_steps[j - 1] = t;
		}
	}
	if (g_shot_dir[0])
		InstallScreenshots(g_shot_dir);
	if (g_net_log || g_share_udp_ports || g_relay_ports[0])
		InstallNetLog(g_share_udp_ports, g_relay_ports, g_net_log_packets);
	// The main loop hook runs the script and keeps the game active.
	g_real_PeekMessageA = reinterpret_cast<PeekMessageAFn>(
		PatchImport("USER32.dll", "PeekMessageA", reinterpret_cast<void *>(&RobotPeekMessageA)));
	hooklog::Write(g_real_PeekMessageA ? "robot: main loop hook installed" : "robot: PeekMessageA import not found");
	for (int i = 0; i < g_step_count; ++i)
		hooklog::Write("robot: step %d at %lu s: %s %s", i + 1, g_steps[i].at_ms / 1000,
		               g_steps[i].kind == 'm'   ? "menu"
		               : g_steps[i].kind == 'u' ? "ui"
		               : g_steps[i].kind == 'g' ? "msg"
		               : g_steps[i].kind == 'p' ? "map"
		               : g_steps[i].kind == 'k' ? "peek"
		               : g_steps[i].kind == 'j' ? "game"
		               : g_steps[i].kind == 'w' ? "slots"
		               : g_steps[i].kind == 'a' ? "arrow"
		               : g_steps[i].kind == 'v' ? "mem"
		               : g_steps[i].kind == 's' ? "shot"
		                                        : "console",
		               g_steps[i].text);
	g_real_MessageBoxA = reinterpret_cast<MessageBoxAFn>(
		PatchImport("USER32.dll", "MessageBoxA", reinterpret_cast<void *>(&RobotMessageBoxA)));
	// time limit, and reports of main-loop stalls
	HANDLE t = CreateThread(nullptr, 0, &Watchdog, nullptr, 0, nullptr);
	if (t)
		CloseHandle(t);
}

bool Active()
{
	return g_active;
}

void OnAiTurnStart(void *ai)
{
	if (!g_active)
		return;
	const LONG turns = InterlockedIncrement(&g_turns);
	hooklog::Write("robot: AI turn %ld started (ai=%p)", turns, ai);
	if (g_quit_after_turns && turns > g_quit_after_turns) {
		hooklog::Write("robot: %ld AI turns done, closing the game", g_quit_after_turns);
		hooklog::Close();
		ExitProcess(0);
	}
}

}  // namespace robot
