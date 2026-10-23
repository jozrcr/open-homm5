// Robot screenshots: saves the game's own frames as .bmp files.
//
// The game draws with Direct3D 9. While its window is in the background nothing
// reaches the screen, so screenshots of the desktop come out blank. Instead the
// picture is taken inside the game:
//   Direct3DCreate9 (an import of the exe)  replaced, to see the Direct3D object;
//   IDirect3D9::CreateDevice                hooked, to see the rendering device;
//   IDirect3DDevice9::Present               hooked: when a screenshot is due, the
//                                           finished frame is copied to memory and
//                                           written to a file, then shown as usual.
// The two hooks replace entries of the vtables in the system d3d9.dll (data, not
// code), like the DirectInput hooks in robot_input.cpp.
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

#include "log.h"
#include "robot.h"

namespace robot {
namespace {

const int kCreateDevice = 16;  // IDirect3D9 vtable slot (IUnknown first)
const int kPresent = 17;       // IDirect3DDevice9 vtable slot

typedef IDirect3D9 *(WINAPI *Direct3DCreate9Fn)(UINT sdk_version);
typedef HRESULT(STDMETHODCALLTYPE *CreateDeviceFn)(IDirect3D9 *self, UINT adapter, D3DDEVTYPE type, HWND window,
                                                   DWORD flags, D3DPRESENT_PARAMETERS *params,
                                                   IDirect3DDevice9 **device);
typedef HRESULT(STDMETHODCALLTYPE *PresentFn)(IDirect3DDevice9 *self, const RECT *source, const RECT *dest,
                                              HWND window, const RGNDATA *dirty);
Direct3DCreate9Fn g_real_create9 = nullptr;
CreateDeviceFn g_real_create_device = nullptr;
PresentFn g_real_present = nullptr;

char g_dir[MAX_PATH] = {};
volatile LONG g_requests = 0;  // screenshots asked for and not yet taken
volatile DWORD g_last_present = 0;
int g_taken = 0;
char g_label[64] = {};

void *PatchSlot(void *object, int slot, void *replacement)
{
	void **table = *static_cast<void ***>(object);
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

// Writes a locked 32-bit (X8R8G8B8 / A8R8G8B8) image as a 24-bit .bmp file.
bool WriteBmp(const char *path, const BYTE *pixels, int pitch, int width, int height)
{
	FILE *f = nullptr;
	if (fopen_s(&f, path, "wb") != 0 || !f)
		return false;
	const int row = (width * 3 + 3) & ~3;
	BITMAPFILEHEADER file = {};
	BITMAPINFOHEADER info = {};
	file.bfType = 0x4d42;  // "BM"
	file.bfOffBits = sizeof(file) + sizeof(info);
	file.bfSize = file.bfOffBits + row * height;
	info.biSize = sizeof(info);
	info.biWidth = width;
	info.biHeight = height;  // bottom-up
	info.biPlanes = 1;
	info.biBitCount = 24;
	fwrite(&file, sizeof(file), 1, f);
	fwrite(&info, sizeof(info), 1, f);
	BYTE *line = static_cast<BYTE *>(calloc(row, 1));
	for (int y = height - 1; line && y >= 0; --y) {
		const BYTE *src = pixels + y * pitch;
		for (int x = 0; x < width; ++x) {
			line[x * 3 + 0] = src[x * 4 + 0];
			line[x * 3 + 1] = src[x * 4 + 1];
			line[x * 3 + 2] = src[x * 4 + 2];
		}
		fwrite(line, row, 1, f);
	}
	free(line);
	fclose(f);
	return true;
}

// Copies the back buffer (the frame about to be shown) and saves it.
void Capture(IDirect3DDevice9 *device)
{
	IDirect3DSurface9 *back = nullptr, *copy = nullptr;
	D3DSURFACE_DESC desc = {};
	HRESULT hr = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
	if (SUCCEEDED(hr))
		hr = back->GetDesc(&desc);
	if (SUCCEEDED(hr) && desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8)
		hr = E_NOTIMPL;
	if (SUCCEEDED(hr))
		hr = device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &copy,
		                                         nullptr);
	if (SUCCEEDED(hr))
		hr = device->GetRenderTargetData(back, copy);
	D3DLOCKED_RECT locked = {};
	if (SUCCEEDED(hr))
		hr = copy->LockRect(&locked, nullptr, D3DLOCK_READONLY);
	if (SUCCEEDED(hr)) {
		char path[MAX_PATH];
		_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\frame-%03d%s%s.bmp", g_dir, g_taken, g_label[0] ? "-" : "",
		            g_label);
		const bool ok = WriteBmp(path, static_cast<const BYTE *>(locked.pBits), locked.Pitch,
		                         static_cast<int>(desc.Width), static_cast<int>(desc.Height));
		copy->UnlockRect();
		hooklog::Write("robot: screenshot %s %s (%ux%u)", path, ok ? "saved" : "could not be written", desc.Width,
		               desc.Height);
		++g_taken;
	}
	else {
		hooklog::Write("robot: screenshot failed (hr=%08lx, format %d)", static_cast<unsigned long>(hr),
		               static_cast<int>(desc.Format));
	}
	if (copy)
		copy->Release();
	if (back)
		back->Release();
}

HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9 *self, const RECT *source, const RECT *dest, HWND window,
                                      const RGNDATA *dirty)
{
	static bool logged = false;
	if (!logged) {
		logged = true;
		hooklog::Write("robot: game presents its first frame");
	}
	g_last_present = GetTickCount();
	if (g_requests > 0) {
		InterlockedDecrement(&g_requests);
		__try {
			Capture(self);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			hooklog::Write("robot: screenshot crashed (caught)");
		}
	}
	return g_real_present(self, source, dest, window, dirty);
}

HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirect3D9 *self, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
                                           D3DPRESENT_PARAMETERS *params, IDirect3DDevice9 **device)
{
	const HRESULT hr = g_real_create_device(self, adapter, type, window, flags, params, device);
	if (SUCCEEDED(hr) && device && *device) {
		if (void *p = PatchSlot(*device, kPresent, reinterpret_cast<void *>(&HookPresent)))
			g_real_present = reinterpret_cast<PresentFn>(p);
		hooklog::Write("robot: Direct3D device %p hooked for screenshots (%ux%u, format %d)", *device,
		               params ? params->BackBufferWidth : 0, params ? params->BackBufferHeight : 0,
		               params ? static_cast<int>(params->BackBufferFormat) : 0);
	}
	return hr;
}

IDirect3D9 *WINAPI RobotDirect3DCreate9(UINT sdk_version)
{
	IDirect3D9 *d3d = g_real_create9(sdk_version);
	if (d3d) {
		if (void *p = PatchSlot(d3d, kCreateDevice, reinterpret_cast<void *>(&HookCreateDevice)))
			g_real_create_device = reinterpret_cast<CreateDeviceFn>(p);
	}
	return d3d;
}

}  // namespace

void InstallScreenshots(const char *dir)
{
	lstrcpynA(g_dir, dir, MAX_PATH);
	CreateDirectoryA(g_dir, nullptr);
	g_real_create9 = reinterpret_cast<Direct3DCreate9Fn>(
		PatchImport("d3d9.dll", "Direct3DCreate9", reinterpret_cast<void *>(&RobotDirect3DCreate9)));
	hooklog::Write(g_real_create9 ? "robot: screenshots go to %s" : "robot: Direct3DCreate9 import not found (%s)", g_dir);
}

DWORD LastFrameTick()
{
	return g_last_present;
}

void RequestScreenshot(const char *label)
{
	if (!g_real_create9)
		return;
	hooklog::Write("robot: screenshot \"%s\" requested", label ? label : "");
	lstrcpynA(g_label, label ? label : "", sizeof(g_label));
	InterlockedIncrement(&g_requests);
}

}  // namespace robot
