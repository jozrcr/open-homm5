// The template builder's widgets in the game's style (see rmg_builder_widgets.h).
// The pictures are the game's own, read from its data through the overlay
// (MMH5.5's paks count, as for the game); when one cannot be read, that part
// is drawn in plain colours of the same look.
#include "rmg_builder_widgets.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../hook/log.h"
#include "../hook/overlay.h"

namespace mods {
namespace builder_ui {
namespace {

struct PictureFile {
	const char *path;
	float w, h;  // the picture's size (the .dds holds exactly that)
};
const PictureFile kFiles[kPictureCount] = {
	{"UI/RMGScreen/MainBg.(Texture).dds", 1024, 512},
	{"UI/Common/DlgButtons/Button_122_34_Normal.(Texture).dds", 122, 34},
	{"UI/Common/DlgButtons/Button_122_34_Highlight.(Texture).dds", 122, 34},
	{"UI/Common/DlgButtons/Button_122_34_Click.(Texture).dds", 122, 34},
	{"UI/Common/DlgButtons/Button_122_34_Disabled.(Texture).dds", 122, 34},
	{"UI/Common/CheckBox/CheckBox_0_Normal.(Texture).dds", 28, 25},
	{"UI/Common/CheckBox/CheckBox_1_Normal.(Texture).dds", 28, 25},
	{"UI/Common/ComboBox_201_33/Combo.(Texture).dds", 201, 33},
	{"UI/Common/ComboBox_201_33/ComboBtnNormal.(Texture).dds", 25, 25},
	{"UI/Common/ComboBox_201_33/ComboBtnHighlight.(Texture).dds", 25, 25},
	{"UI/Common/ComboBox_201_33/ComboList.(Texture).dds", 201, 33},
	{"UI/Common/ComboBox_201_33/ComboListSelection.(Texture).dds", 201, 33},
};
void *g_pictures[kPictureCount];
bool g_told[kPictureCount];

ImVec2 Size(int picture)
{
	return ImVec2(kFiles[picture].w, kFiles[picture].h);
}

float Round(float v)
{
	return floorf(v + 0.5f);
}

// A small arrow box (the steppers): the game's dark field, a bronze edge, a gold triangle.
void ArrowBox(ImDrawList *draw, ImVec2 p0, ImVec2 p1, int direction, bool enabled, bool hovered, bool pressed, float k)
{
	const ImU32 fill_top = pressed ? IM_COL32(40, 33, 22, 255) : hovered && enabled ? IM_COL32(104, 84, 50, 255)
	                                                                                 : IM_COL32(84, 68, 42, 255);
	const ImU32 fill_bottom = pressed ? IM_COL32(60, 48, 30, 255) : IM_COL32(42, 34, 22, 255);
	draw->AddRectFilledMultiColor(p0, p1, fill_top, fill_top, fill_bottom, fill_bottom);
	draw->AddRect(p0, p1, enabled ? kEdge : IM_COL32(90, 80, 66, 255), 0.0f, 0, 1.0f);
	const float w = p1.x - p0.x, h = p1.y - p0.y, d = pressed ? k : 0.0f;
	const float cx = p0.x + w / 2 + d, cy = p0.y + h / 2 + d, r = floorf(h * 0.28f);
	const ImU32 colour = !enabled ? IM_COL32(110, 100, 86, 255) : hovered ? IM_COL32(255, 228, 140, 255) : kGold;
	if (direction < 0)
		draw->AddTriangleFilled(ImVec2(cx - r * 0.8f, cy), ImVec2(cx + r * 0.6f, cy - r), ImVec2(cx + r * 0.6f, cy + r), colour);
	else
		draw->AddTriangleFilled(ImVec2(cx + r * 0.8f, cy), ImVec2(cx - r * 0.6f, cy + r), ImVec2(cx - r * 0.6f, cy - r), colour);
}

void Field(ImDrawList *draw, ImVec2 p0, ImVec2 p1, int picture, float k)
{
	if (ImTextureID tex = Tex(picture))
		NineSlice(draw, tex, p0, p1, Size(picture), 4.0f, Round(3.0f * k));
	else {
		draw->AddRectFilled(p0, p1, IM_COL32(50, 42, 32, 255));
		draw->AddRect(p0, p1, kEdge, 0.0f, 0, Round(2.0f * k));
	}
}

}  // namespace

const ImU32 kGold = IM_COL32(240, 208, 128, 255);
const ImU32 kParchment = IM_COL32(234, 223, 196, 255);
const ImU32 kDim = IM_COL32(190, 176, 146, 255);
const ImU32 kWarn = IM_COL32(244, 196, 120, 255);
const ImU32 kError = IM_COL32(248, 132, 108, 255);
const ImU32 kEdge = IM_COL32(150, 110, 60, 255);

void LoadPictures(IDirect3DDevice9 *device)
{
	int loads = 2;  // picture files read per frame at most
	for (int i = 0; i < kPictureCount; ++i) {
		bool failed = false;
		g_pictures[i] = overlay::GameTexture(device, kFiles[i].path, &loads, &failed);
		if (failed && !g_told[i]) {
			g_told[i] = true;
			hooklog::Write("rmg builder: the game's picture %s is not readable: plain style", kFiles[i].path);
		}
	}
}

ImTextureID Tex(int picture)
{
	return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g_pictures[picture]));
}

float TextWidth(ImFont *font, float px, const char *text)
{
	return font->CalcTextSizeA(px, 100000.0f, 0.0f, text).x;
}

std::string Fit(ImFont *font, float px, const char *text, float max_w)
{
	if (TextWidth(font, px, text) <= max_w)
		return text;
	std::string s(text);
	const char kDots[] = "\xe2\x80\xa6";  // the ellipsis
	while (!s.empty()) {
		// drop one UTF-8 letter (and the spaces before the dots)
		size_t n = s.size() - 1;
		while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xc0) == 0x80)
			--n;
		s.erase(n);
		while (!s.empty() && s.back() == ' ')
			s.pop_back();
		if (TextWidth(font, px, (s + kDots).c_str()) <= max_w)
			return s + kDots;
	}
	return "";
}

void ShadowText(ImDrawList *draw, ImFont *font, float px, ImVec2 p, ImU32 colour, const char *text)
{
	p.x = Round(p.x);
	p.y = Round(p.y);
	draw->AddText(font, px, ImVec2(p.x + 1, p.y + 1), IM_COL32(0, 0, 0, 200), text);
	draw->AddText(font, px, p, colour, text);
}

void NineSlice(ImDrawList *draw, ImTextureID tex, ImVec2 p0, ImVec2 p1, ImVec2 tex_size, float border, float border_px)
{
	const float bx = border_px < (p1.x - p0.x) / 2 ? border_px : (p1.x - p0.x) / 2;
	const float by = border_px < (p1.y - p0.y) / 2 ? border_px : (p1.y - p0.y) / 2;
	const float xs[4] = {p0.x, p0.x + bx, p1.x - bx, p1.x};
	const float ys[4] = {p0.y, p0.y + by, p1.y - by, p1.y};
	const float us[4] = {0.0f, border / tex_size.x, 1.0f - border / tex_size.x, 1.0f};
	const float vs[4] = {0.0f, border / tex_size.y, 1.0f - border / tex_size.y, 1.0f};
	for (int j = 0; j < 3; ++j)
		for (int i = 0; i < 3; ++i)
			if (xs[i + 1] > xs[i] && ys[j + 1] > ys[j])
				draw->AddImage(tex, ImVec2(xs[i], ys[j]), ImVec2(xs[i + 1], ys[j + 1]), ImVec2(us[i], vs[j]),
				               ImVec2(us[i + 1], vs[j + 1]));
}

void ButtonPicture(ImDrawList *draw, ImVec2 p0, ImVec2 p1, const char *label, ImFont *font, float px, bool hovered,
                   bool pressed, float k)
{
	ImTextureID picture = Tex(pressed ? kButtonClick : kButtonNormal);
	if (picture)
		draw->AddImage(picture, p0, p1);
	else
		draw->AddRectFilled(p0, p1, pressed ? IM_COL32(70, 52, 30, 255) : IM_COL32(96, 72, 40, 255), 4.0f * k);
	if (hovered && !pressed) {
		if (ImTextureID highlight = Tex(kButtonHighlight))
			draw->AddImage(highlight, p0, p1);
		else
			draw->AddRect(p0, p1, kGold, 4.0f * k, 0, 1.5f * k);
	}
	const std::string text = Fit(font, px, label, p1.x - p0.x - 12.0f * k);
	const ImVec2 t = font->CalcTextSizeA(px, 10000.0f, 0.0f, text.c_str());
	const float down = pressed ? k : 0.0f;
	ShadowText(draw, font, px, ImVec2((p0.x + p1.x - t.x) / 2, (p0.y + p1.y - t.y) / 2 + down), IM_COL32(255, 236, 190, 255),
	           text.c_str());
}

bool Button(const char *id, ImVec2 p0, ImVec2 p1, const char *label, ImFont *font, float px, bool enabled, float k)
{
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImGui::SetCursorScreenPos(p0);
	ImGui::BeginDisabled(!enabled);
	const bool clicked = ImGui::InvisibleButton(id, ImVec2(p1.x - p0.x, p1.y - p0.y));
	ImGui::EndDisabled();
	if (!enabled) {
		if (ImTextureID disabled = Tex(kButtonDisabled))
			draw->AddImage(disabled, p0, p1);
		else
			draw->AddRectFilled(p0, p1, IM_COL32(70, 64, 56, 255), 4.0f * k);
		const std::string text = Fit(font, px, label, p1.x - p0.x - 12.0f * k);
		const ImVec2 t = font->CalcTextSizeA(px, 10000.0f, 0.0f, text.c_str());
		ShadowText(draw, font, px, ImVec2((p0.x + p1.x - t.x) / 2, (p0.y + p1.y - t.y) / 2), IM_COL32(150, 140, 125, 255),
		           text.c_str());
		return false;
	}
	const bool hovered = ImGui::IsItemHovered();
	ButtonPicture(draw, p0, p1, label, font, px, hovered, hovered && ImGui::IsItemActive(), k);
	return clicked;
}

bool Combo(const char *id, ImVec2 p, ImVec2 size, const char *const *items, int count, int *current, const char *shown,
           ImFont *font, float px, float k)
{
	ImGui::PushID(id);
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImGui::SetCursorScreenPos(p);
	const bool clicked = ImGui::InvisibleButton("##field", size);
	const bool hovered = ImGui::IsItemHovered();
	const ImVec2 p1(p.x + size.x, p.y + size.y);
	Field(draw, p, p1, kComboField, k);
	// the arrow button, inside the field on the right (the game's ComboBtn: 25x25 at -4, 4 of a 33 high field)
	const float inset = Round(3.0f * k), bs = size.y - 2 * inset;
	const ImVec2 b0(p1.x - inset - bs, p.y + inset), b1(p1.x - inset, p.y + inset + bs);
	ImTextureID arrow = Tex(hovered ? kComboButtonHighlight : kComboButton);
	if (!arrow)
		arrow = Tex(kComboButton);
	if (arrow)
		draw->AddImage(arrow, b0, b1);
	else {
		draw->AddRectFilled(b0, b1, IM_COL32(92, 90, 85, 255));
		const float cx = (b0.x + b1.x) / 2, cy = (b0.y + b1.y) / 2, r = bs * 0.25f;
		draw->AddTriangleFilled(ImVec2(cx - r, cy - r * 0.6f), ImVec2(cx + r, cy - r * 0.6f), ImVec2(cx, cy + r * 0.7f),
		                        IM_COL32(220, 216, 206, 255));
	}
	const char *label = shown ? shown : (*current >= 0 && *current < count ? items[*current] : "");
	const float pad = Round(7.0f * k);
	const std::string text = Fit(font, px, label, b0.x - p.x - pad - Round(3.0f * k));
	ShadowText(draw, font, px, ImVec2(p.x + pad, p.y + (size.y - px) / 2 - 1), hovered ? IM_COL32(255, 240, 205, 255) : kParchment,
	           text.c_str());
	if (clicked)
		ImGui::OpenPopup("##list");

	bool changed = false;
	const float row = Round(px * 1.55f), border = Round(2.0f * k);
	const int shown_rows = count < 10 ? count : 10;
	const float list_h = row * shown_rows + 2 * border;
	const float screen_h = ImGui::GetIO().DisplaySize.y;
	const bool above = p1.y + list_h > screen_h && p.y - list_h >= 0;
	ImGui::SetNextWindowPos(ImVec2(p.x, above ? p.y - list_h : p1.y));
	ImGui::SetNextWindowSize(ImVec2(size.x, list_h));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(border, border));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, Round(8.0f * k));
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(36, 30, 22, 250));
	ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(24, 20, 14, 255));
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, kEdge);
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(190, 146, 84, 255));
	ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, kGold);
	if (ImGui::BeginPopup("##list", ImGuiWindowFlags_NoMove)) {
		ImDrawList *list = ImGui::GetWindowDrawList();
		const ImVec2 w0 = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
		list->AddRect(w0, ImVec2(w0.x + ws.x, w0.y + ws.y), kEdge, 0.0f, 0, border);
		const float width = ImGui::GetContentRegionAvail().x;
		for (int i = 0; i < count; ++i) {
			ImGui::PushID(i);
			const ImVec2 r0 = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("##item", ImVec2(width, row))) {
				changed = *current != i;
				*current = i;
				ImGui::CloseCurrentPopup();
			}
			const bool over = ImGui::IsItemHovered();
			const ImVec2 r1(r0.x + width, r0.y + row);
			if (over) {
				if (ImTextureID sel = Tex(kComboSelection))
					list->AddImage(sel, r0, r1);
				else
					list->AddRectFilled(r0, r1, IM_COL32(96, 76, 44, 255));
			}
			const std::string item = Fit(font, px, items[i], width - 2 * pad);
			ShadowText(list, font, px, ImVec2(r0.x + pad, r0.y + (row - px) / 2 - 1),
			           i == *current ? kGold : over ? IM_COL32(255, 240, 205, 255) : kParchment, item.c_str());
			if (i == *current && ImGui::IsWindowAppearing())
				ImGui::SetScrollHereY(0.5f);
			ImGui::PopID();
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleColor(5);
	ImGui::PopStyleVar(5);
	ImGui::PopID();
	return changed;
}

int Stepper(const char *id, ImVec2 right, float h, int value, bool can_down, bool can_up, int *blocked, ImFont *font,
            float px, float k)
{
	ImGui::PushID(id);
	ImDrawList *draw = ImGui::GetWindowDrawList();
	const float box = h, gap = Round(2.0f * k), number = Round(h * 1.25f);
	const ImVec2 up0(right.x - box, right.y), up1(right.x, right.y + box);
	const ImVec2 n0(up0.x - gap - number, right.y), n1(up0.x - gap, right.y + box);
	const ImVec2 down0(n0.x - gap - box, right.y), down1(n0.x - gap, right.y + box);
	int result = 0;
	*blocked = 0;
	ImGui::SetCursorScreenPos(down0);
	if (ImGui::InvisibleButton("##down", ImVec2(box, box))) {
		if (can_down)
			result = -1;
		else
			*blocked = -1;
	}
	ArrowBox(draw, down0, down1, -1, can_down, ImGui::IsItemHovered(), can_down && ImGui::IsItemActive(), k);
	ImGui::SetCursorScreenPos(up0);
	if (ImGui::InvisibleButton("##up", ImVec2(box, box))) {
		if (can_up)
			result = 1;
		else
			*blocked = 1;
	}
	ArrowBox(draw, up0, up1, 1, can_up, ImGui::IsItemHovered(), can_up && ImGui::IsItemActive(), k);
	draw->AddRectFilled(n0, n1, IM_COL32(26, 21, 15, 200));
	char text[16];
	_snprintf_s(text, sizeof(text), _TRUNCATE, "%d", value);
	const float tw = TextWidth(font, px, text);
	ShadowText(draw, font, px, ImVec2((n0.x + n1.x - tw) / 2, right.y + (box - px) / 2 - 1), IM_COL32(255, 255, 255, 255), text);
	ImGui::PopID();
	return result;
}

bool CheckBox(const char *id, ImVec2 p, float h, const char *label, bool *value, ImFont *font, float px, float max_w,
              float k)
{
	ImDrawList *draw = ImGui::GetWindowDrawList();
	const float bw = Round(h * 28.0f / 25.0f), gap = Round(5.0f * k);
	const std::string text = Fit(font, px, label, max_w - bw - gap);
	const float tw = TextWidth(font, px, text.c_str());
	ImGui::SetCursorScreenPos(p);
	const bool clicked = ImGui::InvisibleButton(id, ImVec2(bw + gap + tw, h));
	const bool hovered = ImGui::IsItemHovered();
	if (clicked)
		*value = !*value;
	const ImVec2 b0 = p, b1(p.x + bw, p.y + h);
	if (ImTextureID tex = Tex(*value ? kCheck1 : kCheck0))
		draw->AddImage(tex, b0, b1);
	else {
		draw->AddRectFilled(b0, b1, IM_COL32(30, 25, 18, 255), 3.0f * k);
		draw->AddRect(b0, b1, kEdge, 3.0f * k, 0, 1.5f * k);
		if (*value)
			draw->AddText(font, px, ImVec2(b0.x + bw * 0.25f, b0.y), kGold, "x");
	}
	if (hovered)
		draw->AddRect(b0, b1, IM_COL32(255, 228, 140, 160), 3.0f * k, 0, 1.0f);
	ShadowText(draw, font, px, ImVec2(p.x + bw + gap, p.y + (h - px) / 2 - 1), hovered ? IM_COL32(255, 240, 205, 255) : kParchment,
	           text.c_str());
	return clicked;
}

}  // namespace builder_ui
}  // namespace mods
