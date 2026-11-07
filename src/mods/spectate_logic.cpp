// Following a friend's view: the parts without the game. See spectate_logic.h.
#include "spectate_logic.h"

#include <math.h>
#include <string.h>

namespace mods {
namespace spectate {
namespace {

// The value: the mark in bits 24-31, the kind in bits 22-23, the player - 1 in bits
// 19-21, then the kind's own bits:
//   watch:  the followed player - 1 in bits 16-18, on in bit 15
//   view:   the floor in bit 18, x in bits 9-17, y in bits 0-8 (tiles)
//   status: the screen in bits 0-3
//   placement: bit 18 = 0, a stack: its index in bits 15-17, slot in 12-14, placed in bit 11,
//              hex x in bits 6-10, y in bits 1-5; bit 18 = 1, the list's end: the number of
//              stacks in bits 15-17, the check in bits 0-14 (0.3.4 and older refuse kind 3)
uint32_t Head(int kind, int player)
{
	return kMark << 24 | (static_cast<uint32_t>(kind) & 3) << 22 | (static_cast<uint32_t>(player - 1) & 7) << 19;
}

bool GoodPlayer(int player)
{
	return player >= 1 && player <= 8;
}

}  // namespace

uint32_t EncodeWatch(int follower, int followed, bool on)
{
	return Head(kWatch, follower) | (static_cast<uint32_t>(followed - 1) & 7) << 16 | (on ? 1u : 0u) << 15;
}

uint32_t EncodeView(int player, int floor, int x_tile, int y_tile)
{
	return Head(kView, player) | (floor ? 1u : 0u) << 18 | (static_cast<uint32_t>(x_tile) & 0x1ff) << 9 |
	       (static_cast<uint32_t>(y_tile) & 0x1ff);
}

uint32_t EncodeStatus(int player, int screen)
{
	return Head(kStatus, player) | (static_cast<uint32_t>(screen) & 0xf);
}

bool PlacementHexFits(int x, int y)
{
	return x >= 0 && x <= 31 && y >= 0 && y <= 31;
}

uint32_t EncodePlacementStack(int player, int index, int slot, bool placed, int x, int y)
{
	if (!placed || !PlacementHexFits(x, y))
		x = y = 0;
	return Head(kPlacement, player) | (static_cast<uint32_t>(index) & 7) << 15 | (static_cast<uint32_t>(slot) & 7) << 12 |
	       (placed ? 1u : 0u) << 11 | (static_cast<uint32_t>(x) & 31) << 6 | (static_cast<uint32_t>(y) & 31) << 1;
}

uint32_t EncodePlacementEnd(int player, int count, int check)
{
	return Head(kPlacement, player) | 1u << 18 | (static_cast<uint32_t>(count) & 7) << 15 |
	       (static_cast<uint32_t>(check) & 0x7fff);
}

int PlacementCheck(const int *types, const int *counts, int n)
{
	uint32_t h = 5381;
	for (int i = 0; i < n; ++i) {
		h = (h * 33) ^ static_cast<uint32_t>(types[i]);
		h = (h * 33) ^ static_cast<uint32_t>(counts[i]);
	}
	return static_cast<int>((h ^ (h >> 15)) & 0x7fff);
}

Message Decode(uint32_t value)
{
	Message m;
	memset(&m, 0, sizeof(m));
	if (value >> 24 != kMark)
		return m;
	m.kind = static_cast<int>((value >> 22) & 3);
	m.player = static_cast<int>((value >> 19) & 7) + 1;
	switch (m.kind) {
	case kWatch:
		m.target = static_cast<int>((value >> 16) & 7) + 1;
		m.on = ((value >> 15) & 1) != 0;
		m.valid = m.target != m.player && (value & 0x7fff) == 0;
		break;
	case kView:
		m.floor = static_cast<int>((value >> 18) & 1);
		m.x = static_cast<int>((value >> 9) & 0x1ff);
		m.y = static_cast<int>(value & 0x1ff);
		m.valid = true;
		break;
	case kStatus:
		m.screen = static_cast<int>(value & 0xf);
		m.valid = m.screen <= kElsewhere && (value & 0x7fff0) == 0;
		break;
	case kPlacement:
		m.end = ((value >> 18) & 1) != 0;
		if (m.end) {
			m.count = static_cast<int>((value >> 15) & 7);
			m.check = static_cast<int>(value & 0x7fff);
			m.valid = m.count >= 1;
		}
		else {
			m.index = static_cast<int>((value >> 15) & 7);
			m.slot = static_cast<int>((value >> 12) & 7);
			m.placed = ((value >> 11) & 1) != 0;
			m.hx = static_cast<int>((value >> 6) & 31);
			m.hy = static_cast<int>((value >> 1) & 31);
			m.valid = m.index <= 6 && m.slot <= 6 && (value & 1) == 0;
		}
		break;
	default:
		break;
	}
	if (!GoodPlayer(m.player))
		m.valid = false;
	return m;
}

int ToTile(float world)
{
	const float t = floorf(world / 2.0f);
	return t < 0.0f ? 0 : t > 511.0f ? 511 : static_cast<int>(t);
}

float FromTile(int tile)
{
	return static_cast<float>(tile) * 2.0f + 1.0f;
}

void Glide(float *x, float *y, float tx, float ty, float dt)
{
	const float dx = tx - *x, dy = ty - *y;
	if (dx * dx + dy * dy > kJump * kJump) {
		*x = tx;
		*y = ty;
		return;
	}
	if (dt <= 0.0f)
		return;
	const float f = 1.0f - expf(-dt / 0.15f);
	*x += dx * f;
	*y += dy * f;
}

bool TookOver(float x, float y, float set_x, float set_y)
{
	const float dx = x - set_x, dy = y - set_y;
	return dx * dx + dy * dy > kTakeOver * kTakeOver;
}

void Watchers::Clear()
{
	memset(since, 0, sizeof(since));
}

void Watchers::On(int player, uint32_t now)
{
	if (GoodPlayer(player))
		since[player] = now ? now : 1;
}

void Watchers::Off(int player)
{
	if (GoodPlayer(player))
		since[player] = 0;
}

bool Watchers::Active(int player, uint32_t now) const
{
	return GoodPlayer(player) && since[player] != 0 && now - since[player] < kWatchLapse;
}

bool Watchers::Any(uint32_t now) const
{
	for (int p = 1; p <= 8; ++p)
		if (Active(p, now))
			return true;
	return false;
}

bool MayFollow(int follower, int follower_team, int followed, int followed_team)
{
	return GoodPlayer(follower) && GoodPlayer(followed) && follower != followed && follower_team == followed_team;
}

}  // namespace spectate
}  // namespace mods
