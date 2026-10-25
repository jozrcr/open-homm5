// Mod: the game's texts in another language, chosen by each player (see
// languages.cpp and docs/plans/languages.md).
#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace mods {

// open-homm5.ini [lang] texts=
enum class Texts {
	Game,     // "game" (default): the texts as installed, nothing changes
	French,   // "fr": the .txt files of bin\open-homm5\lang\fr\*.pak
	English,  // "en": designed, not built yet (the game's own texts are used)
};

// The setting (read once from open-homm5.ini).
Texts TextsChoice();

// From hooks::InstallEarly (DLL load, a build recognized then): with
// texts=fr, puts our function in the game's file system table before the
// game reads its first text. The pack index waits for the first file opened.
void InstallLanguagesEarly();

// From hooks::InstallAll: the same if it could not go in at DLL load, and the
// log line about the setting.
void ApplyLanguagesFromSettings();

namespace lang {

// "/Text\Game/X.txt" -> "text/game/x.txt" (the game compares paths this way)
std::string NormalizePath(const char *begin, const char *end);

// A text file: the path ends in ".txt", any case.
bool IsTextPath(const char *begin, const char *end);

// A text file's bytes as the game reads them (0x004c7310): FF FE, then UTF-16
// LE. Accepts UTF-16 LE with or without the mark and UTF-8 with or without
// its mark (then line ends become CRLF, like the game's own texts). False for
// anything else (UTF-16 BE, broken UTF-8): such a file is not used.
bool ToGameText(const std::string &raw, std::string *out);

// One file of a zip archive (.pak), from its central directory.
struct ZipEntry {
	std::string key;   // normalized path
	WORD method;       // 0 stored, 8 deflated
	DWORD packed_size;
	DWORD size;
	DWORD header;      // offset of the local header
	DWORD dos_time;    // date (high word) and time (low word): compares in time order
};

// The central directory's place, from the last bytes of a zip file (at most
// 65557, the end record and its comment). False when there is no end record.
bool FindDirectory(const unsigned char *tail, size_t size, DWORD *dir_offset, DWORD *dir_size);

// The files of a central directory (folders left out).
void ReadDirectory(const unsigned char *dir, size_t size, std::vector<ZipEntry> *out);

// From the 30 bytes of a local header: where the file's data starts, counted
// from the header (30 + name + extra), or 0 when it is not a local header.
DWORD DataStart(const unsigned char local[30]);

}  // namespace lang
}  // namespace mods
