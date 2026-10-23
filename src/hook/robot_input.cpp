// Robot keyboard: lets robot mode press keys in the game through DirectInput.
//
// The game reads the keyboard with DirectInput 8 (window messages are ignored),
// and DirectInput8Create goes through our proxy. In robot mode we replace three
// entries of the real DirectInput COM vtables (data tables in the system
// dinput8.dll, not code):
//   IDirectInput8::CreateDevice      to notice the keyboard device being created
//   IDirectInputDevice8::GetDeviceState / GetDeviceData
//                                    to add our key presses to what the game reads
// Keyboard and mouse devices share one vtable, so the hooks pass everything that
// is not the keyboard straight through.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include "log.h"
#include "robot.h"

namespace robot {
namespace {

// vtable slots (IUnknown first): IDirectInput8 CreateDevice = 3;
// IDirectInputDevice8 GetDeviceState = 9, GetDeviceData = 10.
const int kCreateDevice = 3;
const int kGetDeviceState = 9;
const int kGetDeviceData = 10;
const int kAcquire = 7;

typedef HRESULT(STDMETHODCALLTYPE *CreateDeviceFn)(void *self, REFGUID guid, void **device, IUnknown *outer);
typedef HRESULT(STDMETHODCALLTYPE *GetDeviceStateFn)(void *self, DWORD size, void *data);
typedef HRESULT(STDMETHODCALLTYPE *GetDeviceDataFn)(void *self, DWORD size, DIDEVICEOBJECTDATA *data, DWORD *count,
                                                    DWORD flags);
CreateDeviceFn g_real_create_device = nullptr;
GetDeviceStateFn g_real_get_state = nullptr;
GetDeviceDataFn g_real_get_data = nullptr;
typedef HRESULT(STDMETHODCALLTYPE *AcquireFn)(void *self);
AcquireFn g_real_acquire = nullptr;

void *g_keyboard = nullptr;

// One pending key press, held for kHoldMs then released.
// phase: 0 idle, 1 pressed (down not yet delivered), 2 down delivered, 3 release pending
const DWORD kHoldMs = 150;
volatile LONG g_key = 0;  // DIK_ code, 0 = none
volatile LONG g_phase = 0;
DWORD g_down_at = 0;
bool g_uses_buffer = false;  // the game reads buffered events (GetDeviceData)
DWORD g_sequence = 0x10000;

void Advance()
{
	if (g_phase == 2 && GetTickCount() - g_down_at >= kHoldMs)
		InterlockedExchange(&g_phase, 3);
}

void Clear()
{
	hooklog::Write("robot: key %02lx released", g_key);
	InterlockedExchange(&g_key, 0);
	InterlockedExchange(&g_phase, 0);
}

void **Vtable(void *object)
{
	return *static_cast<void ***>(object);
}

void *PatchSlot(void *object, int slot, void *replacement)
{
	void **table = Vtable(object);
	if (table[slot] == replacement)
		return nullptr;  // already patched
	DWORD old;
	if (!VirtualProtect(&table[slot], sizeof(void *), PAGE_READWRITE, &old))
		return nullptr;
	void *original = table[slot];
	table[slot] = replacement;
	VirtualProtect(&table[slot], sizeof(void *), old, &old);
	return original;
}

HRESULT STDMETHODCALLTYPE HookGetDeviceState(void *self, DWORD size, void *data)
{
	HRESULT hr = g_real_get_state(self, size, data);
	static bool logged = false;
	if (self == g_keyboard && !logged) {
		logged = true;
		hooklog::Write("robot: game polls the keyboard (GetDeviceState, hr=%08lx)", static_cast<unsigned long>(hr));
	}
	if (self != g_keyboard || size < 256)
		return hr;
	if (FAILED(hr)) {  // not acquired (window in background): report "no keys", plus ours
		ZeroMemory(data, size);
		hr = DI_OK;
	}
	if (!g_key)
		return hr;
	if (!g_uses_buffer && g_phase == 1)
		InterlockedExchange(&g_phase, 2);
	Advance();
	if (g_phase == 1 || g_phase == 2)
		static_cast<BYTE *>(data)[g_key] = 0x80;
	else if (g_phase == 3 && !g_uses_buffer)
		Clear();
	return hr;
}

HRESULT STDMETHODCALLTYPE HookGetDeviceData(void *self, DWORD size, DIDEVICEOBJECTDATA *data, DWORD *count, DWORD flags)
{
	const DWORD capacity = count ? *count : 0;
	HRESULT hr = g_real_get_data(self, size, data, count, flags);
	if (self != g_keyboard)
		return hr;
	if (!g_uses_buffer)
		hooklog::Write("robot: game reads keyboard events (GetDeviceData, hr=%08lx)", static_cast<unsigned long>(hr));
	g_uses_buffer = true;
	if (FAILED(hr) && count) {  // not acquired (window in background): "no events", plus ours
		*count = 0;
		hr = DI_OK;
	}
	if (!g_key || !count || !data || size < sizeof(DIDEVICEOBJECTDATA_DX3) || (flags & DIGDD_PEEK))
		return hr;
	Advance();
	const LONG phase = g_phase;
	if ((phase == 1 || phase == 3) && *count < capacity) {
		DIDEVICEOBJECTDATA *e = reinterpret_cast<DIDEVICEOBJECTDATA *>(reinterpret_cast<BYTE *>(data) + *count * size);
		ZeroMemory(e, size);
		e->dwOfs = static_cast<DWORD>(g_key);
		e->dwData = phase == 1 ? 0x80 : 0;
		e->dwTimeStamp = GetTickCount();
		e->dwSequence = ++g_sequence;
		++*count;
		if (phase == 1) {
			g_down_at = GetTickCount();
			InterlockedExchange(&g_phase, 2);
		}
		else {
			Clear();
		}
	}
	return hr;
}

// The keyboard can only be "acquired" while the game window is in front. The robot
// runs in the background, so for the keyboard we always report success.
HRESULT STDMETHODCALLTYPE HookAcquire(void *self)
{
	HRESULT hr = g_real_acquire(self);
	if (self == g_keyboard && FAILED(hr)) {
		static bool logged = false;
		if (!logged) {
			logged = true;
			hooklog::Write("robot: keyboard Acquire failed (%08lx, window in background): reporting success",
			               static_cast<unsigned long>(hr));
		}
		return DI_OK;
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE HookCreateDevice(void *self, REFGUID guid, void **device, IUnknown *outer)
{
	HRESULT hr = g_real_create_device(self, guid, device, outer);
	if (SUCCEEDED(hr) && device && *device && IsEqualGUID(guid, GUID_SysKeyboard)) {
		g_keyboard = *device;
		if (void *a = PatchSlot(g_keyboard, kAcquire, reinterpret_cast<void *>(&HookAcquire)))
			g_real_acquire = reinterpret_cast<AcquireFn>(a);
		void *s = PatchSlot(g_keyboard, kGetDeviceState, reinterpret_cast<void *>(&HookGetDeviceState));
		void *d = PatchSlot(g_keyboard, kGetDeviceData, reinterpret_cast<void *>(&HookGetDeviceData));
		if (s)
			g_real_get_state = reinterpret_cast<GetDeviceStateFn>(s);
		if (d)
			g_real_get_data = reinterpret_cast<GetDeviceDataFn>(d);
		hooklog::Write("robot: keyboard device %p hooked", g_keyboard);
	}
	return hr;
}

}  // namespace

void OnDirectInputCreated(void *direct_input)
{
	if (!Active() || !direct_input)
		return;
	InstallMenuHooks();  // DirectInput is created before the main menu is built
	void *orig = PatchSlot(direct_input, kCreateDevice, reinterpret_cast<void *>(&HookCreateDevice));
	if (orig)
		g_real_create_device = reinterpret_cast<CreateDeviceFn>(orig);
	hooklog::Write("robot: DirectInput %p hooked for the robot keyboard", direct_input);
}

bool PressKey(int dik_code)
{
	if (!g_keyboard || g_key)
		return false;  // no keyboard yet, or a key is still being pressed
	g_down_at = GetTickCount();
	InterlockedExchange(&g_phase, 1);
	InterlockedExchange(&g_key, dik_code);
	return true;
}

}  // namespace robot
