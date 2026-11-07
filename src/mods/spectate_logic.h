// Following a friend's view: the parts without the game (messages, the glide, who
// follows whom). See spectate.cpp and docs/plans/spectate.md.
#pragma once

#include <stdint.h>

namespace mods {
namespace spectate {

// Our messages ride in world-checksum messages with check number -1 (the game ignores
// them), 32 bits each. The top byte is the mark (hero looks use 'k' and 'K', the co-op
// guard 'G', Undo End Turn 'U', the co-op features 'F').
const unsigned kMark = 0x57;  // 'W'

enum Kind { kWatch = 0, kView = 1, kStatus = 2, kPlacement = 3 };
// What the followed player has on screen.
enum Screen { kOnMap = 0, kInTown = 1, kInBattle = 2, kInHeroScreen = 3, kElsewhere = 4 };

struct Message {
	bool valid;
	int kind;
	int player;  // 1..8: the sender (for kWatch: the follower)
	int target;  // kWatch: the followed player, 1..8
	bool on;     // kWatch: starts (or goes on) following / stops
	int floor;   // kView: 0 surface, 1 underground
	int x, y;    // kView: the point looked at, in tiles (0..511)
	int screen;  // kStatus: a Screen
	// kPlacement (the sender's army placement before its battle, since 0.3.5): one stack of the
	// list, or the list's end
	bool end;
	int index;    // stack: its place in the list, 0..6
	int slot;     // stack: its slot in the bar under the battlefield, 0..6
	bool placed;  // stack: on the battlefield (else waiting in the bar)
	int hx, hy;   // stack: its hex, 0..31
	int count;    // end: how many stacks the list has, 1..7
	int check;    // end: PlacementCheck of the stacks' creature types and counts
};

uint32_t EncodeWatch(int follower, int followed, bool on);
uint32_t EncodeView(int player, int floor, int x_tile, int y_tile);
uint32_t EncodeStatus(int player, int screen);
// A hex is 0..31 here; a stack's hex outside that cannot be sent (false).
bool PlacementHexFits(int x, int y);
uint32_t EncodePlacementStack(int player, int index, int slot, bool placed, int x, int y);
uint32_t EncodePlacementEnd(int player, int count, int check);
// 15 bits from the stacks' creature types and counts, in list order: both PCs' lists must
// name the same stacks before one is copied into the other.
int PlacementCheck(const int *types, const int *counts, int n);
// valid = false for anything else (another mark, a bad player number).
Message Decode(uint32_t value);

// The camera's point, in world units (a tile is 2 units) -> tiles, clamped to 0..511.
int ToTile(float world);
// A tile -> its centre in world units.
float FromTile(int tile);

// One frame of the glide toward (tx, ty), dt seconds after the last: exponential
// (about 0.15 s), and a jump when farther than kJump units.
const float kJump = 40.0f;  // 20 tiles
void Glide(float *x, float *y, float tx, float ty, float dt);

// Did the player move the camera? (it is farther than this from where we put it)
const float kTakeOver = 3.0f;
bool TookOver(float x, float y, float set_x, float set_y);

// Who follows this PC's player: the last "on" of each follower, in ms (GetTickCount),
// 0 = not following. An eye lapses after kWatchLapse without a new "on".
const uint32_t kWatchLapse = 12000;
const uint32_t kWatchRepeat = 5000;  // the follower's "on" again, this often
struct Watchers {
	uint32_t since[9];  // by player number 1..8 (0 unused)
	void Clear();
	void On(int player, uint32_t now);
	void Off(int player);
	bool Active(int player, uint32_t now) const;
	bool Any(uint32_t now) const;
};

// May `follower` follow `followed`? Two different players of the same team.
bool MayFollow(int follower, int follower_team, int followed, int followed_team);

}  // namespace spectate
}  // namespace mods
