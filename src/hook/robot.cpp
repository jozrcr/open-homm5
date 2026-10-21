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
bool g_command_done = false;
DWORD g_end_turn_ms = 0;     // 0 = never
bool g_trace_segments = false;
DWORD g_segment_ms = 0;  // 0 = leave the game's 200 ms
DWORD g_press_enter_ms = 0;  // 0 = never
DWORD g_last_enter = 0;
wchar_t g_repeat_command[256] = {};  // console command run every repeat_every_seconds
DWORD g_repeat_ms = 0;
DWORD g_last_repeat = 0;

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
	if (!g_command_done && g_command[0]) {
		const DWORD now = GetTickCount();
		if (!g_first_peek)
			g_first_peek = now;
		else if (now - g_first_peek >= g_command_delay_ms) {
			g_command_done = true;
			hooklog::Write("robot: running console command: %ls", g_command);
			hooklog::Write(RunConsoleCommand(g_command) ? "robot: console command returned"
			                                            : "robot: console command FAILED (unknown build or crash caught)");
			g_last_enter = now;
		}
	}
	// End the passive human player's turn when no AI is playing.
	InstallEndTurnHelper();
	if (g_trace_segments)
		InstallSegmentTrace();
	if (g_segment_ms)
		SetSegmentLength(g_segment_ms);
	if (g_command_done)
		MaybeEndHumanTurn(g_end_turn_ms, 3000);
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
			if (strcmp(reinterpret_cast<const char *>(by_name->Name), function) != 0)
				continue;
			DWORD old;
			if (!VirtualProtect(&slots->u1.Function, sizeof(DWORD), PAGE_READWRITE, &old))
				return nullptr;
			void *original = reinterpret_cast<void *>(slots->u1.Function);
			slots->u1.Function = reinterpret_cast<DWORD>(replacement);
			VirtualProtect(&slots->u1.Function, sizeof(DWORD), old, &old);
			return original;
		}
	}
	return nullptr;
}

DWORD WINAPI Watchdog(LPVOID)
{
	Sleep(g_quit_after_seconds * 1000);
	hooklog::Write("robot: time limit of %lu s reached after %ld AI turns, closing the game", g_quit_after_seconds, g_turns);
	hooklog::Close();
	ExitProcess(2);
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
		else if (strcmp(line, "command_delay_seconds") == 0)
			g_command_delay_ms = static_cast<DWORD>(atol(value)) * 1000;
		else if (strcmp(line, "segment_ms") == 0)
			g_segment_ms = static_cast<DWORD>(atol(value));
		else if (strcmp(line, "trace_segments") == 0)
			g_trace_segments = atol(value) != 0;
		else if (strcmp(line, "end_turn_every_seconds") == 0)
			g_end_turn_ms = static_cast<DWORD>(atol(value)) * 1000;
		else if (strcmp(line, "press_enter_every_seconds") == 0)
			g_press_enter_ms = static_cast<DWORD>(atol(value)) * 1000;
		else if (strcmp(line, "repeat_command") == 0)
			MultiByteToWideChar(CP_ACP, 0, value, -1, g_repeat_command, 256);
		else if (strcmp(line, "repeat_every_seconds") == 0)
			g_repeat_ms = static_cast<DWORD>(atol(value)) * 1000;
	}
	fclose(f);
	return true;
}

}  // namespace

void Init()
{
	wchar_t path[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
	while (n > 0 && path[n - 1] != L'\\')
		--n;
	lstrcpynW(path + n, L"open-homm5-robot.ini", MAX_PATH - n);
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

	if (g_command[0]) {
		g_real_PeekMessageA = reinterpret_cast<PeekMessageAFn>(
			PatchImport("USER32.dll", "PeekMessageA", reinterpret_cast<void *>(&RobotPeekMessageA)));
		hooklog::Write(g_real_PeekMessageA ? "robot: will run \"%ls\" %lu s after start-up"
		                                   : "robot: cannot run \"%ls\" (PeekMessageA import not found)",
		               g_command, g_command_delay_ms / 1000);
	}
	if (g_quit_after_seconds) {
		HANDLE t = CreateThread(nullptr, 0, &Watchdog, nullptr, 0, nullptr);
		if (t)
			CloseHandle(t);
	}
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
