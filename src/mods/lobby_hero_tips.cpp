// Mod: the waiting room's hero tooltip also tells the hero's class, starting skills and
// specialty ([ui] hero_tips=1, each player's own; the owner's wish, 2026-11-06: "when selecting a
// hero on map launch, to have his special skill + class").
//
// In plain words: in the multiplayer waiting room, the tooltip of a row's hero picture says
// "Starting hero: <name>, <specialty>" (the game's UI/MPWait/PlayersList/Item/
// HeroToolTipFormat.txt). With this, three more lines: the hero's class, its starting skills
// and abilities (with their mastery), and what its specialty does, all in the game's own words
// (its data and texts), so in the game's language.
//
// How:
//   - A row (NUI::CMPWaitItem, vftable 0x00e39ce8) keeps its tooltips' templates in five
//     tables at +0x108 (hash_map<int, wstring>, 0x18 bytes each; a node is {next, key,
//     wstring}; host_rules_bonus.cpp changes the fourth). The hero's is the one whose key 0
//     holds "<value=heroname>" (the game fills <value=heroname> and
//     <value=herospecialisation> when it shows a hero).
//   - The row's refresh, 0x00713fe0 (thiscall(row, int), ret 4; called at 0x00714a55 and
//     0x0077e524 on every change of the row: hero arrows, players...), gives the hero icon
//     (+0x98) its tooltip from that template. Both calls are wrapped: just before the game's
//     refresh, the row's own copy of the template becomes the game's text plus our lines for
//     that row's hero, or the game's text alone when the hero is random.
//   - The row's hero: the row keeps it at +0x88 (a database pointer, the same on the host and
//     on the PCs that join; the game setup's own slots, NWorld::CPlayersStartInfo, are not
//     readable the same way on a PC that joins: robot, 2026-11-06), and its database entry
//     holds the hero file's path (RowHeroFile).
//   - The hero's file gives its class (HERO_CLASS_KNIGHT), specialty texts, skills (with
//     their mastery) and abilities (perkIDs); GameMechanics/RefTables/HeroClass.xdb and
//     Skills.xdb give their names' text files.
#include "lobby_hero_tips.h"

#include <windows.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "../hook/log.h"
#include "../hook/overlay.h"
#include "../hook/patch.h"
#include "../hook/texts.h"

namespace mods {
namespace {

const DWORD kRefreshCalls[] = {0x00714a55, 0x0077e524};
const DWORD kRefresh = 0x00713fe0;           // NUI::CMPWaitItem: its icons' tooltips, thiscall(row, int), ret 4
const DWORD kWaitItemVftable = 0x00e39ce8;   // NUI::CMPWaitItem
const DWORD kRowTables = 0x108;              // vector of 5 hash_map<int, wstring> (0x18 bytes each)
const DWORD kAssign = 0x0040e960;            // nstl::wstring::assign(first, last): thiscall, ret 8
const DWORD kRowHero = 0x88;                 // the row's hero: CDBPtr<NDb::AdvMapHeroShared> (entry, flag)
const DWORD kEntryPath = 0x20;               // the entry's database id (+0xc): its path (a string: begin, end)

struct WString {  // nstl::wstring
	wchar_t *begin;
	wchar_t *end;
	wchar_t *cap;
};

typedef void(__fastcall *RefreshFn)(void *row, void *edx, int flag);
typedef void *(__fastcall *AssignFn)(WString *self, void *edx, const wchar_t *first, const wchar_t *last);

RefreshFn g_refresh_next = nullptr;
std::wstring g_base;                          // the game's own template (the first one seen)
std::vector<std::wstring> g_written;          // every template we gave a row
std::map<std::string, std::wstring> g_lines;  // our lines, by hero file
bool g_tables_read = false;
std::map<std::string, std::string> g_class_names;              // HERO_CLASS_KNIGHT -> its name's text file
std::map<std::string, std::vector<std::string>> g_skill_names; // HERO_SKILL_X -> its names' text files (by mastery)
int g_told = 0;

bool Readable(const void *p, size_t n)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (!p || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
		return false;
	return static_cast<const BYTE *>(p) + n <= static_cast<const BYTE *>(mbi.BaseAddress) + mbi.RegionSize;
}

DWORD Read(const void *base, DWORD offset)
{
	const BYTE *p = static_cast<const BYTE *>(base) + offset;
	return Readable(p, 4) ? *reinterpret_cast<const DWORD *>(p) : 0;
}

// --- the game's data and texts --------------------------------------------------------------------
std::string FileText(const std::string &path)
{
	std::string raw;
	std::string p = path;
	const size_t hash = p.find('#');
	if (hash != std::string::npos)
		p.resize(hash);
	return overlay::ReadGameFile(p.c_str(), &raw) ? raw : std::string();
}

// A game text file (UTF-16), its lines joined with a space (the tooltip's lines are <br>);
// empty when there is none.
std::wstring GameText(const std::string &path)
{
	const std::string raw = FileText(path);
	std::wstring out;
	size_t at = raw.size() >= 2 && raw[0] == '\xff' && raw[1] == '\xfe' ? 2 : 0;
	for (; at + 1 < raw.size(); at += 2) {
		const wchar_t c = static_cast<wchar_t>(static_cast<unsigned char>(raw[at]) | static_cast<unsigned char>(raw[at + 1]) << 8);
		if (c == L'\n')
			out += L' ';
		else if (c != L'\r')
			out += c;
	}
	while (!out.empty() && (out.back() == L'\n' || out.back() == L' '))
		out.pop_back();
	return out;
}

// The text of <tag>...</tag> after `from` (and before `until`), else "".
std::string Value(const std::string &xml, const char *tag, size_t from = 0, size_t until = std::string::npos)
{
	const std::string open = std::string("<") + tag + ">";
	const size_t a = xml.find(open, from);
	if (a == std::string::npos || a >= until)
		return std::string();
	const size_t b = xml.find('<', a + open.size());
	return b == std::string::npos ? std::string() : xml.substr(a + open.size(), b - a - open.size());
}

// The href of <tag href="..."/> after `from` (and before `until`), else "".
std::string Href(const std::string &xml, const char *tag, size_t from = 0, size_t until = std::string::npos)
{
	const std::string open = std::string("<") + tag + " href=\"";
	const size_t a = xml.find(open, from);
	if (a == std::string::npos || a >= until)
		return std::string();
	const size_t b = xml.find('"', a + open.size());
	return b == std::string::npos ? std::string() : xml.substr(a + open.size(), b - a - open.size());
}

// A link relative to the file it is in, made a full path ("/GameMechanics/RefTables/" + ...).
std::string Resolve(const std::string &href, const std::string &folder)
{
	if (href.empty() || href[0] == '/')
		return href;
	return folder + href;
}

void ReadTables()
{
	if (g_tables_read)
		return;
	g_tables_read = true;
	const std::string folder = "/GameMechanics/RefTables/";
	const std::string classes = FileText(folder + "HeroClass.xdb");
	for (size_t at = classes.find("<ID>"); at != std::string::npos; at = classes.find("<ID>", at + 4)) {
		const std::string id = Value(classes, "ID", at);
		const size_t end = classes.find("</obj>", at);
		const std::string name = Href(classes, "NameFileRef", at, end);
		if (!id.empty() && !name.empty())
			g_class_names[id] = Resolve(name, folder);
	}
	const std::string skills = FileText(folder + "Skills.xdb");
	for (size_t at = skills.find("<ID>"); at != std::string::npos; at = skills.find("<ID>", at + 4)) {
		const std::string id = Value(skills, "ID", at);
		const size_t end = skills.find("</obj>", at);
		const size_t names = skills.find("<NameFileRef>", at);
		std::vector<std::string> refs;
		if (names != std::string::npos && names < end) {
			const size_t close = skills.find("</NameFileRef>", names);
			for (size_t i = skills.find("<Item href=\"", names); i != std::string::npos && i < close;
			     i = skills.find("<Item href=\"", i + 1)) {
				const size_t q = skills.find('"', i + 12);
				refs.push_back(Resolve(skills.substr(i + 12, q - i - 12), folder));
			}
		}
		else {
			const std::string one = Href(skills, "NameFileRef", at, end);
			if (!one.empty())
				refs.push_back(Resolve(one, folder));
		}
		if (!id.empty() && !refs.empty())
			g_skill_names[id] = refs;
	}
	hooklog::Write("lobby hero tips: %d classes and %d skills and abilities named (the game's tables)",
	               static_cast<int>(g_class_names.size()), static_cast<int>(g_skill_names.size()));
}

std::wstring SkillName(const std::string &id, int mastery)
{
	auto it = g_skill_names.find(id);
	if (it == g_skill_names.end() || it->second.empty())
		return std::wstring();
	const size_t i = mastery >= 1 && static_cast<size_t>(mastery) <= it->second.size() ? static_cast<size_t>(mastery) - 1 : 0;
	return GameText(it->second[i]);
}

// Our lines for a hero (its file): "<br>Class: ...<br>Starting skills: ...<br><br><what its specialty does>".
std::wstring Lines(const std::string &hero_file)
{
	auto it = g_lines.find(hero_file);
	if (it != g_lines.end())
		return it->second;
	ReadTables();
	const std::string xml = FileText(hero_file);
	std::wstring lines;
	if (!xml.empty()) {
		auto cls = g_class_names.find(Value(xml, "Class"));
		const std::wstring class_name = cls != g_class_names.end() ? GameText(cls->second) : std::wstring();
		std::wstring skills;
		const size_t editable = xml.find("<Editable>");
		const size_t skills_at = xml.find("<skills>", editable == std::string::npos ? 0 : editable);
		const size_t skills_end = skills_at == std::string::npos ? std::string::npos : xml.find("</skills>", skills_at);
		for (size_t at = skills_at == std::string::npos ? std::string::npos : xml.find("<Item>", skills_at);
		     at != std::string::npos && at < skills_end; at = xml.find("<Item>", at + 6)) {
			const std::string m = Value(xml, "Mastery", at, skills_end);
			const int mastery = m == "MASTERY_EXPERT" ? 3 : m == "MASTERY_ADVANCED" ? 2 : 1;
			const std::wstring name = SkillName(Value(xml, "SkillID", at, skills_end), mastery);
			if (!name.empty())
				skills += (skills.empty() ? L"" : L", ") + name;
		}
		const size_t perks_at = xml.find("<perkIDs>", editable == std::string::npos ? 0 : editable);
		const size_t perks_end = perks_at == std::string::npos ? std::string::npos : xml.find("</perkIDs>", perks_at);
		for (size_t at = perks_at == std::string::npos ? std::string::npos : xml.find("<Item>", perks_at);
		     at != std::string::npos && at < perks_end; at = xml.find("<Item>", at + 6)) {
			const std::wstring name = SkillName(Value(xml, "Item", at, perks_end), 1);
			if (!name.empty())
				skills += (skills.empty() ? L"" : L", ") + name;
		}
		const std::wstring speciality = GameText(Href(xml, "SpecializationDescFileRef"));
		if (!class_name.empty())
			lines += L"<br>" + texts::Wide(texts::Format("lobby.hero_class", {{"class", texts::Narrow(class_name)}}));
		if (!skills.empty())
			lines += L"<br>" + texts::Wide(texts::Format("lobby.hero_skills", {{"skills", texts::Narrow(skills)}}));
		if (!speciality.empty())
			lines += L"<br><br>" + speciality;
	}
	g_lines[hero_file] = lines;
	if (g_told++ < 20)
		hooklog::Write("lobby hero tips: %s: %d character(s) added to its tooltip", hero_file.c_str(), static_cast<int>(lines.size()));
	return lines;
}

// --- the row --------------------------------------------------------------------------------------
// The row's hero template (key 0 of the table whose key 0 holds "<value=heroname>"), else nullptr.
WString *HeroTemplate(BYTE *row)
{
	const BYTE *tables = reinterpret_cast<const BYTE *>(Read(row, kRowTables));
	if (!tables)
		return nullptr;
	for (int t = 0; t < 5; ++t) {
		const BYTE *map = tables + t * 0x18;
		if (!Readable(map, 0xc))
			return nullptr;
		const DWORD b = Read(map, 4), e = Read(map, 8);
		const DWORD buckets = e > b ? (e - b) / 4 : 0;
		if (buckets == 0 || buckets > 4096 || !Readable(reinterpret_cast<const void *>(b), buckets * 4))
			continue;
		const BYTE *node = reinterpret_cast<const BYTE *>(Read(reinterpret_cast<const void *>(b), 0));
		for (int steps = 0; node && steps < 64; ++steps) {
			if (!Readable(node, 0x14))
				break;
			if (Read(node, 4) == 0) {
				WString *s = reinterpret_cast<WString *>(const_cast<BYTE *>(node) + 8);
				if (s->end >= s->begin && s->end - s->begin < 8192 &&
				    (s->end == s->begin || Readable(s->begin, (s->end - s->begin) * sizeof(wchar_t)))) {
					static const wchar_t kMark[] = L"<value=heroname>";
					const size_t n = static_cast<size_t>(s->end - s->begin), m = wcslen(kMark);
					for (size_t i = 0; i + m <= n; ++i)
						if (wcsncmp(s->begin + i, kMark, m) == 0)
							return s;
				}
				break;
			}
			node = reinterpret_cast<const BYTE *>(Read(node, 0));
		}
	}
	return nullptr;
}

// The row's hero file, else "" (a random hero). The row keeps its hero at +0x88 (a
// CDBPtr<NDb::AdvMapHeroShared>: the database entry, then a flag), on every PC; the refresh
// reads it there (0x004a6740 on row+0x88, then the hero's name and specialty). The entry
// holds the hero's database id at +0xc (0x009b0f60 asks the database for it), a type then
// a path, as the game setup's slots hold theirs (type +0x2c, path +0x40): so the path at
// +0x20 ("/MapObjects/Haven/Brem.(AdvMapHeroShared).xdb#xpointer(/AdvMapHeroShared)").
bool RowHeroFile(BYTE *row, char *out, size_t size)
{
	out[0] = 0;
	const BYTE *entry = reinterpret_cast<const BYTE *>(Read(row, kRowHero));
	if (!entry)
		return true;
	const char *b = reinterpret_cast<const char *>(Read(entry, kEntryPath));
	const char *e = reinterpret_cast<const char *>(Read(entry, kEntryPath + 4));
	if (!b || e <= b || static_cast<size_t>(e - b) >= size || !Readable(b, e - b)) {
		static int s_told = 0;
		if (s_told++ < 2)
			hooklog::Write("lobby hero tips: a row's hero entry is not as expected (%08lx %08lx %08lx %08lx %08lx %08lx "
			               "%08lx %08lx %08lx %08lx %08lx %08lx)",
			               Read(entry, 0), Read(entry, 4), Read(entry, 8), Read(entry, 0xc), Read(entry, 0x10),
			               Read(entry, 0x14), Read(entry, 0x18), Read(entry, 0x1c), Read(entry, 0x20), Read(entry, 0x24),
			               Read(entry, 0x28), Read(entry, 0x2c));
		return false;
	}
	memcpy(out, b, e - b);
	out[e - b] = 0;
	return strstr(out, ".xdb") != nullptr;
}

// The facts of a row, read under a guard (the game's objects).
bool RowFacts(BYTE *row, WString **tpl, char *hero, size_t size)
{
	__try {
		if (Read(row, 0) != kWaitItemVftable)
			return false;
		*tpl = HeroTemplate(row);
		return *tpl && RowHeroFile(row, hero, size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool Assign(WString *text, const std::wstring &value)
{
	__try {
		reinterpret_cast<AssignFn>(kAssign)(text, nullptr, value.c_str(), value.c_str() + value.size());
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool Ours(const std::wstring &text)
{
	for (const std::wstring &w : g_written)
		if (w == text)
			return true;
	return false;
}

// Just before the game's refresh of a row: its hero template, for its hero.
void Prepare(BYTE *row)
{
	WString *tpl = nullptr;
	char hero[300];
	if (!RowFacts(row, &tpl, hero, sizeof(hero)))
		return;
	const std::wstring now(tpl->begin, tpl->end);
	if (g_base.empty() && !Ours(now))
		g_base = now;
	if (g_base.empty())
		return;
	const std::wstring want = g_base + (hero[0] ? Lines(hero) : std::wstring());
	if (now == want)
		return;
	if (g_written.size() < 256 && !Ours(want))
		g_written.push_back(want);
	static int s_logged = 0;
	if (Assign(tpl, want) && s_logged++ < 8)
		hooklog::Write("lobby hero tips: a row's hero tooltip: %s", texts::Narrow(want).c_str());
}

void __fastcall RefreshHook(void *row, void *edx, int flag)
{
	Prepare(static_cast<BYTE *>(row));
	g_refresh_next(row, edx, flag);
}

}  // namespace

void ApplyLobbyHeroTipsFromSettings()
{
	wchar_t ini[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together", L".ini", ini, MAX_PATH);
	if (GetPrivateProfileIntW(L"ui", L"hero_tips", 1, ini) == 0) {
		hooklog::Write("lobby hero tips: off ([ui] hero_tips=0): the game's own hero tooltip");
		return;
	}
	g_refresh_next = reinterpret_cast<RefreshFn>(
		hookpatch::RedirectCalls(kRefreshCalls, 2, kRefresh, reinterpret_cast<const void *>(&RefreshHook)));
	hooklog::Write(g_refresh_next ? "lobby hero tips: on: the waiting room's hero tooltip also tells the class, the starting skills "
	                                "and the specialty"
	                              : "lobby hero tips: the game's code is not as expected: off");
}

}  // namespace mods
