// DirectInput 8 hooks shared by the robot's keyboard (robot_input.cpp) and the
// overlay's modal input (overlay_input.cpp): one set of hooks, several users.
//
// The game reads its keyboard (and maybe its mouse) through DirectInput 8, and
// DirectInput8Create goes through our proxy (dllmain.cpp). We replace entries
// of the real DirectInput COM vtables (data tables in the system dinput8.dll,
// not code), once per vtable:
//   IDirectInput8::CreateDevice           to learn the keyboard and mouse devices;
//   IDirectInputDevice8::Acquire          (the robot: success in the background);
//   IDirectInputDevice8::GetDeviceState   what the game polls ("which keys are down");
//   IDirectInputDevice8::GetDeviceData    what the game reads as events ("key pressed").
// Keyboard and mouse share one device vtable, so every hook asks which device
// it was called for. After the real call, each user's filter runs in order
// (lower first) and may change what the game gets: the robot adds its key
// presses (order 0), then the overlay holds everything back while a modal
// panel is open (order 100).
//
// Nothing is hooked until a user has registered its filters (no robot, no
// modal panel: DirectInput is untouched).
#pragma once
#include <windows.h>

struct DIDEVICEOBJECTDATA;

namespace dihook {

enum DeviceKind { kKeyboard, kMouse, kOtherDevice };

// Each filter gets the result of the real call (or of the filters before it)
// and returns the result the game gets.
struct Filters {
	// GetDeviceState(size, data). For a keyboard, data is 256 bytes (one per
	// DIK_ code, 0x80 = down); for a mouse, DIMOUSESTATE (16) or DIMOUSESTATE2 (20).
	HRESULT (*state)(DeviceKind kind, void *device, DWORD size, void *data, HRESULT hr);
	// GetDeviceData(size, data, count, flags): *count events of `size` bytes
	// each were written; `capacity` is the room the game gave (*count before).
	HRESULT (*data)(DeviceKind kind, void *device, DWORD size, DIDEVICEOBJECTDATA *data, DWORD *count, DWORD capacity,
	                DWORD flags, HRESULT hr);
	HRESULT (*acquire)(DeviceKind kind, void *device, HRESULT hr);
	// A keyboard or mouse device was created (and hooked).
	void (*created)(DeviceKind kind, void *device);
};

// Any member may be null. Register before the game's first DirectInput8Create
// returns (hooks::InstallAll runs before it), or from OnDirectInputCreated's caller.
void AddFilters(const Filters &filters, int order);

// From the DirectInput8Create proxy, with the new object: hooks its vtable
// (when any filters are registered).
void OnDirectInputCreated(void *direct_input);

}  // namespace dihook
