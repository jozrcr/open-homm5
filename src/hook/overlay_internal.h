// The overlay's parts (overlay.h), shared between its files:
//   overlay.cpp            the clients, the ImGui frame, fonts, the screen on top, keys;
//   overlay_input.cpp      modal panels: the window procedure, the ImGui feed, the DirectInput mute;
//   overlay_textures.cpp   pictures from the game's data;
//   overlay_test_panel.cpp the test panel.
#pragma once
#include <windows.h>

struct IDirect3DDevice9;

namespace overlay {
namespace internal {

// overlay_textures.cpp
bool InstallTextures();  // false: no pictures (the panels use plain colours)
void ReleaseTextures();

// overlay_input.cpp, from the frame (overlay.cpp), in this order:
// before the clients are asked: closes the modal panel if its screen is gone,
// Esc was pressed, or no frame was drawn for too long; puts the window
// procedure in once the game's window is known.
void ModalFrameStart();
// Is a modal panel open (after ModalFrameStart)?
bool ModalOpen();
// Just before ImGui::NewFrame: the mouse and keys for ImGui (when a modal
// panel is open; otherwise ImGui gets no mouse at all).
void FeedImGui(float back_buffer_w, float back_buffer_h);
// After the frame was drawn with the modal panel open.
void ModalDrawn();
// A client crashed: its panel closes.
void CloseModalOf(const char *owner, const char *why);
// For the test panel: where the cursor and the buttons come from, this frame.
const char *InputInfo();

// overlay.cpp
// Is this window the game's, and in front?
bool GameInFront();

}  // namespace internal
}  // namespace overlay
