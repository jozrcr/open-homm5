// Direct3D 9 hooks shared by the overlay and the robot (see d3d9_hook.h).
#include "d3d9_hook.h"

#include <d3d9.h>

#include "log.h"
#include "patch.h"

namespace d3dhook {
namespace {

const int kCreateDeviceSlot = 16;  // IDirect3D9 (IUnknown's 3 first)
const int kResetSlot = 16;         // IDirect3DDevice9
const int kPresentSlot = 17;       // IDirect3DDevice9

typedef IDirect3D9 *(WINAPI *Direct3DCreate9Fn)(UINT sdk_version);
typedef HRESULT(STDMETHODCALLTYPE *CreateDeviceFn)(IDirect3D9 *self, UINT adapter, D3DDEVTYPE type, HWND window,
                                                   DWORD flags, D3DPRESENT_PARAMETERS *params,
                                                   IDirect3DDevice9 **device);
typedef HRESULT(STDMETHODCALLTYPE *PresentFn)(IDirect3DDevice9 *self, const RECT *source, const RECT *dest,
                                              HWND window, const RGNDATA *dirty);
typedef HRESULT(STDMETHODCALLTYPE *ResetFn)(IDirect3DDevice9 *self, D3DPRESENT_PARAMETERS *params);

bool g_installed = false;
Direct3DCreate9Fn g_real_create9 = nullptr;
HWND volatile g_window = nullptr;

// The replaced vtable entries, per vtable (normally one of each kind; a
// different d3d9 implementation for another device type would add one more).
struct D3dTable {
	void **vtable;
	CreateDeviceFn create_device;
};
struct DeviceTable {
	void **vtable;
	PresentFn present;
	ResetFn reset;
};
const int kTables = 4;
D3dTable g_d3d[kTables];
DeviceTable g_devices[kTables];
CRITICAL_SECTION g_lock;

struct PresentClient {
	PresentCallback callback;
	int order;
};
const int kClients = 8;
PresentClient g_present[kClients];
int g_present_count = 0;
ResetCallback g_reset[kClients];
int g_reset_count = 0;

// Replaces one vtable entry; returns what it held.
void *ReplaceSlot(void **vtable, int slot, void *replacement)
{
	DWORD old;
	if (!VirtualProtect(&vtable[slot], sizeof(void *), PAGE_READWRITE, &old))
		return nullptr;
	void *original = vtable[slot];
	vtable[slot] = replacement;
	VirtualProtect(&vtable[slot], sizeof(void *), old, &old);
	return original;
}

HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirect3D9 *self, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
                                           D3DPRESENT_PARAMETERS *params, IDirect3DDevice9 **device);
HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9 *self, const RECT *source, const RECT *dest, HWND window,
                                      const RGNDATA *dirty);
HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9 *self, D3DPRESENT_PARAMETERS *params);

D3dTable *D3dTableOf(void **vtable)
{
	for (D3dTable &t : g_d3d)
		if (t.vtable == vtable)
			return &t;
	return nullptr;
}

DeviceTable *DeviceTableOf(void **vtable)
{
	for (DeviceTable &t : g_devices)
		if (t.vtable == vtable)
			return &t;
	return nullptr;
}

// A Direct3D object: its CreateDevice entry goes through us (once per vtable).
void HookD3d(IDirect3D9 *d3d)
{
	void **vtable = *reinterpret_cast<void ***>(d3d);
	EnterCriticalSection(&g_lock);
	if (!D3dTableOf(vtable)) {
		for (D3dTable &t : g_d3d) {
			if (t.vtable)
				continue;
			void *original = ReplaceSlot(vtable, kCreateDeviceSlot, reinterpret_cast<void *>(&HookCreateDevice));
			if (original && original != reinterpret_cast<void *>(&HookCreateDevice)) {
				t.create_device = reinterpret_cast<CreateDeviceFn>(original);
				t.vtable = vtable;
			}
			break;
		}
	}
	LeaveCriticalSection(&g_lock);
}

void HookDevice(IDirect3DDevice9 *device)
{
	void **vtable = *reinterpret_cast<void ***>(device);
	EnterCriticalSection(&g_lock);
	if (!DeviceTableOf(vtable)) {
		for (DeviceTable &t : g_devices) {
			if (t.vtable)
				continue;
			void *present = ReplaceSlot(vtable, kPresentSlot, reinterpret_cast<void *>(&HookPresent));
			void *reset = ReplaceSlot(vtable, kResetSlot, reinterpret_cast<void *>(&HookReset));
			if (present && reset) {
				t.present = reinterpret_cast<PresentFn>(present);
				t.reset = reinterpret_cast<ResetFn>(reset);
				t.vtable = vtable;
			}
			break;
		}
	}
	LeaveCriticalSection(&g_lock);
}

HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirect3D9 *self, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
                                           D3DPRESENT_PARAMETERS *params, IDirect3DDevice9 **device)
{
	const D3dTable *t = D3dTableOf(*reinterpret_cast<void ***>(self));
	if (!t)
		return D3DERR_INVALIDCALL;  // cannot happen: only our vtables lead here
	const HRESULT hr = t->create_device(self, adapter, type, window, flags, params, device);
	if (SUCCEEDED(hr) && device && *device) {
		HookDevice(*device);
		g_window = window ? window : (params ? params->hDeviceWindow : nullptr);
		hooklog::Write("d3d9: device %p (%ux%u, %s, format %d)", *device, params ? params->BackBufferWidth : 0,
		               params ? params->BackBufferHeight : 0, params && params->Windowed ? "windowed" : "fullscreen",
		               params ? static_cast<int>(params->BackBufferFormat) : 0);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9 *self, const RECT *source, const RECT *dest, HWND window,
                                      const RGNDATA *dirty)
{
	const DeviceTable *t = DeviceTableOf(*reinterpret_cast<void ***>(self));
	if (!t)
		return D3DERR_INVALIDCALL;
	for (int i = 0; i < g_present_count; ++i) {
		__try {
			g_present[i].callback(self);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			static LONG reported = 0;
			if (InterlockedIncrement(&reported) <= 3)
				hooklog::Write("d3d9: a frame callback crashed (caught)");
		}
	}
	return t->present(self, source, dest, window, dirty);
}

HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9 *self, D3DPRESENT_PARAMETERS *params)
{
	const DeviceTable *t = DeviceTableOf(*reinterpret_cast<void ***>(self));
	if (!t)
		return D3DERR_INVALIDCALL;
	for (int i = 0; i < g_reset_count; ++i)
		g_reset[i](self, false, false);
	const HRESULT hr = t->reset(self, params);
	for (int i = 0; i < g_reset_count; ++i)
		g_reset[i](self, true, SUCCEEDED(hr));
	hooklog::Write("d3d9: device %p reset (%ux%u, %s): %s", self, params ? params->BackBufferWidth : 0,
	               params ? params->BackBufferHeight : 0, params && params->Windowed ? "windowed" : "fullscreen",
	               SUCCEEDED(hr) ? "ok" : "failed");
	return hr;
}

IDirect3D9 *WINAPI HookDirect3DCreate9(UINT sdk_version)
{
	IDirect3D9 *d3d = g_real_create9(sdk_version);
	if (d3d)
		HookD3d(d3d);
	return d3d;
}

}  // namespace

void AddPresentCallback(PresentCallback callback, int order)
{
	if (g_present_count >= kClients)
		return;
	int i = g_present_count++;
	while (i > 0 && g_present[i - 1].order > order) {
		g_present[i] = g_present[i - 1];
		--i;
	}
	g_present[i] = {callback, order};
}

void AddResetCallback(ResetCallback callback)
{
	if (g_reset_count < kClients)
		g_reset[g_reset_count++] = callback;
}

bool Install(bool late)
{
	if (!g_installed) {
		InitializeCriticalSection(&g_lock);
		g_real_create9 = reinterpret_cast<Direct3DCreate9Fn>(hookpatch::PatchImport(
			"d3d9.dll", "Direct3DCreate9", reinterpret_cast<void *>(&HookDirect3DCreate9)));
		if (!g_real_create9) {
			hooklog::Write("d3d9: the exe does not import Direct3DCreate9: no overlay, no screenshots");
			return false;
		}
		g_installed = true;
	}
	static bool late_done = false;
	if (late && !late_done) {
		// The game may have made its Direct3D object already: replace the
		// CreateDevice entry of the (shared) vtable through one of our own.
		late_done = true;
		if (IDirect3D9 *d3d = g_real_create9(D3D_SDK_VERSION)) {
			HookD3d(d3d);
			d3d->Release();
		}
	}
	return true;
}

HWND DeviceWindow()
{
	return g_window;
}

}  // namespace d3dhook
