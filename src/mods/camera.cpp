// Mod: let the camera zoom out farther, on the adventure map and in battles.
//
// The game stops the camera at a fixed farthest distance from the point it
// looks at. Players with big screens (and MMH5.5's bigger maps) find that close.
// With [camera] zoom_out=150 the adventure map's camera may go 1.5 times as far
// as the game allows; combat_zoom_out does the same in battles. Both are in %
// of the game's own limit, so they follow whatever the game and MMH5.5 set.
// The default (100, or 0 / empty) is the game's own limit, and then this file
// installs nothing. Findings and addresses: docs/subsystems/camera.md.
//
// Where the limits come from:
//   - Adventure map: profiles\autoexec_a2.cfg in the game folder runs console
//     commands at start-up, e.g. "mission_camera_limits 50 10 70 -55 -30 0 0"
//     (field of view, nearest and farthest distance, steepest and flattest
//     tilt, turn limits). One limits record per camera mode of the options
//     screen ("normal", "vertically fixed", "classic") plus one for mini
//     dialogs, each with a "soft" twin (where the camera springs back to). The
//     game's file is the same with and without MMH5.5: farthest distance 70 in
//     every mode. When the adventure map's camera is set up (map start, camera
//     mode change), CAdvMapCameraConfigurator asks 0x0058a5e0 / 0x0058a630 for
//     the record of the current mode and gives it to the camera.
//   - Battles: CArenaCameraConfigurator (0x007d3730) asks 0x007d3650 for the
//     farthest distance of this arena (a small table by arena size; MMH5.5's
//     55u.dll changes the entry of its bigger battlefields, from its
//     MMH55Stats.xml), then gives the camera limits and soft limits with it.
//
// How: the calls to those three functions (5 "call" instructions) are pointed
// at ours, after checking that each one calls the expected function (otherwise
// nothing is changed and the log says so). Ours call the game's function and:
//   - adventure map: hand the camera a copy of the game's record with the
//     farthest distance (and its soft twin) multiplied; the game's own records
//     stay as they are;
//   - battles: return the game's farthest distance multiplied.
// The nearest distance, the tilt and everything else stay the game's.
//
// Only this PC's camera changes: the limits are only read by the camera set-up
// code, and the camera is not part of the game world, of the world checksums
// or of the commands the PCs send each other. Each player can choose.
#include "camera.h"

#include <windows.h>
#include <string.h>

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

// ---- the game (vanilla 3.1 layout, also MMH55_Utility) ----

// __fastcall(int mode) -> limits record; mode 1000 = the options' camera mode
// (console variable adv_camera_mode, 0x00fd7158), 3 = mini dialog.
const DWORD kLimitsFn = 0x0058a5e0;
const DWORD kSoftLimitsFn = 0x0058a630;  // the same for the soft limits
// Their only callers: CAdvMapCameraConfigurator 0x007d3180 (the camera
// operator) and 0x007d3310 (the camera).
const DWORD kLimitsSites[] = {0x007d31fa, 0x007d3332};
const DWORD kSoftLimitsSites[] = {0x007d321b, 0x007d3353};

// A limits record: 15 floats (0x3c bytes), filled by the parser 0x0058a790:
// [0] nearest distance, [1] farthest distance, [2] [3] tilt range, [4] [5] turn
// range (radians), [6] attenuation, [7] lowest height, the rest unknown.
const int kLimitFloats = 15;
const int kFarthest = 1;

// The records (filled by the console commands named here).
struct Record {
	DWORD address;
	const char *name;
};
const Record kRecords[] = {
	{0x00f33900, "normal camera"},                       // mission_camera_limits
	{0x00f3393c, "normal camera, soft limit"},           // mission_camera_softlimits
	{0x00f33a18, "vertically fixed camera"},             // mission_camera_vertical_fixed_limits
	{0x00f33a54, "vertically fixed camera, soft limit"}, // mission_camera_vertical_fixed_softlimits
	{0x00f33a90, "classic camera"},                      // mission_camera_classic_like_limits
	{0x00f33acc, "classic camera, soft limit"},          // mission_camera_classic_like_softlimits
	{0x00f33b08, "mini dialog camera"},                  // mission_mini_dialog_camera_limits
	{0x00f33b44, "mini dialog camera, soft limit"},      // mission_mini_dialog_camera_softlimits
};

// __thiscall(CArenaCameraConfigurator *) -> float (in ST0), no stack arguments:
// the farthest distance of the battle's arena. Called once, from 0x007d3730.
const DWORD kArenaFarthestFn = 0x007d3650;
const DWORD kArenaFarthestSite = 0x007d386b;

typedef const float *(__fastcall *LimitsFn)(int mode);
typedef float(__fastcall *ArenaFarthestFn)(void *configurator);
LimitsFn g_limits_next = nullptr;
LimitsFn g_soft_limits_next = nullptr;
ArenaFarthestFn g_arena_next = nullptr;

float g_map_scale = 1.0f;     // [camera] zoom_out / 100
float g_combat_scale = 1.0f;  // [camera] combat_zoom_out / 100

// ---- the log: one line each time a limit we change is new ----

const LONG kMaxLines = 40;
LONG g_lines = 0;  // the camera is set up on the main thread only

const char *RecordName(const float *game)
{
	for (const Record &r : kRecords)
		if (reinterpret_cast<const float *>(r.address) == game)
			return r.name;
	return "other camera";
}

// ---- adventure map ----

// Our copies of the game's records, one per record (the game has 8). The camera
// may keep the pointer it is given, so each copy stays where it is.
struct Copy {
	const float *game;
	float data[kLimitFloats];
	float logged;  // the game's farthest distance when we last logged it
};
const int kMaxCopies = 16;
Copy g_copies[kMaxCopies];
int g_copy_count = 0;

const float *Farther(const float *game)
{
	if (!game)
		return game;
	Copy *copy = nullptr;
	for (int i = 0; i < g_copy_count && !copy; ++i)
		if (g_copies[i].game == game)
			copy = &g_copies[i];
	if (!copy) {
		if (g_copy_count == kMaxCopies)
			return game;  // (cannot happen: the game has 8 records)
		copy = &g_copies[g_copy_count++];
		copy->game = game;
		copy->logged = -1.0f;
	}
	memcpy(copy->data, game, sizeof(copy->data));
	copy->data[kFarthest] = game[kFarthest] * g_map_scale;
	if (copy->logged != game[kFarthest] && g_lines < kMaxLines) {
		copy->logged = game[kFarthest];
		++g_lines;
		hooklog::Write("camera: adventure map, %s: farthest zoom %g -> %g (nearest %g)", RecordName(game),
		               game[kFarthest], copy->data[kFarthest], game[0]);
	}
	return copy->data;
}

const float *__fastcall LimitsHook(int mode)
{
	return Farther(g_limits_next(mode));
}

const float *__fastcall SoftLimitsHook(int mode)
{
	return Farther(g_soft_limits_next(mode));
}

// ---- battles ----

float g_arena_logged = -1.0f;

float __fastcall ArenaFarthestHook(void *configurator)
{
	const float game = g_arena_next(configurator);
	const float ours = game * g_combat_scale;
	if (game != g_arena_logged && g_lines < kMaxLines) {
		g_arena_logged = game;
		++g_lines;
		hooklog::Write("camera: battle: farthest zoom %g -> %g", game, ours);
	}
	return ours;
}

// ---- settings ----

const int kMaxPercent = 400;

// [camera] <key> in % (0 or empty = the game's own); returns the factor, 1 = the game's.
float Factor(const wchar_t *ini, const wchar_t *key, const char *name)
{
	const int percent = static_cast<int>(GetPrivateProfileIntW(L"camera", key, 0, ini));
	if (percent <= 0 || percent == 100)
		return 1.0f;
	if (percent < 100) {
		hooklog::Write("camera: %s=%d is less than 100: the game's own zoom", name, percent);
		return 1.0f;
	}
	if (percent > kMaxPercent) {
		hooklog::Write("camera: %s=%d is more than %d: %d is used", name, percent, kMaxPercent, kMaxPercent);
		return kMaxPercent / 100.0f;
	}
	return percent / 100.0f;
}

}  // namespace

void ApplyCameraFromSettings()
{
	wchar_t ini[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", ini, MAX_PATH);  // open-homm5-<instance>.ini for the robot
	g_map_scale = Factor(ini, L"zoom_out", "zoom_out");
	g_combat_scale = Factor(ini, L"combat_zoom_out", "combat_zoom_out");

	if (g_map_scale == 1.0f) {
		hooklog::Write("camera: adventure map: the game's own zoom limit (zoom_out=100)");
	}
	else {
		g_limits_next = reinterpret_cast<LimitsFn>(hookpatch::RedirectCalls(kLimitsSites, 2, kLimitsFn, &LimitsHook));
		if (g_limits_next)
			g_soft_limits_next = reinterpret_cast<LimitsFn>(
				hookpatch::RedirectCalls(kSoftLimitsSites, 2, kSoftLimitsFn, &SoftLimitsHook));
		if (!g_limits_next || !g_soft_limits_next) {
			// The two go together (limits and soft limits): if only the first went
			// in, it stays, harmless (the soft limit then stops the camera earlier).
			hooklog::Write("camera: the adventure map's camera set-up is not as expected (%08lx, %08lx): "
			               "the game's own zoom limit",
			               kLimitsSites[0], kSoftLimitsSites[0]);
		}
		else {
			hooklog::Write("camera: adventure map: the camera may zoom out to %d%% of the game's limit (zoom_out)",
			               static_cast<int>(g_map_scale * 100.0f + 0.5f));
		}
	}

	if (g_combat_scale == 1.0f) {
		hooklog::Write("camera: battles: the game's own zoom limit (combat_zoom_out=100)");
	}
	else {
		g_arena_next = reinterpret_cast<ArenaFarthestFn>(
			hookpatch::RedirectCall(kArenaFarthestSite, kArenaFarthestFn, &ArenaFarthestHook));
		if (!g_arena_next)
			hooklog::Write("camera: the battle camera's set-up is not as expected (%08lx): the game's own zoom limit",
			               kArenaFarthestSite);
		else
			hooklog::Write("camera: battles: the camera may zoom out to %d%% of the game's limit (combat_zoom_out)",
			               static_cast<int>(g_combat_scale * 100.0f + 0.5f));
	}
}

}  // namespace mods
