// Mod: modern timings for the game's LAN link (LAN and GameRanger games).
//
// Heroes V talks to the other PCs of a multiplayer game over a small "reliable
// UDP" link (src/../docs/subsystems/multiplayer-sync.md, "Measured"). Its
// timings fit 2006 modems:
//   - a link sends at most one packet (~256 bytes of game data) every 50 ms,
//     about 5 KB/s;
//   - a PC with nothing to send acknowledges what it received only once a
//     second (its "keepalive"), and the other PC may not send more until then.
// In co-op, only the host runs the AI and every AI move goes to the other PCs.
// With these limits the host's queue backs up, every PC sees the others
// lagging and slows its own game down, and each AI move waits 1-2.5 s.
// Measured with the robot (2 copies on one PC, 2 humans + 1 AI): 2.5 s per AI
// move with the game's timings, 0.6 s with the timings below.
//
// What this mod changes, on this PC:
//   keepalive_ms       an idle link sends (and so acknowledges) every 50 ms
//                      instead of every 1000 ms;
//   send_interval_ms   a link may send a packet every 10 ms instead of 50 ms;
//   batched_segments   a PC with nothing to say reports each finished command
//                      segment at once (0) instead of holding the reports back
//                      and sending them in batches (the game's
//                      net_batched_segments, default 3).
// Measured contributions (seconds per AI move): game 2.5; send interval only
// 2.2; keepalive only 1.6; both 0.9; all three 0.6.
// These are network timings only: game commands still run in the same order on
// every PC. Every player of a game should run the mod: each PC only speeds up
// what it sends.
//
// How: three operands in the game's code are replaced in memory (nothing on
// disk changes), only after checking they hold the expected original values:
//   0x008fdf25  the address of the 1.0f the keepalive test 0x008fdf20 compares
//               with (fcomp dword ptr [0x00e0a7e4]); 1.0f is shared with other
//               code, so the test is pointed at our own value instead;
//   0x0047e553  the 0.05f in "mov dword ptr [edi+0x3e8e4], 0x3d4ccccd" at
//               0x0047e54d, the link's send timer (per-link update 0x0047e050);
//   0x0040247f  the address in "mov edx, [0x00f27398]" at 0x0040247d, the only
//               read of net_batched_segments (0x00402440, once per segment); it
//               is pointed at our own value, because the game reloads the
//               console variable from the player's profile.
#include "net_tuning.h"

#include <windows.h>
#include <string.h>

#include "../hook/log.h"

namespace mods {
namespace {

const DWORD kKeepaliveOperand = 0x008fdf25;  // holds 0x00e0a7e4 (address of 1.0f)
const DWORD kSendIntervalImm = 0x0047e553;   // holds 0x3d4ccccd (0.05f)
const DWORD kBatchedOperand = 0x0040247f;    // holds 0x00f27398 (net_batched_segments)
float g_keepalive_seconds = 1.0f;
int g_batched_segments = 3;

bool Write(DWORD address, DWORD expected, DWORD value, const char *what)
{
	DWORD *p = reinterpret_cast<DWORD *>(address);
	if (*p == value)
		return true;  // already set
	if (*p != expected) {
		hooklog::Write("net tuning: %s at %08lx is %08lx, not %08lx: left alone", what, address, *p, expected);
		return false;
	}
	DWORD old;
	if (!VirtualProtect(p, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &old))
		return false;
	*p = value;
	VirtualProtect(p, sizeof(DWORD), old, &old);
	FlushInstructionCache(GetCurrentProcess(), p, sizeof(DWORD));
	return true;
}

}  // namespace

void ApplyNetTuning(DWORD keepalive_ms, DWORD send_interval_ms, int batched_segments)
{
	if (keepalive_ms) {
		g_keepalive_seconds = keepalive_ms / 1000.0f;
		if (Write(kKeepaliveOperand, 0x00e0a7e4, reinterpret_cast<DWORD>(&g_keepalive_seconds), "keepalive"))
			hooklog::Write("net tuning: idle links send every %lu ms (game: 1000)", keepalive_ms);
	}
	if (send_interval_ms) {
		const float seconds = send_interval_ms / 1000.0f;
		DWORD bits;
		memcpy(&bits, &seconds, sizeof(bits));
		if (Write(kSendIntervalImm, 0x3d4ccccd, bits, "send interval"))
			hooklog::Write("net tuning: links may send every %lu ms (game: 50)", send_interval_ms);
	}
	if (batched_segments >= 0) {
		g_batched_segments = batched_segments;
		if (Write(kBatchedOperand, 0x00f27398, reinterpret_cast<DWORD>(&g_batched_segments), "batched segments"))
			hooklog::Write("net tuning: idle PCs batch up to %d segment reports (game: net_batched_segments, 3)",
			               batched_segments);
	}
}

void ApplyNetTuningFromSettings()
{
	// open-homm5.ini next to the game exe (open-homm5-<instance>.ini for the
	// robot's copies, see hooklog::PathNextToExe), section [net]; all optional.
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	if (!GetPrivateProfileIntW(L"net", L"tuning", 1, path)) {
		hooklog::Write("net tuning: off (open-homm5.ini)");
		return;
	}
	ApplyNetTuning(GetPrivateProfileIntW(L"net", L"keepalive_ms", 50, path),
	               GetPrivateProfileIntW(L"net", L"send_interval_ms", 10, path),
	               static_cast<int>(GetPrivateProfileIntW(L"net", L"batched_segments", 0, path)));
}

}  // namespace mods
