// Test of following a friend's view (src/mods/spectate_logic.cpp): the messages
// between PCs, the glide, who follows whom. No game needed.
#include <math.h>
#include <stdio.h>

#include "../../mods/spectate_logic.h"

using namespace mods::spectate;

namespace {

int g_failures = 0;

void Check(bool ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		++g_failures;
}

bool Near(float a, float b)
{
	return fabsf(a - b) < 0.01f;
}

}  // namespace

int main()
{
	// --- the messages ---
	Message m = Decode(EncodeWatch(3, 1, true));
	Check(m.valid && m.kind == kWatch && m.player == 3 && m.target == 1 && m.on, "watch on: follower, followed");
	m = Decode(EncodeWatch(8, 2, false));
	Check(m.valid && m.kind == kWatch && m.player == 8 && m.target == 2 && !m.on, "watch off, player 8");
	m = Decode(EncodeWatch(2, 2, true));
	Check(!m.valid, "a player cannot follow themselves");
	m = Decode(EncodeView(5, 1, 511, 0));
	Check(m.valid && m.kind == kView && m.player == 5 && m.floor == 1 && m.x == 511 && m.y == 0, "view: floor, edges");
	m = Decode(EncodeView(1, 0, 137, 260));
	Check(m.valid && m.player == 1 && m.floor == 0 && m.x == 137 && m.y == 260, "view: a tile");
	m = Decode(EncodeStatus(4, kInBattle));
	Check(m.valid && m.kind == kStatus && m.player == 4 && m.screen == kInBattle, "status: in a battle");
	Check(!Decode(EncodeStatus(4, 9)).valid, "status: an unknown screen is refused");
	Check(!Decode(0x55000001u).valid, "another mark ('U', Undo End Turn) is not ours");

	// --- the army's placement before a battle (kind 3, since 0.3.5) ---
	m = Decode(EncodePlacementStack(2, 6, 4, true, 31, 17));
	Check(m.valid && m.kind == kPlacement && m.player == 2 && !m.end && m.index == 6 && m.slot == 4 && m.placed &&
	          m.hx == 31 && m.hy == 17,
	      "placement: a stack on the field");
	m = Decode(EncodePlacementStack(8, 0, 0, false, 12, 3));
	Check(m.valid && m.player == 8 && !m.placed && m.hx == 0 && m.hy == 0, "placement: a stack in the bar (no hex)");
	Check(PlacementHexFits(0, 31) && !PlacementHexFits(32, 0) && !PlacementHexFits(0, -1), "placement: hexes 0..31");
	m = Decode(EncodePlacementEnd(3, 7, 0x7fff));
	Check(m.valid && m.kind == kPlacement && m.player == 3 && m.end && m.count == 7 && m.check == 0x7fff,
	      "placement: the list's end");
	Check(!Decode(EncodePlacementEnd(3, 0, 5)).valid, "placement: an empty list is refused");
	Check(!Decode(EncodePlacementStack(1, 7, 0, true, 1, 1)).valid, "placement: index 7 is refused");
	Check(!Decode(EncodePlacementStack(1, 0, 0, true, 1, 1) | 1u).valid, "placement: bit 0 must be 0");
	{
		const int types[3] = {0x47, 12, 12}, counts[3] = {7, 20, 21}, swapped[3] = {12, 0x47, 12};
		const int a = PlacementCheck(types, counts, 3);
		Check(a >= 0 && a <= 0x7fff && a == PlacementCheck(types, counts, 3), "placement check: 15 bits, the same twice");
		Check(a != PlacementCheck(types, counts, 2) && a != PlacementCheck(swapped, counts, 3),
		      "placement check: another list gives another check");
	}
	Check(EncodeWatch(1, 2, true) >> 24 == kMark && EncodeView(1, 0, 3, 4) >> 24 == kMark &&
	          EncodeStatus(1, kOnMap) >> 24 == kMark && EncodePlacementStack(1, 0, 0, true, 1, 1) >> 24 == kMark &&
	          EncodePlacementEnd(1, 1, 0) >> 24 == kMark,
	      "every message carries the mark 'W'");

	// --- tiles ---
	Check(ToTile(0.0f) == 0 && ToTile(1.9f) == 0 && ToTile(2.0f) == 1 && ToTile(43.0f) == 21, "world units to tiles");
	Check(ToTile(-5.0f) == 0 && ToTile(5000.0f) == 511, "tiles are clamped to 0..511");
	Check(Near(FromTile(21), 43.0f), "a tile's centre");

	// --- the glide ---
	float x = 0, y = 0;
	Glide(&x, &y, 10.0f, 0.0f, 0.0f);
	Check(x == 0.0f && y == 0.0f, "no time, no move");
	Glide(&x, &y, 10.0f, 0.0f, 1.0f / 60.0f);
	Check(x > 0.5f && x < 2.0f && y == 0.0f, "one frame at 60 fps: about a tenth of the way");
	for (int i = 0; i < 60; ++i)
		Glide(&x, &y, 10.0f, 0.0f, 1.0f / 60.0f);
	Check(fabsf(x - 10.0f) < 0.05f, "one second later: there (within a fortieth of a tile)");
	x = 0;
	y = 0;
	Glide(&x, &y, 100.0f, 50.0f, 1.0f / 60.0f);
	Check(x == 100.0f && y == 50.0f, "far away (more than 20 tiles): a jump");
	float a = 0, b = 0, c = 0, d = 0;
	for (int i = 0; i < 30; ++i)
		Glide(&a, &b, 20.0f, 0.0f, 1.0f / 30.0f);
	for (int i = 0; i < 120; ++i)
		Glide(&c, &d, 20.0f, 0.0f, 1.0f / 120.0f);
	Check(fabsf(a - c) < 0.05f, "the same pace at 30 and 120 frames a second");

	// --- the player moved the camera ---
	Check(!TookOver(10.0f, 10.0f, 10.5f, 9.5f), "the game's own small changes are not a take-over");
	Check(TookOver(10.0f, 10.0f, 15.0f, 10.0f), "5 units away: the player moved the camera");

	// --- who follows this PC ---
	Watchers w;
	w.Clear();
	Check(!w.Any(1000), "nobody follows at first");
	w.On(3, 1000);
	Check(w.Active(3, 1000) && w.Active(3, 1000 + kWatchLapse - 1) && w.Any(5000), "a follower");
	Check(!w.Active(3, 1000 + kWatchLapse), "the eye lapses without a new message");
	w.On(3, 1000 + kWatchRepeat);
	Check(w.Active(3, 1000 + kWatchLapse + 1), "the repeated message keeps it");
	w.Off(3);
	Check(!w.Active(3, 2000) && !w.Any(2000), "off");
	w.On(0, 1000);
	w.On(9, 1000);
	Check(!w.Any(1000), "bad player numbers are ignored");
	w.On(5, 0);
	Check(w.Active(5, 100), "the clock's 0 still counts as following");

	// --- who may follow whom ---
	Check(MayFollow(1, 0, 2, 0), "two players of a team");
	Check(!MayFollow(1, 0, 2, 1), "not across teams");
	Check(!MayFollow(2, 0, 2, 0), "not oneself");
	Check(!MayFollow(0, 0, 2, 0) && !MayFollow(1, 0, 9, 0), "only players 1..8");

	printf(g_failures ? "%d FAILED\n" : "all ok\n", g_failures);
	return g_failures ? 1 : 0;
}
