// Test of the settings menu's pure work (src/mods/settings_table.cpp, settings_ini.cpp,
// settings_menu_data.cpp):
//   - the table of every setting against the shipped tools/package/files/h5-together.ini
//     (the same settings, in the same sections, with the same defaults);
//   - each row of the menu: its words in en.txt and fr.txt, its choices, its default;
//   - the in-place writer, with the cases of tools/package/test_scripts.py (a fresh file,
//     a byte-order mark, LF, upper case, spaces, a second copy, a missing section, UTF-16),
//     and Windows' own reading (GetPrivateProfileString) of what it wrote;
//   - the Options screen's data: our category in a copy of the game's uiconsts file, the
//     texts we serve.
// Usage: settings_test.exe <shipped h5-together.ini> <src/mods/texts>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../../mods/settings_ini.h"
#include "../../mods/settings_menu_data.h"
#include "../../mods/settings_table.h"
#include "../texts_logic.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char *what, const std::string &detail = std::string())
{
	printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) {
		++g_failures;
		if (!detail.empty())
			printf("      %s\n", detail.c_str());
	}
}

std::string ReadAll(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	std::stringstream s;
	s << f.rdbuf();
	return s.str();
}

std::string Lower(std::string s)
{
	for (char &c : s)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return s;
}

std::string Crlf(const std::string &lf)
{
	std::string s;
	for (char c : lf) {
		if (c == '\n')
			s += '\r';
		if (c != '\r')
			s += c;
	}
	return s;
}

// The text with LF line ends only (a checkout may have CRLF: git's autocrlf on GitHub's machines).
std::string Lf(const std::string &text)
{
	std::string s;
	for (char c : text)
		if (c != '\r')
			s += c;
	return s;
}

// "section key=default" of each ";key=..." line of the shipped file, in order.
struct Shipped {
	std::string section, key, def;
};
std::vector<Shipped> ShippedSettings(const std::string &file)
{
	std::vector<Shipped> out;
	std::string section;
	std::istringstream in(file);
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.size() > 2 && line[0] == '[' && line.find(']') != std::string::npos) {
			section = line.substr(1, line.find(']') - 1);
			continue;
		}
		if (line.size() < 3 || line[0] != ';')
			continue;
		size_t i = 1;
		while (i < line.size() && (isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_'))
			++i;
		if (i > 1 && i < line.size() && line[i] == '=')
			out.push_back({section, line.substr(1, i - 1), line.substr(i + 1)});
	}
	return out;
}

std::string Replaced(std::string text, const std::string &from, const std::string &to)
{
	const size_t at = text.find(from);
	if (at == std::string::npos || text.find(from, at + 1) != std::string::npos) {
		Check(false, ("test setup: \"" + from + "\" should be in the text once").c_str());
		return text;
	}
	return text.replace(at, from.size(), to);
}

using mods::settings::Change;

std::string Edited(const std::string &file, std::vector<Change> changes)
{
	std::string out;
	if (!mods::settings::Edit(file, changes, &out))
		return "(refused)";
	return out;
}

Change Set(const char *section, const char *key, const char *value)
{
	Change c;
	c.section = section;
	c.key = key;
	c.value = value;
	return c;
}

Change Default(const char *section, const char *key, const char *def)
{
	Change c;
	c.section = section;
	c.key = key;
	c.def = def;
	c.to_default = true;
	return c;
}

// What Windows reads from these bytes (GetPrivateProfileString on a file in %TEMP%).
std::string WindowsReads(const std::string &bytes, const wchar_t *section, const wchar_t *key)
{
	wchar_t dir[MAX_PATH], path[MAX_PATH];
	GetTempPathW(MAX_PATH, dir);
	swprintf_s(path, L"%ssettings_test_%lu.ini", dir, GetCurrentProcessId());
	{
		std::ofstream f(path, std::ios::binary);
		f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}
	wchar_t value[256];
	GetPrivateProfileStringW(section, key, L"(none)", value, 256, path);
	DeleteFileW(path);
	std::string s;
	for (const wchar_t *p = value; *p; ++p)
		s += static_cast<char>(*p);
	return s;
}

void TableTests(const std::string &ini_path, const texts::Book &en, const texts::Book &fr)
{
	using namespace mods::settings;
	const std::string ini = ReadAll(ini_path);
	Check(!ini.empty(), "the shipped h5-together.ini reads");
	const std::vector<Shipped> shipped = ShippedSettings(ini);
	std::set<std::string> in_file, in_table;
	std::string missing, wrong_default, not_listed;
	for (const Shipped &s : shipped) {
		in_file.insert(Lower(s.section + "." + s.key));
		const Setting *t = Find(s.section.c_str(), s.key.c_str());
		if (!t || !t->shipped)
			not_listed += " [" + s.section + "] " + s.key;
		else if (s.def != t->def)
			wrong_default += " [" + s.section + "] " + s.key + " (file " + s.def + ", table " + t->def + ")";
	}
	int shipped_count = 0, hidden = 0, rows = 0, page1 = 0, page2 = 0;
	std::string duplicates, hidden_in_file, no_words, bad_default, bad_choice, fr_missing;
	for (int i = 0; i < kSettingCount; ++i) {
		const Setting &s = kSettings[i];
		const std::string name = Lower(std::string(s.section) + "." + s.key);
		if (!in_table.insert(name).second)
			duplicates += " " + name;
		if (s.shipped) {
			++shipped_count;
			if (!in_file.count(name))
				missing += " " + name;
		}
		else {
			++hidden;
			if (in_file.count(name))
				hidden_in_file += " " + name;
		}
		if (s.def[0] && !Valid(s, s.def) && s.kind != kText)  // (empty: not set, the mod's own way)
			bad_default += " " + name + "=" + s.def;
		if (s.group == kFileOnly || s.group == kRulesPanel)
			continue;
		++rows;
		(PageOf(s.group) == 1 ? page1 : page2) += 1;
		if (!s.text && s.control == kNone)
			continue;  // [coop] fast_segments: in the row of segment_ms
		std::vector<const char *> keys = {s.text, s.help, s.unit};
		for (int v = 0; v < s.value_count; ++v)
			keys.push_back(s.values[v].text);
		if (s.kind == kKey)
			keys.push_back(kKeyValues[kKeyValueCount - 1].text);
		for (const char *k : keys) {
			if (!k)
				continue;
			if (!en.texts.count(k))
				no_words += std::string(" ") + k;
			if (!fr.texts.count(k))
				fr_missing += std::string(" ") + k;
		}
		if (!s.text || !s.help)
			no_words += " (" + name + ": no text key)";
		if (s.control != kNone && ChoiceIndex(s, s.def) < 0)
			bad_choice += " " + name + " (its default is not a choice)";
		if (s.control == kCheck && ChoiceCount(s) != 2)
			bad_choice += " " + name + " (a check box needs two values)";
		for (int v = 0; v < s.value_count; ++v)
			if (!Valid(s, s.values[v].value) && !(s.kind == kChoice && SameValue(s, s.values[v].value, s.def)) &&
			    !(s.kind == kInt && !s.values[v].value[0]))
				bad_choice += " " + name + "=" + s.values[v].value;
	}
	Check(not_listed.empty(), "table: every setting of the shipped file is in the table", not_listed);
	Check(missing.empty(), "table: every setting of the table (not hidden) is in the shipped file", missing);
	Check(wrong_default.empty(), "table: the same defaults as the shipped file", wrong_default);
	Check(duplicates.empty(), "table: no setting twice", duplicates);
	Check(hidden_in_file.empty(), "table: the hidden test settings are not in the shipped file", hidden_in_file);
	Check(shipped_count == static_cast<int>(shipped.size()) && shipped_count >= 108,
	      "table: as many settings as the shipped file (108 in 0.3.0: 103 in 0.2.7, 5 more)", std::to_string(shipped_count));
	Check(hidden >= 9, "table: the hidden test settings of appendix B are listed", std::to_string(hidden));
	Check(bad_default.empty(), "table: each default is a valid value of its setting", bad_default);
	Check(no_words.empty(), "menu: each row's words (name, help, choices, unit) are in en.txt", no_words);
	Check(fr_missing.empty(), "menu: and in fr.txt", fr_missing);
	Check(bad_choice.empty(), "menu: each row's choices are valid values, and its default is one of them", bad_choice);
	// (a new row: add it to the table and its words to en.txt and fr.txt; these numbers only need to grow)
	Check(rows >= 47 && page1 >= 24 && page2 >= 23, "menu: at least 47 settings, 24 on the first page and 23 on the second",
	      std::to_string(rows) + " rows, " + std::to_string(page1) + " + " + std::to_string(page2));

	// choices
	const Setting *top = Find("ui", "player_list_top"), *zoom = Find("camera", "zoom_out"), *start = Find("camera", "start_zoom");
	const Setting *key = Find("UI", "Player_List_Key"), *comp = Find("save", "compression"), *steps = Find("coop", "segment_ms");
	const Setting *sky = Find("camera", "edge_sky"), *tuning = Find("net", "tuning");
	Check(top && zoom && start && key && comp && steps && sky && tuning, "table: Find (upper/lower case ignored)");
	if (top && zoom && start && key && comp && steps && sky && tuning) {
		Check(ChoiceValue(*top, ChoiceIndex(*top, "34")) == "34" && ChoiceValue(*top, ChoiceIndex(*top, "33")) == "32" &&
		          ChoiceValue(*zoom, ChoiceIndex(*zoom, "130")) == "125" && ChoiceValue(*zoom, ChoiceIndex(*zoom, "999")) == "400",
		      "choices: a number between the steps shows as the nearest step");
		Check(ChoiceIndex(*start, "") == 0 && ChoiceIndex(*start, " ") == 0 && ChoiceValue(*start, ChoiceIndex(*start, "20")) == "20",
		      "choices: an empty start_zoom is the game's own start");
		Check(ChoiceIndex(*key, "f9") == 8 && ChoiceValue(*key, ChoiceIndex(*key, "None")) == "none" && ChoiceIndex(*key, "F13") < 0,
		      "choices: keys F1..F12 and none, any case");
		Check(ChoiceIndex(*comp, "-1") == 0 && ChoiceIndex(*comp, "9") == 9 && ChoiceValue(*steps, ChoiceIndex(*steps, "100")) == "100",
		      "choices: compression -1 (the game's) and 1..9; co-op steps");
		Check(ChoiceIndex(*sky, "game") == 0 && ChoiceIndex(*sky, "NONE") == 1 && ChoiceIndex(*tuning, "0") == 0 &&
		          ChoiceIndex(*tuning, "1") == 1 && ChoiceIndex(*tuning, "") == 1,
		      "choices: a check box is the first value unticked, the second ticked; empty = the default");
		Check(SameValue(*tuning, "1", " 1 ") && SameValue(*key, "F9", "f9") && SameValue(*zoom, "", "100") &&
		          !SameValue(*zoom, "125", "100") && Valid(*zoom, "400") && !Valid(*zoom, "401") && !Valid(*zoom, "abc") &&
		          Valid(*Find("ui", "language"), "pt-br") && !Valid(*Find("ui", "language"), "..\\x") &&
		          Valid(*Find("rmg", "teams"), "1,1,2,2") && !Valid(*Find("rmg", "teams"), "x"),
		      "values: SameValue and Valid follow the mods' rules");
	}
}

void IniTests(const std::string &ini_path)
{
	using mods::settings::Read;
	const std::string tpl_lf = Lf(ReadAll(ini_path));
	const std::string tpl = Crlf(tpl_lf);  // the zip's file has CRLF (make_package.py)

	// a value fills its commented line; back to the default restores the file byte for byte
	const std::string one = Edited(tpl, {Set("net", "tuning", "0")});
	Check(one == Replaced(tpl, "\r\n;tuning=1\r\n", "\r\ntuning=0\r\n"), "ini: a value fills its own commented line (CRLF kept)");
	Check(Edited(one, {Default("net", "tuning", "1")}) == tpl, "ini: back to the default: the file is the shipped one again");
	Check(Edited(tpl_lf, {Set("net", "tuning", "0")}) == Replaced(tpl_lf, "\n;tuning=1\n", "\ntuning=0\n"),
	      "ini: a file with LF line ends keeps them");
	Check(Edited(tpl, {Default("net", "tuning", "1")}) == tpl && Edited(tpl, {}) == tpl, "ini: nothing to change: nothing changes");
	Check(Edited(one, {Set("net", "tuning", "1")}) == Replaced(tpl, "\r\n;tuning=1\r\n", "\r\ntuning=1\r\n"),
	      "ini: Edit writes what it is given (the menu comments a value equal to the default itself)");
	const std::string skins = Edited(tpl, {Set("skins", "key", "F3"), Set("rules", "key", "none")});
	Check(skins == Replaced(Replaced(tpl, "\r\n;key=F7\r\n", "\r\nkey=F3\r\n"), "\r\n;key=F2\r\n", "\r\nkey=none\r\n"),
	      "ini: the same key in two sections: each in its own");
	Check(WindowsReads(skins, L"skins", L"key") == "F3" && WindowsReads(skins, L"rules", L"key") == "none" &&
	          WindowsReads(skins, L"net", L"tuning") == "(none)",
	      "ini: Windows reads the values written, and none for the settings left commented");

	// the odd file of test_scripts.py: a byte-order mark, LF, a setting before [net], upper/lower
	// case and spaces, a second copy, comments that are not settings, a line without "="
	const std::string odd = "\xef\xbb\xbftop=1\n[NET]\n  Tuning = 0  \ntuning=1\n#keepalive_ms=5\n; keepalive_ms=7\nlonely line\n[ai]\nfast_paths=0\n";
	std::string v;
	Check(Read(odd, "net", "tuning", &v) && v == "0" && WindowsReads(odd.substr(3), L"net", L"tuning") == "0",
	      "ini: odd file: the first copy counts, as for Windows");
	Check(Edited(odd, {Set("net", "tuning", "1")}) == Replaced(odd, "  Tuning = 0  \n", "tuning=1\n"),
	      "ini: odd file: a value replaces the first copy, the BOM and LF stay");
	const std::string back = Edited(odd, {Default("net", "tuning", "1")});
	Check(back == Replaced(Replaced(odd, "  Tuning = 0  \n", ";tuning=1\n"), "tuning=1\n#", "#") &&
	          WindowsReads(back.substr(3), L"net", L"tuning") == "(none)",
	      "ini: odd file: back to the default comments the first copy and removes the second (Windows reads none)");
	Check(Edited(odd, {Set("net", "keepalive_ms", "20")}) == Replaced(odd, "lonely line\n", "lonely line\nkeepalive_ms=20\n"),
	      "ini: odd file: no line for the key (\"; keepalive_ms\" with a space is a comment): after the section's last line");
	Check(Edited(odd, {Set("ai", "fast_paths", "1")}) == Replaced(odd, "fast_paths=0\n", "fast_paths=1\n") &&
	          Edited(odd, {Set("ai", "port_relax", "0")}) == odd + "port_relax=0\n",
	      "ini: odd file: the last section, its last line");

	// sections, empty files, the last line
	Check(Edited(tpl, {Set("chain", "dll", "none")}) == Replaced(tpl, "\r\n;dll=dinput8_next.dll\r\n", "\r\ndll=none\r\n"),
	      "ini: the last setting of the file");
	Check(Edited("[net]\r\ntuning=0\r\n", {Set("chain", "dll", "none")}) == "[net]\r\ntuning=0\r\n\r\n[chain]\r\ndll=none\r\n",
	      "ini: a section the file lacks goes at the end");
	Check(Edited("", {Set("net", "tuning", "0")}) == "[net]\r\ntuning=0\r\n" && Edited("", {Default("net", "tuning", "1")}).empty(),
	      "ini: an empty (or missing) file: the section and the value, CRLF");
	Check(Edited("[net]\r\n;tuning=1", {Set("net", "tuning", "0")}) == "[net]\r\ntuning=0" &&
	          Edited("[net]\r\nfoo=1", {Set("net", "bar", "2")}) == "[net]\r\nfoo=1\r\nbar=2" &&
	          Edited("[net]\r\nfoo=1\r\nbar=2", {Default("net", "bar", "")}) == "[net]\r\nfoo=1\r\n;bar=",
	      "ini: a last line without a line ending");
	Check(Edited("[net]\nx=1\n\n[ui]\n[net]\ntuning=0\n", {Set("net", "tuning", "5")}) == "[net]\nx=1\ntuning=5\n\n[ui]\n[net]\ntuning=0\n",
	      "ini: two sections of one name: only the first counts (as for Windows)");
	Check(Edited("[net]\r\n;tuning=1\r\ntuning=0\r\n", {Default("net", "tuning", "1")}) == "[net]\r\n;tuning=1\r\n",
	      "ini: back to the default with the commented line there: the value's line goes");
	Check(Edited(std::string("\xff\xfe[\0n\0", 6), {Set("net", "tuning", "0")}) == "(refused)",
	      "ini: a UTF-16 file (Notepad's \"Unicode\") is refused, left as it is");
}

void MenuDataTests(const std::string &texts_dir)
{
	using namespace mods::settings;
	// a small stand-in for the game's uiconsts file (the real one is the game's: never in the repo)
	const std::string game =
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<UIGameConstsH5>\r\n\t<GameOptions>\r\n\t\t<Categories>\r\n"
	    "\t\t\t<Item>\r\n\t\t\t\t<ID>game</ID>\r\n\t\t\t\t<Pages/>\r\n\t\t\t\t<Enabled>true</Enabled>\r\n\t\t\t</Item>\r\n"
	    "\t\t</Categories>\r\n\t</GameOptions>\r\n\t<TutorialOptions/>\r\n</UIGameConstsH5>\r\n";
	MenuOptions options;
	options.wine = false;
	options.packs = false;
	std::string out;
	Check(AddCategory(game, options, &out), "Options screen: our category goes into the game's list");
	const size_t ours = out.find("<ID>h5together</ID>");
	Check(ours != std::string::npos && ours > out.find("<ID>game</ID>") && ours < out.find("</Categories>") &&
	          out.compare(0, out.find("<ID>game</ID>"), game, 0, game.find("<ID>game</ID>")) == 0 &&
	          out.find("</Categories>\r\n\t</GameOptions>\r\n\t<TutorialOptions/>") != std::string::npos,
	      "Options screen: after the game's categories, the rest of the file as it was");
	int items = 0;
	for (size_t at = out.find("<Type>OPTION_", ours); at != std::string::npos; at = out.find("<Type>OPTION_", at + 1))
		++items;
	Check(out.find("<GlobalVar1>h5t_notify_sound</GlobalVar1>") != std::string::npos &&
	          out.find("<GlobalVar1>h5t_camera_wheel_fix</GlobalVar1>") == std::string::npos &&
	          out.find("<GlobalVar1>h5t_lang_texts</GlobalVar1>") == std::string::npos,
	      "Options screen: our rows, without the Wine-only and the French-packs-only ones when they do not apply");
	Check(out.find("<DisableByCodeGlobalVar>disable_options_by_multiplayer</DisableByCodeGlobalVar>") != std::string::npos,
	      "Options screen: the host's and the advanced rows are read only during a multiplayer game");
	std::string again;
	Check(!AddCategory(out, options, &again), "Options screen: a file that already has our category is left alone");
	Check(!AddCategory("<UIGameConstsH5><Other/></UIGameConstsH5>", options, &again), "Options screen: a file without the list is left alone");
	options.wine = true;
	options.packs = true;
	Check(AddCategory(game, options, &out) && out.find("<GlobalVar1>h5t_camera_wheel_fix</GlobalVar1>") != std::string::npos &&
	          out.find("<GlobalVar1>h5t_lang_texts</GlobalVar1>") != std::string::npos,
	      "Options screen: under Wine and with the French packs, their rows too");
	Check(out.find("<GlobalVar1>h5t_audio_original_music</GlobalVar1>") != std::string::npos &&
	          out.find("<GlobalVar1>h5t_rules_key</GlobalVar1>") != std::string::npos &&
	          out.find("<GlobalVar1>h5t_coop_endless_turns</GlobalVar1>") == std::string::npos &&
	          out.find("<GlobalVar1>h5t_video_hd_textures</GlobalVar1>") == std::string::npos,
	      "Options screen: with MMH5.5, its rows (original music, the rules' key) and no endless turns (its PEST does that) "
	      "nor HD textures (always there)");
	options.mmh55 = false;
	Check(AddCategory(game, options, &out) && out.find("<GlobalVar1>h5t_coop_endless_turns</GlobalVar1>") != std::string::npos &&
	          out.find("<GlobalVar1>h5t_audio_original_music</GlobalVar1>") == std::string::npos &&
	          out.find("<GlobalVar1>h5t_camera_edge_sky</GlobalVar1>") == std::string::npos &&
	          out.find("<GlobalVar1>h5t_coop_script_rhythm</GlobalVar1>") == std::string::npos &&
	          out.find("<GlobalVar1>h5t_rules_key</GlobalVar1>") == std::string::npos &&
	          out.find("<GlobalVar1>h5t_rules_button</GlobalVar1>") == std::string::npos &&
	          out.find("<GlobalVar1>h5t_lang_texts</GlobalVar1>") != std::string::npos &&
	          out.find("<GlobalVar1>h5t_video_hd_textures</GlobalVar1>") != std::string::npos &&
	          out.find("<GlobalVar1>h5t_notify_sound</GlobalVar1>") != std::string::npos,
	      "Options screen: the 2008 game without MMH5.5: endless turns shown, MMH5.5's rows hidden, its own French packs' "
	      "row shown (lang\\fr-original), the others as before");
	printf("      (%d items in our category)\n", items);
	Check(VarName(*Find("notify", "sound")) == "h5t_notify_sound" && VarName(*Find("coop", "segment_ms")) == "h5t_coop_segment_ms",
	      "Options screen: the console variable of each row");
	(void)texts_dir;
}

}  // namespace

int main(int argc, char **argv)
{
	if (argc < 3) {
		printf("usage: settings_test <tools/package/files/h5-together.ini> <src/mods/texts>\n");
		return 2;
	}
	const std::string texts_dir = argv[2];
	texts::Book en, fr;
	Check(texts::Parse(ReadAll(texts_dir + "\\en.txt"), &en) && texts::Parse(ReadAll(texts_dir + "\\fr.txt"), &fr),
	      "en.txt and fr.txt read");
	TableTests(argv[1], en, fr);
	IniTests(argv[1]);
	MenuDataTests(texts_dir);
	printf("%s (%d failed)\n", g_failures ? "SETTINGS TEST FAILED" : "settings test passed", g_failures);
	return g_failures ? 1 : 0;
}
