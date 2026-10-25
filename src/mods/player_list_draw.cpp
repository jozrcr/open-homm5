// The player list overlay: the panel itself. Everything about its look, and
// all its words, are in this file (DrawPanel); the rest of the overlay only
// hands it a Snapshot of the game.
//
// The owner's design (2026-10-25), over the adventure map's left edge, between
// the resource bar and the minimap:
//   [ Jour 34 - 3/4 Prêts ]              the day, and how many humans are done
//   (1)                                  a team: a numbered circle, a thin line
//    |  [(face) Joz          ● ]          down along its players' pills
//    |  [(face) Paul         ✓ ]
//   (2)
//    |  [(face) Computer 1  ••• ]
// Each pill: the hero's round portrait (ring in the player's colour), the
// player's name, a status mark: green dot = playing, grey tick = turn ended,
// three blinking purple dots = the AI is moving, hollow grey ring = waiting.
// No team circles or lines in a free-for-all. Sizes follow the screen height
// (the sketch: 820 lines, pills 26 px high and 125 px wide).
#include "player_list_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../hook/third_party/imgui/imgui.h"

namespace mods {
namespace plist {
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
const float kHeaderHeight = 32.0f, kHeaderPad = 12.0f;
const float kAfterHeader = 14.0f;
const float kCircle = 16.0f;  // a team's numbered circle
const float kCircleToPill = 17.0f;
const float kPillHeight = 26.0f, kPillWidth = 125.0f, kPillGap = 9.0f;
const float kTeamGap = 20.0f;   // after a team's last pill
const float kTeamIndent = 24.0f;  // pills right of the team line
const float kNameFont = 13.0f, kHeaderFont = 14.0f, kNumberFont = 10.0f;
const float kMinPill = 20.0f;   // pills never get smaller than this
// The panel stays between these (fractions of the screen height): below the
// resource bar, above the multiplayer chat box (0.64) and the minimap (0.73).
const float kHighest = 0.08f, kLowest = 0.63f;

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

void Pill(ImDrawList *draw, const Row &r, const Look &look, ImVec2 p, float k)
{
	ImFont *font = static_cast<ImFont *>(look.font);
	const float h = kPillHeight * k, w = kPillWidth * k, a = look.alpha;
	draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Rgba(16, 20, 25, 0.85f, a), h / 2);
	// the hero's portrait, nearly the pill's height, in a ring of the player's colour
	const float radius = h / 2 - 1.5f * k;
	const ImVec2 c(p.x + h / 2, p.y + h / 2);
	if (r.face_texture)
		draw->AddImageRounded(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(r.face_texture)),
		                      ImVec2(c.x - radius, c.y - radius), ImVec2(c.x + radius, c.y + radius), ImVec2(0, 0),
		                      ImVec2(1, 1), Rgba(255, 255, 255, 1.0f, a), radius);
	else
		draw->AddCircleFilled(c, radius, Rgba(255, 255, 255, 0.10f, a));
	draw->AddCircle(c, radius, PlayerColour(r.colour, a), 0, 1.5f * k);
	if (!r.face_texture) {
		const char *mark = r.human ? "?" : "AI";
		const float size = kNumberFont * k;
		const ImVec2 t = TextSize(font, size, mark);
		draw->AddText(font, size, ImVec2(c.x - t.x / 2, c.y - t.y / 2), Rgba(238, 241, 244, 1.0f, a), mark);
	}
	// the name, muted once the turn is ended
	const Words &words = kWords[look.language];
	char name[96];
	if (r.name[0])
		lstrcpynA(name, r.name, sizeof(name));
	else
		_snprintf_s(name, sizeof(name), _TRUNCATE, r.human ? words.player : words.computer,
		            r.human ? r.id : r.computer);
	const float size = kNameFont * k;
	const float x = p.x + h + 5.0f * k, right = p.x + w - h + 2.0f * k;
	char fitted[96];
	FitText(font, size, name, right - x, fitted, sizeof(fitted));
	const ImVec2 t = TextSize(font, size, fitted);
	const ImU32 ink = r.status == kEnded ? Rgba(200, 207, 214, 1.0f, a) : Rgba(255, 255, 255, 1.0f, a);
	draw->AddText(font, size, ImVec2(x, p.y + (h - t.y) / 2), ink, fitted);
	StatusMark(draw, r.status, ImVec2(p.x + w - h / 2, p.y + h / 2), k, a);
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
	const float left = floorf(look.left * screen.x + 0.5f);
	float right = left;

	// The header pill: "Jour 34 - 3/4 Prêts"; N/M = humans done / humans.
	const Words &words = kWords[look.language];
	int humans = 0, humans_done = 0;
	for (int i = 0; i < s.count; ++i) {
		humans += s.rows[i].human;
		humans_done += s.rows[i].human && s.rows[i].status == kEnded;
	}
	char day[32], dash[8], counts[16], ready[32];
	_snprintf_s(day, sizeof(day), _TRUNCATE, "%s %d", words.day, s.day);
	lstrcpynA(dash, " - ", sizeof(dash));
	_snprintf_s(counts, sizeof(counts), _TRUNCATE, "%d/%d ", humans_done, humans);
	lstrcpynA(ready, words.ready, sizeof(ready));
	const float hsize = kHeaderFont * k, nsize = kNameFont * k;
	const float w_day = TextSize(bold, hsize, day).x, w_dash = TextSize(font, nsize, dash).x;
	const float w_counts = TextSize(font, nsize, counts).x, w_ready = TextSize(font, nsize, ready).x;
	const float hh = kHeaderHeight * k;
	const float hw = 2 * kHeaderPad * k + w_day + w_dash + w_counts + w_ready;
	draw->AddRectFilled(ImVec2(left, top), ImVec2(left + hw, top + hh), Rgba(16, 20, 25, 0.85f, a), hh / 2);
	float x = left + kHeaderPad * k;
	const float base = top + hh / 2;
	draw->AddText(bold, hsize, ImVec2(x, base - TextSize(bold, hsize, day).y / 2), Rgba(255, 255, 255, 1.0f, a), day);
	x += w_day;
	const float ny = base - TextSize(font, nsize, dash).y / 2;
	draw->AddText(font, nsize, ImVec2(x, ny), Rgba(255, 255, 255, 1.0f, a), dash);
	x += w_dash;
	draw->AddText(font, nsize, ImVec2(x, ny), Rgba(238, 241, 244, 1.0f, a), counts);
	x += w_counts;
	draw->AddText(font, nsize, ImVec2(x, ny), Rgba(167, 177, 187, 1.0f, a), ready);
	right = left + hw;

	// The teams and their pills.
	float y = top + hh + kAfterHeader * k;
	const float circle_x = left + kCircle / 2 * k;
	float line_top = 0;
	for (int i = 0; i < count; ++i) {
		const Row &r = s.rows[order[i]];
		const bool first = i == 0 || team_of[i] != team_of[i - 1];
		const bool last = i + 1 == count || team_of[i + 1] != team_of[i];
		if (teams && first) {
			const ImVec2 c(circle_x, y + kCircle / 2 * k);
			draw->AddCircleFilled(c, kCircle / 2 * k, Rgba(16, 20, 25, 0.85f, a));
			draw->AddCircle(c, kCircle / 2 * k, Rgba(255, 255, 255, 0.55f, a), 0, 1.0f * k);
			char number[8];
			_snprintf_s(number, sizeof(number), _TRUNCATE, "%d", team_of[i]);
			const float size = kNumberFont * k;
			const ImVec2 t = TextSize(bold, size, number);
			draw->AddText(bold, size, ImVec2(c.x - t.x / 2, c.y - t.y / 2), Rgba(238, 241, 244, 1.0f, a), number);
			line_top = y + (kCircle + 2.0f) * k;
			y += (kCircle + kCircleToPill) * k;
		}
		const float px = teams ? left + kTeamIndent * k : left;
		Pill(draw, r, look, ImVec2(px, y), k);
		if (px + kPillWidth * k > right)
			right = px + kPillWidth * k;
		y += kPillHeight * k;
		if (teams && last)  // the team's line, down to its last pill
			draw->AddLine(ImVec2(circle_x, line_top), ImVec2(circle_x, y), Rgba(255, 255, 255, 0.35f, a), 1.0f * k);
		rect[3] = y;
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
