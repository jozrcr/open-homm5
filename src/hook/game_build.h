// Which game exe are we running in? Hooks use fixed addresses, so they are only
// installed in builds whose layout we know.
#pragma once

enum class GameBuild {
	Unknown,
	Vanilla31,       // ToE 3.1 (2008-06-04): H5_GameOLD.exe / unwrapped reference exe
	Mmh55Utility,    // same code + MMH5.5's 12 patches: MMH55_Utility(_64).exe (co-op exe)
};

GameBuild DetectGameBuild();
const char *GameBuildName(GameBuild build);
inline bool IsHookable(GameBuild b) { return b == GameBuild::Vanilla31 || b == GameBuild::Mmh55Utility; }
