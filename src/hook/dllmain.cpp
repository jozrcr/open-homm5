// open-homm5 hook DLL, loaded by the game as a proxy for dinput8.dll.
//
// The game imports one function from DINPUT8.dll: DirectInput8Create. Windows
// looks for the DLL in the game folder first, so our dinput8.dll is loaded at
// start-up. We forward every export to the real system dinput8.dll, and on the
// first DirectInput8Create call (the game is running and its code is decrypted
// by then, even for the Steam-wrapped exe) we check which exe we are in and
// install our hooks only for a build we know.
#include <windows.h>
#include <unknwn.h>

#include "game_build.h"
#include "hooks.h"
#include "log.h"

namespace {

HMODULE g_real = nullptr;
bool g_hooks_done = false;

FARPROC Real(const char *name)
{
	if (!g_real) {
		wchar_t path[MAX_PATH];
		UINT n = GetSystemDirectoryW(path, MAX_PATH);  // SysWOW64 for a 32-bit process
		lstrcpynW(path + n, L"\\dinput8.dll", MAX_PATH - n);
		g_real = LoadLibraryW(path);
		if (!g_real) {
			hooklog::Write("cannot load the system dinput8.dll (error %lu)", GetLastError());
			return nullptr;
		}
	}
	return GetProcAddress(g_real, name);
}

void InstallHooksOnce()
{
	if (g_hooks_done)
		return;
	g_hooks_done = true;
	const GameBuild build = DetectGameBuild();
	hooklog::Write("game build: %s", GameBuildName(build));
	if (IsHookable(build))
		hooks::InstallAll();
	else
		hooklog::Write("unknown build: no hooks installed, forwarding only");
}

}  // namespace

extern "C" {

HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID *out, LPUNKNOWN outer)
{
	InstallHooksOnce();
	typedef HRESULT(WINAPI * Fn)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
	Fn fn = reinterpret_cast<Fn>(Real("DirectInput8Create"));
	return fn ? fn(inst, version, riid, out, outer) : E_FAIL;
}

HRESULT WINAPI Proxy_DllCanUnloadNow()
{
	typedef HRESULT(WINAPI * Fn)();
	Fn fn = reinterpret_cast<Fn>(Real("DllCanUnloadNow"));
	return fn ? fn() : S_FALSE;
}

HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID *out)
{
	typedef HRESULT(WINAPI * Fn)(REFCLSID, REFIID, LPVOID *);
	Fn fn = reinterpret_cast<Fn>(Real("DllGetClassObject"));
	return fn ? fn(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI Proxy_DllRegisterServer()
{
	typedef HRESULT(WINAPI * Fn)();
	Fn fn = reinterpret_cast<Fn>(Real("DllRegisterServer"));
	return fn ? fn() : E_FAIL;
}

HRESULT WINAPI Proxy_DllUnregisterServer()
{
	typedef HRESULT(WINAPI * Fn)();
	Fn fn = reinterpret_cast<Fn>(Real("DllUnregisterServer"));
	return fn ? fn() : E_FAIL;
}

LPCVOID WINAPI Proxy_GetdfDIJoystick()
{
	typedef LPCVOID(WINAPI * Fn)();
	Fn fn = reinterpret_cast<Fn>(Real("GetdfDIJoystick"));
	return fn ? fn() : nullptr;
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(inst);
		hooklog::Open(inst);
		hooklog::Write("open-homm5 hook DLL loaded (built " __DATE__ " " __TIME__ ")");
	}
	else if (reason == DLL_PROCESS_DETACH) {
		hooklog::Write("process exiting");
		hooklog::Close();
	}
	return TRUE;
}
