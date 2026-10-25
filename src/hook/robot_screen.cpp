// Robot screenshots: saves the game's own frames as .bmp files.
//
// The game draws with Direct3D 9. While its window is in the background nothing
// reaches the screen, so screenshots of the desktop come out blank. Instead the
// picture is taken inside the game, from the finished frame just before it is
// shown (IDirect3DDevice9::Present, through the shared Direct3D hooks of
// d3d9_hook.cpp): it is copied to memory and written to a file. The frame is
// taken after the overlay drew on it (src/mods/player_list.cpp), so the
// screenshots show what a player sees.
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

#include "d3d9_hook.h"
#include "log.h"
#include "robot.h"

namespace robot {
namespace {

const int kScreenshotOrder = 100;  // after the overlay (order 0)

bool g_installed = false;
char g_dir[MAX_PATH] = {};
volatile LONG g_requests = 0;  // screenshots asked for and not yet taken
volatile DWORD g_last_present = 0;
int g_taken = 0;
char g_label[64] = {};

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

void OnPresent(IDirect3DDevice9 *device)
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
			Capture(device);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			hooklog::Write("robot: screenshot crashed (caught)");
		}
	}
}

}  // namespace

void InstallScreenshots(const char *dir)
{
	lstrcpynA(g_dir, dir, MAX_PATH);
	CreateDirectoryA(g_dir, nullptr);
	// From DllMain: only the import table entry now (see d3d9_hook.h).
	g_installed = d3dhook::Install(false);
	if (g_installed)
		d3dhook::AddPresentCallback(&OnPresent, kScreenshotOrder);
	hooklog::Write(g_installed ? "robot: screenshots go to %s" : "robot: Direct3DCreate9 import not found (%s)", g_dir);
}

DWORD LastFrameTick()
{
	return g_last_present;
}

void RequestScreenshot(const char *label)
{
	if (!g_installed)
		return;
	hooklog::Write("robot: screenshot \"%s\" requested", label ? label : "");
	lstrcpynA(g_label, label ? label : "", sizeof(g_label));
	InterlockedIncrement(&g_requests);
}

}  // namespace robot
