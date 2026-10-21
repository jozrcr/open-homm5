// Smoke test for the hook DLL outside the game: load build/hook/dinput8.dll like
// the game would, call DirectInput8Create, and check we get a real DirectInput
// object from the system DLL. In this test exe the build check must fail, so the
// DLL must stay in forwarding-only mode (see the log next to this exe).
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	const char *dll = argc > 1 ? argv[1] : "dinput8.dll";
	HMODULE h = LoadLibraryA(dll);
	if (!h) {
		printf("FAIL: cannot load %s (error %lu)\n", dll, GetLastError());
		return 1;
	}
	typedef HRESULT(WINAPI * Fn)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
	Fn create = reinterpret_cast<Fn>(GetProcAddress(h, "DirectInput8Create"));
	if (!create) {
		printf("FAIL: DirectInput8Create not exported\n");
		return 1;
	}
	IDirectInput8A *di = nullptr;
	HRESULT hr = create(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A,
	                    reinterpret_cast<void **>(&di), nullptr);
	if (FAILED(hr) || !di) {
		printf("FAIL: DirectInput8Create returned %08lx\n", static_cast<unsigned long>(hr));
		return 1;
	}
	di->Release();
	printf("OK: DirectInput8Create forwarded to the system DLL\n");
	return 0;
}
