// Which files the 2008 game must not see in a folder where MMH5.5 is installed (pure work,
// unit-tested by src/hook/test/mmh55_files_test.cpp; the mod is mmh55_files.cpp).
#pragma once

#include <string.h>

namespace mmh55files {

// `name`: a file name a search of the game found (no folder). MMH5.5's archives: its paks in
// data\ (MMH55-Data.pak, MMH55-Index.pak, ... also a stray "MMH55-Settings - Copie.pak") and
// its user mods in UserMODs\ (MMH55-SPMaps.h5u, ...), any case. Its HD textures
// (MMH55-HDTex.pak) only when they are not kept.
inline bool Hidden(const char *name, bool keep_hd_textures)
{
	if (!name || _strnicmp(name, "MMH55-", 6) != 0)
		return false;
	const size_t n = strlen(name);
	if (n < 10)
		return false;
	const char *ext = name + n - 4;
	if (_stricmp(ext, ".pak") != 0 && _stricmp(ext, ".h5u") != 0)
		return false;
	if (keep_hd_textures && _stricmp(name, "MMH55-HDTex.pak") == 0)
		return false;
	return true;
}

}  // namespace mmh55files
