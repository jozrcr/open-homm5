// The template builder's widgets in the game's style (rmg_builder_widgets.cpp):
// the game's buttons, drop-down lists, check boxes and arrows, drawn with
// ImGui from the game's own pictures. Positions and sizes are in pixels.
#pragma once

#include <string>

#include "../hook/third_party/imgui/imgui.h"

struct IDirect3DDevice9;

namespace mods {
namespace builder_ui {

enum Picture {
	kBackground,       // UI/RMGScreen/MainBg: the random map screen's panel
	kButtonNormal,     // UI/Common/DlgButtons/Button_122_34_*: the game's buttons
	kButtonHighlight,
	kButtonClick,
	kButtonDisabled,
	kCheck0,           // UI/Common/CheckBox/CheckBox_{0,1}_Normal
	kCheck1,
	kComboField,       // UI/Common/ComboBox_201_33/*: the drop-down lists of the screen's options
	kComboButton,
	kComboButtonHighlight,
	kComboList,
	kComboSelection,
	kPictureCount
};
// In the overlay's prepare: reads the pictures, a few per frame.
void LoadPictures(IDirect3DDevice9 *device);
ImTextureID Tex(int picture);  // 0 when not loaded (then plain colours are drawn)

// The game's colours.
extern const ImU32 kGold, kParchment, kDim, kWarn, kError, kEdge;

float TextWidth(ImFont *font, float px, const char *text);
// `text` cut to `max_w` pixels with "..." (a last resort: the words are short).
std::string Fit(ImFont *font, float px, const char *text, float max_w);
// Text with the game's dark shadow (one pixel down and right).
void ShadowText(ImDrawList *draw, ImFont *font, float px, ImVec2 p, ImU32 colour, const char *text);

// A picture stretched over a rectangle, its `border` (in picture pixels, shown
// `border_px` wide) kept unstretched: the frames of the game's fields.
void NineSlice(ImDrawList *draw, ImTextureID tex, ImVec2 p0, ImVec2 p1, ImVec2 tex_size, float border, float border_px);

// The game's 122x34 button with a centred label; true when clicked (on release).
bool Button(const char *id, ImVec2 p0, ImVec2 p1, const char *label, ImFont *font, float px, bool enabled, float k);
// A non-interactive copy of it (for a button that reads the mouse itself).
void ButtonPicture(ImDrawList *draw, ImVec2 p0, ImVec2 p1, const char *label, ImFont *font, float px, bool hovered,
                   bool pressed, float k);
// A drop-down list like the screen's own: `shown` (or items[*current]) in the
// field; a click opens the list below it. True when another item was chosen.
bool Combo(const char *id, ImVec2 p, ImVec2 size, const char *const *items, int count, int *current, const char *shown,
           ImFont *font, float px, float k);
// [<] value [>], right-aligned at `right` (its top-right corner): -1 or +1 when
// an arrow was clicked; *blocked = -1 or +1 when a greyed arrow was clicked.
int Stepper(const char *id, ImVec2 right, float h, int value, bool can_down, bool can_up, int *blocked, ImFont *font,
            float px, float k);
// The game's check box with a label after it; true when clicked (*value changes).
bool CheckBox(const char *id, ImVec2 p, float h, const char *label, bool *value, ImFont *font, float px, float max_w,
              float k);

}  // namespace builder_ui
}  // namespace mods
