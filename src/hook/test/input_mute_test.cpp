// Test of the overlay's modal input rules (src/hook/input_mute.cpp): what the
// game gets of its window messages and of DirectInput while a modal panel is
// open, and just after. No game, no window needed.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <string.h>

#include "../input_mute.h"

using inputmute::Action;
using inputmute::kDefault;
using inputmute::kPass;
using inputmute::kSwallow;

namespace {

int g_failures = 0;

void Check(bool ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		++g_failures;
}

const LPARAM kRepeat = 1 << 30;  // WM_KEYDOWN: the key was already down

void TestMessages()
{
	inputmute::Messages m;
	// Not muted: everything passes.
	Check(m.Filter(WM_MOUSEMOVE, 0, 0, false) == kPass, "messages: mouse move passes when not muted");
	Check(m.Filter(WM_KEYDOWN, 'A', 0, false) == kPass && m.Filter(WM_CHAR, 'a', 0, false) == kPass &&
	          m.Filter(WM_KEYUP, 'A', 0, false) == kPass,
	      "messages: a key passes when not muted");
	Check(m.Filter(WM_LBUTTONDOWN, 0, 0, false) == kPass && m.Filter(WM_LBUTTONUP, 0, 0, false) == kPass,
	      "messages: a click passes when not muted");

	// Muted: nothing of a click, a key or the wheel.
	Check(m.Filter(WM_MOUSEMOVE, 0, 0, true) == kSwallow, "messages: mouse move held back");
	Check(m.Filter(WM_LBUTTONDOWN, 0, 0, true) == kSwallow && m.Filter(WM_LBUTTONUP, 0, 0, true) == kSwallow,
	      "messages: a click held back");
	Check(m.Filter(WM_RBUTTONDBLCLK, 0, 0, true) == kSwallow && m.Filter(WM_RBUTTONUP, 0, 0, true) == kSwallow,
	      "messages: a right double click held back");
	Check(m.Filter(WM_MOUSEWHEEL, MAKEWPARAM(0, 120), 0, true) == kSwallow, "messages: the wheel held back");
	Check(m.Filter(WM_KEYDOWN, VK_ESCAPE, 0, true) == kSwallow && m.Filter(WM_CHAR, 27, 0, true) == kSwallow,
	      "messages: Esc held back");
	Check(m.Filter(WM_SYSKEYDOWN, VK_MENU, 0, true) == kSwallow, "messages: Alt held back");
	Check(m.Filter(WM_SYSKEYDOWN, VK_F4, 0, true) == kDefault, "messages: Alt+F4 goes to Windows' default handling");
	Check(m.Filter(WM_SYSKEYUP, VK_F4, 0, true) == kSwallow && m.Filter(WM_KEYUP, VK_MENU, 0, true) == kSwallow,
	      "messages: and their releases held back");
	Check(m.Filter(WM_ACTIVATE, WA_ACTIVE, 0, true) == kPass && m.Filter(WM_SETCURSOR, 0, 0, true) == kPass &&
	          m.Filter(WM_PAINT, 0, 0, true) == kPass,
	      "messages: other messages pass while muted");

	// Esc closed the panel: the release (and repeats) of what went down during the mute stay hidden.
	Check(m.Filter(WM_KEYDOWN, VK_ESCAPE, kRepeat, false) == kSwallow, "messages: Esc repeating after the mute held back");
	Check(m.Filter(WM_KEYUP, VK_ESCAPE, 0, false) == kSwallow, "messages: Esc's release after the mute held back");
	Check(m.Filter(WM_KEYDOWN, VK_ESCAPE, 0, false) == kPass && m.Filter(WM_KEYUP, VK_ESCAPE, 0, false) == kPass,
	      "messages: the next Esc reaches the game");

	// A key the game saw go down before the mute gets its release during it.
	Check(m.Filter(WM_KEYDOWN, VK_F11, 0, false) == kPass, "messages: F11 down reaches the game (the panel opens)");
	Check(m.Filter(WM_KEYDOWN, VK_F11, kRepeat, true) == kSwallow, "messages: F11's repeats held back while muted");
	Check(m.Filter(WM_KEYUP, VK_F11, 0, true) == kPass, "messages: F11's release reaches the game while muted");
	Check(m.Filter(WM_LBUTTONDOWN, 0, 0, false) == kPass && m.Filter(WM_LBUTTONUP, 0, 0, true) == kPass,
	      "messages: a button pressed before the mute is released for the game");

	// A press during the mute, released after it: the release stays hidden.
	Check(m.Filter(WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON2), 0, true) == kSwallow &&
	          m.Filter(WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), 0, false) == kSwallow,
	      "messages: a button pressed while muted, released after: hidden");
	Check(m.Filter(WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON2), 0, false) == kPass, "messages: then it works again");
	Check(m.Filter(WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), 0, false) == kPass, "messages: and its release too");

	// Focus lost: the releases go elsewhere, nothing stays hidden.
	Check(m.Filter(WM_KEYDOWN, 'B', 0, true) == kSwallow, "messages: B pressed while muted");
	Check(m.Filter(WM_KILLFOCUS, 0, 0, true) == kPass, "messages: focus lost");
	Check(m.Filter(WM_KEYDOWN, 'B', kRepeat, false) == kPass, "messages: after a focus loss, B is not held back");
	Check(m.Filter(WM_KEYUP, 'B', 0, false) == kPass, "messages: nor its release");

	bool down = false;
	Check(inputmute::MessageButton(WM_MBUTTONDBLCLK, 0, &down) == 2 && down, "messages: middle double click = press");
	Check(inputmute::MessageButton(WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), &down) == 3 && !down, "messages: X1 release");
	Check(inputmute::MessageButton(WM_KEYDOWN, 0, &down) == -1, "messages: a key is not a button");
}

void TestKeyboardState()
{
	inputmute::DirectInput d;
	BYTE keys[256];
	memset(keys, 0, sizeof(keys));
	keys[DIK_A] = 0x80;
	d.KeyboardState(keys, false);
	Check(keys[DIK_A] == 0x80, "keyboard state: untouched when not muted");
	keys[DIK_ESCAPE] = 0x80;
	d.KeyboardState(keys, true);
	Check(keys[DIK_A] == 0 && keys[DIK_ESCAPE] == 0, "keyboard state: no key down while muted");
	keys[DIK_A] = 0x80;
	keys[DIK_ESCAPE] = 0x80;
	d.KeyboardState(keys, false);
	Check(keys[DIK_A] == 0 && keys[DIK_ESCAPE] == 0, "keyboard state: keys held since the mute stay up for the game");
	keys[DIK_ESCAPE] = 0;
	keys[DIK_SPACE] = 0x80;
	d.KeyboardState(keys, false);
	Check(keys[DIK_A] == 0 && keys[DIK_SPACE] == 0x80, "keyboard state: a new key after the mute is seen");
	keys[DIK_ESCAPE] = 0x80;
	keys[DIK_A] = 0;
	d.KeyboardState(keys, false);
	Check(keys[DIK_ESCAPE] == 0x80, "keyboard state: Esc pressed again after its release is seen");
	keys[DIK_A] = 0x80;
	d.KeyboardState(keys, false);
	Check(keys[DIK_A] == 0x80, "keyboard state: A pressed again after its release is seen");
}

void TestMouseState()
{
	inputmute::DirectInput d;
	DIMOUSESTATE2 s = {};
	s.lX = 5;
	s.lY = -3;
	s.lZ = 120;
	s.rgbButtons[0] = 0x80;
	s.rgbButtons[5] = 0x80;
	d.MouseState(&s, sizeof(s), true);
	Check(s.lX == 5 && s.lY == -3, "mouse state: the motion still reaches the game (its software cursor)");
	Check(s.lZ == 0 && s.rgbButtons[0] == 0 && s.rgbButtons[5] == 0, "mouse state: no wheel, no button while muted");
	s.lZ = 120;
	s.rgbButtons[0] = 0x80;
	s.rgbButtons[5] = 0;
	d.MouseState(&s, sizeof(s), false);
	Check(s.lZ == 120 && s.rgbButtons[0] == 0, "mouse state: after the mute the wheel works, a held button stays up");
	s.rgbButtons[0] = 0;
	d.MouseState(&s, sizeof(s), false);
	s.rgbButtons[0] = 0x80;
	d.MouseState(&s, sizeof(s), false);
	Check(s.rgbButtons[0] == 0x80, "mouse state: the next press is seen");
	DIMOUSESTATE four = {};
	four.lZ = -120;
	four.rgbButtons[3] = 0x80;
	d.MouseState(&four, sizeof(four), true);
	Check(four.lZ == 0 && four.rgbButtons[3] == 0, "mouse state: DIMOUSESTATE (4 buttons) too");
}

DIDEVICEOBJECTDATA Event(DWORD ofs, DWORD data)
{
	DIDEVICEOBJECTDATA e = {};
	e.dwOfs = ofs;
	e.dwData = data;
	e.dwSequence = ofs * 100 + data;
	return e;
}

void TestEvents()
{
	inputmute::DirectInput d;
	DIDEVICEOBJECTDATA keys[4] = {Event(DIK_A, 0x80), Event(DIK_B, 0), Event(DIK_ESCAPE, 0x80), Event(DIK_C, 0)};
	Check(d.Events(true, keys, sizeof(DIDEVICEOBJECTDATA), 4, false) == 4, "events: untouched when not muted");
	const DWORD left = d.Events(true, keys, sizeof(DIDEVICEOBJECTDATA), 4, true);
	Check(left == 2 && keys[0].dwOfs == DIK_B && keys[1].dwOfs == DIK_C && keys[1].dwSequence == DIK_C * 100,
	      "events: keyboard presses removed, releases kept in order");

	DIDEVICEOBJECTDATA mouse[6] = {Event(DIMOFS_X, 7),       Event(DIMOFS_BUTTON0, 0x80), Event(DIMOFS_Z, 120),
	                               Event(DIMOFS_Y, 0xfffffffd), Event(DIMOFS_BUTTON1, 0),    Event(DIMOFS_BUTTON7, 0x80)};
	const DWORD kept = d.Events(false, mouse, sizeof(DIDEVICEOBJECTDATA), 6, true);
	Check(kept == 3 && mouse[0].dwOfs == DIMOFS_X && mouse[1].dwOfs == DIMOFS_Y && mouse[2].dwOfs == DIMOFS_BUTTON1,
	      "events: mouse motion and releases kept, presses and the wheel removed");

	DIDEVICEOBJECTDATA_DX3 old[2] = {{DIK_A, 0x80, 0, 0}, {DIK_A, 0, 0, 0}};
	Check(d.Events(true, old, sizeof(DIDEVICEOBJECTDATA_DX3), 2, true) == 1 && old[0].dwData == 0,
	      "events: the old 16-byte event format too");
	Check(d.Events(true, nullptr, sizeof(DIDEVICEOBJECTDATA), 3, true) == 3, "events: no buffer (a count query) untouched");
}

}  // namespace

int main()
{
	TestMessages();
	TestKeyboardState();
	TestMouseState();
	TestEvents();
	if (g_failures) {
		printf("%d check(s) FAILED\n", g_failures);
		return 1;
	}
	printf("input mute: all checks passed\n");
	return 0;
}
