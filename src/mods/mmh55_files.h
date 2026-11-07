// Mod: the 2008 game started in a folder where MMH5.5 is installed: MMH5.5's files are
// hidden from it, so it is the original game ([video] hd_textures keeps MMH5.5's HD
// textures). See mmh55_files.cpp.
#pragma once

namespace mods {

// At the DLL's load, before the game looks for its data. Acts only in the 2008 game's exe
// (Steam-wrapped H5_GameOLD.exe too), never in MMH5.5's exes.
void InstallMmh55FilesEarly();

// This PC runs the 2008 exe in a folder with MMH5.5's files, and hides them.
bool Mmh55FilesHidden();

// Its HD textures are kept ([video] hd_textures=1 and MMH55-HDTex.pak is there).
bool Mmh55HdTexturesKept();

}  // namespace mods
