// Robot lobby helpers: choosing the map on the multiplayer "create a game" screen.
//
//   step=16|map|<part of the map file path, e.g. A1M4/>
//
// The screen (NUI::CMPCreateScreen) keeps its map list in a NUI::CMapsFormer at
// +0x17c. The map used by "create" is the row highlighted in the list widget
// (IScrollableContainer at former+0x34: slot 5 returns the highlighted row,
// slot 4 highlights one, slot 7 scrolls to it). The map entries are a vector of
// pointers at former+0x18..0x1c; each holds its list row at +0x90 and a filter
// mask at +0x94 (0x00704df0, 0x00705130), and the map file path at +0x3c (narrow
// string, e.g. "/Maps/Multiplayer/A1M4/map.xdb#xpointer(/AdvMapDesc)"; found by
// listing the text each entry points to, the displayed names are elsewhere).
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "robot.h"

namespace robot {
namespace {

bool Readable(const void *p, size_t n)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (!p || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
	    (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
		return false;
	return static_cast<const BYTE *>(p) + n <= static_cast<const BYTE *>(mbi.BaseAddress) + mbi.RegionSize;
}

// Text at p (narrow or UTF-16, printable ASCII, at least 4 characters), or false.
bool TextAt(const void *p, char *out, size_t size)
{
	if (!Readable(p, 128))
		return false;
	const BYTE *b = static_cast<const BYTE *>(p);
	size_t n = 0;
	const bool wide = b[1] == 0 && b[0] >= 0x20 && b[0] < 0x7f;
	const size_t step = wide ? 2 : 1;
	for (size_t i = 0; i + step <= 128 && n + 1 < size; i += step) {
		const BYTE c = b[i];
		if (wide && b[i + 1] != 0)
			break;
		if (c < 0x20 || c >= 0x7f)
			break;
		out[n++] = static_cast<char>(c);
	}
	out[n] = 0;
	return n >= 4;
}

bool ContainsNoCase(const char *text, const char *part)
{
	const size_t n = strlen(part);
	for (; *text; ++text) {
		if (_strnicmp(text, part, n) == 0)
			return true;
	}
	return false;
}

typedef void *(__fastcall *GetSelectedFn)(void *container, void *edx);
typedef void(__fastcall *SelectFn)(void *container, void *edx, void *row);

}  // namespace

bool PickMap(const char *wanted)
{
	BYTE *screen = static_cast<BYTE *>(FindScreen("NUI::CMPCreateScreen"));
	if (!screen)
		return false;
	bool picked = false;
	__try {
		BYTE *former = *reinterpret_cast<BYTE **>(screen + 0x17c);
		void *container = *reinterpret_cast<void **>(former + 0x34);
		BYTE **begin = *reinterpret_cast<BYTE ***>(former + 0x18);
		BYTE **end = *reinterpret_cast<BYTE ***>(former + 0x1c);
		void **vt = *static_cast<void ***>(container);
		hooklog::Write("robot: map list: %d maps", static_cast<int>(end - begin));
		for (BYTE **it = begin; it != end; ++it) {
			BYTE *entry = *it;
			char path[128];
			if (!TextAt(*reinterpret_cast<void **>(entry + 0x3c), path, sizeof(path)))
				lstrcpynA(path, "?", sizeof(path));
			const bool match = !picked && wanted[0] && ContainsNoCase(path, wanted);
			hooklog::Write("robot: map %d%s: %s", static_cast<int>(it - begin), match ? " (picked)" : "", path);
			if (match) {
				void *row = *reinterpret_cast<void **>(entry + 0x90);
				reinterpret_cast<SelectFn>(vt[4])(container, nullptr, row);
				reinterpret_cast<SelectFn>(vt[7])(container, nullptr, row);
				picked = reinterpret_cast<GetSelectedFn>(vt[5])(container, nullptr) == row;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("robot: map list crashed (caught)");
		return false;
	}
	if (picked)
		PostGameMessage("mapselected", 0);  // let the screen show the map's description
	else
		hooklog::Write("robot: no map matching \"%s\"", wanted);
	return picked;
}

}  // namespace robot
