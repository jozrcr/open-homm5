// The overlay: what our panels draw over the game's frames, shared by all of
// them (the player list, src/mods/player_list.cpp; the template builder,
// src/mods/rmg_builder.cpp; the overlay test panel, overlay_test_panel.cpp).
//
// It owns one Dear ImGui context (third_party/imgui and its DirectX 9
// renderer), the fonts, the pictures read from the game's data, and the
// game's Direct3D device's handling (d3d9_hook.cpp: a frame callback just
// before each frame is shown, and the device resets). Each panel registers as
// a client; once per frame the overlay asks every client whether it wants to
// draw, lets them load pictures and ask for fonts, then runs one ImGui frame
// in which each of them draws, in the order they registered.
//
// Modal panels (overlay_input.cpp): while one is open, the game gets no mouse
// or keyboard input and the panel gets it all:
//   - the game window's procedure (window class "H5") is subclassed: its mouse
//     and keyboard messages go to ImGui instead of the game;
//   - what the game reads from DirectInput (keyboard and mouse, through our
//     dinput8.dll proxy, dinput_hook.cpp) is held back: no key, no button, no
//     wheel (the mouse's motion still moves the game's own cursor);
//   - Esc closes the panel.
// Safety nets, so input always comes back:
//   - the panel belongs to one game screen: it closes when that screen is no
//     longer on top (checked every frame: another screen, a message box, a
//     lost connection...);
//   - if no frame was drawn for 2 s (alt-tab in fullscreen, a lost device, a
//     panel that cannot draw), the input goes back to the game at once, and
//     the panel closes on the next frame;
//   - a client that crashes is turned off, and its panel closes.
// Without an open modal panel, everything passes through untouched.
#pragma once
#include <windows.h>

#include <string>

struct IDirect3DDevice9;
struct ImFont;

namespace overlay {

struct Frame {
	IDirect3DDevice9 *device;
	float width, height;  // the back buffer, in pixels (what ImGui lays out in)
	float delta;          // seconds since the last frame drawn (1/60 after a pause)
};

struct Client {
	const char *name;  // for the log, and the owner name of its modal panel
	// Every frame shown (also when nothing is drawn): true = draw this frame.
	// Keep it cheap: the client's own state, its key, the screen on top.
	bool (*wanted)();
	// The frame will be drawn: load pictures (GameTexture) and ask for the
	// fonts this frame needs (WantFont). Optional.
	void (*prepare)(const Frame &frame);
	// Draw, with ImGui (between ImGui::NewFrame and ImGui::Render).
	void (*draw)(const Frame &frame);
};

// From DLL load (hooks::InstallEarly), when a client will be added: the
// Direct3D import goes in now, so the game's first device is seen too.
void InstallEarly();
// From hooks::InstallAll: adds a client (and the overlay's hooks, once).
// False when the overlay cannot work in this game (no Direct3D 9 import).
bool AddClient(const Client &client);

// --- fonts (the overlay's one face: Tahoma Bold, else Segoe UI Bold, else Arial
// Bold, else ImGui's own; Latin, Latin-1, Latin Extended-A, Cyrillic) ---------
// In prepare: this frame needs the font at this size (pixels, rounded).
void WantFont(float px);
// In draw: the font at that size (the one asked for in prepare), or the
// closest one there is.
ImFont *Font(float px);

// --- pictures from the game's data (overlay_textures.cpp) --------------------
// The picture of a .dds file of the game's data as a texture of the device
// (IDirect3DTexture9 *, an ImTextureID), or nullptr: not loaded yet (at most
// *loads_left files are read per call; each takes 0.1-1 ms), or not readable
// (then *failed, if given, is true). The pictures stay until the device goes.
void *GameTexture(IDirect3DDevice9 *device, const char *dds_path, int *loads_left, bool *failed = nullptr);
// A file of the game's data, read through the game's own file system (MMH5.5's
// paks count as they do for the game). False before the game opened its first
// file, or when there is no such file.
bool ReadGameFile(const char *path, std::string *out);

// --- the game ----------------------------------------------------------------
// The RTTI class name of the game's screen on top (".?AVCAdventureScreen@NUI@@",
// ".?AVCRMGScreen@NUI@@"...), or false (no screen, or the game is not as expected).
bool TopScreen(char *class_name, int size);
// The screen object on top (the one TopScreen names; the whole object, where
// its main vftable is), or nullptr.
void *TopScreenObject();
// The words of our panels: [ui] language=auto (the game's language), en or fr.
enum Language { kEnglish, kFrench };
Language PanelLanguage();
// The mouse in back buffer pixels (Windows' cursor), or false when it is not over the game.
bool MouseInBackBuffer(float w, float h, float *x, float *y);
// The cursor the player sees, in back buffer pixels: the game's own cursor
// (it draws it), else Windows'. False when the game's window is not known.
bool CursorInBackBuffer(float w, float h, float *x, float *y);
// The game's window (the device's), or nullptr.
HWND GameWindow();

// --- keys --------------------------------------------------------------------
// One of our keys (F1..F12...): true once per press, while the game's window
// is in front (read with GetAsyncKeyState, so the game still gets the key), or
// when the robot pressed it (its step "key|F11", robot::TakeOverlayKey).
// `was_down` is the caller's memory.
bool KeyPressed(int vk, bool *was_down);
// "F1".."F12" or "none" (0) from the ini; `fallback` when it is neither.
int ParseKey(const wchar_t *text, int fallback);

// --- modal panels (overlay_input.cpp) ----------------------------------------
// From hooks::InstallAll, by every client that opens modal panels: the window
// procedure and DirectInput hooks go in.
void EnableModalInput();
// Opens `owner`'s modal panel (its client name), for the screen on top now
// (it closes when another screen is on top). One modal panel at a time: false
// when another one is open, or modal input is not enabled.
bool OpenModal(const char *owner);
// Closes it (nothing if it is not open).
void CloseModal(const char *owner);
// Is `owner`'s panel open? The overlay may have closed it (Esc, its screen, the timeout).
bool ModalOpen(const char *owner);

// The overlay test panel ([ui] overlay_test_panel=1, F10; overlay_test_panel.cpp).
void InstallTestPanelEarly();
void ApplyTestPanelFromSettings();

}  // namespace overlay
