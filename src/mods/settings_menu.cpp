// Mod: the settings menu (docs/plans/community-release.md, part 2): an
// "H5 Together" category in the game's own Options screen.
//
// The Options screen (NUI::CGameOptionsScreen, vftable 0x00e3da1c) is built from
// the "GameOptions" part of UI/uiconsts.(UIGameConstsH5).xdb (settings_menu_data.h):
// a button per category on the screen's scroll (Game, Video & audio, then ours),
// and per category two pages side by side, each a list of the game's own rows.
// When the game opens that file through its file system (NVFS::CWinVFS slot 7,
// which also reads the paks), we serve it with our category at the end of the
// list, and our words as the game's own text files (UI/Consts/GameOptions/H5Together/).
// Nothing on disk changes.
//
// Each of our rows shows and sets a console variable of ours (h5t_<section>_<key>,
// the index of the row's choice), through the game's own code:
//   - 0x00c11860 __fastcall(const nstl::string *name, const value *fallback): the
//     variable's value {float, wide string}, or the fallback when there is none;
//   - 0x00c11900 __fastcall(name, const value *, int level), ret 4: sets it (and
//     makes it); level 2 = saved in the profile's user_a2.cfg (what the screen's
//     Accept asks for), 0 = not saved. The level is the int just before the value.
// When a category's button is pressed (the screen's command "template",
// 0x0075cea0 -> 0x0075c1a0 -> 0x0075c030, which reads every row's variable), we
// first set our variables from h5-together.ini. Accept (command "accept",
// 0x0075c8e0) and Default (the "restore the defaults?" box's Yes, 0x0075c680)
// write the shown category's variables and then save the profile (call 0x008353e0
// at 0x0075ca80 and 0x0075c70e): that call goes through us first, and we write
// the variables that changed into h5-together.ini (settings_ini.h: in place, the
// installer's way), make the live ones live, put their level back to 0 (so the
// game's profile file never holds them), and show a line about the ones that wait
// for a restart or the next game. Back (command "options_back") writes nothing.
//
// The rows of the host and of the advanced settings use the game's own
// disable_options_by_multiplayer, so they are read only during a multiplayer game,
// like the game's own "quick combat".
//
// Also here:
//   - F3 opens the Options screen on our page (main menu: game message "options";
//     adventure map: the screen's own "GameOptions" handler; the Options screen: our category);
//   - the main menu's version line (MMH5.5's "Version: 5.69k", UI/MainMenu2/Version.txt)
//     gets a second line, "H5 Together 0.3.0", in the game's own font;
//   - the first-start question about the update check (welcome, in the game's
//     panel style), once, on the main menu; never in robot runs unless
//     [update] welcome_test=1.
//
// Robot steps (it cannot click): "panel|settings open" (our page, on the Options
// screen), "panel|settings set <section>.<key> <value>" (as a click on the row
// would: the row shows it, Accept writes it), "panel|settings default" (the
// Default box's Yes), "panel|settings welcome yes|no|close", then the game's own
// "ui|NUI::CGameOptionsScreen accept" / "options_back".
#include "settings_menu.h"

#include <windows.h>
#include <bcrypt.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "rmg_builder_widgets.h"
#include "rmg_templates.h"
#include "settings_ini.h"
#include "settings_menu_data.h"
#include "settings_reload.h"
#include "settings_table.h"

#include "../hook/game_build.h"
#include "../hook/log.h"
#include "../hook/old_names.h"
#include "../hook/overlay.h"
#include "../hook/patch.h"
#include "../hook/robot.h"
#include "../hook/texts.h"
#include "../hook/version.h"
#include "../hook/third_party/imgui/imgui.h"

namespace mods {
namespace {

namespace st = settings;
namespace ui = builder_ui;

const char kName[] = "settings menu";
const char kOptionsScreen[] = ".?AVCGameOptionsScreen@NUI@@";
const char kMainMenu[] = ".?AVCMainMenuScreen@NUI@@";
const char kMapScreen[] = ".?AVCAdventureScreen@NUI@@";
const char kCategoryId[] = "h5together";

// --- the game ------------------------------------------------------------------------------------
const DWORD kWinVfsOpenSlot = 0x00e6651c;  // NVFS::CWinVFS vftable 0x00e66500, slot 7: OpenFile
const DWORD kWinVfsOpen = 0x009e0020;      // __thiscall(CWinVFS *, const nstl::string *path), ret 4
const DWORD kReactSlot = 0x00e3da14;       // CGameOptionsScreen's IProgrammedReactionsAndChecks (at +0x174), slot 0
const DWORD kReact = 0x0075cea0;           // __thiscall(reactions, sender, const nstl::string *command, ?), ret 0xc
const DWORD kReactionsOffset = 0x174;
const DWORD kSaveSites[2] = {0x0075ca80, 0x0075c70e};  // Accept's and Default's "save the profile"
const DWORD kSave = 0x008353e0;            // void(void)
const DWORD kShowCategory = 0x0075c030;    // __thiscall(screen, int index), ret 4 (-1: the screen's scroll)
const DWORD kCategoryRows = 0x0075bac0;    // __thiscall(screen, category, int mode), ret 8 (1 read the variables, 3 refresh)
const DWORD kDefaultsYes = 0x0075c680;     // __thiscall(screen): the "restore the defaults?" box's Yes
const DWORD kVarGet = 0x00c11860;
const DWORD kVarSet = 0x00c11900;
// The adventure screen's handler of its "GameOptions" message (its Options button, and the
// game's F4): it sets the screen's disable_* variables (disable_options_by_multiplayer...)
// and opens the Options screen (0x0075c7c0). Called as the message dispatch calls it
// (this = the screen, the message on the stack, ret 4); it reads neither. Posting the
// message does not work: the screen's observer drops a message without a sender.
const DWORD kMapOptions = 0x00593520;
const BYTE kMapOptionsBytes[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x83, 0xec, 0x34, 0x53, 0x8d, 0x44, 0x24, 0x28, 0x50, 0xe8};
const DWORD kCategoriesBegin = 0x1a4, kCategoriesEnd = 0x1a8;  // vector of SCategory *
const DWORD kCategoryIdBegin = 0x0c, kCategoryIdEnd = 0x10;    // its ID, nstl::string

struct GameString {  // nstl::string
	const char *begin, *end, *cap;
};
struct GameStream {  // CMemoryStream: the whole file in memory
	void **vftable;
	char *begin, *cap, *end, *pos;
	DWORD flags;  // bit 0: broken
};
struct VarValue {  // a console variable's value
	float number;
	const wchar_t *begin, *end, *cap;
};

typedef GameStream *(__fastcall *OpenFn)(void *vfs, void *edx, const GameString *path);
typedef void(__fastcall *DeleteFn)(GameStream *self, void *edx, int flags);
typedef bool(__fastcall *ReactFn)(void *self, void *edx, void *sender, const GameString *command, void *c);
typedef void(__cdecl *SaveFn)();
typedef const VarValue *(__fastcall *VarGetFn)(const GameString *name, const VarValue *fallback);
typedef void(__fastcall *VarSetFn)(const GameString *name, const VarValue *value, int level);
typedef void(__fastcall *ShowCategoryFn)(void *screen, void *edx, int index);
typedef void(__fastcall *CategoryRowsFn)(void *screen, void *edx, void *category, int mode);
typedef void(__fastcall *DefaultsYesFn)(void *screen, void *edx);

OpenFn g_open_next = nullptr;
ReactFn g_react_next = nullptr;
SaveFn g_save_next = nullptr;

// --- state ---------------------------------------------------------------------------------------
st::MenuOptions g_options;         // what the served category holds (set once, when it is served)
volatile LONG g_served = 0;        // our category is in the Options screen's list
std::vector<st::MenuRow> g_rows;   // our rows with a variable, both pages
std::map<std::string, int> g_shown;  // variable -> the index we set it to from the file
std::string g_after;               // the line after Accept
ImU32 g_after_colour = 0;
DWORD g_after_until = 0;
bool g_f3_was = false;
DWORD g_open_pending = 0;          // F3 posted the Options screen's message: open our page when it shows
std::string g_mmh55;

std::wstring IniPath()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together", L".ini", path, MAX_PATH);
	return path;
}

std::wstring ExeFolder()
{
	wchar_t path[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
	while (n > 0 && path[n - 1] != L'\\')
		--n;
	return std::wstring(path, n);
}

std::string ReadFileBytes(const std::wstring &path)
{
	std::string out;
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return out;
	LARGE_INTEGER size = {};
	if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < 8 * 1024 * 1024) {
		out.resize(static_cast<size_t>(size.QuadPart));
		DWORD got = 0;
		if (!ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &got, nullptr))
			got = 0;
		out.resize(got);
	}
	CloseHandle(h);
	return out;
}

std::string Narrow(const std::wstring &w)
{
	std::string s;
	for (wchar_t c : w)
		s += c < 128 ? static_cast<char>(c) : '?';
	return s;
}

// The value the mods read (the first [section], its first key=), else the default.
std::string FileValue(const std::string &file, const st::Setting &s)
{
	std::string v;
	if (!st::Read(file, s.section, s.key, &v) || v.empty())
		return s.def;
	return v;
}

const char *Get(const char *key)
{
	return texts::Get(key);
}

// --- what this PC has ----------------------------------------------------------------------------
bool UnderWine()
{
	const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

bool HasFiles(const std::wstring &pattern)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	FindClose(h);
	return true;
}

st::MenuOptions OptionsNow()
{
	st::MenuOptions o;
	o.wine = UnderWine();
	const std::wstring bin = ExeFolder();
	// (the original game without MMH5.5 reads its own folder, lang\fr-original: languages.cpp)
	o.packs = GameHasMmh55() ? HasFiles(bin + L"h5-together\\lang\\fr\\*.pak") ||
	                               HasFiles(bin + L"h5-together\\lang\\fr\\*.zip") ||
	                               HasFiles(bin + L"open-homm5\\lang\\fr\\*.pak")
	                         : HasFiles(bin + L"h5-together\\lang\\fr-original\\*.pak") ||
	                               HasFiles(bin + L"h5-together\\lang\\fr-original\\*.zip");
	// our texts files: their codes and their own names
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((bin + L"h5-together\\texts\\*.txt").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			std::wstring name = fd.cFileName;
			name = name.substr(0, name.size() - 4);
			const std::string code = Narrow(name);
			if (!texts::ValidCode(code))
				continue;
			texts::Book book;
			if (texts::Parse(ReadFileBytes(bin + L"h5-together\\texts\\" + fd.cFileName), &book) && book.texts.count("language.name"))
				o.names.push_back({code, book.texts["language.name"]});
			if (code != "en" && code != "fr")
				o.languages.push_back(code);
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
	o.names.push_back({"en", "English"});  // (built in; a file's own name comes first)
	// values of the file the menu's choices lack (a list of teams, another file name...)
	const std::string file = ReadFileBytes(IniPath());
	for (int i = 0; i < st::kSettingCount; ++i) {
		const st::Setting &s = st::kSettings[i];
		if (s.control == st::kNone || s.kind != st::kChoice)
			continue;
		const std::string v = FileValue(file, s);
		if (st::ChoiceIndex(s, v) < 0 && st::Valid(s, v))
			o.extra.push_back({std::string(s.section) + "." + s.key, v});
	}
	// the game's French texts: also when the file already asks for them
	const st::Setting *texts_row = st::Find("lang", "texts");
	if (texts_row && !st::SameValue(*texts_row, FileValue(file, *texts_row), "game"))
		o.packs = true;
	// online play over Steam: Valve's steam_api.dll where steam_runtime.cpp looks for it
	wchar_t steam_dll[MAX_PATH];
	oldnames::PathInOurFolder(L"steam_api.dll", steam_dll, MAX_PATH);
	o.steam_dll = GetFileAttributesW(steam_dll) != INVALID_FILE_ATTRIBUTES;
	o.mmh55 = GameHasMmh55();
	return o;
}

// MMH5.5's version, from its runtime patcher 55u.dll (it changes with every release;
// install.ps1 tells the versions apart the same way).
std::string Mmh55Name()
{
	const std::string bytes = ReadFileBytes(ExeFolder() + L"55u.dll");
	if (bytes.empty())
		return texts::Get("settings.mmh55_none");
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	unsigned char digest[32] = {};
	bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
	          BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0 &&
	          BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>(bytes.data())), static_cast<ULONG>(bytes.size()), 0) == 0 &&
	          BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
	if (hash)
		BCryptDestroyHash(hash);
	if (alg)
		BCryptCloseAlgorithmProvider(alg, 0);
	char hex[65] = "";
	for (int i = 0; ok && i < 32; ++i)
		sprintf_s(hex + 2 * i, 3, "%02x", digest[i]);
	if (strcmp(hex, "a6de006f9492f1e8af6eae19619b5a345536c443a5f2e4bb5e1e26947988085b") == 0)
		return "MMH5.5 RC19d";
	if (strcmp(hex, "a28663da081b6e23f0c2fa4000117d0b5d1457ca3314e3735bca215c1d5d304d") == 0)
		return "MMH5.5 RC19k";
	return texts::Get("settings.mmh55_unknown");
}

// --- the console variables -----------------------------------------------------------------------
bool SafeGetVar(const char *name, size_t n, int *value, int **level)
{
	const GameString s = {name, name + n, name + n};
	const VarValue fallback = {-100000.0f, nullptr, nullptr, nullptr};
	__try {
		const VarValue *v = reinterpret_cast<VarGetFn>(kVarGet)(&s, &fallback);
		if (!v || v == &fallback)
			return false;
		*value = static_cast<int>(floorf(v->number + 0.5f));
		if (level)
			*level = reinterpret_cast<int *>(const_cast<VarValue *>(v)) - 1;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool SafeSetVar(const char *name, size_t n, int value, const wchar_t *text, size_t text_n)
{
	const GameString s = {name, name + n, name + n};
	const VarValue v = {static_cast<float>(value), text, text + text_n, text + text_n};
	__try {
		reinterpret_cast<VarSetFn>(kVarSet)(&s, &v, 0);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool GetVar(const std::string &name, int *value)
{
	return SafeGetVar(name.c_str(), name.size(), value, nullptr);
}

bool SetVar(const std::string &name, int value)
{
	wchar_t text[16];
	swprintf_s(text, L"%d", value);
	return SafeSetVar(name.c_str(), name.size(), value, text, wcslen(text));
}

// Not saved in the game's profile file (the screen's Accept asked for level 2).
void Unsave(const std::string &name)
{
	int value = 0, *level = nullptr;
	if (SafeGetVar(name.c_str(), name.size(), &value, &level) && level) {
		__try {
			*level = 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
}

// The row's index of the file's value: its choice, or (a number between the steps) the nearest; else the default.
int IndexOf(const st::MenuRow &r, const std::string &value)
{
	int i = st::ChoiceIndex(*r.s, value);
	if (i >= 0 && i < static_cast<int>(r.items.size()) && st::SameValue(*r.s, r.items[static_cast<size_t>(i)].value, value))
		return i;
	for (size_t k = 0; k < r.items.size(); ++k)
		if (st::SameValue(*r.s, r.items[k].value, value))
			return static_cast<int>(k);
	return i >= 0 ? i : r.def;
}

bool IsSteps(const st::Setting &s)
{
	return strcmp(s.section, "coop") == 0 && strcmp(s.key, "segment_ms") == 0;
}

// The row's value in the file (the co-op steps: "200" when fast_segments=0).
std::string RowValue(const std::string &file, const st::Setting &s)
{
	if (IsSteps(s)) {
		const st::Setting *fast = st::Find("coop", "fast_segments");
		if (fast && st::SameValue(*fast, FileValue(file, *fast), "0"))
			return st::kCoopStepsGame;
	}
	return FileValue(file, s);
}

// Our variables from h5-together.ini: the rows show what the mods use.
void Refresh(const char *why)
{
	if (!g_served)
		return;
	const std::string file = ReadFileBytes(IniPath());
	int set = 0;
	for (const st::MenuRow &r : g_rows) {
		const int index = IndexOf(r, RowValue(file, *r.s));
		if (SetVar(r.var, index)) {
			g_shown[r.var] = index;
			++set;
		}
	}
	static bool told = false;
	if (!told || set != static_cast<int>(g_rows.size())) {
		told = true;
		hooklog::Write("settings: %d of our %d rows set from h5-together.ini (%s)", set, static_cast<int>(g_rows.size()), why);
	}
}

// --- after Accept or Default: the file ------------------------------------------------------------
st::Change Change(const st::Setting &s, const std::string &value)
{
	st::Change c;
	c.section = s.section;
	c.key = s.key;
	c.def = s.def;
	c.value = value;
	c.to_default = st::SameValue(s, value, s.def);
	return c;
}

void ShowAfter(const std::string &line, ImU32 colour)
{
	g_after = line;
	g_after_colour = colour;
	g_after_until = GetTickCount() + 9000;
}

void Sync()
{
	if (!g_served)
		return;
	std::vector<st::Change> changes;
	std::vector<const st::Setting *> changed;
	int restart = 0, next_game = 0;
	// During a multiplayer game the game greys the host's and the advanced rows out (it sets
	// this variable when it opens the Options screen from the map); a change that got there
	// anyway is not written, so this PC never disagrees with the others halfway.
	int multiplayer = 0;
	if (!GetVar("disable_options_by_multiplayer", &multiplayer))
		multiplayer = 0;
	for (const st::MenuRow &r : g_rows) {
		int index = 0;
		const auto shown = g_shown.find(r.var);
		if (shown == g_shown.end() || !GetVar(r.var, &index) || index == shown->second)
			continue;
		if (index < 0 || index >= static_cast<int>(r.items.size()))
			continue;
		if (multiplayer && (r.group == st::kHostGroup || r.group == st::kAdvanced || r.group == st::kOnline)) {
			hooklog::Write("settings: [%s] %s not changed: read only during a multiplayer game", r.s->section, r.s->key);
			SetVar(r.var, shown->second);
			continue;
		}
		const std::string value = r.items[static_cast<size_t>(index)].value;
		if (IsSteps(*r.s)) {  // one row, two settings
			const st::Setting *fast = st::Find("coop", "fast_segments");
			if (value == st::kCoopStepsGame) {
				changes.push_back(Change(*fast, "0"));
				changes.push_back(Change(*r.s, r.s->def));
			}
			else {
				changes.push_back(Change(*fast, "1"));
				changes.push_back(Change(*r.s, value));
			}
		}
		else
			changes.push_back(Change(*r.s, value));
		changed.push_back(r.s);
		g_shown[r.var] = index;
		if (r.s->when == st::kRestart)
			++restart;
		else if (r.s->when == st::kNextGame)
			++next_game;
		hooklog::Write("settings: [%s] %s = %s (%s)", r.s->section, r.s->key, value.empty() ? "(empty)" : value.c_str(),
		               r.s->when == st::kRestart ? "after a restart" : r.s->when == st::kNextGame ? "from the next game" : "at once");
	}
	for (const st::MenuRow &r : g_rows)
		Unsave(r.var);
	if (changes.empty())
		return;
	std::string error;
	if (!st::EditFile(IniPath().c_str(), changes, &error)) {
		hooklog::Write("settings: h5-together.ini could not be written: %s", error.c_str());
		ShowAfter(texts::Format("settings.after.failed", {{"why", error}}), ui::kError);
		return;
	}
	// what Windows (and so every mod) now reads from the file
	std::string read_back;
	const std::wstring ini = IniPath();
	for (const st::Change &c : changes) {
		wchar_t value[256];
		GetPrivateProfileStringW(texts::Wide(c.section).c_str(), texts::Wide(c.key).c_str(), L"(none)", value, 256, ini.c_str());
		read_back += (read_back.empty() ? "" : ", ") + c.section + "." + c.key + "=" + Narrow(value);
	}
	hooklog::Write("settings: h5-together.ini: %d line(s) changed; Windows reads %s", static_cast<int>(changes.size()), read_back.c_str());
	for (const st::Setting *s : changed)
		if (s->when == st::kLive || s->when == st::kNextGame)
			settings::Reload(s->section, s->key);
	std::string line;
	if (restart)
		line = restart == 1 ? texts::Get("settings.after.restart_one")
		                    : texts::Format("settings.after.restart", {{"n", std::to_string(restart)}});
	if (next_game) {
		const std::string more = next_game == 1 ? texts::Get("settings.after.next_game_one")
		                                        : texts::Format("settings.after.next_game", {{"n", std::to_string(next_game)}});
		line += (line.empty() ? "" : " ") + more;
	}
	if (!line.empty())
		ShowAfter(line, ui::kGold);
}

void __cdecl SaveHook()
{
	try {
		Sync();
	} catch (...) {  // out of memory: the game's save still runs
	}
	g_save_next();
}

// --- the Options screen --------------------------------------------------------------------------
DWORD At(const void *object, DWORD offset)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(object) + offset);
}

// Our category's index on this screen and the category object, or -1.
int SafeOurCategory(void *screen, void **category)
{
	__try {
		void *const *begin = reinterpret_cast<void *const *>(At(screen, kCategoriesBegin));
		void *const *end = reinterpret_cast<void *const *>(At(screen, kCategoriesEnd));
		if (!begin || end < begin || end - begin > 16)
			return -1;
		const size_t n = strlen(kCategoryId);
		for (void *const *p = begin; p != end; ++p) {
			const char *id = reinterpret_cast<const char *>(At(*p, kCategoryIdBegin));
			const char *id_end = reinterpret_cast<const char *>(At(*p, kCategoryIdEnd));
			if (id && static_cast<size_t>(id_end - id) == n && memcmp(id, kCategoryId, n) == 0) {
				*category = *p;
				return static_cast<int>(p - begin);
			}
		}
		return -1;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

bool SafeShow(void *screen, int index)
{
	__try {
		reinterpret_cast<ShowCategoryFn>(kShowCategory)(screen, nullptr, index);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool SafeReread(void *screen, void *category)
{
	__try {
		reinterpret_cast<CategoryRowsFn>(kCategoryRows)(screen, nullptr, category, 1);
		reinterpret_cast<CategoryRowsFn>(kCategoryRows)(screen, nullptr, category, 3);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool SafeDefaultsYes(void *screen)
{
	__try {
		reinterpret_cast<DefaultsYesFn>(kDefaultsYes)(screen, nullptr);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// Robot: scrolls page 1 or 2 of our category so that its line `row` shows, as the scroll
// bar would: the category's pages (vector at +0x40); each page's lines (SOption, vector at
// +0x3c), each line's window (+0x18); the page's IScrollableContainer (+0x20), its slot 8:
// __thiscall(container, IWindow *line), ret 4, which scrolls down until that line shows.
DWORD g_scroll_error = 0;

bool SafeScroll(void *category, int page, int row, DWORD *where)
{
	typedef void(__fastcall * ScrollFn)(void *container, void *edx, void *window);
	*where = 0;
	__try {
		void *const *pages = reinterpret_cast<void *const *>(At(category, 0x40));
		void *const *pages_end = reinterpret_cast<void *const *>(At(category, 0x44));
		*where = 1;
		if (!pages || page < 1 || page > pages_end - pages)
			return false;
		void *p = pages[page - 1];
		void *const *lines = reinterpret_cast<void *const *>(At(p, 0x3c));
		void *const *lines_end = reinterpret_cast<void *const *>(At(p, 0x40));
		*where = 2;
		if (!lines || row < 0 || row >= lines_end - lines)
			return false;
		void *window = reinterpret_cast<void *>(At(lines[row], 0x18));
		void *container = reinterpret_cast<void *>(At(p, 0x20));
		*where = 3;
		if (!container || !window)
			return false;
		void **vtable = *static_cast<void ***>(container);
		*where = reinterpret_cast<DWORD>(vtable[8]);
		reinterpret_cast<ScrollFn>(vtable[8])(container, nullptr, window);
		return true;
	}
	__except (g_scroll_error = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void *OptionsScreen()
{
	char top[96] = "";
	if (!overlay::TopScreen(top, sizeof(top)) || strcmp(top, kOptionsScreen) != 0)
		return nullptr;
	return overlay::TopScreenObject();
}

bool OpenOurPage(const char *why)
{
	void *screen = OptionsScreen();
	void *category = nullptr;
	const int index = screen ? SafeOurCategory(screen, &category) : -1;
	if (index < 0) {
		hooklog::Write("settings: our page cannot be opened (%s): %s", why, screen ? "our category is not on the Options screen" : "the Options screen is not on top");
		return false;
	}
	Refresh(why);
	const bool ok = SafeShow(screen, index);
	hooklog::Write("settings: our page opened (%s)%s", why, ok ? "" : ": crashed (caught)");
	return ok;
}

bool SafeMapOptions(void *screen)
{
	if (memcmp(reinterpret_cast<const void *>(kMapOptions), kMapOptionsBytes, sizeof(kMapOptionsBytes)) != 0)
		return false;
	__try {
		reinterpret_cast<void(__fastcall *)(void *, void *, DWORD)>(kMapOptions)(screen, nullptr, 0);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool __fastcall ReactHook(void *self, void *edx, void *sender, const GameString *command, void *c)
{
	if (command && command->begin && command->end - command->begin == 8 && memcmp(command->begin, "template", 8) == 0)
		Refresh("a category's button");  // before the screen reads the rows of the category opened
	return g_react_next(self, edx, sender, command, c);
}

// --- the served files ----------------------------------------------------------------------------
const char kUiConsts[] = "ui/uiconsts.(uigameconstsh5).xdb";
const char kVersionText[] = "ui/mainmenu2/version.txt";

std::string Key(const char *begin, const char *end)
{
	while (begin < end && (*begin == '/' || *begin == '\\'))
		++begin;
	std::string s(begin, end);
	for (char &c : s)
		c = c == '\\' ? '/' : (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	return s;
}

bool ReadStream(GameStream *s, std::string *out)
{
	if (!s || (s->flags & 1) || !s->begin || s->end < s->begin)
		return false;
	out->assign(s->begin, s->end);
	return true;
}

void Delete(GameStream *s)
{
	if (s)
		reinterpret_cast<DeleteFn>(s->vftable[0])(s, nullptr, 1);
}

GameStream *Replace(GameStream *original, const std::string &bytes)
{
	GameStream *ours = static_cast<GameStream *>(NewGameStream(bytes));
	if (!ours)
		return original;
	Delete(original);
	return ours;
}

GameStream *ServeUiConsts(GameStream *original)
{
	std::string game, ours;
	if (!ReadStream(original, &game)) {
		hooklog::Write("settings: the game's uiconsts file is not readable: no H5 Together page in the Options screen");
		return original;
	}
	if (!g_served) {
		g_options = OptionsNow();
		g_rows.clear();
		for (int page = 1; page <= 2; ++page)
			for (const st::MenuRow &r : st::MenuRows(g_options, page))
				if (r.s && r.s->control != st::kNone)
					g_rows.push_back(r);
	}
	if (!st::AddCategory(game, g_options, &ours)) {
		hooklog::Write("settings: the game's uiconsts file has no Options categories as we know them (%u bytes): no H5 Together page",
		               static_cast<unsigned>(game.size()));
		return original;
	}
	if (InterlockedExchange(&g_served, 1) == 0)
		hooklog::Write("settings: the Options screen gets our \"H5 Together\" category (%d rows%s%s; uiconsts %u -> %u bytes)",
		               static_cast<int>(g_rows.size()), g_options.wine ? ", the wheel fix (Wine)" : "",
		               g_options.packs ? ", the game's texts" : "", static_cast<unsigned>(game.size()), static_cast<unsigned>(ours.size()));
	return Replace(original, ours);
}

// "Version: 5.69k" -> "H5 Together 0.3.0" above it, in the same text (the game's own font).
GameStream *ServeVersion(GameStream *original)
{
	std::string game;
	if (!ReadStream(original, &game) || game.size() < 4 || game.compare(0, 2, "\xff\xfe") != 0)
		return original;
	std::string text;
	for (size_t i = 2; i + 1 < game.size(); i += 2)
		text += static_cast<char>(game[i + 1] == 0 && static_cast<unsigned char>(game[i]) < 0x80 ? game[i] : '?');
	// after the leading style tag ("<h3_bright>"), if there is one
	size_t at = 0;
	if (text.compare(0, 1, "<") == 0 && text.find('>') != std::string::npos)
		at = text.find('>') + 1;
	const std::string line = "H5 Together " OPEN_HOMM5_VERSION "<br>";
	std::string served = game.substr(0, 2 + 2 * at);
	served += st::GameText(line).substr(2);
	served += game.substr(2 + 2 * at);
	static bool told = false;
	if (!told) {
		told = true;
		hooklog::Write("settings: the main menu's version line also says \"H5 Together " OPEN_HOMM5_VERSION "\"");
	}
	return Replace(original, served);
}

// The pages' list (PageCont, shared by every category) has no scroll bar in the game: its
// pages fit. Ours are longer, so it gets the game's own vertical scroll bar, the random map
// screen's options list's (UI/RMGScreen/OptionsContainerScroller), hidden while there is
// nothing to scroll ("HideDisabled"): the game's own pages look as before.
const char kPageCont[] = "ui/gameoptions/pagecont.(windowscrollablecontainershared).xdb";
const char kOurScroll[] = "ui/gameoptions/h5togetherscroll.(windowscrollbar).xdb";
const char kRmgScroll[] = "UI/RMGScreen/OptionsContainerScroller.(WindowScrollBar).xdb";
std::string g_scroll;  // our scroll bar's file, made from the random map screen's

bool ReadBelow(void *vfs, const char *path, std::string *out)
{
	const size_t n = strlen(path);
	const GameString name = {path, path + n, path + n};
	GameStream *s = g_open_next(vfs, nullptr, &name);
	const bool ok = ReadStream(s, out);
	Delete(s);
	return ok;
}

bool ReplaceOnce(std::string *text, const char *from, const char *to)
{
	const size_t at = text->find(from);
	if (at == std::string::npos || text->find(from, at + 1) != std::string::npos)
		return false;
	text->replace(at, strlen(from), to);
	return true;
}

GameStream *ServePageCont(void *vfs, GameStream *original)
{
	std::string game, scroll;
	if (!ReadStream(original, &game) || !ReadBelow(vfs, kRmgScroll, &scroll) ||
	    !ReplaceOnce(&scroll, "<HideDisabled>false</HideDisabled>", "<HideDisabled>true</HideDisabled>") ||
	    !ReplaceOnce(&game, "<ScrollBar/>",
	                 "<ScrollBar href=\"H5TogetherScroll.(WindowScrollBar).xdb#xpointer(/WindowScrollBar)\"/>")) {
		hooklog::Write("settings: the Options screen's page list or the random map screen's scroll bar is not as we know it: "
		               "our pages cannot scroll (their last rows are cut)");
		return original;
	}
	g_scroll = scroll;
	static bool told = false;
	if (!told) {
		told = true;
		hooklog::Write("settings: the Options screen's pages get the game's scroll bar (shown only when a page is too long)");
	}
	return Replace(original, game);
}

GameStream *ServeText(const std::string &key, GameStream *original)
{
	std::string words;
	if (g_mmh55.empty())
		g_mmh55 = Mmh55Name();
	const std::string file = ReadFileBytes(IniPath());
	std::string peers;
	st::Read(file, "net", "lan_peers", &peers);
	if (!st::MenuText(key.substr(strlen(st::kTextFolder)), g_options, &Get, OPEN_HOMM5_VERSION, g_mmh55, peers, &words))
		return original;
	return Replace(original, st::GameText(words));
}

GameStream *__fastcall OpenHook(void *vfs, void *edx, const GameString *path)
{
	if (!path || !path->begin || path->end - path->begin < 20 || path->end - path->begin > 220)
		return g_open_next(vfs, edx, path);
	try {
		const std::string key = Key(path->begin, path->end);
		if (key == kUiConsts)
			return ServeUiConsts(g_open_next(vfs, edx, path));
		if (key == kVersionText)
			return ServeVersion(g_open_next(vfs, edx, path));
		if (g_served && key.compare(0, strlen(st::kTextFolder), st::kTextFolder) == 0)
			return ServeText(key, g_open_next(vfs, edx, path));
		if (g_served && key == kPageCont)
			return ServePageCont(vfs, g_open_next(vfs, edx, path));
		if (key == kOurScroll && !g_scroll.empty())
			return Replace(g_open_next(vfs, edx, path), g_scroll);
	} catch (...) {  // out of memory: the game's own files
	}
	return g_open_next(vfs, edx, path);
}

// --- the welcome question --------------------------------------------------------------------------
enum WelcomeState { kWelcomeNo, kWelcomeWaiting, kWelcomeShown, kWelcomeDone };
WelcomeState g_welcome = kWelcomeNo;
const float kRefH = 768.0f, kRefW = 1024.0f;
const float kBoxW = 520.0f, kBoxH = 250.0f;

void Answer(bool yes, const char *why)
{
	std::vector<st::Change> changes;
	st::Change asked;
	asked.section = "update";
	asked.key = "asked";
	asked.value = "1";
	changes.push_back(asked);
	if (yes) {
		st::Change check;
		check.section = "update";
		check.key = "check";
		check.value = "1";
		changes.push_back(check);
	}
	std::string error;
	const bool ok = st::EditFile(IniPath().c_str(), changes, &error);
	hooklog::Write("settings: welcome question: %s (%s)%s%s", yes ? "yes, check for new versions" : "no", why,
	               ok ? "; written to h5-together.ini" : "; h5-together.ini could not be written: ", ok ? "" : error.c_str());
	overlay::CloseModal(kName);
	g_welcome = kWelcomeDone;
}

// Asked once: [update] asked=1 in the file once answered, or [update] check set by hand.
bool WelcomeWanted()
{
	const std::wstring ini = IniPath();
	wchar_t text[16];
	const bool test = GetPrivateProfileIntW(L"update", L"welcome_test", 0, ini.c_str()) != 0;
	if (robot::Active() && !test)
		return false;
	if (GetPrivateProfileIntW(L"update", L"asked", 0, ini.c_str()) != 0)
		return false;
	GetPrivateProfileStringW(L"update", L"check", L"", text, 16, ini.c_str());
	return text[0] == 0 || test;
}

void DrawWelcome(const overlay::Frame &frame)
{
	const float k = frame.height / kRefH;
	const float x0 = floorf((frame.width - kBoxW * k) / 2 + 0.5f), y0 = floorf((frame.height - kBoxH * k) / 2 + 0.5f);
	const auto at = [&](float x, float y) { return ImVec2(x0 + x * k, y0 + y * k); };
	const auto px = [&](float size) { return floorf(size * k + 0.5f); };
	ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(frame.width, frame.height), IM_COL32(0, 0, 0, 115));
	const ImVec2 p0 = at(0, 0), p1 = at(kBoxW, kBoxH);
	ImGui::SetNextWindowPos(p0, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::Begin("##welcome", nullptr,
	             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
	                 ImGuiWindowFlags_NoScrollWithMouse);
	ImDrawList *draw = ImGui::GetWindowDrawList();
	// the random map screen's panel (its frame and parchment border), cut to our size: the
	// game rules panel's look
	if (ImTextureID bg = ui::Tex(ui::kBackground)) {
		const float u = kBoxW / 1024.0f, v = kBoxH / 512.0f;
		draw->AddImage(bg, p0, ImVec2(p1.x - 16 * k, p1.y - 16 * k), ImVec2(0, 0), ImVec2(u - 16.0f / 1024.0f, v - 16.0f / 512.0f));
		draw->AddImage(bg, ImVec2(p1.x - 16 * k, p0.y), ImVec2(p1.x, p1.y - 16 * k), ImVec2(633.0f / 1024.0f, 0),
		               ImVec2(649.0f / 1024.0f, v - 16.0f / 512.0f));
		draw->AddImage(bg, ImVec2(p0.x, p1.y - 16 * k), ImVec2(p1.x - 16 * k, p1.y), ImVec2(0, 492.0f / 512.0f),
		               ImVec2(u - 16.0f / 1024.0f, 508.0f / 512.0f));
		draw->AddImage(bg, ImVec2(p1.x - 16 * k, p1.y - 16 * k), p1, ImVec2(633.0f / 1024.0f, 492.0f / 512.0f),
		               ImVec2(649.0f / 1024.0f, 508.0f / 512.0f));
	}
	else {
		draw->AddRectFilled(p0, p1, IM_COL32(30, 24, 18, 248), 8.0f * k);
		draw->AddRect(p0, p1, IM_COL32(150, 116, 70, 255), 8.0f * k, 0, 2.0f * k);
	}
	// one plain field over the random map screen's boxes inside its frame (as the game rules panel does)
	draw->AddRectFilled(at(26, 72), at(kBoxW - 26, kBoxH - 24), IM_COL32(34, 27, 19, 245), 4.0f * k);
	draw->AddRect(at(26, 72), at(kBoxW - 26, kBoxH - 24), ui::kEdge, 4.0f * k, 0, 1.0f * k);
	ImFont *title = overlay::Font(px(20)), *body = overlay::Font(px(15)), *small = overlay::Font(px(12));
	const std::string t = texts::Get("welcome.title");
	const float tw = ui::TextWidth(title, px(20), t.c_str());
	ui::ShadowText(draw, title, px(20), ImVec2(floorf((p0.x + p1.x - tw) / 2), at(0, 36).y), ui::kGold, t.c_str());
	// the question and its detail, wrapped
	const auto wrapped = [&](ImFont *font, float size, float y, ImU32 colour, const char *text) {
		std::string line, word;
		const std::string all = std::string(text) + " ";
		const float width = (kBoxW - 80) * k;
		for (char c : all) {
			if (c != ' ') {
				word += c;
				continue;
			}
			const std::string candidate = line.empty() ? word : line + " " + word;
			if (!line.empty() && ui::TextWidth(font, size, candidate.c_str()) > width) {
				ui::ShadowText(draw, font, size, ImVec2(at(40, 0).x, y), colour, line.c_str());
				y += size * 1.3f;
				line = word;
			}
			else
				line = candidate;
			word.clear();
		}
		if (!line.empty()) {
			ui::ShadowText(draw, font, size, ImVec2(at(40, 0).x, y), colour, line.c_str());
			y += size * 1.3f;
		}
		return y;
	};
	float y = wrapped(body, px(15), at(0, 84).y, ui::kParchment, texts::Get("welcome.question"));
	wrapped(small, px(12), y + 6 * k, ui::kDim, texts::Get("welcome.detail"));
	ImFont *button = overlay::Font(px(15));
	if (ui::Button("##yes", at(70, 180), at(70 + 122, 214), texts::Get("welcome.yes"), button, px(15), true, k))
		Answer(true, "its button");
	else if (ui::Button("##no", at(kBoxW - 70 - 122, 180), at(kBoxW - 70, 214), texts::Get("welcome.no"), button, px(15), true, k))
		Answer(false, "its button");
	ImGui::End();
	ImGui::PopStyleVar(2);
}

// --- robot -----------------------------------------------------------------------------------------
void RobotCommand(const char *text)
{
	char key[96] = "", value[64] = "";
	if (strcmp(text, "open") == 0) {
		if (!OpenOurPage("robot"))
			g_open_pending = GetTickCount();
		return;
	}
	int page = 0;
	int line = 0;
	if (sscanf_s(text, "scroll %d %d", &page, &line) == 2) {
		void *screen = OptionsScreen(), *category = nullptr;
		DWORD where = 0;
		g_scroll_error = 0;
		const bool ok = screen && SafeOurCategory(screen, &category) >= 0 && SafeScroll(category, page, line, &where);
		hooklog::Write("settings: robot: page %d scrolled to its line %d%s (screen %p, category %p, step %08lx, error %08lx)", page, line,
		               ok ? "" : ": not possible", screen, category, where, g_scroll_error);
		return;
	}
	if (sscanf_s(text, "category %d", &page) == 1) {  // another category (0 = the game's first), for comparing
		void *screen = OptionsScreen();
		hooklog::Write("settings: robot: category %d%s", page, screen && SafeShow(screen, page) ? "" : ": not on the Options screen");
		return;
	}
	if (strcmp(text, "default") == 0) {
		void *screen = OptionsScreen();
		hooklog::Write("settings: robot: the Default box's Yes%s", screen && SafeDefaultsYes(screen) ? "" : ": not on the Options screen (or crashed)");
		return;
	}
	if (strncmp(text, "welcome ", 8) == 0) {
		if (strcmp(text + 8, "show") == 0) {
			g_welcome = kWelcomeWaiting;
			return;
		}
		if (g_welcome != kWelcomeShown) {
			hooklog::Write("settings: robot: the welcome question is not shown");
			return;
		}
		if (strcmp(text + 8, "close") == 0) {
			overlay::CloseModal(kName);
			g_welcome = kWelcomeDone;
			hooklog::Write("settings: welcome question: closed without an answer (robot): asked again next time");
		}
		else
			Answer(strcmp(text + 8, "yes") == 0, "robot");
		return;
	}
	if (sscanf_s(text, "set %95s %63s", key, static_cast<unsigned>(sizeof(key)), value, static_cast<unsigned>(sizeof(value))) >= 1 &&
	    strncmp(text, "set ", 4) == 0) {
		if (strcmp(value, "-") == 0 || strcmp(value, "(empty)") == 0)
			value[0] = 0;
		char *dot = strchr(key, '.');
		const st::Setting *s = dot ? (*dot = 0, st::Find(key, dot + 1)) : nullptr;
		const st::MenuRow *row = nullptr;
		for (const st::MenuRow &r : g_rows)
			if (r.s == s)
				row = &r;
		void *screen = OptionsScreen(), *category = nullptr;
		if (!row || !screen || SafeOurCategory(screen, &category) < 0) {
			hooklog::Write("settings: robot: \"%s\": %s", text, !row ? "no such row" : "not on the Options screen");
			return;
		}
		int index = -1;
		for (size_t i = 0; i < row->items.size(); ++i)
			if (st::SameValue(*s, row->items[i].value, value))
				index = static_cast<int>(i);
		if (index < 0) {
			hooklog::Write("settings: robot: %s.%s has no choice \"%s\"", s->section, s->key, value);
			return;
		}
		SetVar(row->var, index);
		SafeReread(screen, category);
		hooklog::Write("settings: robot: the row %s shows choice %d (\"%s\"), as after a click", row->var.c_str(), index, value);
		return;
	}
	hooklog::Write("settings: robot command \"%s\" not known", text);
}

// --- the overlay client ------------------------------------------------------------------------------
bool Wanted()
{
	char top[96] = "";
	overlay::TopScreen(top, sizeof(top));
	char command[256];
	if (robot::TakePanelCommand(command, sizeof(command), "settings "))
		RobotCommand(command);
	const bool options = strcmp(top, kOptionsScreen) == 0;
	// F3: the Options screen on our page
	if (overlay::KeyPressed(VK_F3, &g_f3_was)) {
		if (options)
			OpenOurPage("F3");
		else if (strcmp(top, kMainMenu) == 0) {
			g_open_pending = GetTickCount();
			robot::PostGameMessage("options", 0);  // as the main menu's Options button
		}
		else if (strcmp(top, kMapScreen) == 0) {
			g_open_pending = GetTickCount();
			if (!SafeMapOptions(overlay::TopScreenObject())) {
				g_open_pending = 0;
				hooklog::Write("settings: F3: the adventure screen's Options code is not as expected (or crashed): use the game's own Options");
			}
		}
	}
	if (g_open_pending && options) {
		g_open_pending = 0;
		OpenOurPage("F3, the Options screen opened");
	}
	else if (g_open_pending && GetTickCount() - g_open_pending > 4000) {
		g_open_pending = 0;
		hooklog::Write("settings: F3: the Options screen did not open (on the map the game opens it only when it waits for "
		               "you, as with its own F4)");
	}
	// the words are ready before the Options screen asks for its texts (they may read the game's own)
	if (strcmp(top, kMainMenu) == 0 || options)
		texts::Get("settings.category");
	// the welcome question
	if (g_welcome == kWelcomeNo && strcmp(top, kMainMenu) == 0)
		g_welcome = WelcomeWanted() ? kWelcomeWaiting : kWelcomeDone;
	if (g_welcome == kWelcomeWaiting && strcmp(top, kMainMenu) == 0 && overlay::OpenModal(kName)) {
		g_welcome = kWelcomeShown;
		hooklog::Write("settings: welcome question shown (the update check)");
	}
	if (g_welcome == kWelcomeShown && !overlay::ModalOpen(kName)) {
		g_welcome = kWelcomeDone;  // Esc, or another screen: no answer, asked again at the next start
		hooklog::Write("settings: welcome question closed without an answer: asked again next time");
	}
	if (g_after_until && (!options || GetTickCount() > g_after_until))
		g_after_until = 0;
	return g_welcome == kWelcomeShown || g_after_until != 0;
}

void Prepare(const overlay::Frame &frame)
{
	ui::LoadPictures(frame.device);
	const float k = frame.height / kRefH;
	for (float size : {12.0f, 14.0f, 15.0f, 20.0f})
		overlay::WantFont(floorf(size * k + 0.5f));
}

void Draw(const overlay::Frame &frame)
{
	if (g_welcome == kWelcomeShown) {
		DrawWelcome(frame);
		return;
	}
	if (!g_after_until)
		return;
	// the line after Accept: at the bottom left, above the version line, on a dark field
	const float k = frame.height / kRefH, px = floorf(14.0f * k + 0.5f);
	const float x0 = floorf((frame.width - kRefW * k) / 2 + 0.5f) + 34.0f * k, y0 = 690.0f * k;
	ImFont *font = overlay::Font(px);
	ImDrawList *draw = ImGui::GetForegroundDrawList();
	const float w = ui::TextWidth(font, px, g_after.c_str());
	draw->AddRectFilled(ImVec2(x0 - 6 * k, y0 - 4 * k), ImVec2(x0 + w + 6 * k, y0 + px + 4 * k), IM_COL32(20, 14, 8, 200), 4 * k);
	ui::ShadowText(draw, font, px, ImVec2(x0, y0), g_after_colour, g_after.c_str());
	static std::string told;
	if (told != g_after) {
		told = g_after;
		hooklog::Write("settings: the Options screen says \"%s\"", g_after.c_str());
	}
}

}  // namespace

void InstallSettingsMenuEarly()
{
	if (g_open_next)
		return;
	g_open_next = reinterpret_cast<OpenFn>(hookpatch::PatchPointer(kWinVfsOpenSlot, kWinVfsOpen, &OpenHook));
	if (!g_open_next)
		hooklog::Write("settings: the game's file system is not as expected (%08lx): no H5 Together page in the Options screen",
		               kWinVfsOpenSlot);
	overlay::InstallEarly();
}

void ApplySettingsMenuFromSettings()
{
	if (!g_open_next)
		InstallSettingsMenuEarly();  // (a Steam-wrapped exe: too late for the Options list if the game has read it)
	g_react_next = reinterpret_cast<ReactFn>(hookpatch::PatchPointer(kReactSlot, kReact, &ReactHook));
	g_save_next = reinterpret_cast<SaveFn>(hookpatch::RedirectCalls(kSaveSites, 2, kSave, &SaveHook));
	if (!g_react_next || !g_save_next) {
		hooklog::Write("settings: the Options screen's code is not as expected (%08lx, %08lx): our page shows, but Accept "
		               "cannot save it",
		               kReactSlot, kSaveSites[0]);
	}
	const overlay::Client client = {kName, &Wanted, &Prepare, &Draw};
	if (overlay::AddClient(client))
		overlay::EnableModalInput();
	hooklog::Write("settings: the settings menu is in the game's Options screen (Options, H5 Together; F3 opens it)");
}

}  // namespace mods
