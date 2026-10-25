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
// Setting, open-homm5.ini [rmg]: templates=1 (default; 0 = off).
#include <windows.h>
#include <string.h>

#include <string>
#include <vector>

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
	int all = 0, ours = 0;
	for (const GameString *s = out.begin; s && s != out.end; ++s, ++all)
		for (const Template &t : g_templates)
			ours += Normalize(s->begin, s->end) == t.key;
	hooklog::Write("rmg templates: self-test: the game lists %d templates in RMG/Templates, %d of them ours", all, ours);
}

GameStream *__fastcall OpenHook(void *vfs, void *edx, const GameString *path)
{
	if (g_selftest == 1 && InterlockedExchange(&g_selftest, 2) == 1)
		SelfTest();
	GameStream *s = g_open_next(vfs, edx, path);
	if (!path || !path->begin || path->end - path->begin < 14)
		return s;
	const std::string key = Normalize(path->begin, path->end);
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
	if (!out || !dir || !mask || g_templates.empty())
		return;
	std::string folder = Normalize(dir->begin, dir->end);
	if (!folder.empty() && folder.back() != '/')
		folder += '/';
	const std::string pattern(mask->begin, mask->end);
	int added = 0;
	for (const Template &t : g_templates) {
		if (t.key.compare(0, folder.size(), folder) != 0 || !Matches(pattern.c_str(), t.file.c_str()))
			continue;
		bool known = false;
		for (const GameString *s = out->begin; s != out->end && !known; ++s)
			known = Normalize(s->begin, s->end) == t.key;
		if (!known && Append(out, "RMG\\Templates\\" + t.file))
			++added;
	}
	static LONG logged = 0;
	if (added && InterlockedIncrement(&logged) <= 3)
		hooklog::Write("rmg templates: %d of ours added to the list of %s%s", added, folder.c_str(), pattern.c_str());
}

}  // namespace

void ApplyRmgTemplatesFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	if (!GetPrivateProfileIntW(L"rmg", L"templates", 1, path))
		return;
	LoadTemplates();
	if (g_templates.empty())
		return;
	g_selftest = GetPrivateProfileIntW(L"rmg", L"templates_selftest", 0, path) ? 1 : 0;
	g_open_next = reinterpret_cast<OpenFn>(hookpatch::PatchPointer(kWinVfsOpenSlot, kWinVfsOpen, &OpenHook));
	g_list_next = reinterpret_cast<ListFn>(hookpatch::PatchPointer(kWinVfsListSlot, kWinVfsList, &ListHook));
	if (!g_open_next || !g_list_next) {
		hooklog::Write("rmg templates: the game's file system is not as expected: our templates are not added");
		return;
	}
	std::string names;
	for (size_t i = 0; i < g_templates.size() && i < 12; ++i)
		names += (i ? ", " : "") + g_templates[i].file;
	hooklog::Write("rmg templates: %u template(s) from open-homm5\\templates: %s%s", static_cast<unsigned>(g_templates.size()),
	               names.c_str(), g_templates.size() > 12 ? ", ..." : "");
}

}  // namespace mods
