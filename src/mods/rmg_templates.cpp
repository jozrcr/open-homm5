// Mod: our random map templates in the game's own list, with no file copied into
// the game folder (map generator plan, step 1: docs/plans/map-generator.md).
//
// The random map screen lists the templates by asking the game's file system
// (NVFS::CWinVFS, vftable 0x00e66500) for the files in RMG/Templates matching
// "*.xdb" (slot 10, List), then opens each one (slot 7, Open) and keeps the
// ones that read as a template; the list shows each template's Name field.
// This mod reads the .xdb files of the folder open-homm5\templates next to the
// exe once, adds them to that listing as RMG\Templates\<file>, and serves their
// bytes when the game opens them. Nothing on disk changes.
//
// Every player of a multiplayer game must have the same templates: each PC
// builds the random map itself from the template's path, and a PC without the
// file would crash there (0x0090e8db reads the template without a check).
//
// Recipe names (docs/plans/in-game-generator.md, step 2): a template named
// RMG/Templates/OH-X1-<16 characters>.xdb needs no file. Its name holds every
// choice (rmg_recipe.cpp), and Open builds its bytes on the fly with the C++
// template maker (rmg_maker.cpp): the same bytes on every PC. The listing adds
// every recipe this PC was asked to open, and those named in the settings.
//
// Joining someone else's random map game, and the guard against a missing
// template: rmg_join.cpp (step 3).
//
// Settings, open-homm5.ini [rmg]:
//   templates=1  (default; 0 = off: neither the folder's files nor recipes;
//                the guard against a missing template stays on)
//   recipes=     recipe names to offer in the game's list, separated by commas
#include <windows.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "rmg_join.h"
#include "rmg_maker.h"
#include "rmg_recipe.h"
#include "rmg_templates.h"

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

const DWORD kWinVfsOpenSlot = 0x00e6651c;  // slot 7: __thiscall(vfs, const nstl::string *path), ret 4
const DWORD kWinVfsOpen = 0x009e0020;
const DWORD kWinVfsListSlot = 0x00e66528;  // slot 10: __thiscall(vfs, vector<string> *out, string *dir, string *mask), ret 0xc
const DWORD kWinVfsList = 0x009e02b0;
const DWORD kMemoryStreamCtor = 0x004034d0;  // __thiscall(this): empty CMemoryStream
const DWORD kStreamSetSize = 0x008c7610;     // __thiscall(this, int size), ret 4
const DWORD kGameNew = 0x00878fd0;           // cdecl void *(size)
const DWORD kGameDelete = 0x00879240;        // cdecl void (void *)
const size_t kMaxFiles = 200;
const DWORD kMaxSize = 1 << 20;
const size_t kMaxRecipes = 64;  // in the list; any recipe name is still served when asked for
const char kTemplatesDir[] = "rmg/templates/";

struct GameString {  // nstl::string
	char *begin;
	char *end;       // the terminating zero
	char *cap;
};
struct GameStrings {  // nstl::vector<nstl::string>
	GameString *begin;
	GameString *end;
	GameString *cap;
};
struct GameStream {  // CMemoryStream, 0x18 bytes
	void **vftable;
	char *begin;
	char *cap;
	char *end;
	char *pos;
	DWORD flags;
};

typedef GameStream *(__fastcall *OpenFn)(void *vfs, void *edx, const GameString *path);
typedef void(__fastcall *ListFn)(void *vfs, void *edx, GameStrings *out, const GameString *dir, const GameString *mask);
typedef GameStream *(__fastcall *CtorFn)(GameStream *self, void *edx);
typedef void(__fastcall *SetSizeFn)(GameStream *self, void *edx, int size);
typedef void(__fastcall *DeleteFn)(GameStream *self, void *edx, int flags);
typedef void *(__cdecl *NewFn)(size_t size);
typedef void(__cdecl *FreeFn)(void *p);
OpenFn g_open_next = nullptr;
ListFn g_list_next = nullptr;

struct Template {
	std::string file;  // as on disk, e.g. OH-2v2v2-Lu-Normal.xdb
	std::string key;   // rmg/templates/oh-2v2v2-lu-normal.xdb
	std::string data;
};
std::vector<Template> g_templates;

// Recipe names to list, as rmg::EncodeRecipe writes them, without ".xdb":
// those of recipes= first, then those the game opened. Open and List may run
// on different threads, so the list has a lock.
std::vector<std::string> g_recipes;
SRWLOCK g_recipes_lock = SRWLOCK_INIT;

// Adds a recipe to the list; true when it was not there yet. `always`: even
// past the limit (the template of a game this PC joins must be listed).
bool AddRecipe(const std::string &recipe, bool always = false)
{
	bool added = false;
	AcquireSRWLockExclusive(&g_recipes_lock);
	try {
		if ((always || g_recipes.size() < kMaxRecipes) &&
		    std::find(g_recipes.begin(), g_recipes.end(), recipe) == g_recipes.end()) {
			g_recipes.push_back(recipe);
			added = true;
		}
	} catch (...) {  // out of memory: not listed, still served
	}
	ReleaseSRWLockExclusive(&g_recipes_lock);
	return added;
}

// Recipe names are served only when this very DLL builds what the tests
// recorded (rmg::SelfCheck): a PC that built other bytes than its friends
// would build another map from the same name.
bool g_recipes_on = false;

bool MakerSelfCheck()
{
	std::string problem;
	try {
		if (mods::rmg::SelfCheck(&problem))
			return true;
	} catch (...) {
		problem = "out of memory";
	}
	hooklog::Write("rmg templates: the template maker's self-check FAILED (%s): recipe names are not served", problem.c_str());
	return false;
}

std::vector<std::string> Recipes()
{
	std::vector<std::string> copy;
	AcquireSRWLockShared(&g_recipes_lock);
	try {
		copy = g_recipes;
	} catch (...) {
	}
	ReleaseSRWLockShared(&g_recipes_lock);
	return copy;
}

// "/RMG\Templates/X.xdb" -> "rmg/templates/x.xdb" (the game compares paths this way)
std::string Normalize(const char *begin, const char *end)
{
	while (begin < end && (*begin == '/' || *begin == '\\'))
		++begin;
	std::string s(begin, end);
	for (char &c : s)
		c = c == '\\' ? '/' : (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	return s;
}

// The game's mask test on a file name: * and ?, any case.
bool Matches(const char *mask, const char *name)
{
	if (!*mask)
		return !*name;
	if (*mask == '*')
		return Matches(mask + 1, name) || (*name && Matches(mask, name + 1));
	const char a = *mask >= 'A' && *mask <= 'Z' ? static_cast<char>(*mask - 'A' + 'a') : *mask;
	const char b = *name >= 'A' && *name <= 'Z' ? static_cast<char>(*name - 'A' + 'a') : *name;
	return *name && (*mask == '?' || a == b) && Matches(mask + 1, name + 1);
}

void LoadTemplates()
{
	// <exe folder>\open-homm5 (the same for every copy the robot runs)
	wchar_t dir[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
	while (n > 0 && dir[n - 1] != L'\\')
		--n;
	lstrcpynW(dir + n, L"open-homm5", MAX_PATH - n);
	std::wstring pattern = std::wstring(dir) + L"\\templates\\*.xdb";
	WIN32_FIND_DATAW fd;
	HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE)
		return;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY || fd.nFileSizeHigh || fd.nFileSizeLow > kMaxSize)
			continue;
		if (g_templates.size() >= kMaxFiles)
			break;
		char name[MAX_PATH];
		if (!WideCharToMultiByte(CP_ACP, 0, fd.cFileName, -1, name, MAX_PATH, nullptr, nullptr))
			continue;
		std::string recipe;
		if (mods::rmg::CanonicalRecipe(name, &recipe)) {  // the name decides, the same on every PC
			hooklog::Write("rmg templates: open-homm5\\templates\\%s skipped: a recipe name, built from the name itself", name);
			continue;
		}
		const std::wstring path = std::wstring(dir) + L"\\templates\\" + fd.cFileName;
		HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		if (f == INVALID_HANDLE_VALUE)
			continue;
		Template t;
		t.file = name;
		const std::string inside = std::string("RMG/Templates/") + name;
		t.key = Normalize(inside.data(), inside.data() + inside.size());
		t.data.resize(fd.nFileSizeLow);
		DWORD got = 0;
		if (ReadFile(f, &t.data[0], fd.nFileSizeLow, &got, nullptr) && got == fd.nFileSizeLow)
			g_templates.push_back(t);
		CloseHandle(f);
	} while (FindNextFileW(find, &fd));
	FindClose(find);
}

GameStream *NewStream(const std::string &data)
{
	GameStream *m = static_cast<GameStream *>(reinterpret_cast<NewFn>(kGameNew)(sizeof(GameStream)));
	if (!m)
		return nullptr;
	reinterpret_cast<CtorFn>(kMemoryStreamCtor)(m, nullptr);
	reinterpret_cast<SetSizeFn>(kStreamSetSize)(m, nullptr, static_cast<int>(data.size()));
	if (!(m->flags & 1) && m->begin && static_cast<size_t>(m->end - m->begin) == data.size())
		memcpy(m->begin, data.data(), data.size());
	return m;
}

// templates_selftest=1 (robot runs): at the first file the game opens, list
// RMG/Templates through the game's whole file system (the combiner at
// 0x00fd63c4, slot 10: what the random map screen does) and log what came back.
const DWORD kGameVfs = 0x00fd63c4;
volatile LONG g_selftest = 0;

void SelfTest()
{
	void *vfs = *reinterpret_cast<void **>(kGameVfs);
	if (!vfs || IsBadReadPtr(vfs, 4))
		return;
	static char dir_text[] = "RMG/Templates";
	static char mask_text[] = "*.xdb";
	GameString dir = {dir_text, dir_text + sizeof(dir_text) - 1, dir_text + sizeof(dir_text)};
	GameString mask = {mask_text, mask_text + sizeof(mask_text) - 1, mask_text + sizeof(mask_text)};
	GameStrings out = {};  // left to the game's heap (a test, once)
	const ListFn list = reinterpret_cast<ListFn>((*static_cast<DWORD **>(vfs))[10]);
	list(vfs, nullptr, &out, &dir, &mask);
	int all = 0, ours = 0, recipes = 0;
	for (const GameString *s = out.begin; s && s != out.end; ++s, ++all) {
		const std::string key = Normalize(s->begin, s->end);
		std::string recipe;
		for (const Template &t : g_templates)
			ours += key == t.key;
		recipes += mods::rmg::RecipeOfPath(key, &recipe);
	}
	hooklog::Write("rmg templates: self-test: the game lists %d templates in RMG/Templates, %d of them our files, %d recipes",
	               all, ours, recipes);
}

GameStream *__fastcall OpenHook(void *vfs, void *edx, const GameString *path)
{
	if (g_selftest == 1 && InterlockedExchange(&g_selftest, 2) == 1)
		SelfTest();
	GameStream *s = g_open_next(vfs, edx, path);
	if (!path || !path->begin || path->end - path->begin < 14)
		return s;
	const std::string key = Normalize(path->begin, path->end);
	std::string recipe;
	if (g_recipes_on && mods::rmg::RecipeOfPath(key, &recipe)) {
		GameStream *ours = nullptr;
		try {
			mods::rmg::Template t;
			std::string error;
			if (!mods::rmg::BuildRecipe(recipe, &t, &error)) {  // cannot happen: the name was checked
				hooklog::Write("rmg templates: %s not built: %s", recipe.c_str(), error.c_str());
				return s;
			}
			ours = NewStream(t.xml);
			// The game opens every template each time it lists them: log each new
			// recipe, and the first few builds of those recipes= named.
			static LONG builds = 0;
			if (ours && (AddRecipe(recipe) || InterlockedIncrement(&builds) <= 4))
				hooklog::Write("rmg templates: %s.xdb built from its name: \"%s\", %d players, %d zones, %s, %u bytes",
				               recipe.c_str(), t.name.c_str(), t.stats.players, t.stats.zones, t.stats.size.c_str(),
				               static_cast<unsigned>(t.xml.size()));
		} catch (...) {  // out of memory: leave the file to the game
			return s;
		}
		if (!ours)
			return s;
		if (s)  // a file of that name in the game's data: the name wins
			reinterpret_cast<DeleteFn>(s->vftable[0])(s, nullptr, 1);
		return ours;
	}
	for (const Template &t : g_templates) {
		if (t.key != key)
			continue;
		GameStream *ours = NewStream(t.data);
		if (!ours)
			return s;
		if (s)  // a template of the same name in the game's data: ours wins
			reinterpret_cast<DeleteFn>(s->vftable[0])(s, nullptr, 1);
		return ours;
	}
	return s;
}

// Appends a game string to a game vector, growing it with the game's allocator.
bool Append(GameStrings *out, const std::string &text)
{
	const NewFn game_new = reinterpret_cast<NewFn>(kGameNew);
	if (out->end == out->cap) {
		const size_t n = out->end - out->begin;
		const size_t cap = n ? 2 * n : 8;
		GameString *items = static_cast<GameString *>(game_new(cap * sizeof(GameString)));
		if (!items)
			return false;
		if (n)
			memcpy(items, out->begin, n * sizeof(GameString));
		if (out->begin)
			reinterpret_cast<FreeFn>(kGameDelete)(out->begin);
		out->begin = items;
		out->end = items + n;
		out->cap = items + cap;
	}
	char *buf = static_cast<char *>(game_new(text.size() + 1));
	if (!buf)
		return false;
	memcpy(buf, text.c_str(), text.size() + 1);
	out->end->begin = buf;
	out->end->end = buf + text.size();
	out->end->cap = buf + text.size() + 1;
	++out->end;
	return true;
}

void __fastcall ListHook(void *vfs, void *edx, GameStrings *out, const GameString *dir, const GameString *mask)
{
	g_list_next(vfs, edx, out, dir, mask);
	if (!out || !dir || !mask)
		return;
	std::string folder = Normalize(dir->begin, dir->end);
	if (!folder.empty() && folder.back() != '/')
		folder += '/';
	// Everything of ours is in rmg/templates/: other folders get nothing.
	if (std::string(kTemplatesDir).compare(0, folder.size(), folder) != 0)
		return;
	const std::vector<std::string> recipes = Recipes();
	const std::string pattern(mask->begin, mask->end);
	// Adds RMG\Templates\<file>, unless the mask doesn't match it or the list has it already.
	auto offer = [out, &pattern](const std::string &key, const std::string &file) {
		if (!Matches(pattern.c_str(), file.c_str()))
			return false;
		for (const GameString *s = out->begin; s != out->end; ++s)
			if (Normalize(s->begin, s->end) == key)
				return false;
		return Append(out, "RMG\\Templates\\" + file);
	};
	int added = 0, added_recipes = 0;
	for (const Template &t : g_templates)
		added += offer(t.key, t.file);
	for (const std::string &r : recipes) {
		const std::string inside = kTemplatesDir + r + ".xdb";
		added_recipes += offer(Normalize(inside.data(), inside.data() + inside.size()), r + ".xdb");
	}
	static LONG logged = 0;
	if ((added || added_recipes) && InterlockedIncrement(&logged) <= 3)
		hooklog::Write("rmg templates: %d of our files and %d recipes added to the list of %s%s", added, added_recipes,
		               folder.c_str(), pattern.c_str());
}

// [rmg] recipes=OH-X1-...,OH-X1-...: recipe names to offer in the game's list
// (for tests, and until the builder panel exists). See rmg::ParseRecipeList.
void ReadRecipesSetting(const wchar_t *ini)
{
	wchar_t text[4096];
	GetPrivateProfileStringW(L"rmg", L"recipes", L"", text, ARRAYSIZE(text), ini);
	std::string narrow;
	for (const wchar_t *p = text; *p; ++p)
		narrow += *p < 0x80 ? static_cast<char>(*p) : '?';  // recipe names are plain ASCII
	std::vector<std::string> names, bad;
	mods::rmg::ParseRecipeList(narrow, &names, &bad);
	for (const std::string &b : bad)
		hooklog::Write("rmg templates: recipes=: \"%s\" is not a recipe name (%s and 16 characters), skipped", b.c_str(),
		               mods::rmg::kRecipePrefix);
	for (const std::string &n : names)
		if (!AddRecipe(n))
			hooklog::Write("rmg templates: recipes=: %s not listed (more than %u)", n.c_str(), static_cast<unsigned>(kMaxRecipes));
}

// Serves the folder's files and the recipes; true when recipes are served.
bool ServeTemplates(const wchar_t *path)
{
	if (!GetPrivateProfileIntW(L"rmg", L"templates", 1, path))
		return false;
	LoadTemplates();
	g_recipes_on = MakerSelfCheck();
	if (g_recipes_on)
		ReadRecipesSetting(path);
	if (g_templates.empty() && !g_recipes_on)
		return false;
	// The hooks go in even with no file and no recipe to offer: any recipe name
	// the game asks for (the template of a game this PC joins) must be served.
	g_selftest = GetPrivateProfileIntW(L"rmg", L"templates_selftest", 0, path) ? 1 : 0;
	g_open_next = reinterpret_cast<OpenFn>(hookpatch::PatchPointer(kWinVfsOpenSlot, kWinVfsOpen, &OpenHook));
	g_list_next = reinterpret_cast<ListFn>(hookpatch::PatchPointer(kWinVfsListSlot, kWinVfsList, &ListHook));
	if (!g_open_next || !g_list_next) {
		hooklog::Write("rmg templates: the game's file system is not as expected: our templates are not added");
		return false;
	}
	std::string names;
	for (size_t i = 0; i < g_templates.size() && i < 12; ++i)
		names += (i ? ", " : "") + g_templates[i].file;
	hooklog::Write("rmg templates: %u template(s) from open-homm5\\templates%s%s%s", static_cast<unsigned>(g_templates.size()),
	               names.empty() ? "" : ": ", names.c_str(), g_templates.size() > 12 ? ", ..." : "");
	if (!g_recipes_on)
		return false;
	std::string offered;
	for (const std::string &r : Recipes())
		offered += (offered.empty() ? "" : ", ") + r;
	hooklog::Write("rmg templates: recipe names (RMG/Templates/%s....xdb) are built when the game asks for them (maker "
	               "self-check ok); in the list from recipes=: %s",
	               mods::rmg::kRecipePrefix, offered.empty() ? "none" : offered.c_str());
	return true;
}

}  // namespace

bool RegisterRecipe(const std::string &recipe)
{
	return AddRecipe(recipe, true);
}

bool RecipesServed()
{
	return g_recipes_on && g_open_next && g_list_next;
}

void *NewGameStream(const std::string &data)
{
	return NewStream(data);
}

void ApplyRmgTemplatesFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	// Joining random map games: the guard against a missing template goes in
	// even with templates=0; the recipe part only when recipes are served.
	ApplyRmgJoin(ServeTemplates(path));
}

}  // namespace mods
