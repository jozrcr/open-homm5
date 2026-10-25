// The overlay's modal panels (see overlay.h): while one is open, the game gets
// no mouse or keyboard input and ImGui gets it. The rules of what is held back
// are in input_mute.cpp (tested by test/input_mute_test.cpp); this file wires
// them to the game:
//
//   the game window's procedure (class "H5"), subclassed once its window is
//     known: while muted, mouse and keyboard messages are put in a small queue
//     for ImGui and not given to the game. We feed ImGui ourselves (button,
//     wheel, key and character events) instead of using ImGui's Win32 backend,
//     which would also change the cursor and want every message;
//   DirectInput (dinput_hook.cpp, after the robot's filter): keys, buttons and
//     the wheel are held back. The mouse's motion is not: with the game's
//     software cursor (ui_use_hwcursor = 0, the default) the game moves its
//     cursor from it (0x00835ce0 keeps that cursor in client pixels at
//     0x00fe6da0/0x00fe6da4), and ImGui follows that same cursor, so what the
//     player sees under the cursor is what gets the click. If the game takes
//     the mouse for itself (DirectInput exclusive mode, then Windows sends no
//     mouse messages), the buttons and wheel for ImGui come from DirectInput.
//
// When muted: "a panel is open, its screen is on top (checked at every frame),
// and a frame was drawn with it in the last 2 s". The input hooks check the
// time themselves, so input comes back even when no frame is drawn at all.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <float.h>
#include <string.h>

#include "dinput_hook.h"
#include "input_mute.h"
#include "log.h"
#include "overlay.h"
#include "overlay_internal.h"
#include "third_party/imgui/imgui.h"

namespace overlay {
namespace {

const DWORD kTimeoutMs = 2000;          // no frame drawn for this long: the game gets its input back
const DWORD kMouseMessagesMs = 1000;    // no mouse message for this long: the buttons come from DirectInput

bool g_enabled = false;  // EnableModalInput was called

// --- the open panel (changed on the frame's thread, read by the input hooks) ---------------
char g_owner[64] = "";    // its client's name, "" = none
char g_screen[96] = "";   // the game screen it belongs to (RTTI class name)
volatile LONG g_open = 0;
volatile LONG g_active = 0;      // open and its screen on top, at the last frame
volatile DWORD g_drawn_at = 0;   // GetTickCount() of the last frame drawn with it open
volatile LONG g_escape = 0;      // Esc was pressed while muted: closes on the next frame
volatile DWORD g_mouse_message_at = 0;  // the last mouse message to the game's window

bool Muted()
{
	return g_active && GetTickCount() - g_drawn_at < kTimeoutMs;
}

void Close(const char *why)
{
	if (!g_open)
		return;
	InterlockedExchange(&g_active, 0);
	InterlockedExchange(&g_open, 0);
	InterlockedExchange(&g_escape, 0);
	hooklog::Write("overlay: modal panel \"%s\" closed (%s): the game gets its input again", g_owner, why);
	g_owner[0] = 0;
}

// --- the queue of input for ImGui ---------------------------------------------------------
enum EventKind { kButton, kWheel, kKey, kChar, kFocus };
enum Source { kFromMessage, kFromDirectInput };
struct Event {
	BYTE kind;
	BYTE source;
	WORD code;   // the button (0..4), the virtual key, the character (UTF-16), 1 = horizontal wheel
	LONG value;  // down (1/0), the wheel in 1/120 notches, focused (1/0)
};
const int kQueue = 256;
Event g_queue[kQueue];
int g_queue_count = 0;
CRITICAL_SECTION g_queue_lock;

void Push(EventKind kind, Source source, int code, LONG value)
{
	EnterCriticalSection(&g_queue_lock);
	if (g_queue_count < kQueue)
		g_queue[g_queue_count++] = {static_cast<BYTE>(kind), static_cast<BYTE>(source), static_cast<WORD>(code), value};
	LeaveCriticalSection(&g_queue_lock);
}

// A window message for ImGui (only while muted).
void Queue(UINT msg, WPARAM wp, LPARAM lp)
{
	(void)lp;
	bool down = false;
	const int button = inputmute::MessageButton(msg, wp, &down);
	if (button >= 0) {
		Push(kButton, kFromMessage, button, down);
		return;
	}
	switch (msg) {
	case WM_MOUSEWHEEL:
		Push(kWheel, kFromMessage, 0, GET_WHEEL_DELTA_WPARAM(wp));
		break;
	case WM_MOUSEHWHEEL:
		Push(kWheel, kFromMessage, 1, GET_WHEEL_DELTA_WPARAM(wp));
		break;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		Push(kKey, kFromMessage, static_cast<int>(wp & 0xff), 1);
		break;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		Push(kKey, kFromMessage, static_cast<int>(wp & 0xff), 0);
		break;
	case WM_CHAR: {
		wchar_t c = static_cast<wchar_t>(wp);
		if (wp >= 0x80 && wp <= 0xff && !IsWindowUnicode(GameWindow())) {  // the window's code page
			const char narrow = static_cast<char>(wp);
			MultiByteToWideChar(CP_ACP, 0, &narrow, 1, &c, 1);
		}
		if (c >= 0x20)
			Push(kChar, kFromMessage, c, 0);
		break;
	}
	case WM_SETFOCUS:
		Push(kFocus, kFromMessage, 0, 1);
		break;
	case WM_KILLFOCUS:
		Push(kFocus, kFromMessage, 0, 0);
		break;
	case WM_ACTIVATEAPP:
		Push(kFocus, kFromMessage, 0, wp ? 1 : 0);
		break;
	default:
		break;
	}
}

ImGuiKey KeyOf(int vk)
{
	if (vk >= '0' && vk <= '9')
		return static_cast<ImGuiKey>(ImGuiKey_0 + (vk - '0'));
	if (vk >= 'A' && vk <= 'Z')
		return static_cast<ImGuiKey>(ImGuiKey_A + (vk - 'A'));
	if (vk >= VK_F1 && vk <= VK_F12)
		return static_cast<ImGuiKey>(ImGuiKey_F1 + (vk - VK_F1));
	switch (vk) {
	case VK_TAB: return ImGuiKey_Tab;
	case VK_LEFT: return ImGuiKey_LeftArrow;
	case VK_RIGHT: return ImGuiKey_RightArrow;
	case VK_UP: return ImGuiKey_UpArrow;
	case VK_DOWN: return ImGuiKey_DownArrow;
	case VK_PRIOR: return ImGuiKey_PageUp;
	case VK_NEXT: return ImGuiKey_PageDown;
	case VK_HOME: return ImGuiKey_Home;
	case VK_END: return ImGuiKey_End;
	case VK_INSERT: return ImGuiKey_Insert;
	case VK_DELETE: return ImGuiKey_Delete;
	case VK_BACK: return ImGuiKey_Backspace;
	case VK_SPACE: return ImGuiKey_Space;
	case VK_RETURN: return ImGuiKey_Enter;
	case VK_ESCAPE: return ImGuiKey_Escape;
	case VK_SHIFT: return ImGuiKey_LeftShift;
	case VK_CONTROL: return ImGuiKey_LeftCtrl;
	case VK_MENU: return ImGuiKey_LeftAlt;
	default: return ImGuiKey_None;
	}
}

// --- the cursor ---------------------------------------------------------------------------
// 0x00835ce0, the game's cursor update: GetCursorPos, ScreenToClient, then
//   fild [esp+8]; fstp [0x00fe6da0]; fild [esp+0Ch]; fstp [0x00fe6da4]
const DWORD kCursorStore = 0x00835d6c;
const BYTE kCursorStoreBytes[] = {0xdb, 0x44, 0x24, 0x08, 0xd9, 0x1d, 0xa0, 0x6d, 0xfe, 0x00,
                                  0xdb, 0x44, 0x24, 0x0c, 0xd9, 0x1d, 0xa4, 0x6d, 0xfe, 0x00};
const float *const kGameCursor = reinterpret_cast<const float *>(0x00fe6da0);
bool g_game_cursor = false;  // the code is as expected: follow the game's own cursor

bool ReadGameCursor(float *x, float *y)
{
	__try {
		*x = kGameCursor[0];
		*y = kGameCursor[1];
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
	return _finite(*x) && _finite(*y) && *x > -16384.0f && *x < 16384.0f && *y > -16384.0f && *y < 16384.0f;
}

// The cursor in back buffer pixels: the game's own cursor (it draws it), else Windows'.
bool CursorInBackBuffer(float w, float h, float *x, float *y, bool *from_game)
{
	*from_game = false;
	HWND window = GameWindow();
	RECT client;
	if (!window || !GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0)
		return false;
	float cx, cy;
	if (g_game_cursor && ReadGameCursor(&cx, &cy)) {
		*from_game = true;
	}
	else {
		POINT p;
		if (!GetCursorPos(&p) || !ScreenToClient(window, &p))
			return false;
		cx = static_cast<float>(p.x);
		cy = static_cast<float>(p.y);
	}
	*x = cx * w / client.right;
	*y = cy * h / client.bottom;
	return true;
}

// --- the game window's procedure -------------------------------------------------------------
inputmute::Messages g_messages;
HWND g_window = nullptr;         // the game's window now (its messages pass through WndProc)
HWND g_device_window = nullptr;  // the device's window when g_window was found
// Each subclassed window's previous procedure (a new game window would get its
// own entry; the old one keeps calling its own).
struct Subclassed {
	HWND window;
	WNDPROC next;
	bool unicode;
};
const int kWindows = 4;
Subclassed g_windows[kWindows];
volatile LONG g_window_count = 0;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	const Subclassed *w = &g_windows[0];
	for (int i = 0; i < g_window_count; ++i)
		if (g_windows[i].window == hwnd)
			w = &g_windows[i];
	if (hwnd == g_window) {
		if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)
			g_mouse_message_at = GetTickCount();
		const bool muted = Muted();
		if (muted) {
			Queue(msg, wp, lp);
			if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wp == VK_ESCAPE)
				InterlockedExchange(&g_escape, 1);
		}
		switch (g_messages.Filter(msg, wp, lp, muted)) {
		case inputmute::kSwallow:
			return 0;
		case inputmute::kDefault:
			return w->unicode ? DefWindowProcW(hwnd, msg, wp, lp) : DefWindowProcA(hwnd, msg, wp, lp);
		default:
			break;
		}
	}
	return w->unicode ? CallWindowProcW(w->next, hwnd, msg, wp, lp) : CallWindowProcA(w->next, hwnd, msg, wp, lp);
}

// The game's main window: its class is "H5" (RegisterClassA in WinMain). The
// device's window is that one; if not, the "H5" window of this process.
HWND FindGameWindow()
{
	HWND device_window = GameWindow();
	if (!device_window)
		return nullptr;
	char name[64] = "";
	GetClassNameA(device_window, name, sizeof(name));
	if (strcmp(name, "H5") == 0)
		return device_window;
	for (HWND w = FindWindowExA(nullptr, nullptr, "H5", nullptr); w; w = FindWindowExA(nullptr, w, "H5", nullptr)) {
		DWORD pid = 0;
		GetWindowThreadProcessId(w, &pid);
		if (pid == GetCurrentProcessId())
			return w;
	}
	static bool told = false;
	if (!told) {
		told = true;
		hooklog::Write("overlay: the device's window has class \"%s\" and no \"H5\" window was found: using it", name);
	}
	return device_window;
}

// Once the game's window is known (and again if the game makes a new device
// on a new window). A window is subclassed once: if another program puts its
// own procedure over ours, ours is still called through it.
void EnsureSubclass()
{
	if (!g_enabled)
		return;
	HWND device_window = GameWindow();
	if (!device_window || (g_window && device_window == g_device_window && IsWindow(g_window)))
		return;
	g_device_window = device_window;
	HWND window = FindGameWindow();
	if (!window || window == g_window)
		return;
	for (int i = 0; i < g_window_count; ++i) {
		if (g_windows[i].window == window) {  // subclassed before
			g_window = window;
			return;
		}
	}
	if (g_window_count >= kWindows)
		return;
	const bool unicode = IsWindowUnicode(window) != 0;
	// The entry first: WndProc may be called as soon as the procedure is set.
	Subclassed &w = g_windows[g_window_count];
	w.window = window;
	w.unicode = unicode;
	w.next = unicode ? reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC))
	                 : reinterpret_cast<WNDPROC>(GetWindowLongPtrA(window, GWLP_WNDPROC));
	InterlockedIncrement(&g_window_count);
	const LONG_PTR proc = reinterpret_cast<LONG_PTR>(&WndProc);
	const LONG_PTR previous =
		unicode ? SetWindowLongPtrW(window, GWLP_WNDPROC, proc) : SetWindowLongPtrA(window, GWLP_WNDPROC, proc);
	if (!previous) {
		InterlockedDecrement(&g_window_count);  // (tried again on the next frame)
		g_device_window = nullptr;
		static bool told = false;
		if (!told) {
			told = true;
			hooklog::Write("overlay: cannot pass the game window's messages through us (error %lu): no modal panels",
			               GetLastError());
		}
		return;
	}
	w.next = reinterpret_cast<WNDPROC>(previous);
	g_window = window;
	hooklog::Write("overlay: the game window %p's messages pass through us (for modal panels)", window);
}

// --- DirectInput -------------------------------------------------------------------------------
inputmute::DirectInput g_di;
BYTE g_di_buttons[8];  // the mouse buttons DirectInput last said (for ImGui, while muted)

void RecordMouseButton(int button, bool down)
{
	if (button < 0 || button >= 8 || (g_di_buttons[button] != 0) == down)
		return;
	g_di_buttons[button] = down ? 1 : 0;
	if (button < 5)
		Push(kButton, kFromDirectInput, button, down);
}

HRESULT MuteState(dihook::DeviceKind kind, void *device, DWORD size, void *data, HRESULT hr)
{
	(void)device;
	if (FAILED(hr))
		return hr;
	const bool muted = Muted();
	if (kind == dihook::kKeyboard && size >= 256) {
		BYTE *keys = static_cast<BYTE *>(data);
		if (muted && (keys[DIK_ESCAPE] & 0x80))
			InterlockedExchange(&g_escape, 1);
		g_di.KeyboardState(keys, muted);
	}
	else if (kind == dihook::kMouse && size >= 16) {
		if (muted) {
			const DIMOUSESTATE *s = static_cast<const DIMOUSESTATE *>(data);
			for (int i = 0; i < 4; ++i)
				RecordMouseButton(i, (s->rgbButtons[i] & 0x80) != 0);
			if (s->lZ)
				Push(kWheel, kFromDirectInput, 0, s->lZ);
		}
		g_di.MouseState(data, size, muted);
	}
	return hr;
}

HRESULT MuteData(dihook::DeviceKind kind, void *device, DWORD size, DIDEVICEOBJECTDATA *data, DWORD *count,
                 DWORD capacity, DWORD flags, HRESULT hr)
{
	(void)device;
	(void)capacity;
	if (FAILED(hr) || !data || !count || size < 8)
		return hr;
	const bool muted = Muted();
	if (muted && !(flags & DIGDD_PEEK)) {
		for (DWORD i = 0; i < *count; ++i) {
			const DIDEVICEOBJECTDATA *e =
				reinterpret_cast<const DIDEVICEOBJECTDATA *>(reinterpret_cast<const BYTE *>(data) + i * size);
			const bool down = (e->dwData & 0x80) != 0;
			if (kind == dihook::kKeyboard && e->dwOfs == DIK_ESCAPE && down)
				InterlockedExchange(&g_escape, 1);
			else if (kind == dihook::kMouse && e->dwOfs >= DIMOFS_BUTTON0 && e->dwOfs <= DIMOFS_BUTTON7)
				RecordMouseButton(static_cast<int>(e->dwOfs - DIMOFS_BUTTON0), down);
			else if (kind == dihook::kMouse && e->dwOfs == DIMOFS_Z && e->dwData)
				Push(kWheel, kFromDirectInput, 0, static_cast<LONG>(e->dwData));
		}
	}
	*count = g_di.Events(kind == dihook::kKeyboard, data, size, *count, muted);
	return hr;
}

bool g_fed = false;  // the last frame fed ImGui (a panel was open)
char g_info[160] = "";

}  // namespace

namespace internal {

void ModalFrameStart()
{
	EnsureSubclass();
	if (!g_open)
		return;
	if (g_escape) {
		Close("Esc");
		return;
	}
	if (GetTickCount() - g_drawn_at >= kTimeoutMs) {
		Close("no frame drawn for 2 s");
		return;
	}
	char top[96];
	TopScreen(top, sizeof(top));
	if (strcmp(top, g_screen) != 0) {
		Close(top[0] ? "another screen is on top" : "its screen is gone");
		return;
	}
	InterlockedExchange(&g_active, 1);
}

bool ModalOpen()
{
	return g_open != 0;
}

void ModalDrawn()
{
	g_drawn_at = GetTickCount();
}

void CloseModalOf(const char *owner, const char *why)
{
	if (g_open && strcmp(g_owner, owner) == 0)
		Close(why);
}

void FeedImGui(float w, float h)
{
	ImGuiIO &io = ImGui::GetIO();
	if (!g_enabled)
		return;
	EnterCriticalSection(&g_queue_lock);
	Event events[kQueue];
	const int n = g_queue_count;
	memcpy(events, g_queue, n * sizeof(Event));
	g_queue_count = 0;
	LeaveCriticalSection(&g_queue_lock);
	if (!g_open) {
		if (g_fed) {  // just closed: ImGui lets go of every key and button
			g_fed = false;
			io.ClearInputKeys();
			io.ClearInputMouse();
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		}
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse;  // (display-only panels never take the mouse)
		return;
	}
	g_fed = true;
	io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
	float x, y;
	bool from_game = false;
	if (CursorInBackBuffer(w, h, &x, &y, &from_game))
		io.AddMousePosEvent(x, y);
	// The buttons and the wheel come from the window's messages; from
	// DirectInput only when the window gets no mouse messages (the game took the
	// mouse for itself), so that a click never counts twice.
	const bool messages = GetTickCount() - g_mouse_message_at < kMouseMessagesMs;
	for (int i = 0; i < n; ++i) {
		const Event &e = events[i];
		const bool mouse = e.kind == kButton || e.kind == kWheel;
		if (mouse && (e.source == kFromMessage) != messages)
			continue;
		switch (e.kind) {
		case kButton:
			io.AddMouseButtonEvent(e.code, e.value != 0);
			break;
		case kWheel:
			if (e.code)
				io.AddMouseWheelEvent(-e.value / 120.0f, 0.0f);
			else
				io.AddMouseWheelEvent(0.0f, e.value / 120.0f);
			break;
		case kKey: {
			const ImGuiKey key = KeyOf(e.code);
			if (key != ImGuiKey_None)
				io.AddKeyEvent(key, e.value != 0);
			if (e.code == VK_SHIFT)
				io.AddKeyEvent(ImGuiMod_Shift, e.value != 0);
			else if (e.code == VK_CONTROL)
				io.AddKeyEvent(ImGuiMod_Ctrl, e.value != 0);
			else if (e.code == VK_MENU)
				io.AddKeyEvent(ImGuiMod_Alt, e.value != 0);
			break;
		}
		case kChar:
			io.AddInputCharacterUTF16(e.code);
			break;
		case kFocus:
			io.AddFocusEvent(e.value != 0);
			break;
		default:
			break;
		}
	}
	_snprintf_s(g_info, sizeof(g_info), _TRUNCATE, "cursor: %s; buttons: %s; window: %s",
	            from_game ? "the game's" : "Windows'", messages ? "window messages" : "DirectInput",
	            g_window ? "subclassed" : "not subclassed");
}

const char *InputInfo()
{
	return g_info;
}

}  // namespace internal

void EnableModalInput()
{
	if (g_enabled)
		return;
	g_enabled = true;
	InitializeCriticalSection(&g_queue_lock);
	g_game_cursor = memcmp(reinterpret_cast<const void *>(kCursorStore), kCursorStoreBytes, sizeof(kCursorStoreBytes)) == 0;
	const dihook::Filters filters = {&MuteState, &MuteData, nullptr, nullptr};
	dihook::AddFilters(filters, 100);  // after the robot's keys: they are held back too
	hooklog::Write("overlay: modal panels ready (%s)",
	               g_game_cursor ? "they follow the game's own cursor" : "the game's cursor is not as expected: they follow Windows' cursor");
}

bool OpenModal(const char *owner)
{
	if (g_open)
		return strcmp(g_owner, owner) == 0;
	if (!g_enabled || !g_window) {
		hooklog::Write("overlay: modal panel \"%s\" not opened: %s", owner,
		               !g_enabled ? "modal input is not enabled" : "the game window's messages do not pass through us");
		return false;
	}
	TopScreen(g_screen, sizeof(g_screen));
	lstrcpynA(g_owner, owner, sizeof(g_owner));
	InterlockedExchange(&g_escape, 0);
	g_drawn_at = GetTickCount();
	InterlockedExchange(&g_open, 1);
	InterlockedExchange(&g_active, 1);
	hooklog::Write("overlay: modal panel \"%s\" open over %s: the game gets no mouse or keyboard input", owner,
	               g_screen[0] ? g_screen : "(an unknown screen)");
	return true;
}

void CloseModal(const char *owner)
{
	internal::CloseModalOf(owner, "closed by its panel");
}

bool ModalOpen(const char *owner)
{
	return g_open && strcmp(g_owner, owner) == 0;
}

}  // namespace overlay
