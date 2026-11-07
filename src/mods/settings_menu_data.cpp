// The settings menu's data for the game's Options screen (see settings_menu_data.h).
#include "settings_menu_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "../hook/texts_logic.h"

namespace mods {
namespace settings {
namespace {

const char kHref[] = "Consts/GameOptions/H5Together/";  // as the uiconsts file names it (from UI/)
const char kCategoryId[] = "h5together";
const char kMpVar[] = "disable_options_by_multiplayer";  // the game's own: 1 during a multiplayer game

std::string Lower(std::string s)
{
	for (char &c : s)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return s;
}

const char *GroupName(Group g)
{
	switch (g) {
	case kInterface: return "interface";
	case kCamera: return "camera";
	case kSound: return "sound";
	case kLanguage: return "language";
	case kRandomMaps: return "random_maps";
	case kOnline: return "online";
	case kHostGroup: return "host";
	case kAdvanced: return "advanced";
	case kHelp: return "help";
	default: return "";
	}
}

// The text keys of a group's header and its tooltip (literal, for tools/lang/ui_texts.py check).
const char *GroupText(Group g, bool help)
{
	switch (g) {
	case kInterface: return help ? "settings.group.interface.help" : "settings.group.interface";
	case kCamera: return help ? "settings.group.camera.help" : "settings.group.camera";
	case kSound: return help ? "settings.group.sound.help" : "settings.group.sound";
	case kLanguage: return help ? "settings.group.language.help" : "settings.group.language";
	case kRandomMaps: return help ? "settings.group.random_maps.help" : "settings.group.random_maps";
	case kOnline: return help ? "settings.group.online.help" : "settings.group.online";
	case kHostGroup: return help ? "settings.group.host.help" : "settings.group.host";
	case kAdvanced: return help ? "settings.group.advanced.help" : "settings.group.advanced";
	case kHelp: return help ? "settings.group.help.help" : "settings.group.help";
	default: return "settings.category";
	}
}

bool Is(const Setting &s, const char *section, const char *key)
{
	return strcmp(s.section, section) == 0 && strcmp(s.key, key) == 0;
}

bool Shown(const Setting &s, const MenuOptions &o)
{
	if (Is(s, "camera", "wheel_fix"))
		return o.wine;
	if (Is(s, "lang", "texts"))
		return o.packs;  // (MMH5.5's packs in lang\fr; the original game's in lang\fr-original)
	// what acts only on MMH5.5's data (its music, its violet sky, its scripts, its rules)
	if (Is(s, "audio", "original_music") || Is(s, "camera", "edge_sky") || Is(s, "coop", "script_rhythm") ||
	    Is(s, "rules", "key") || Is(s, "rules", "button"))
		return o.mmh55;
	// MMH5.5's PEST, for the 2008 game (with MMH5.5, its 55u.dll does it); MMH5.5's HD
	// textures kept in the 2008 game (with MMH5.5 they are always there)
	if (Is(s, "coop", "endless_turns") || Is(s, "video", "hd_textures"))
		return !o.mmh55;
	// the skill wheel's button on the hero screen: MMH5.5 has its own wheel there
	if (Is(s, "ui", "skill_wheel"))
		return !o.mmh55;
	return true;
}

MenuRow Line(Group g, const std::string &file)
{
	MenuRow r;
	r.group = g;
	r.var = file;
	return r;
}

MenuRow Row(const Setting &s, const MenuOptions &o)
{
	MenuRow r;
	r.s = &s;
	r.group = s.group;
	r.var = VarName(s);
	if (s.control == kNone)
		return r;  // a line of words (lan_peers)
	const int count = ChoiceCount(s);
	for (int i = 0; i < count; ++i) {
		MenuItem item;
		item.value = ChoiceValue(s, i);
		item.text = s.kind == kBool ? nullptr : s.kind == kKey ? kKeyValues[i].text : s.values[i].text;
		r.items.push_back(item);
	}
	const std::string name = std::string(s.section) + "." + s.key;
	if (name == "ui.language")
		for (const std::string &code : o.languages)
			r.items.push_back({code, nullptr});
	for (const auto &e : o.extra)
		if (e.first == name && ChoiceIndex(s, e.second) < 0 && Valid(s, e.second))
			r.items.push_back({e.second, nullptr});
	r.def = ChoiceIndex(s, s.def);
	if (r.def < 0)
		r.def = 0;
	return r;
}

void Add(std::string *x, int tabs, const std::string &line, const std::string &eol)
{
	x->append(static_cast<size_t>(tabs), '\t');
	*x += line;
	*x += eol;
}

std::string Ref(const char *tag, const std::string &file)
{
	return "<" + std::string(tag) + " href=\"" + kHref + file + "\"/>";
}

const char *TypeOf(const MenuRow &r)
{
	if (!r.s || r.s->control == kNone)
		return "OPTION_BLOCK_HEADER";
	return r.s->control == kCheck ? "OPTION_CHECK_BOX" : r.s->control == kSlider ? "OPTION_SCROLLER" : "OPTION_COMBO_BOX";
}

void OptionXml(std::string *x, const MenuRow &r, const std::string &eol)
{
	const int t = 7;
	const bool header = !r.s || r.s->control == kNone;
	const bool mp = r.s && (r.group == kHostGroup || r.group == kAdvanced || r.group == kOnline);  // read only during a multiplayer game
	Add(x, t, "<Item>", eol);
	Add(x, t + 1, header ? std::string("<ID/>") : "<ID>" + r.var + "</ID>", eol);
	Add(x, t + 1, std::string("<Type>") + TypeOf(r) + "</Type>", eol);
	Add(x, t + 1, Ref("NameFileRef", r.var + "/Name.txt"), eol);
	Add(x, t + 1, Ref("TooltipFileRef", r.var + "/Tooltip.txt"), eol);
	if (header || r.s->control == kCheck)
		Add(x, t + 1, "<Items/>", eol);
	else {
		Add(x, t + 1, "<Items>", eol);
		for (size_t i = 0; i < r.items.size(); ++i) {
			char file[32];
			snprintf(file, sizeof(file), "/Item%02u.txt", static_cast<unsigned>(i));
			Add(x, t + 2, "<Item>", eol);
			Add(x, t + 3, Ref("NameFileRef", r.var + file), eol);
			Add(x, t + 3, "<Value>" + std::to_string(i) + "</Value>", eol);
			Add(x, t + 2, "</Item>", eol);
		}
		Add(x, t + 1, "</Items>", eol);
	}
	Add(x, t + 1, "<ItemsGlobalVar/>", eol);
	Add(x, t + 1, "<UseItemNameAsValue>false</UseItemNameAsValue>", eol);
	Add(x, t + 1, header ? std::string("<GlobalVar1/>") : "<GlobalVar1>" + r.var + "</GlobalVar1>", eol);
	Add(x, t + 1, "<GlobalVar2/>", eol);
	Add(x, t + 1, "<DefaultValue1>" + std::to_string(header ? 0 : r.def) + "</DefaultValue1>", eol);
	Add(x, t + 1, "<DefaultValue2>0</DefaultValue2>", eol);
	Add(x, t + 1, "<OptionToDisableID/>", eol);
	Add(x, t + 1, "<OptionDisableByValue1>0</OptionDisableByValue1>", eol);
	Add(x, t + 1, "<OptionToDisableValue1>0</OptionToDisableValue1>", eol);
	Add(x, t + 1, mp ? std::string("<DisableByCodeGlobalVar>") + kMpVar + "</DisableByCodeGlobalVar>" : "<DisableByCodeGlobalVar/>", eol);
	if (!header && r.s->control == kSlider) {
		Add(x, t + 1, "<LabelsFileRef>", eol);
		Add(x, t + 2, "<Item href=\"" + std::string(kHref) + r.var + "/Label00.txt\"/>", eol);
		Add(x, t + 2, "<Item href=\"" + std::string(kHref) + r.var + "/Label01.txt\"/>", eol);
		Add(x, t + 1, "</LabelsFileRef>", eol);
	}
	else
		Add(x, t + 1, "<LabelsFileRef/>", eol);
	Add(x, t + 1, "<Hide>false</Hide>", eol);
	Add(x, t + 1, "<Global>false</Global>", eol);
	Add(x, t + 1, "<Group/>", eol);
	Add(x, t + 1, "<Preview>false</Preview>", eol);
	Add(x, t, "</Item>", eol);
}

std::string Fill(const char *text, std::initializer_list<texts::Arg> args)
{
	return texts::Fill(text ? text : "", args);
}

// "Applies after a restart of the game." and the like, for a tooltip.
std::string WhenWords(const Setting &s, GetText get)
{
	std::string out;
	switch (s.when) {
	case kRestart: out = get("settings.when.restart"); break;
	case kNextGame: out = get("settings.when.next_game"); break;
	case kLive: out = get(strcmp(s.section, "camera") == 0 ? "settings.when.next_map" : "settings.when.live"); break;
	default: break;
	}
	if (s.who == kHost)
		out += std::string(" ") + get("settings.who.host");
	else if (s.who == kGroup)
		out += std::string(" ") + get("settings.who.group");
	return out;
}

// The rows' order in the menu, group by group (a row the list lacks comes after, in the table's order).
struct Name {
	const char *section, *key;
};
const Name kOrder[] = {
	{"ui", "player_list"}, {"ui", "player_list_key"}, {"ui", "player_list_top"}, {"ui", "player_list_left"},
	{"notify", "turn"}, {"notify", "sound"}, {"notify", "popup"}, {"coop", "share_view"}, {"coop", "watch_battles"}, {"coop", "stay_after_defeat"}, {"rules", "key"}, {"rules", "button"},
	{"ui", "hero_tips"},
	{"skins", "skins"}, {"skins", "key"}, {"skins", "hero_screen_button"}, {"ui", "skill_wheel"}, {"ui", "skill_wheel_key"}, {"skins", "show_others"},
	{"camera", "zoom_out"}, {"camera", "combat_zoom_out"}, {"camera", "start_zoom"}, {"camera", "edge_sky"}, {"camera", "anim_saver"}, {"video", "fps_limit"}, {"video", "vsync"}, {"menu", "scene"},
	{"camera", "wheel_fix"}, {"audio", "original_music"}, {"audio", "volume_curve"}, {"ui", "language"},
	{"lang", "texts"}, {"rmg", "builder"}, {"rmg", "builder_key"}, {"rmg", "teams"}, {"rmg", "lobby_teams"},
	{"rmg", "recipe_teams"}, {"rmg", "faction_terrain"}, {"coop", "undo_end_turn"}, {"coop", "endless_turns"}, {"coop", "script_rhythm"}, {"ai", "parallel_turns"}, {"coop", "resume_save"}, {"coop", "guard"}, {"coop", "segment_ms"},
	{"net", "view_delay_ms"}, {"net", "tuning"}, {"ai", "fast_paths"}, {"net", "lan_all_adapters"}, {"net", "lan_peers"},
	{"save", "fast_save_list"}, {"save", "game_names"}, {"save", "compression"}, {"save", "autosave_every"}, {"rmg", "templates"},
	{"rules", "enabled"}, {"skins", "live_hero_screen"}, {"debug", "hang_watchdog"}, {"debug", "hang_seconds"},
	{"debug", "crash_report"}, {"chain", "dll"}, {"update", "check"}, {"online", "mode"},
};

bool Starts(const std::string &s, const char *prefix)
{
	return s.compare(0, strlen(prefix), prefix) == 0;
}

}  // namespace

const char kTextFolder[] = "ui/consts/gameoptions/h5together/";

std::string VarName(const Setting &s)
{
	return std::string("h5t_") + s.section + "_" + s.key;
}

std::vector<MenuRow> MenuRows(const MenuOptions &o, int page)
{
	std::vector<MenuRow> out;
	out.push_back(Line(page == 1 ? kInterface : kHostGroup, page == 1 ? "info_legend1" : "info_legend2"));
	for (int g = kInterface; g < kGroupCount; ++g) {
		const Group group = static_cast<Group>(g);
		if (PageOf(group) != page)
			continue;
		out.push_back(Line(group, std::string("group_") + GroupName(group)));
		if (group == kAdvanced)
			out.push_back(Line(group, "info_same"));
		if (group == kHelp)
			out.push_back(Line(group, "info_version"));
		// the menu's order (kOrder), then any other row of the group in the table's order
		std::vector<const Setting *> rows;
		for (const Name &name : kOrder) {
			const Setting *s = Find(name.section, name.key);
			if (s && s->group == group)
				rows.push_back(s);
		}
		for (int i = 0; i < kSettingCount; ++i)
			if (kSettings[i].group == group && std::find(rows.begin(), rows.end(), &kSettings[i]) == rows.end())
				rows.push_back(&kSettings[i]);
		for (const Setting *s : rows)
			if (s->text && Shown(*s, o))
				out.push_back(Row(*s, o));
		if (group == kHostGroup && o.mmh55)
			out.push_back(Line(group, "info_rules"));
		if (group == kOnline)
			out.push_back(Line(group, "info_online"));
		if (group == kHelp)
			out.push_back(Line(group, "info_file"));
	}
	return out;
}

bool AddCategory(const std::string &game_file, const MenuOptions &o, std::string *out)
{
	const size_t options = game_file.find("<GameOptions>");
	const size_t list_end = options == std::string::npos ? std::string::npos : game_file.find("</Categories>", options);
	const size_t options_end = game_file.find("</GameOptions>", options == std::string::npos ? 0 : options);
	if (options == std::string::npos || list_end == std::string::npos || options_end == std::string::npos || list_end > options_end)
		return false;
	if (game_file.find(std::string("<ID>") + kCategoryId + "</ID>") != std::string::npos)
		return false;
	// the line of "</Categories>" starts at the line's first tab
	size_t at = list_end;
	while (at > 0 && game_file[at - 1] == '\t')
		--at;
	const size_t nl = game_file.find('\n', options);
	const std::string eol = nl != std::string::npos && nl > 0 && game_file[nl - 1] == '\r' ? "\r\n" : "\n";
	std::string x;
	Add(&x, 3, "<Item>", eol);
	Add(&x, 4, std::string("<ID>") + kCategoryId + "</ID>", eol);
	Add(&x, 4, Ref("MenuButtonNameFileRef", "Category.txt"), eol);
	Add(&x, 4, Ref("NameFileRef", "CategoryName.txt"), eol);
	Add(&x, 4, Ref("TooltipFileRef", "CategoryTooltip.txt"), eol);
	Add(&x, 4, "<Pages>", eol);
	for (int page = 1; page <= 2; ++page) {
		const std::string n = std::to_string(page);
		Add(&x, 5, "<Item>", eol);
		Add(&x, 6, std::string("<ID>") + kCategoryId + "_" + n + "</ID>", eol);
		Add(&x, 6, Ref("NameFileRef", "Page" + n + ".txt"), eol);
		Add(&x, 6, Ref("TooltipFileRef", "Page" + n + "Tooltip.txt"), eol);
		Add(&x, 6, "<Options>", eol);
		for (const MenuRow &r : MenuRows(o, page))
			OptionXml(&x, r, eol);
		Add(&x, 6, "</Options>", eol);
		Add(&x, 5, "</Item>", eol);
	}
	Add(&x, 4, "</Pages>", eol);
	Add(&x, 4, "<Enabled>true</Enabled>", eol);
	Add(&x, 3, "</Item>", eol);
	*out = game_file.substr(0, at) + x + game_file.substr(at);
	return true;
}

std::string ItemWords(const Setting &s, const MenuItem &item, GetText get)
{
	if (item.text)
		return get(item.text);
	if (s.unit && !item.value.empty())
		return Fill(get(s.unit), {{"n", item.value}});
	return item.value;
}

bool MenuText(const std::string &raw_name, const MenuOptions &o, GetText get, const std::string &version,
              const std::string &mmh55, const std::string &lan_peers, std::string *out)
{
	const std::string name = Lower(raw_name);
	const char *const kPages[2][2] = {{"settings.page1", "settings.page1.help"}, {"settings.page2", "settings.page2.help"}};
	if (name == "category.txt" || name == "categoryname.txt") {
		*out = get(name == "category.txt" ? "settings.category" : "settings.category.title");
		return true;
	}
	if (name == "categorytooltip.txt") {
		*out = get("settings.category.help");
		return true;
	}
	for (int p = 0; p < 2; ++p) {
		const std::string n = std::to_string(p + 1);
		if (name == "page" + n + ".txt" || name == "page" + n + "tooltip.txt") {
			*out = get(kPages[p][name == "page" + n + ".txt" ? 0 : 1]);
			return true;
		}
	}
	const size_t slash = name.find('/');
	if (slash == std::string::npos)
		return false;
	const std::string stem = name.substr(0, slash), file = name.substr(slash + 1);
	const bool tooltip = file == "tooltip.txt";
	if (file != "name.txt" && !tooltip && !Starts(file, "item") && !Starts(file, "label"))
		return false;
	// a header, or a line of words
	if (Starts(stem, "group_")) {
		for (int g = kInterface; g < kGroupCount; ++g)
			if (stem == std::string("group_") + GroupName(static_cast<Group>(g))) {
				*out = get(GroupText(static_cast<Group>(g), tooltip));
				return true;
			}
		return false;
	}
	if (Starts(stem, "info_")) {
		// a line of words in a page: the body's look, not a header's
		const std::string body = "<body>";
		if (stem == "info_version")
			*out = tooltip ? get("settings.info.version.help")
			               : body + Fill(get("settings.info.version"), {{"version", version}, {"mmh55", mmh55}});
		else if (stem == "info_file")
			*out = tooltip ? get("settings.info.file.help") : body + get("settings.info.file");
		else if (stem == "info_rules")
			*out = tooltip ? get("settings.info.rules.help") : body + get("settings.info.rules");
		else if (stem == "info_same")
			*out = tooltip ? get("settings.info.same.help") : body + get("settings.info.same");
		else if (stem == "info_online" && o.steam_dll)  // where friends' games show
			*out = tooltip ? get("settings.info.online.help") : body + get("settings.info.online");
		else if (stem == "info_online")  // no steam_api.dll: say so, plainly
			*out = tooltip ? get("settings.info.online_no_dll.help") : body + get("settings.info.online_no_dll");
		else if (stem == "info_legend1")
			*out = tooltip ? get("settings.when.restart") : body + get("settings.mark.restart");
		else if (stem == "info_legend2")
			*out = tooltip ? get("settings.when.next_game") : body + get("settings.mark.next_game");
		else
			return false;
		return true;
	}
	// a setting's row
	for (int i = 0; i < kSettingCount; ++i) {
		const Setting &s = kSettings[i];
		if (!s.text || Lower(VarName(s)) != stem)
			continue;
		if (s.control == kNone) {  // lan_peers: its value, set in the file
			*out = tooltip ? std::string(get(s.help)) + "<br>" + Fill(get("settings.file_line"), {{"section", s.section}, {"key", s.key}})
			               : "<body>" + Fill(get(s.text), {{"peers", lan_peers.empty() ? get("settings.value.no_peers") : lan_peers}});
			return true;
		}
		if (file == "name.txt") {
			// the game's rows are narrow: a mark, explained by the line at the top of the page
			*out = std::string(get(s.text)) + (s.when == kRestart ? " *" : s.when == kNextGame ? " **" : "");
			return true;
		}
		if (tooltip) {
			const bool steps = strcmp(s.section, "coop") == 0 && strcmp(s.key, "segment_ms") == 0;
			*out = std::string(get(s.help)) + "<br>" + WhenWords(s, get) + "<br>" +
			       Fill(get("settings.file_line"), {{"section", s.section}, {"key", steps ? "fast_segments, segment_ms" : s.key}});
			return true;
		}
		const MenuRow row = Row(s, o);
		if (Starts(file, "item")) {
			const int index = atoi(file.c_str() + 4);
			if (index < 0 || index >= static_cast<int>(row.items.size()))
				return false;
			const MenuItem &item = row.items[static_cast<size_t>(index)];
			*out = ItemWords(s, item, get);
			if (!item.text && strcmp(s.section, "ui") == 0 && strcmp(s.key, "language") == 0)
				for (const auto &n : o.names)
					if (n.first == item.value && !n.second.empty())
						*out = n.second;
			return true;
		}
		if (file == "label00.txt" || file == "label01.txt") {
			if (row.items.empty())
				return false;
			*out = ItemWords(s, file == "label00.txt" ? row.items.front() : row.items.back(), get);
			return true;
		}
		return false;
	}
	return false;
}

std::string GameText(const std::string &utf8)
{
	std::string out("\xff\xfe", 2);
	const unsigned char *p = reinterpret_cast<const unsigned char *>(utf8.c_str());
	while (*p) {
		unsigned code = *p++;
		if (code >= 0xc0 && code < 0xe0 && (*p & 0xc0) == 0x80)
			code = ((code & 0x1f) << 6) | (*p++ & 0x3f);
		else if (code >= 0xe0 && code < 0xf0 && (p[0] & 0xc0) == 0x80 && (p[1] & 0xc0) == 0x80) {
			code = ((code & 0x0f) << 12) | ((p[0] & 0x3f) << 6) | (p[1] & 0x3f);
			p += 2;
		}
		else if (code >= 0x80)
			code = '?';
		if (code == '\n') {  // the game's own line break
			out.append("<\0b\0r\0>\0", 8);
			continue;
		}
		out += static_cast<char>(code & 0xff);
		out += static_cast<char>(code >> 8);
	}
	return out;
}

}  // namespace settings
}  // namespace mods
