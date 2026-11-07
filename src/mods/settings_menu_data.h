// The settings menu's data for the game's Options screen (settings_menu.cpp):
// what does not need the game, so test/settings_test.cpp can check it.
//
// The game's Options screen (NUI::CGameOptionsScreen) builds its categories,
// pages and rows from the "GameOptions" part of UI/uiconsts.(UIGameConstsH5).xdb:
// each category has a button on the screen's scroll and up to two pages side by
// side; each page is a list of rows (OPTION_CHECK_BOX, OPTION_COMBO_BOX,
// OPTION_SCROLLER) and headers (OPTION_BLOCK_HEADER); each row shows and sets one
// console variable (GlobalVar1) with the game's own controls, and the screen's
// Accept, Default and Back buttons work on the category shown. We add one
// category, "H5 Together", with two pages; its rows' variables are ours
// (h5t_<section>_<key>), holding the index of the row's choice, and
// settings_menu.cpp moves them to and from h5-together.ini.
//
// Our words go to the game as text files of its own kind (UTF-16), under
// UI/Consts/GameOptions/H5Together/ (names only we use).
#pragma once

#include <string>
#include <vector>

#include "settings_table.h"

namespace mods {
namespace settings {

// What the menu shows on this PC.
struct MenuOptions {
	bool wine = false;   // the mouse wheel fix only matters under Wine
	bool packs = false;  // the game's French texts: only when the packs are in h5-together\lang\fr
	bool steam_dll = false;  // Valve's steam_api.dll is in h5-together\ (online play over Steam can start)
	bool mmh55 = true;       // the game runs with MMH5.5 (false: the 2008 game alone; its rows hide, endless_turns shows)
	std::vector<std::string> languages;  // our texts files' codes besides en and fr ("de", "xx")
	std::vector<std::pair<std::string, std::string>> names;  // a language's code -> its own name ("fr" -> "Français")
	std::vector<std::pair<std::string, std::string>> extra;  // "section.key" -> a value of the file the choices lack
};

// One choice of a row: its value in the file, and its words (a text key, else
// the value itself, with the setting's unit if it has one).
struct MenuItem {
	std::string value;
	const char *text;
};

// One line of a page: a header (s == nullptr: group's words) or a setting's row.
struct MenuRow {
	const Setting *s = nullptr;
	Group group = kFileOnly;
	std::vector<MenuItem> items;  // the choices (a check box: the two values)
	std::string var;              // the console variable (h5t_<section>_<key>)
	int def = 0;                  // the default's index in items
};

// The lines of page 1 and page 2, in order, for these options.
std::vector<MenuRow> MenuRows(const MenuOptions &options, int page);

// The row's console variable: "h5t_" + section + "_" + key.
std::string VarName(const Setting &s);

// The game's uiconsts file with our category added at the end of its list. False
// (and *out untouched) when the file has no GameOptions category list, or
// already has our category.
bool AddCategory(const std::string &game_file, const MenuOptions &options, std::string *out);

// The served text files: their path under the game's data (lower case, '/'),
// "ui/consts/gameoptions/h5together/...". The folder:
extern const char kTextFolder[];

// The words of one of our text files (`name`: the part after the folder, as in
// the uiconsts file, e.g. "h5t_notify_sound/Name.txt"), in UTF-8 with the game's
// tags. `get` gives a text key's words; `version`, `mmh55`: for the help lines.
// False when the name is not one of ours.
typedef const char *(*GetText)(const char *key);
bool MenuText(const std::string &name, const MenuOptions &options, GetText get, const std::string &version,
              const std::string &mmh55, const std::string &lan_peers, std::string *out);

// UTF-8 -> the game's text files: UTF-16 (little end first) with its byte order mark.
std::string GameText(const std::string &utf8);

// The words a value shows as ("150 %", "F9", "None").
std::string ItemWords(const Setting &s, const MenuItem &item, GetText get);

}  // namespace settings
}  // namespace mods
