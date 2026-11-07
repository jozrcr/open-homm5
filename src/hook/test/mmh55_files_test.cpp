// Test of which files the 2008 game must not see in a folder with MMH5.5
// (src/mods/mmh55_files_logic.h). No game needed.
#include <stdio.h>

#include "../../mods/mmh55_files_logic.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		++g_failures;
}

}  // namespace

int main()
{
	using mmh55files::Hidden;
	// MMH5.5's archives in data\ (the owner's folder, RC19k)
	const char *const paks[] = {"MMH55-Creaturepedia.pak", "MMH55-Data.pak", "MMH55-Frame.pak", "MMH55-Index.pak",
	                            "MMH55-Settings - Copie.pak", "MMH55-Settings.pak", "MMH55-Skillwheel.pak", "MMH55-Stats.pak",
	                            "MMH55-Templates.pak", "MMH55-Texts-EN.pak", "MMH55-Texts-FR.pak"};
	bool all = true;
	for (const char *p : paks)
		all = all && Hidden(p, false) && Hidden(p, true);
	Check(all, "every MMH5.5 pak of data\\ is hidden, also with the HD textures kept (a stray copy too)");
	Check(Hidden("MMH55-SPMaps.h5u", false) && Hidden("MMH55-Cam-Texts-EN.h5u", true), "MMH5.5's user mods (UserMODs\\*.h5u) are hidden");
	Check(Hidden("mmh55-data.PAK", false), "any case (Windows names)");
	Check(Hidden("MMH55-HDTex.pak", false) && !Hidden("MMH55-HDTex.pak", true), "the HD textures: hidden, unless kept");
	// the 2008 game's own files and other mods stay
	const char *const own[] = {"data.pak", "a2p1-data.pak", "texts.pak", "a2p1-texts.pak", "sound.pak", "soundsfx.pak",
	                           "All_campaigns.data.h5u", "MMH55_RMG_Map_002.h5m", "MMH55-notes.txt", "index.bin"};
	all = true;
	for (const char *f : own)
		all = all && !Hidden(f, false);
	Check(all, "the game's own paks, other user mods, maps and other files stay");
	Check(!Hidden(nullptr, false) && !Hidden("", false) && !Hidden("MMH55-", false), "nothing, or a name too short: stays");
	if (g_failures)
		printf("mmh55 files test: %d FAILED\n", g_failures);
	else
		printf("mmh55 files test: all passed\n");
	return g_failures ? 1 : 0;
}
