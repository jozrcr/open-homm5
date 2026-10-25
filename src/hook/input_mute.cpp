// The rules of the overlay's modal input (see input_mute.h).
#include "input_mute.h"

#include <string.h>

namespace inputmute {

int MessageButton(UINT msg, WPARAM wp, bool *down)
{
	*down = false;
	switch (msg) {
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		*down = true;
		return 0;
	case WM_LBUTTONUP:
		return 0;
	case WM_RBUTTONDOWN:
	case WM_RBUTTONDBLCLK:
		*down = true;
		return 1;
	case WM_RBUTTONUP:
		return 1;
	case WM_MBUTTONDOWN:
	case WM_MBUTTONDBLCLK:
		*down = true;
		return 2;
	case WM_MBUTTONUP:
		return 2;
	case WM_XBUTTONDOWN:
	case WM_XBUTTONDBLCLK:
		*down = true;
		return HIWORD(wp) == XBUTTON1 ? 3 : 4;
	case WM_XBUTTONUP:
		return HIWORD(wp) == XBUTTON1 ? 3 : 4;
	default:
		return -1;
	}
}

Messages::Messages()
{
	Forget();
}

void Messages::Forget()
{
	memset(hidden_down_, 0, sizeof(hidden_down_));
	memset(hidden_button_, 0, sizeof(hidden_button_));
	memset(game_down_, 0, sizeof(game_down_));
	memset(game_button_, 0, sizeof(game_button_));
}

Action Messages::Filter(UINT msg, WPARAM wp, LPARAM lp, bool muted)
{
	bool down = false;
	const int b = MessageButton(msg, wp, &down);
	if (b >= 0) {
		if (down) {
			if (muted) {
				if (!game_button_[b])
					hidden_button_[b] = true;
				return kSwallow;
			}
			hidden_button_[b] = false;  // a new press: what was hidden has been released
			game_button_[b] = true;
			return kPass;
		}
		if (game_button_[b]) {  // the game saw it go down: it gets the release, even now
			game_button_[b] = hidden_button_[b] = false;
			return kPass;
		}
		if (hidden_button_[b]) {  // went down while muted: its release stays hidden too
			hidden_button_[b] = false;
			return kSwallow;
		}
		return muted ? kSwallow : kPass;
	}
	switch (msg) {
	case WM_MOUSEMOVE:
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
		return muted ? kSwallow : kPass;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN: {
		const int vk = static_cast<int>(wp & 0xff);
		const bool repeat = (lp & (1 << 30)) != 0;  // the key was already down (auto-repeat)
		if (muted) {
			if (!game_down_[vk])
				hidden_down_[vk] = true;
			// Alt+F4 still closes the game (Windows turns it into WM_SYSCOMMAND,
			// which the game gets as usual); every other key is held back.
			return msg == WM_SYSKEYDOWN && vk == VK_F4 ? kDefault : kSwallow;
		}
		if (hidden_down_[vk] && repeat)
			return kSwallow;  // still held since the mute
		hidden_down_[vk] = false;
		game_down_[vk] = true;
		return kPass;
	}
	case WM_KEYUP:
	case WM_SYSKEYUP: {
		const int vk = static_cast<int>(wp & 0xff);
		if (game_down_[vk]) {
			game_down_[vk] = hidden_down_[vk] = false;
			return kPass;
		}
		if (hidden_down_[vk]) {
			hidden_down_[vk] = false;
			return kSwallow;
		}
		return muted ? kSwallow : kPass;
	}
	case WM_CHAR:
	case WM_DEADCHAR:
	case WM_SYSCHAR:
	case WM_SYSDEADCHAR:
	case WM_UNICHAR:
		return muted ? kSwallow : kPass;
	case WM_KILLFOCUS:
		Forget();  // the releases go to another window now
		return kPass;
	case WM_ACTIVATEAPP:
		if (!wp)
			Forget();
		return kPass;
	default:
		return kPass;
	}
}

DirectInput::DirectInput()
{
	memset(key_held_, 0, sizeof(key_held_));
	memset(button_held_, 0, sizeof(button_held_));
}

void DirectInput::KeyboardState(BYTE *keys, bool muted)
{
	for (int k = 0; k < 256; ++k) {
		const bool down = (keys[k] & 0x80) != 0;
		if (muted) {
			key_held_[k] = down;
			keys[k] = 0;
		}
		else if (key_held_[k]) {
			if (down)
				keys[k] = 0;  // pressed during the mute, not released yet
			else
				key_held_[k] = false;
		}
	}
}

void DirectInput::MouseState(void *state, DWORD size, bool muted)
{
	if (size < 16)
		return;
	LONG *axes = static_cast<LONG *>(state);  // lX, lY, lZ (the wheel)
	BYTE *buttons = static_cast<BYTE *>(state) + 12;
	const int n = size >= 20 ? 8 : 4;
	if (muted)
		axes[2] = 0;
	for (int i = 0; i < n; ++i) {
		const bool down = (buttons[i] & 0x80) != 0;
		if (muted) {
			button_held_[i] = down;
			buttons[i] = 0;
		}
		else if (button_held_[i]) {
			if (down)
				buttons[i] = 0;
			else
				button_held_[i] = false;
		}
	}
}

DWORD DirectInput::Events(bool keyboard, void *data, DWORD size, DWORD count, bool muted)
{
	if (!muted || !data || size < 8)
		return count;
	BYTE *events = static_cast<BYTE *>(data);
	DWORD kept = 0;
	for (DWORD i = 0; i < count; ++i) {
		BYTE *e = events + i * size;
		DWORD ofs, value;
		memcpy(&ofs, e, 4);
		memcpy(&value, e + 4, 4);
		const bool release = (value & 0x80) == 0;
		bool keep;
		if (keyboard)
			keep = release;  // dwOfs = the DIK_ code
		else if (ofs == 0 || ofs == 4)
			keep = true;  // DIMOFS_X, DIMOFS_Y: the game's cursor keeps moving
		else if (ofs >= 12 && ofs < 20)
			keep = release;  // DIMOFS_BUTTON0..7
		else
			keep = false;  // DIMOFS_Z: the wheel
		if (!keep)
			continue;
		if (kept != i)
			memmove(events + kept * size, e, size);
		++kept;
	}
	return kept;
}

}  // namespace inputmute
