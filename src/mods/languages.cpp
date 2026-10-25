// Mod: the game's texts in French for every part of the game (docs/plans/languages.md).
//
// With MMH5.5 the game mixes languages: the base game's texts are in the
// language of the install (French on a French Steam copy), the 3.1 patch's
// and MMH5.5's are English, and the newest file of a path wins. With
// [lang] texts=fr, every text the game reads is taken, when it exists there,
// from the packs in bin\open-homm5\lang\fr\ (*.pak, also *.zip): MMH5.5's own
// French pack for the player's MMH5.5 version (each player downloads it from
// MMH5.5's ModDB page; any version works, the log names it) and our pack of
// what it leaves out. Nothing on disk changes, and each player chooses for
// themselves: texts are only shown, never part of the game world (the game
// objects keep a text's path, not its words), so players with different
// choices should be able to play together (the robot LAN check is in the plan).
//
// How the game reads a text (docs/subsystems/texts.md): the first time a text
// is needed, 0x004c7430 opens its .txt file through the game's file system
// (NVFS::CWinVFS vftable 0x00e66500, slot 7 = 0x009e0020, which jumps to
// 0x009dfef0), reads it as UTF-16 after an FF FE mark (0x004c7310: without the
// mark the text is empty) and keeps it until the game closes. We wrap slot 7:
// for a .txt path one of our packs has, the game gets a new memory stream with
// that file instead (UTF-16 with the mark, made so if the pack has UTF-8).
// Every other file goes on to the game unchanged. Only .txt files are ever
// taken from a pack, so a language pack cannot change game data.
//
// When: the hook goes in when the DLL is loaded, before the game reads its first
// text; the pack index is built when the game opens its first text.
// Among our packs the newest file of a path wins, like in the game; on equal
// dates the pack whose name sorts last. A change of setting needs a restart
// (the game keeps the texts it has read).
//
// Settings, open-homm5.ini [lang]:
//   texts=game     game (default: the texts as installed), fr; en is planned,
//                  not built yet (the game's own texts are used)
//   log_texts=0    testing: log the first N texts taken from our packs
#include "languages.h"

#include <windows.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

// ---- the game (vanilla 3.1 layout, also MMH55_Utility) ----

const DWORD kWinVfsOpenSlot = 0x00e6651c;  // NVFS::CWinVFS vftable 0x00e66500, slot 7: OpenFile
const DWORD kWinVfsOpen = 0x009e0020;      // __thiscall(CWinVFS *, const nstl::string *path), ret 4
const unsigned char kWinVfsOpenCode[5] = {0xe9, 0xcb, 0xfe, 0xff, 0xff};  // jmp 0x009dfef0
const DWORD kMemoryStreamCtor = 0x004034d0;  // __thiscall(this): empty CMemoryStream
const DWORD kStreamSetSize = 0x008c7610;     // __thiscall(this, int size), ret 4: new buffer of size bytes
const DWORD kGameNew = 0x00878fd0;           // cdecl void *(size): the game's operator new

struct GameString {  // nstl::string: first char, one past the last, end of the allocation
	const char *begin;
	const char *end;
	const char *cap;
};
struct GameStream {  // CMemoryStream, 0x18 bytes
	void **vftable;  // slot 0: scalar deleting destructor (this, flags)
	char *begin;
	char *cap;
	char *end;
	char *pos;
	DWORD flags;  // bit 0: broken
};
static_assert(sizeof(GameStream) == 0x18, "CMemoryStream is 0x18 bytes");

typedef GameStream *(__fastcall *OpenFn)(void *vfs, void *edx, const GameString *path);
typedef GameStream *(__fastcall *CtorFn)(GameStream *self, void *edx);
typedef void(__fastcall *SetSizeFn)(GameStream *self, void *edx, int size);
typedef void(__fastcall *DeleteFn)(GameStream *self, void *edx, int flags);
typedef void *(__cdecl *NewFn)(size_t size);

OpenFn g_open_next = nullptr;

// ---- the setting ----

Texts g_choice = Texts::Game;
bool g_read = false;
LONG g_log_texts = 0;

void ReadSettings()
{
	if (g_read)
		return;
	g_read = true;
	wchar_t ini[MAX_PATH];
	hooklog::PathNextToExe(L"open-homm5", L".ini", ini, MAX_PATH);
	wchar_t text[32];
	GetPrivateProfileStringW(L"lang", L"texts", L"game", text, 32, ini);
	if (_wcsicmp(text, L"fr") == 0)
		g_choice = Texts::French;
	else if (_wcsicmp(text, L"en") == 0)
		g_choice = Texts::English;
	else {
		g_choice = Texts::Game;
		if (_wcsicmp(text, L"game") != 0)
			hooklog::Write("languages: texts=%ls is not game, fr or en: the game's own texts", text);
	}
	g_log_texts = static_cast<LONG>(GetPrivateProfileIntW(L"lang", L"log_texts", 0, ini));
}

// ---- zlib (the game's zlib1.dll, cdecl) ----

struct ZStream {  // z_stream of zlib 1.2.x, 32-bit
	const BYTE *next_in;
	unsigned avail_in;
	unsigned long total_in;
	BYTE *next_out;
	unsigned avail_out;
	unsigned long total_out;
	const char *msg;
	void *state;
	void *zalloc;
	void *zfree;
	void *opaque;
	int data_type;
	unsigned long adler;
	unsigned long reserved;
};
static_assert(sizeof(ZStream) == 0x38, "z_stream is 0x38 bytes");

typedef int(__cdecl *InflateInit2Fn)(ZStream *, int window_bits, const char *version, int stream_size);
typedef int(__cdecl *InflateFn)(ZStream *, int flush);
typedef int(__cdecl *InflateEndFn)(ZStream *);
InflateInit2Fn g_inflate_init = nullptr;
InflateFn g_inflate = nullptr;
InflateEndFn g_inflate_end = nullptr;

bool LoadZlib()
{
	HMODULE z = GetModuleHandleW(L"zlib1.dll");  // the game imports it
	if (!z)
		return false;
	g_inflate_init = reinterpret_cast<InflateInit2Fn>(GetProcAddress(z, "inflateInit2_"));
	g_inflate = reinterpret_cast<InflateFn>(GetProcAddress(z, "inflate"));
	g_inflate_end = reinterpret_cast<InflateEndFn>(GetProcAddress(z, "inflateEnd"));
	return g_inflate_init && g_inflate && g_inflate_end;
}

// Raw deflate data (zip method 8) of known size.
bool Inflate(const std::string &packed, size_t size, std::string *out)
{
	out->assign(size, '\0');
	if (size == 0)
		return true;
	ZStream z = {};
	z.next_in = reinterpret_cast<const BYTE *>(packed.data());
	z.avail_in = static_cast<unsigned>(packed.size());
	z.next_out = reinterpret_cast<BYTE *>(&(*out)[0]);
	z.avail_out = static_cast<unsigned>(size);
	if (g_inflate_init(&z, -15, "1.2.3", sizeof(z)) != 0)
		return false;
	const int r = g_inflate(&z, 4 /* Z_FINISH */);
	g_inflate_end(&z);
	return r == 1 /* Z_STREAM_END */ && z.total_out == size;
}

// ---- our packs ----

const DWORD kMaxText = 1 << 20;   // a text file is a few hundred bytes; more is not a text
const size_t kMaxTexts = 200000;

struct Pack {
	std::string name;  // for the log
	HANDLE file;
	int texts;         // its files we use
};
struct Text {
	int pack;
	lang::ZipEntry entry;
};
std::vector<Pack> g_packs;
std::unordered_map<std::string, Text> g_texts;

bool ReadAt(HANDLE file, unsigned long long offset, void *buffer, DWORD size)
{
	OVERLAPPED at = {};  // a read at an offset: safe from several threads at once
	at.Offset = static_cast<DWORD>(offset);
	at.OffsetHigh = static_cast<DWORD>(offset >> 32);
	DWORD got = 0;
	return ReadFile(file, buffer, size, &got, &at) && got == size;
}

std::string Narrow(const std::wstring &w)
{
	char buffer[MAX_PATH * 3];
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, buffer, sizeof(buffer), nullptr, nullptr);
	return n > 0 ? std::string(buffer) : std::string("?");
}

// <exe folder>\open-homm5\lang\fr\ (the robot's copies have their own exe folder)
std::wstring PackFolder()
{
	wchar_t dir[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
	while (n > 0 && dir[n - 1] != L'\\')
		--n;
	return std::wstring(dir, n) + L"open-homm5\\lang\\fr\\";
}

bool ReadDirectoryOf(HANDLE file, std::vector<lang::ZipEntry> *out)
{
	LARGE_INTEGER size;
	if (!GetFileSizeEx(file, &size) || size.QuadPart < 22 || size.QuadPart > 0x7fffffff)
		return false;
	const DWORD tail_size = static_cast<DWORD>(std::min<long long>(size.QuadPart, 0xffff + 22));
	std::vector<unsigned char> tail(tail_size);
	DWORD dir_offset = 0, dir_size = 0;
	if (!ReadAt(file, size.QuadPart - tail_size, tail.data(), tail_size) ||
	    !lang::FindDirectory(tail.data(), tail.size(), &dir_offset, &dir_size))
		return false;
	if (dir_size == 0 || dir_offset + static_cast<unsigned long long>(dir_size) > static_cast<unsigned long long>(size.QuadPart))
		return false;
	std::vector<unsigned char> dir(dir_size);
	if (!ReadAt(file, dir_offset, dir.data(), dir_size))
		return false;
	lang::ReadDirectory(dir.data(), dir.size(), out);
	return true;
}

bool BuildIndex()
{
	const DWORD start = GetTickCount();
	const std::wstring folder = PackFolder();
	if (!LoadZlib()) {
		hooklog::Write("languages: zlib1.dll (inflate) not found in the game: the game's own texts");
		return false;
	}
	// .pak (a zip, as the game's) and .zip (a pack someone did not rename)
	std::vector<std::wstring> names;
	for (const wchar_t *pattern : {L"*.pak", L"*.zip"}) {
		WIN32_FIND_DATAW found;
		HANDLE find = FindFirstFileW((folder + pattern).c_str(), &found);
		if (find == INVALID_HANDLE_VALUE)
			continue;
		do {
			if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				names.push_back(found.cFileName);
		} while (FindNextFileW(find, &found));
		FindClose(find);
	}
	// our own order, the same on Windows and Wine: by name, any case
	std::sort(names.begin(), names.end(), [](const std::wstring &a, const std::wstring &b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
	if (names.empty()) {
		hooklog::Write("languages: texts=fr, but no .pak (or .zip) in %s: the game's own texts (see docs/plans/languages.md)",
		               Narrow(folder).c_str());
		return false;
	}
	int replaced = 0;
	for (const std::wstring &name : names) {
		HANDLE file = CreateFileW((folder + name).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
		                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			hooklog::Write("languages: cannot open %s (error %lu): left out", Narrow(name).c_str(), GetLastError());
			continue;
		}
		std::vector<lang::ZipEntry> entries;
		if (!ReadDirectoryOf(file, &entries)) {
			hooklog::Write("languages: %s is not a zip archive (.pak) we can read: left out", Narrow(name).c_str());
			CloseHandle(file);
			continue;
		}
		const int pack = static_cast<int>(g_packs.size());
		g_packs.push_back(Pack{Narrow(name), file, 0});
		bool has_text = false;
		std::string inner;  // a .pak inside (the download from ModDB is a zip around the .pak)
		for (const lang::ZipEntry &e : entries) {
			const std::string &k = e.key;
			if (k.size() > 4 && k.compare(k.size() - 4, 4, ".pak") == 0)
				inner = k;
			if (!lang::IsTextPath(k.data(), k.data() + k.size()) || (e.method != 0 && e.method != 8) || e.size > kMaxText ||
			    e.packed_size > kMaxText + 1024 || k.find(':') != std::string::npos)
				continue;
			has_text = true;
			auto it = g_texts.find(k);
			if (it == g_texts.end()) {
				if (g_texts.size() >= kMaxTexts)
					continue;
				g_texts.emplace(k, Text{pack, e});
				++g_packs[pack].texts;
			} else if (e.dos_time >= it->second.entry.dos_time) {  // newer, or equal and this pack sorts later
				if (it->second.pack != pack) {
					--g_packs[it->second.pack].texts;
					++g_packs[pack].texts;
					++replaced;
				}
				it->second = Text{pack, e};
			}
		}
		if (!has_text && !inner.empty())
			hooklog::Write("languages: %s holds %s, not texts: unzip it and put the .pak itself in open-homm5\\lang\\fr",
			               Narrow(name).c_str(), inner.c_str());
	}
	if (g_texts.empty()) {
		hooklog::Write("languages: texts=fr, but the packs in %s hold no .txt file: the game's own texts",
		               Narrow(folder).c_str());
		return false;
	}
	std::string list;
	for (const Pack &p : g_packs)
		list += (list.empty() ? "" : ", ") + p.name + " (" + std::to_string(p.texts) + ")";
	hooklog::Write("languages: texts=fr: %u French texts from open-homm5\\lang\\fr: %s; %d of them newer than "
	               "another pack's; indexed in %lu ms",
	               static_cast<unsigned>(g_texts.size()), list.c_str(), replaced, GetTickCount() - start);
	return true;
}

CRITICAL_SECTION g_init_lock;
volatile LONG g_state = 0;  // 0 not built yet, 1 ready, 2 off

bool Ready()
{
	if (g_state == 0) {
		EnterCriticalSection(&g_init_lock);
		if (g_state == 0)
			InterlockedExchange(&g_state, BuildIndex() ? 1 : 2);
		LeaveCriticalSection(&g_init_lock);
	}
	return g_state == 1;
}

// One text of a pack, as the game reads texts.
bool ReadText(const Text &t, std::string *out)
{
	const lang::ZipEntry &e = t.entry;
	HANDLE file = g_packs[t.pack].file;
	unsigned char local[30];
	if (!ReadAt(file, e.header, local, sizeof(local)))
		return false;
	const DWORD data = lang::DataStart(local);
	if (!data)
		return false;
	std::string packed(e.packed_size, '\0');
	if (e.packed_size && !ReadAt(file, static_cast<unsigned long long>(e.header) + data, &packed[0], e.packed_size))
		return false;
	std::string raw;
	if (e.method == 0) {
		if (packed.size() != e.size)
			return false;
		raw.swap(packed);
	} else if (!Inflate(packed, e.size, &raw)) {
		return false;
	}
	return lang::ToGameText(raw, out);
}

// A new memory stream of the game with these bytes (the game deletes it).
GameStream *NewStream(const std::string &data)
{
	GameStream *m = static_cast<GameStream *>(reinterpret_cast<NewFn>(kGameNew)(sizeof(GameStream)));
	if (!m)
		return nullptr;
	reinterpret_cast<CtorFn>(kMemoryStreamCtor)(m, nullptr);
	reinterpret_cast<SetSizeFn>(kStreamSetSize)(m, nullptr, static_cast<int>(data.size()));
	if ((m->flags & 1) || !m->begin || static_cast<size_t>(m->end - m->begin) != data.size()) {
		reinterpret_cast<DeleteFn>(m->vftable[0])(m, nullptr, 1);
		return nullptr;
	}
	memcpy(m->begin, data.data(), data.size());
	return m;
}

// A game text for the log: UTF-8, tags and line breaks out, at most 60 characters.
std::string ForLog(const std::string &game_bytes)
{
	std::wstring w;
	if (game_bytes.size() >= 2)
		w.assign(reinterpret_cast<const wchar_t *>(game_bytes.data() + 2), (game_bytes.size() - 2) / 2);
	std::wstring plain;
	bool tag = false;
	for (wchar_t c : w) {
		if (c == L'<')
			tag = true;
		else if (c == L'>')
			tag = false;
		else if (!tag && c != L'\r')
			plain += c == L'\n' ? L' ' : c;
	}
	if (plain.size() > 60)
		plain.resize(60);
	return Narrow(plain);
}

// MMH5.5's version line in the main menu: which version our French was made
// for, and which version the game has, side by side in the log.
void LogVersions(void *vfs, void *edx, const GameString *path, const std::string &ours)
{
	std::string game;
	GameStream *s = g_open_next(vfs, edx, path);
	if (s) {
		if (!(s->flags & 1) && s->begin && s->end > s->begin) {
			std::string raw(s->begin, s->end), bytes;
			if (lang::ToGameText(raw, &bytes))
				game = ForLog(bytes);
		}
		reinterpret_cast<DeleteFn>(s->vftable[0])(s, nullptr, 1);
	}
	const std::string mine = ForLog(ours);
	hooklog::Write("languages: version text: French pack \"%s\", the game \"%s\"%s", mine.c_str(), game.c_str(),
	               mine == game || game.empty() ? ""
	                                            : " (another MMH5.5 version: some French descriptions may not match the game)");
}

volatile LONG g_served = 0;
volatile LONG g_failed = 0;

GameStream *__fastcall OpenFileHook(void *vfs, void *edx, const GameString *path)
{
	if (!path || !path->begin || path->end <= path->begin || !lang::IsTextPath(path->begin, path->end) || !Ready())
		return g_open_next(vfs, edx, path);
	const auto it = g_texts.find(lang::NormalizePath(path->begin, path->end));
	if (it == g_texts.end())
		return g_open_next(vfs, edx, path);
	std::string data;
	GameStream *ours = ReadText(it->second, &data) ? NewStream(data) : nullptr;
	if (!ours) {
		if (InterlockedIncrement(&g_failed) <= 20)
			hooklog::Write("languages: cannot read %s from %s: the game's own text", it->first.c_str(),
			               g_packs[it->second.pack].name.c_str());
		return g_open_next(vfs, edx, path);
	}
	if (it->first == "ui/mainmenu2/version.txt")
		LogVersions(vfs, edx, path, data);
	const LONG n = InterlockedIncrement(&g_served);
	if (n <= g_log_texts)
		hooklog::Write("languages: [%ld] %s from %s: \"%s\"", n, it->first.c_str(), g_packs[it->second.pack].name.c_str(),
		               ForLog(data).c_str());
	else if (n == 100 || n == 1000 || n % 5000 == 0)
		hooklog::Write("languages: %ld French texts given to the game so far", n);
	return ours;
}

bool g_installed = false;
bool g_early = false;

void Install(bool early)
{
	if (g_installed)
		return;
	ReadSettings();
	if (g_choice != Texts::French)
		return;
	// the code behind the slot must be the 3.1 file system's (a thunk to 0x009dfef0)
	if (memcmp(reinterpret_cast<const void *>(kWinVfsOpen), kWinVfsOpenCode, sizeof(kWinVfsOpenCode)) != 0) {
		if (!early)
			hooklog::Write("languages: the game's file system is not as expected (%08lx): the game's own texts", kWinVfsOpen);
		return;
	}
	static bool lock_made = false;
	if (!lock_made) {
		InitializeCriticalSection(&g_init_lock);
		lock_made = true;
	}
	g_open_next = reinterpret_cast<OpenFn>(hookpatch::PatchPointer(kWinVfsOpenSlot, kWinVfsOpen, &OpenFileHook));
	if (!g_open_next) {
		if (!early)
			hooklog::Write("languages: the game's file system table is not as expected (%08lx): the game's own texts",
			               kWinVfsOpenSlot);
		return;
	}
	g_installed = true;
	g_early = early;
}

}  // namespace

Texts TextsChoice()
{
	ReadSettings();
	return g_choice;
}

void InstallLanguagesEarly()
{
	Install(true);
}

void ApplyLanguagesFromSettings()
{
	ReadSettings();
	switch (g_choice) {
	case Texts::Game:
		hooklog::Write("languages: texts=game (the texts as installed)");
		return;
	case Texts::English:
		hooklog::Write("languages: texts=en is not available yet: the texts as installed");
		return;
	case Texts::French:
		break;
	}
	Install(false);
	if (!g_installed)
		return;
	hooklog::Write("languages: texts=fr: the game's texts come from open-homm5\\lang\\fr\\*.pak where they have them%s",
	               g_early ? "" : " (installed late: texts read before now stay as they were)");
}

}  // namespace mods
