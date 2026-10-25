// The rules of the overlay's modal input (overlay_input.cpp): which of the
// game's mouse and keyboard input is held back while a modal panel is open.
// Pure logic, no game and no window needed, so that test/input_mute_test.cpp
// can check it.
//
// While muted, the game must not see a key or a button go down, nor the mouse
// wheel. Two details keep the game's own state right:
//   - a key (or button) the game saw go down before the mute still gets its
//     release, so the game never thinks it is held;
//   - a key pressed during the mute stays hidden until it is released, also
//     after the mute (Esc closes the panel: the game must not see that Esc).
// Mouse motion is NOT held back from DirectInput: with the game's software
// cursor (the default, ui_use_hwcursor = 0) the game moves its cursor from
// DirectInput's mouse axes, and the player must see it move over our panel.
#pragma once
#include <windows.h>

namespace inputmute {

// What the game's window procedure gets of one message.
enum Action {
	kPass,     // the game gets it
	kSwallow,  // nobody: the message is answered with 0
	kDefault,  // only Windows' default handling (DefWindowProcA): Alt+F4 still closes the game
};

// Window messages (the game window's procedure, subclassed).
class Messages {
public:
	Messages();
	Action Filter(UINT msg, WPARAM wp, LPARAM lp, bool muted);
	void Forget();  // focus lost: releases will not come to this window

private:
	bool game_down_[256];       // the game saw this key go down (no release yet)
	bool hidden_down_[256];     // this key went down while muted (the game did not see it)
	bool game_button_[5];       // the same for the mouse buttons: left, right, middle, X1, X2
	bool hidden_button_[5];
};

// The mouse button of a message (0 left, 1 right, 2 middle, 3 X1, 4 X2), with
// *down = it is a press (or double click); -1 for other messages.
int MessageButton(UINT msg, WPARAM wp, bool *down);

// DirectInput (what the game polls and reads, after the real call).
class DirectInput {
public:
	DirectInput();
	// keys: 256 bytes, one per DIK_ code (0x80 = down).
	void KeyboardState(BYTE *keys, bool muted);
	// DIMOUSESTATE (16 bytes) or DIMOUSESTATE2 (20): the wheel and the buttons
	// are held back, the motion is not.
	void MouseState(void *state, DWORD size, bool muted);
	// Buffered events (DIDEVICEOBJECTDATA, `size` bytes each, dwOfs then dwData
	// first): while muted, presses and the mouse wheel are removed, releases and
	// mouse motion kept. Returns how many events are left (moved to the front).
	DWORD Events(bool keyboard, void *data, DWORD size, DWORD count, bool muted);

private:
	bool key_held_[256];   // went down while muted and is still down: the game sees it up
	bool button_held_[8];
};

}  // namespace inputmute
