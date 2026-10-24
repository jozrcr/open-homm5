// Mod: the original Heroes V music for MMH5.5 players (see original_music.cpp).
#pragma once
#include <string>
#include <vector>

namespace mods {

// With the settings of open-homm5.ini: [audio] original_music=0 (default: the
// music of the installed mod, as without the add-on; 1 = the original game's
// music wherever a mod changed it) and log_music=0 (1 = log the music files the
// game opens, for testing). Only after DetectGameBuild() said the build is
// hookable. Installs nothing when both are 0.
void ApplyOriginalMusicFromSettings();

// The same, called when the DLL is loaded (before the game reads any data: it
// reads the heroes during its splash screen, before the other hooks go in).
// Only puts our function in the game's file system table; the rest waits for
// the first file the game opens. ApplyOriginalMusicFromSettings() then only
// adds log_music. Only after DetectGameBuild() said the build is hookable.
void InstallOriginalMusicEarly();

namespace music {

// One music reference that MakeOriginal changed, for the log. Shortened
// references: "AdvMap-Terrain/Grass.xdb"; empty = none.
struct Change {
	std::string what;  // the element (AdventureMusic) or "track" for a music file name
	std::string from;  // what the mod had
	std::string to;    // what the original game has
};

enum class Result {
	Unchanged,  // nothing to do: same music as the original game
	Patched,    // *out holds the file with the original music
	Skipped,    // music differs but the file could not be matched (see *note)
};

// The content of one game data file (.xdb) with the original game's music.
//   path      the file's path in the game data, lower case with '/', e.g.
//             "mapobjects/_(advmaptile)/grass/flowers.xdb";
//   mod       its content as the game opened it (from a mod's .pak);
//   original  the same file in the original game's data, or null when the
//             original game has no such file.
// Music definitions and music tables (files under sounds/_(music)/) become the
// original file. In other files (map tiles, heroes) only the references to
// music are set back to the original's: element by element, the same element
// at the same place; references to music that the original game does not have
// there are emptied. Nothing else in the file changes.
Result MakeOriginal(const std::string &path, const std::string &mod, const std::string *original, std::string *out,
                    std::vector<Change> *changes, std::string *note);

}  // namespace music
}  // namespace mods
