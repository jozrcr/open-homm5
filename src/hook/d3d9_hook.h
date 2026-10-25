// Direct3D 9 hooks shared by everything that draws on, or looks at, the game's
// frames: the player list overlay (src/mods/player_list.cpp) and the robot's
// screenshots (robot_screen.cpp).
//
// The game draws with Direct3D 9. We see its device like this (data patches
// only, no code is changed; this also works with Wine's and DXVK's d3d9.dll):
//   Direct3DCreate9            the exe's import table entry, to see each new
//                              Direct3D object;
//   IDirect3D9::CreateDevice   its vtable entry, to see each new device;
//   IDirect3DDevice9::Present  the finished frame, just before it is shown;
//   IDirect3DDevice9::Reset    the device is reset (window/fullscreen switch,
//                              new resolution, after alt-tab in fullscreen):
//                              what was made in D3DPOOL_DEFAULT must be released
//                              before, and made again after.
// The vtables live in d3d9.dll and are shared by all objects of a kind, so each
// entry is replaced once. Several users stack on one hook: each Present
// callback runs in order (lower first), then the game's frame is shown.
#pragma once
#include <windows.h>

struct IDirect3DDevice9;

namespace d3dhook {

// Called on the game's render thread, before the frame goes to the screen.
typedef void (*PresentCallback)(IDirect3DDevice9 *device);
// Called before (after = false) and after (after = true, ok = the reset worked)
// IDirect3DDevice9::Reset, on the game's render thread.
typedef void (*ResetCallback)(IDirect3DDevice9 *device, bool after, bool ok);

// The overlay draws at order 0; the robot's screenshots take the frame at 100,
// so they show the overlay.
void AddPresentCallback(PresentCallback callback, int order);
void AddResetCallback(ResetCallback callback);

// Put the hooks in (once; later calls do nothing more). `late` = the game may
// already have made its Direct3D object (after DllMain): then the CreateDevice
// entry is also replaced through a Direct3D object of our own. Never with
// late = true from DllMain. False if the exe does not import Direct3DCreate9.
bool Install(bool late);

// The window of the last device created (its focus window), or nullptr.
HWND DeviceWindow();

}  // namespace d3dhook
