// Mod: endless simultaneous turns on the 2008 game without MMH5.5 ([coop] endless_turns).
//
// In plain words: with "simultaneous turns" ticked in the lobby, the original game lets
// the humans play their turns at the same time only until their heroes get close to each
// other (or a battle, a dimension door, a flight...); from then on, turns go one after the
// other again, which makes co-op games much slower. MMH5.5 ships Deflaktor's PEST
// ("Permanent and Endless Simultaneous Turns", v1.6), 8 small changes to the program that
// keep simultaneous turns for the whole game; things that cannot happen at once (a battle
// between three players) are simply ignored. MMH5.5's 55u.dll makes them at start-up. The
// 2008 game without MMH5.5 has no 55u.dll, so this mod makes the same 8 changes, from
// PEST's public description (MMH5.5's repository,
// assembly/patches/production/migrated/pest.yml, "Original EXE 3.1"):
//
//   0x009c2660  the check that ends simultaneous turns returns at once (its first byte, of
//               "sub esp, 0x174", becomes "ret": it takes no stack arguments)
//   0x00af3664  after a battle: the call that ends them is skipped (5 x nop)
//   0x0042e072, 0x00865ae3, 0x009984f7  a computer player's turn, the end of its turn (twice)
//   0x008659c5  a dimension door
//   0x00b4b6d4  angel wings, boots of levitation
//   0x0098f909  ghost mode
// The last six each follow a call of the world's "are turns simultaneous?" (vtable +0x1c)
// and skip the call that ends them (vtable +0x20): "je" becomes "jmp".
//
// It changes how the game world goes on, so every PC of a game must do it, or none: it is
// one of the co-op features (coop_features.cpp), on only when every PC knows it and the
// host wants it, and switched at the same point of the game on every PC (the decision at
// segment 20, before any hero moved). Off by default (the original game's rules).
#include "endless_turns.h"

#include <windows.h>
#include <string.h>

#include "../hook/game_build.h"
#include "../hook/log.h"

namespace mods {
namespace {

struct Spot {
	DWORD address;
	const char *what;
	BYTE original[6];
	BYTE endless[6];
	int size;
};

const Spot kSpots[] = {
	{0x009c2660, "the check that ends them", {0x81}, {0xc3}, 1},
	{0x00af3664, "after a battle", {0xe8, 0x57, 0xef, 0xec, 0xff}, {0x90, 0x90, 0x90, 0x90, 0x90}, 5},
	{0x0042e072, "a computer player's turn", {0x74, 0x14}, {0xeb, 0x14}, 2},
	{0x00865ae3, "a computer player's end of turn (1)", {0x74, 0x7f}, {0xeb, 0x7f}, 2},
	{0x009984f7, "a computer player's end of turn (2)", {0x74, 0x13}, {0xeb, 0x13}, 2},
	{0x008659c5, "a dimension door", {0x0f, 0x84, 0x6f, 0x05, 0x00, 0x00}, {0xe9, 0x70, 0x05, 0x00, 0x00, 0x90}, 6},
	{0x00b4b6d4, "angel wings, boots of levitation", {0x74, 0x12}, {0xeb, 0x12}, 2},
	{0x0098f909, "ghost mode", {0x74, 0x34}, {0xeb, 0x34}, 2},
};

bool g_available = false;
bool g_on = false;

bool Write(const Spot &s, const BYTE *bytes)
{
	BYTE *p = reinterpret_cast<BYTE *>(s.address);
	DWORD old = 0;
	if (!VirtualProtect(p, s.size, PAGE_EXECUTE_READWRITE, &old))
		return false;
	memcpy(p, bytes, s.size);
	VirtualProtect(p, s.size, old, &old);
	FlushInstructionCache(GetCurrentProcess(), p, s.size);
	return true;
}

}  // namespace

void InstallEndlessTurns()
{
	const GameBuild build = DetectGameBuild();
	if (build != GameBuild::Vanilla31) {
		hooklog::Write("endless turns: not for this game (%s): %s", GameBuildName(build),
		               build == GameBuild::Mmh55Utility ? "MMH5.5's 55u.dll does PEST itself (MMH55Stats.xml)" : "unknown build");
		return;
	}
	for (const Spot &s : kSpots)
		if (memcmp(reinterpret_cast<const void *>(s.address), s.original, s.size) != 0) {
			hooklog::Write("endless turns: the game's code at %08lx (%s) is not as expected: not available", s.address, s.what);
			return;
		}
	g_available = true;
	hooklog::Write("endless turns: available (the 2008 game: the host's [coop] endless_turns decides for each game)");
}

bool EndlessTurnsAvailable()
{
	return g_available;
}

void SetEndlessTurns(bool on)
{
	if (!g_available || on == g_on)
		return;
	int written = 0;
	for (const Spot &s : kSpots)
		written += Write(s, on ? s.endless : s.original) ? 1 : 0;
	g_on = on;
	hooklog::Write("endless turns: %s (%d of %d places)", on ? "ON: simultaneous turns go on for the whole game"
	                                                        : "off: the game's own simultaneous turns", written,
	               static_cast<int>(sizeof(kSpots) / sizeof(kSpots[0])));
}

}  // namespace mods
