// The player list overlay's parts (player_list.cpp), shared between its files:
//   player_list_game.cpp       reads the game: players, teams, turn status, day;
//   player_list_textures.cpp   pictures from the game's data (portraits, frames);
//   player_list_draw.cpp       draws the panel (all of the look and its words);
//   player_list.cpp            settings, Direct3D and Dear ImGui, the F9 key.
#pragma once
#include <windows.h>

#include <string>

struct IDirect3DDevice9;

namespace mods {
namespace plist {

enum Status {
	kPlaying,   // a human whose turn it is, who has not pressed End Turn
	kEnded,     // done for today (pressed End Turn, or an AI that has moved)
	kThinking,  // an AI player whose turn it is (the AI is moving)
	kWaiting,   // the turn has not come yet today
};

const int kMaxPlayers = 8;

struct Row {
	int id;      // player number, 1..8 (PLAYER_1 ...): also the turn order
	int colour;  // the player's colour, 1..8 (red, blue, green, yellow, orange, teal, purple, brown)
	int team;    // the game's team number (0, 1, 2...)
	bool human;
	int computer;  // computer players: 1, 2... in turn order (for "Computer N")
	Status status;
	char name[64];  // UTF-8: the player's name (lobby name, "AI 2" in single player); "" if none
	char hero[64];  // UTF-8: the name of the player's selected (or first) hero, "" without heroes
	char face[160];  // that hero's portrait: a .dds file of the game's data, or ""
	void *face_texture;  // the portrait as a texture (IDirect3DTexture9 *), or nullptr (player_list.cpp)
};

struct Snapshot {
	int day;            // 1 = the first day
	bool simultaneous;  // the humans play at the same time
	int count;          // rows used (players still in the game, in turn order)
	Row rows[kMaxPlayers];
	int done;    // rows with status kEnded
	bool teams;  // a team has more than one player (otherwise no team lines)
};

// player_list_game.cpp
bool InstallGameHooks();  // false: the game is not as expected (the overlay stays off)
void SetTrace(int level);  // 1 = log status changes, 2 = also raw dumps (research)
// The game's state now; false when no adventure map is on screen.
bool ReadSnapshot(Snapshot *out);

// player_list_textures.cpp: pictures from the game's data (portraits, frames).
bool InstallGameTextures();  // false: no portraits, plain style (the rest works)
// The picture of that .dds file as a texture of the device (IDirect3DTexture9 *),
// or nullptr: not loaded yet (*loads_left files are read at most per call), or
// not readable (then *failed, if given, is true).
void *GameTexture(IDirect3DDevice9 *device, const char *dds_path, int *loads_left, bool *failed = nullptr);
void ReleaseGameTextures();
// A file of the game's data, read through the game's own file system (false
// before the game opened its first file, or when there is no such file).
bool ReadGameFile(const char *path, std::string *out);

// player_list_draw.cpp: the panel. All ImGui drawing of the overlay is there,
// and all its words (English, French).
enum Language { kEnglish, kFrench };
struct Look {
	float alpha;     // 0..1, lower while the mouse is over the panel
	float top;       // where it starts, fraction of the screen height (0.34)
	float left;      // its left inset, fraction of the screen height (0.02)
	Language language;
	void *font;       // ImFont *: names
	void *font_bold;  // ImFont *: the day, team numbers
	void *frame;      // the game's resource bar picture (IDirect3DTexture9 *), or nullptr: plain style
};
// The game's picture the panels are made of (the resource bar, 1024x64, DXT3).
extern const char kFramePicture[];
// The font sizes the panel wants at this screen height (pixels).
void FontSizes(float screen_height, float *regular, float *bold);
// Draws the panel; writes the rectangle it covers (x0, y0, x1, y1, in pixels).
void DrawPanel(const Snapshot &s, const Look &look, float rect[4]);

}  // namespace plist
}  // namespace mods
