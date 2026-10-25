// The player list overlay: the panel itself. Everything about its look, and
// all its words, are in this file (DrawPanel); the rest of the overlay only
// hands it a Snapshot of the game.
//
// The owner's design (2026-10-25), over the adventure map's left edge, between
// the resource bar and the minimap:
//   [ Jour 34 - 3/4 Prêts ]              the day, and how many humans are done
//   (1)                                  a team: a numbered round button, a
//    |  [(face) Joz          ● ]          thin line down along its players
//    |  [(face) Paul         ✓ ]
//   (2)
//    |  [(face) Computer 1  ••• ]
// Each pill: the hero's round portrait (ring in the player's colour), the
// player's name, a status mark: green dot = playing, grey tick = turn ended,
// three blinking purple dots = the AI is moving, hollow grey ring = waiting.
// No team buttons or lines in a free-for-all. Sizes follow the screen height,
// as the game's own interface does (the sketch: 820 lines; pills 30 px high,
// portraits 40 px, a bit bigger than the pills, after the owner saw it).
//
// In the game's look: the pills are made of the game's own resource bar
// picture (its metal frame with end caps, black inside), cut in three (the
// two caps keep their shape, the middle stretches); the portrait sits on the
// left cap like the bar's own round button. Round things get the game's thin
// bronze rims; text is white with a dark shadow. Without the picture (not
// readable): the plain style, flat dark capsules.
#include "player_list_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../hook/third_party/imgui/imgui.h"

namespace mods {
namespace plist {

// The game's resource bar: 1024x64; the frame is rows 1..43, the left cap
// columns 2..36, the right cap 660..694, the middle plain (black inside).
const char kFramePicture[] = "Textures/AdventureScreen-FPP-2/ResourceBar-heavy.dds";

namespace {

// --- words ------------------------------------------------------------------------------
struct Words {
	const char *day;       // "Day 34"
	const char *ready;     // "3/4 ready"
	const char *computer;  // a computer player without a name
	const char *player;    // a human without a name
};
const Words kWords[] = {
	{"Day", "ready", "Computer %d", "Player %d"},                 // kEnglish
	{"Jour", "Pr\xc3\xaa" "ts", "Ordinateur %d", "Joueur %d"},  // kFrench (UTF-8)
};

// --- sizes, in pixels on a screen 820 lines high ----------------------------------------------
const float kRefHeight = 820.0f;
const float kHeaderHeight = 34.0f, kHeaderPad = 14.0f;  // pad: beyond the frame's end caps
const float kAfterHeader = 18.0f;
const float kCircle = 18.0f;  // a team's numbered button
const float kCircleToPill = 18.0f;
const float kPillHeight = 30.0f, kPillGap = 14.0f;
const float kFace = 40.0f;      // the portrait: bigger than the pill, over its left cap
const float kFaceToName = 10.0f, kNameToMark = 10.0f, kMark = 14.0f, kMarkToCap = 6.0f;
const float kPillMinWidth = 140.0f, kPillMaxWidth = 230.0f;
const float kTeamGap = 22.0f;   // after a team's last pill
const float kLineToFace = 7.0f;  // the team line, then the portraits
const float kNameFont = 13.0f, kHeaderFont = 14.0f, kNumberFont = 11.0f;
const float kMinPill = 20.0f;  // pills never get smaller than this
// The panel stays between these (fractions of the screen height): below the
// resource bar, above the multiplayer chat box (0.64) and the minimap (0.73).
const float kHighest = 0.08f, kLowest = 0.63f;

// The frame picture (see kFramePicture), in its pixels.
const float kTexW = 1024.0f, kTexH = 64.0f;
const float kFrameTop = 1.0f, kFrameBottom = 43.0f;  // 42 rows
const float kLeftCap0 = 2.0f, kLeftCap1 = 36.0f, kRightCap0 = 660.0f, kRightCap1 = 694.0f;
const float kMiddle0 = 100.0f, kMiddle1 = 600.0f;

// --- colours ----------------------------------------------------------------------------
ImU32 Rgba(int r, int g, int b, float a, float alpha)
{
	return IM_COL32(r, g, b, static_cast<int>(a * alpha * 255.0f + 0.5f));
}

// The game's player colours (UI/RefTables/PlayerColourSchemes.xdb, brightened):
// 1 red, 2 blue, 3 green, 4 yellow, 5 orange, 6 teal, 7 purple, 8 brown.
ImU32 PlayerColour(int colour, float alpha)
{
	static const int kRgb[9][3] = {
		{140, 140, 140},                                  // none
		{224, 82, 75},  {74, 134, 221}, {98, 168, 58},    // red, blue, green
		{229, 170, 42}, {224, 112, 61}, {51, 178, 178},   // yellow, orange, teal
		{178, 89, 191}, {153, 102, 56},                   // purple, brown
	};
	const int *c = kRgb[colour >= 1 && colour <= 8 ? colour : 0];
	return Rgba(c[0], c[1], c[2], 1.0f, alpha);
}

// --- pieces -------------------------------------------------------------------------------
ImVec2 TextSize(ImFont *font, float size, const char *text)
{
	return font->CalcTextSizeA(size, 10000.0f, 0.0f, text);
}

// White (or any) text with a dark shadow one pixel down and right, as the game's.
void Text(ImDrawList *draw, ImFont *font, float size, ImVec2 p, ImU32 colour, const char *text, float alpha)
{
	p.x = floorf(p.x + 0.5f);
	p.y = floorf(p.y + 0.5f);
	draw->AddText(font, size, ImVec2(p.x + 1, p.y + 1), Rgba(0, 0, 0, 0.85f, alpha), text);
	draw->AddText(font, size, p, colour, text);
}

// Text cut to fit `width`, with an ellipsis.
void FitText(ImFont *font, float size, const char *text, float width, char *out, int out_size)
{
	lstrcpynA(out, text, out_size);
	if (TextSize(font, size, out).x <= width)
		return;
	int n = static_cast<int>(strlen(out));
	while (n > 0) {
		do
			--n;  // one UTF-8 character back
		while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xc0) == 0x80);
		_snprintf_s(out + n, out_size - n, _TRUNCATE, "\xe2\x80\xa6");  // "…"
		if (TextSize(font, size, out).x <= width)
			return;
	}
}

// The width of the frame's end caps for a panel this high.
float CapWidth(const Look &look, float h)
{
	return look.frame ? (kLeftCap1 - kLeftCap0) * h / (kFrameBottom - kFrameTop) : h / 2;
}

// A panel: the game's resource bar frame cut in three, or a flat dark capsule.
void Frame(ImDrawList *draw, const Look &look, ImVec2 p0, ImVec2 p1)
{
	const float h = p1.y - p0.y, a = look.alpha;
	if (!look.frame) {
		draw->AddRectFilled(p0, p1, Rgba(16, 20, 25, 0.85f, a), h / 2);
		return;
	}
	const ImTextureID tex = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(look.frame));
	float cap = CapWidth(look, h);
	if (2 * cap > p1.x - p0.x)
		cap = (p1.x - p0.x) / 2;
	const float v0 = kFrameTop / kTexH, v1 = kFrameBottom / kTexH;
	const ImU32 white = Rgba(255, 255, 255, 1.0f, a);
	draw->AddImage(tex, p0, ImVec2(p0.x + cap, p1.y), ImVec2(kLeftCap0 / kTexW, v0), ImVec2(kLeftCap1 / kTexW, v1),
	               white);
	draw->AddImage(tex, ImVec2(p0.x + cap, p0.y), ImVec2(p1.x - cap, p1.y), ImVec2(kMiddle0 / kTexW, v0),
	               ImVec2(kMiddle1 / kTexW, v1), white);
	draw->AddImage(tex, ImVec2(p1.x - cap, p0.y), p1, ImVec2(kRightCap0 / kTexW, v0), ImVec2(kRightCap1 / kTexW, v1),
	               white);
}

// The game's thin bronze rim around round things: light on top, dark below.
void BronzeRim(ImDrawList *draw, ImVec2 c, float r, float thickness, float alpha)
{
	const float pi = 3.14159265f;
	draw->PathArcTo(c, r, pi, 2 * pi, 24);
	draw->PathStroke(Rgba(214, 178, 120, 1.0f, alpha), ImDrawFlags_None, thickness);
	draw->PathArcTo(c, r, 0, pi, 24);
	draw->PathStroke(Rgba(122, 94, 58, 1.0f, alpha), ImDrawFlags_None, thickness);
}

// A team's number, like the game's round buttons: dark disc, bronze rim, deep
// blue inside, white number.
void TeamButton(ImDrawList *draw, ImFont *bold, ImVec2 c, float r, int number, float k, float alpha)
{
	draw->AddCircleFilled(c, r, Rgba(6, 8, 10, 0.95f, alpha));
	draw->AddCircleFilled(c, r - 2.2f * k, Rgba(24, 66, 92, 1.0f, alpha));
	draw->AddCircleFilled(ImVec2(c.x, c.y - 0.25f * r), (r - 2.2f * k) * 0.62f, Rgba(70, 130, 170, 0.25f, alpha));
	BronzeRim(draw, c, r - 1.3f * k, 1.3f * k, alpha);
	char text[8];
	_snprintf_s(text, sizeof(text), _TRUNCATE, "%d", number);
	const float size = kNumberFont * k;
	const ImVec2 t = TextSize(bold, size, text);
	Text(draw, bold, size, ImVec2(c.x - t.x / 2, c.y - t.y / 2), Rgba(255, 255, 255, 1.0f, alpha), text, alpha);
}

// The status mark at c; k = pixels per sketch pixel.
void StatusMark(ImDrawList *draw, Status status, ImVec2 c, float k, float alpha)
{
	switch (status) {
	case kPlaying:  // a green dot with a soft halo
		draw->AddCircleFilled(c, 7.0f * k, Rgba(79, 208, 143, 0.22f, alpha));
		draw->AddCircleFilled(c, 4.0f * k, Rgba(79, 208, 143, 1.0f, alpha));
		break;
	case kEnded: {  // a light grey tick
		const ImVec2 tick[] = {ImVec2(c.x - 4.0f * k, c.y + 0.2f * k), ImVec2(c.x - 1.3f * k, c.y + 3.0f * k),
		                       ImVec2(c.x + 4.2f * k, c.y - 3.2f * k)};
		draw->AddPolyline(tick, 3, Rgba(201, 208, 214, 1.0f, alpha), ImDrawFlags_None, 1.7f * k);
		break;
	}
	case kThinking: {  // three purple dots that blink in turn
		const float t = static_cast<float>(ImGui::GetTime());
		for (int i = 0; i < 3; ++i) {
			float phase = fmodf(t / 1.2f - i * (0.2f / 1.2f) + 1.0f, 1.0f);
			const float a = phase < 0.4f ? 0.25f + 0.75f * phase / 0.4f
			                : phase < 0.8f ? 1.0f - 0.75f * (phase - 0.4f) / 0.4f : 0.25f;
			draw->AddCircleFilled(ImVec2(c.x + (i - 1) * 4.5f * k, c.y), 1.6f * k, Rgba(169, 156, 255, a, alpha));
		}
		break;
	}
	default:  // waiting: a hollow grey ring
		draw->AddCircle(c, 3.5f * k, Rgba(109, 119, 129, 1.0f, alpha), 0, 1.5f * k);
		break;
	}
}

void NameOf(const Row &r, const Look &look, char *out, int size)
{
	const Words &words = kWords[look.language];
	if (r.name[0])
		lstrcpynA(out, r.name, size);
	else
		_snprintf_s(out, size, _TRUNCATE, r.human ? words.player : words.computer, r.human ? r.id : r.computer);
}

// The pill's end cap (the frame picture's, or the capsule's round end).
float PillCap(const Look &look, float h)
{
	return look.frame ? CapWidth(look, h) : h / 2;
}

// The portrait's centre, from the pill's left edge: the middle of the left cap.
float FaceCentre(const Look &look, float h)
{
	return PillCap(look, h) / 2;
}

// How far the portrait reaches left of the pill.
float FaceOverhang(const Look &look, float h, float k)
{
	const float over = kFace * k / 2 - FaceCentre(look, h);
	return over > 0 ? over : 0;
}

// The room a pill needs besides the name: the portrait, the gaps, the mark, the cap.
float PillExtras(const Look &look, float h, float k)
{
	return FaceCentre(look, h) + kFace * k / 2 + kFaceToName * k + kNameToMark * k + kMark * k + kMarkToCap * k +
	       PillCap(look, h);
}

void Pill(ImDrawList *draw, const Row &r, const Look &look, ImVec2 p, float w, float k)
{
	ImFont *font = static_cast<ImFont *>(look.font);
	const float h = kPillHeight * k, a = look.alpha;
	Frame(draw, look, p, ImVec2(p.x + w, p.y + h));
	// the hero's portrait, bigger than the pill (like the game's round buttons
	// on its bars), centred on the left cap, in a ring of the player's colour
	// and a thin bronze rim
	const float radius = kFace * k / 2 - 1.5f * k;
	const ImVec2 c(p.x + FaceCentre(look, h), p.y + h / 2);
	draw->AddCircleFilled(c, radius + 1.5f * k, Rgba(6, 8, 10, 1.0f, a));
	if (r.face_texture)
		draw->AddImageRounded(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(r.face_texture)),
		                      ImVec2(c.x - radius, c.y - radius), ImVec2(c.x + radius, c.y + radius), ImVec2(0, 0),
		                      ImVec2(1, 1), Rgba(255, 255, 255, 1.0f, a), radius);
	else
		draw->AddCircleFilled(c, radius, Rgba(24, 66, 92, 1.0f, a));
	draw->AddCircle(c, radius - 0.5f * k, PlayerColour(r.colour, a), 0, 1.6f * k);
	BronzeRim(draw, c, radius + 0.9f * k, 1.0f * k, a);
	if (!r.face_texture) {
		const char *mark = r.human ? "?" : "AI";
		const float size = kNumberFont * k;
		const ImVec2 t = TextSize(font, size, mark);
		Text(draw, font, size, ImVec2(c.x - t.x / 2, c.y - t.y / 2), Rgba(255, 255, 255, 1.0f, a), mark, a);
	}
	// the name, a little muted once the turn is ended
	char name[96], fitted[96];
	NameOf(r, look, name, sizeof(name));
	const float size = kNameFont * k;
	const float x = c.x + kFace * k / 2 + kFaceToName * k;
	const float mark_x = p.x + w - PillCap(look, h) - kMarkToCap * k - kMark * k / 2;
	const float right = mark_x - kMark * k / 2 - kNameToMark * k;
	FitText(font, size, name, right - x, fitted, sizeof(fitted));
	const ImVec2 t = TextSize(font, size, fitted);
	const ImU32 ink = r.status == kEnded ? Rgba(196, 200, 204, 1.0f, a) : Rgba(255, 255, 255, 1.0f, a);
	Text(draw, font, size, ImVec2(x, p.y + (h - t.y) / 2), ink, fitted, a);
	// the status mark, inside the right cap
	StatusMark(draw, r.status, ImVec2(mark_x, p.y + h / 2), k, a);
}

}  // namespace

void FontSizes(float screen_height, float *regular, float *bold)
{
	float k = screen_height / kRefHeight;
	if (k * kPillHeight < kMinPill)
		k = kMinPill / kPillHeight;
	*regular = floorf(kNameFont * k + 0.5f);
	*bold = floorf(kHeaderFont * k + 0.5f);
}

void DrawPanel(const Snapshot &s, const Look &look, float rect[4])
{
	ImDrawList *draw = ImGui::GetForegroundDrawList();
	ImFont *font = static_cast<ImFont *>(look.font), *bold = static_cast<ImFont *>(look.font_bold);
	const ImVec2 screen = ImGui::GetIO().DisplaySize;
	const float a = look.alpha;

	// The rows by team (teams in the order of their first player).
	int order[kMaxPlayers], count = 0, team_count = 0;
	int team_of[kMaxPlayers];  // display number of each team, by order index
	for (int i = 0; i < s.count; ++i) {
		bool placed = false;
		for (int j = 0; j < count && !placed; ++j)
			placed = s.rows[order[j]].team == s.rows[i].team;
		if (placed)
			continue;
		++team_count;
		for (int j = i; j < s.count; ++j) {
			if (s.rows[j].team == s.rows[i].team) {
				team_of[count] = team_count;
				order[count++] = j;
			}
		}
	}
	const bool teams = s.teams;

	// Height at k = 1, then the scale and the start that fit the screen.
	float units = kHeaderHeight + kAfterHeader;
	for (int i = 0; i < count; ++i) {
		const bool first = i == 0 || team_of[i] != team_of[i - 1];
		const bool last = i + 1 == count || team_of[i + 1] != team_of[i];
		if (teams && first)
			units += kCircle + kCircleToPill;
		units += kPillHeight;
		if (i + 1 < count)
			units += teams && last ? kTeamGap : kPillGap;
	}
	units += (kFace - kPillHeight) / 2;  // the last portrait reaches below its pill
	float k = screen.y / kRefHeight;
	if (k * kPillHeight < kMinPill)
		k = kMinPill / kPillHeight;
	float top = look.top * screen.y;
	const float lowest = kLowest * screen.y, highest = kHighest * screen.y;
	if (top + units * k > lowest)
		top = lowest - units * k > highest ? lowest - units * k : highest;
	if (top + units * k > lowest && kMinPill / kPillHeight < k) {
		k = (lowest - top) / units;  // many players: smaller
		if (k * kPillHeight < kMinPill)
			k = kMinPill / kPillHeight;
	}
	// The game's interface scales with the screen height (it is laid out for 768
	// lines; the minimap keeps its distance to the left edge in those units), so
	// the inset does too: on a wide screen the list stays lined up with the minimap.
	const float left = floorf(look.left * screen.y + 0.5f);
	float right = left;

	// The header: "Jour 34 - 3/4 Prêts"; N/M = humans done / humans.
	const Words &words = kWords[look.language];
	int humans = 0, humans_done = 0;
	for (int i = 0; i < s.count; ++i) {
		humans += s.rows[i].human;
		humans_done += s.rows[i].human && s.rows[i].status == kEnded;
	}
	char day[32], counts[16];
	_snprintf_s(day, sizeof(day), _TRUNCATE, "%s %d", words.day, s.day);
	_snprintf_s(counts, sizeof(counts), _TRUNCATE, " - %d/%d ", humans_done, humans);
	const float hsize = kHeaderFont * k, nsize = kNameFont * k;
	const float w_day = TextSize(bold, hsize, day).x;
	const float w_counts = TextSize(font, nsize, counts).x, w_ready = TextSize(font, nsize, words.ready).x;
	const float hh = kHeaderHeight * k;
	const float pad = (look.frame ? CapWidth(look, hh) : hh / 2) + kHeaderPad * k;
	const float hw = 2 * pad + w_day + w_counts + w_ready;
	Frame(draw, look, ImVec2(left, top), ImVec2(left + hw, top + hh));
	float x = left + pad;
	const float base = top + hh / 2;
	Text(draw, bold, hsize, ImVec2(x, base - TextSize(bold, hsize, day).y / 2), Rgba(255, 255, 255, 1.0f, a), day, a);
	x += w_day;
	const float ny = base - TextSize(font, nsize, counts).y / 2;
	Text(draw, font, nsize, ImVec2(x, ny), Rgba(255, 255, 255, 1.0f, a), counts, a);
	x += w_counts;
	Text(draw, font, nsize, ImVec2(x, ny), Rgba(196, 184, 158, 1.0f, a), words.ready, a);
	right = left + hw;

	// One width for all pills: the longest name, within limits.
	const float ph = kPillHeight * k;
	float pw = kPillMinWidth * k;
	for (int i = 0; i < count; ++i) {
		char name[96];
		NameOf(s.rows[order[i]], look, name, sizeof(name));
		const float need = TextSize(font, kNameFont * k, name).x + PillExtras(look, ph, k);
		if (need > pw)
			pw = need;
	}
	if (pw > kPillMaxWidth * k)
		pw = kPillMaxWidth * k;
	pw = ceilf(pw) + 1.0f;  // (rounding down would cut the longest name)

	// The teams and their pills. The portraits reach left of the pills: in a
	// team they stay clear of the team's line, else they start at the inset.
	float y = top + hh + kAfterHeader * k;
	const float circle_x = left + kCircle / 2 * k;
	const float overhang = FaceOverhang(look, ph, k);
	const float pill_x = teams ? floorf(circle_x + kLineToFace * k + overhang) : left + overhang;
	float line_top = 0;
	for (int i = 0; i < count; ++i) {
		const Row &r = s.rows[order[i]];
		const bool first = i == 0 || team_of[i] != team_of[i - 1];
		const bool last = i + 1 == count || team_of[i + 1] != team_of[i];
		if (teams && first) {
			TeamButton(draw, bold, ImVec2(circle_x, y + kCircle / 2 * k), kCircle / 2 * k, team_of[i], k, a);
			line_top = y + (kCircle + 1.0f) * k;
			y += (kCircle + kCircleToPill) * k;
		}
		const float px = pill_x;
		Pill(draw, r, look, ImVec2(px, y), pw, k);
		if (px + pw > right)
			right = px + pw;
		y += ph;
		if (teams && last) {  // the team's line, a thin pewter one, down to its last pill
			const float lx = floorf(circle_x) + 0.5f;
			draw->AddLine(ImVec2(lx + 1, line_top), ImVec2(lx + 1, y - ph / 2), Rgba(0, 0, 0, 0.5f, a), 1.0f);
			draw->AddLine(ImVec2(lx, line_top), ImVec2(lx, y - ph / 2), Rgba(160, 150, 136, 0.9f, a), 1.2f * k);
		}
		rect[3] = y + (kFace - kPillHeight) / 2 * k;
		if (i + 1 < count)
			y += (teams && last ? kTeamGap : kPillGap) * k;
	}
	rect[0] = left;
	rect[1] = top;
	rect[2] = right;
	if (count == 0)
		rect[3] = top + hh;
}

}  // namespace plist
}  // namespace mods
