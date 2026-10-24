// Mod: the original Heroes V music for MMH5.5 players.
//
// MMH5.5 brings its own music: new tracks for some terrains, the main menu, the
// credits, battles and heroes. Some players prefer the original soundtrack. With
// [audio] original_music=1 the game plays, wherever MMH5.5 changed the music,
// what the original Heroes V (Tribes of the East 3.1) plays there. Nothing on
// disk changes. The default (0) is MMH5.5's music, and then this file installs
// nothing.
//
// How the game picks music (docs/subsystems/audio.md): every track has a small
// "music definition" file in the game data (Sounds/_(Music)/.../x.xdb, giving
// the .ogg file and its fade times), and other data files point at them: each
// map tile type names the music of its terrain, some heroes have a theme, a
// table names each faction's town and battle music. MMH5.5's .pak files
// replace some of those files. One MMH5.5 track can stand for several
// different original tracks (Adventure_Terrain_Swamp_002.ogg plays on tiles
// that had the dirt, grass or water music), so a simple "file A -> file B"
// swap of .ogg files cannot be right; the definitions and references are.
//
// How: the game reads its data through a virtual file system that looks into
// the .pak files (zip archives). We wrap its "open file" function
// (NVFS::CWinVFS vftable 0x00e66500, slot 7 = 0x009e0020, which jumps to
// 0x009dfef0). When the game opens a data file (.xdb) that a mod's .pak
// provides and that mentions music, we read the same file from the original
// game's data.pak / a2p1-data.pak ourselves and give the game a copy of the
// mod's file with the original music (original_music_xdb.cpp):
//   - music definitions and tables (Sounds/_(Music)/...): the original file;
//   - other files (map tiles, heroes): only their music references are set back
//     to the original's (also where MMH5.5 removed the music, e.g. on roads);
//     references to music where the original game has none (new MMH5.5 tiles
//     and heroes, hero themes MMH5.5 added) are emptied, so the game picks
//     music there as the original does (terrain and map music).
// Every other byte of the mod's files is kept, so gameplay data is untouched.
// The original files are read at run time, so this works with any MMH5.5
// version; the zip reading is ours, the decompression is the game's own zlib1.dll.
//
// When: the game reads all heroes during its splash screen, before the other
// hooks go in (first DirectInput call), so this hook goes in when the DLL is
// loaded (InstallOriginalMusicEarly). Only the table entry changes then; the
// index of the paks is built when the game opens its first file (~40 ms).
//
// Testing settings: [audio] log_music=1 logs the name of every music file the
// game starts (fmod.dll!FSOUND_Stream_Open, import table), at most 300 lines;
// log_data_files=N logs the first N files the game opens through its file system.
#include "original_music.h"

#include <windows.h>
#include <string.h>

#include <algorithm>

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace {

// ---- the game (vanilla 3.1 layout, also MMH55_Utility) ----

const DWORD kWinVfsOpenSlot = 0x00e6651c;      // NVFS::CWinVFS vftable 0x00e66500, slot 7: OpenFile
const DWORD kWinVfsOpen = 0x009e0020;          // __thiscall(CWinVFS *, const nstl::string *path), ret 4
const DWORD kMemoryStreamVftable = 0x00e0a980;  // CMemoryStream
const DWORD kMemoryStreamCtor = 0x004034d0;     // __thiscall(this): empty stream, returns this
const DWORD kStreamSetSize = 0x008c7610;        // __thiscall(this, int size), ret 4: new buffer of size bytes
const DWORD kGameNew = 0x00878fd0;              // cdecl void *(size): the game's operator new

// The game's strings (Nival's nstl): a pointer to the first char and one past
// the last, then the end of the allocation.
struct GameString {
	const char *begin;
	const char *end;
	const char *cap;
};

// The game's data streams (CDataStream and its subclasses): the whole content
// is in memory from begin to end (a zip entry is inflated into a CMemoryStream
// when opened; a stored one is a mapped view).
struct GameStream {
	void **vftable;  // slot 0: scalar deleting destructor (this, flags)
	char *begin;
	char *cap;       // end of the buffer
	char *end;       // end of the data
	char *pos;       // read position
	DWORD flags;     // bit 0: broken
};
static_assert(sizeof(GameStream) == 0x18, "CMemoryStream is 0x18 bytes");

typedef GameStream *(__fastcall *OpenFn)(void *vfs, void *edx, const GameString *path);
typedef GameStream *(__fastcall *CtorFn)(GameStream *self, void *edx);
typedef void(__fastcall *SetSizeFn)(GameStream *self, void *edx, int size);
typedef void(__fastcall *DeleteFn)(GameStream *self, void *edx, int flags);
typedef void *(__cdecl *NewFn)(size_t size);

OpenFn g_open_next = nullptr;

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
	HMODULE z = GetModuleHandleW(L"zlib1.dll");  // the game imports it, so it is loaded
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

// ---- paths ----

// "/MapObjects\_(AdvMapTile)/Grass/Flowers.xdb" -> "mapobjects/_(advmaptile)/grass/flowers.xdb"
std::string Normalize(const char *begin, const char *end)
{
	while (begin < end && (*begin == '/' || *begin == '\\'))
		++begin;
	std::string s(begin, end);
	for (char &c : s) {
		if (c == '\\')
			c = '/';
		else if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return s;
}

unsigned long long Hash(const std::string &s)
{
	unsigned long long h = 14695981039346656037ull;  // FNV-1a
	for (unsigned char c : s)
		h = (h ^ c) * 1099511628211ull;
	return h;
}

bool IsXdb(const std::string &s)
{
	return s.size() > 4 && s.compare(s.size() - 4, 4, ".xdb") == 0;
}

// ---- the .pak files (zip archives) ----

bool ReadAt(HANDLE file, unsigned long long offset, void *buffer, DWORD size)
{
	OVERLAPPED at = {};  // a read at an offset: safe from several threads at once
	at.Offset = static_cast<DWORD>(offset);
	at.OffsetHigh = static_cast<DWORD>(offset >> 32);
	DWORD got = 0;
	return ReadFile(file, buffer, size, &got, &at) && got == size;
}

DWORD U32(const BYTE *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<DWORD>(p[3]) << 24);
}

WORD U16(const BYTE *p)
{
	return static_cast<WORD>(p[0] | (p[1] << 8));
}

struct ZipEntry {
	std::string name;  // normalized
	WORD method;       // 0 stored, 8 deflated
	DWORD packed_size;
	DWORD size;
	DWORD header;  // offset of the local header
};

// Every file of a zip archive, from its central directory. The entry count in
// the end record is only 16 bits (data.pak has more than 65535 files), so the
// directory is walked by its size.
template <class F>
bool ForEachEntry(HANDLE file, F f)
{
	LARGE_INTEGER size;
	if (!GetFileSizeEx(file, &size) || size.QuadPart < 22)
		return false;
	const DWORD tail = static_cast<DWORD>(std::min<long long>(size.QuadPart, 0xffff + 22));
	std::vector<BYTE> t(tail);
	if (!ReadAt(file, size.QuadPart - tail, t.data(), tail))
		return false;
	size_t eocd = tail - 22 + 1;
	while (eocd-- > 0)
		if (U32(&t[eocd]) == 0x06054b50)
			break;
	if (eocd == static_cast<size_t>(-1))
		return false;
	const DWORD dir_size = U32(&t[eocd + 12]);
	const DWORD dir_offset = U32(&t[eocd + 16]);
	std::vector<BYTE> dir(dir_size);
	if (dir_size == 0 || !ReadAt(file, dir_offset, dir.data(), dir_size))
		return false;
	for (size_t p = 0; p + 46 <= dir.size() && U32(&dir[p]) == 0x02014b50;) {
		const BYTE *e = &dir[p];
		const WORD name_len = U16(e + 28);
		const size_t next = p + 46 + name_len + U16(e + 30) + U16(e + 32);
		if (next > dir.size())
			break;
		const char *name = reinterpret_cast<const char *>(e + 46);
		f(ZipEntry{Normalize(name, name + name_len), U16(e + 10), U32(e + 20), U32(e + 24), U32(e + 42)});
		p = next;
	}
	return true;
}

std::vector<HANDLE> g_paks;  // the original game's paks we read from (kept open)

// A file of the original game: where it is in which pak.
struct Original {
	unsigned long long hash;
	ZipEntry entry;
	int pak;
};
std::vector<Original> g_originals;           // sorted by hash
std::vector<unsigned long long> g_mod_files;  // hashes of the .xdb files the mods' paks provide, sorted

const Original *FindOriginal(unsigned long long hash)
{
	auto it = std::lower_bound(g_originals.begin(), g_originals.end(), hash,
	                           [](const Original &o, unsigned long long h) { return o.hash < h; });
	return it != g_originals.end() && it->hash == hash ? &*it : nullptr;
}

bool IsModFile(unsigned long long hash)
{
	return std::binary_search(g_mod_files.begin(), g_mod_files.end(), hash);
}

bool ReadOriginal(const Original &o, std::string *out)
{
	const ZipEntry &e = o.entry;
	if (e.size > (64u << 20) || (e.method != 0 && e.method != 8))
		return false;
	BYTE local[30];
	HANDLE pak = g_paks[o.pak];
	if (!ReadAt(pak, e.header, local, sizeof(local)) || U32(local) != 0x04034b50)
		return false;
	const WORD name_len = U16(local + 26);
	std::string name(name_len, '\0');
	if (!ReadAt(pak, e.header + 30, &name[0], name_len) || Normalize(name.data(), name.data() + name_len) != e.name)
		return false;  // not the file we wanted (cannot happen unless two names had the same hash)
	std::string packed(e.packed_size, '\0');
	const unsigned long long data = e.header + 30ull + name_len + U16(local + 28);
	if (e.packed_size && !ReadAt(pak, data, &packed[0], e.packed_size))
		return false;
	if (e.method == 0) {
		*out = packed;
		return out->size() == e.size;
	}
	return Inflate(packed, e.size, out);
}

// The paks of Tribes of the East 3.1 itself. Every other pak in data\ is a mod's.
const wchar_t *const kOriginalSources[] = {L"data.pak", L"a2p1-data.pak"};  // later ones win
const wchar_t *const kOriginalOthers[] = {L"texts.pak", L"a2p1-texts.pak", L"sound.pak", L"soundsfx.pak"};

bool IsOneOf(const wchar_t *name, const wchar_t *const *list, size_t n)
{
	for (size_t i = 0; i < n; ++i)
		if (_wcsicmp(name, list[i]) == 0)
			return true;
	return false;
}

HANDLE OpenRead(const std::wstring &path)
{
	return CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
	                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

std::string Narrow(const std::wstring &w)
{
	char buffer[MAX_PATH * 2];
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, buffer, sizeof(buffer), nullptr, nullptr);
	return n > 0 ? std::string(buffer) : std::string("?");
}

// <game>\data\, from <game>\bin\<exe>.
std::wstring DataFolder()
{
	wchar_t path[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
	for (int cut = 0; cut < 2; ++cut) {  // the exe's name, then bin
		while (n > 0 && path[n - 1] != L'\\')
			--n;
		if (n > 0)
			--n;
	}
	return std::wstring(path, n) + L"\\data\\";
}

// Index the mods' .xdb files and the original game's copies of them.
bool BuildIndex(std::string *mod_paks)
{
	const std::wstring folder = DataFolder();
	WIN32_FIND_DATAW found;
	HANDLE find = FindFirstFileW((folder + L"*.pak").c_str(), &found);
	if (find == INVALID_HANDLE_VALUE) {
		hooklog::Write("original music: no .pak files in %s: off", Narrow(folder).c_str());
		return false;
	}
	do {
		const wchar_t *name = found.cFileName;
		if (IsOneOf(name, kOriginalSources, 2) || IsOneOf(name, kOriginalOthers, 4))
			continue;
		HANDLE pak = OpenRead(folder + name);
		if (pak == INVALID_HANDLE_VALUE)
			continue;
		size_t before = g_mod_files.size();
		ForEachEntry(pak, [](const ZipEntry &e) {
			if (IsXdb(e.name))
				g_mod_files.push_back(Hash(e.name));
		});
		CloseHandle(pak);
		if (g_mod_files.size() > before)
			*mod_paks += (mod_paks->empty() ? "" : ", ") + Narrow(name);
	} while (FindNextFileW(find, &found));
	FindClose(find);
	std::sort(g_mod_files.begin(), g_mod_files.end());
	g_mod_files.erase(std::unique(g_mod_files.begin(), g_mod_files.end()), g_mod_files.end());
	if (g_mod_files.empty()) {
		hooklog::Write("original music: no mod in %s (only the original game's paks): nothing to do", Narrow(folder).c_str());
		return false;
	}

	// The original game's copies, only of the files a mod replaces.
	for (const wchar_t *source : kOriginalSources) {
		HANDLE pak = OpenRead(folder + source);
		if (pak == INVALID_HANDLE_VALUE) {
			hooklog::Write("original music: cannot open %s (error %lu)", Narrow(folder + source).c_str(), GetLastError());
			continue;
		}
		const int index = static_cast<int>(g_paks.size());
		g_paks.push_back(pak);
		ForEachEntry(pak, [index](const ZipEntry &e) {
			if (!IsXdb(e.name))
				return;
			const unsigned long long h = Hash(e.name);
			if (IsModFile(h))
				g_originals.push_back(Original{h, e, index});
		});
	}
	// sorted by hash; for the same file, the later pak (the 3.1 patch) wins
	std::stable_sort(g_originals.begin(), g_originals.end(),
	                 [](const Original &a, const Original &b) { return a.hash < b.hash; });
	std::vector<Original> unique;
	for (const Original &o : g_originals) {
		if (!unique.empty() && unique.back().hash == o.hash)
			unique.back() = o;
		else
			unique.push_back(o);
	}
	g_originals.swap(unique);
	if (g_originals.empty()) {
		hooklog::Write("original music: the original game's data.pak was not found or read: off");
		return false;
	}
	return true;
}

// ---- the hook ----

volatile LONG g_patched = 0;  // files given the original music so far
const LONG kDetailedLines = 150;
volatile LONG g_notes = 0;  // files that could not be matched
const LONG kMaxNotes = 50;

void LogPatched(const std::string &shown, const std::vector<music::Change> &changes)
{
	const LONG n = InterlockedIncrement(&g_patched);
	if (n > kDetailedLines) {
		if (n % 100 == 0)
			hooklog::Write("original music: %ld files set back to the original music so far", n);
		return;
	}
	std::string text;
	for (const music::Change &c : changes) {
		if (!text.empty())
			text += "; ";
		if (c.what == "track")
			text += "MMH5.5 track " + c.from + " -> original " + c.to;
		else if (c.what == "whole file")
			text += "original file (mod: " + c.from + ", original: " + c.to + ")";
		else
			text += c.what + " " + (c.from.empty() ? "none" : c.from) + " -> " + (c.to.empty() ? "none" : c.to);
	}
	hooklog::Write("original music: [%ld] %s: %s", n, shown.c_str(), text.c_str());
}

// A stream with this content instead of the one the game opened.
GameStream *WithContent(GameStream *s, const std::string &data)
{
	const SetSizeFn set_size = reinterpret_cast<SetSizeFn>(kStreamSetSize);
	const auto ok = [&data](const GameStream *m) {
		return !(m->flags & 1) && m->begin && static_cast<size_t>(m->end - m->begin) == data.size();
	};
	if (s->vftable == reinterpret_cast<void **>(kMemoryStreamVftable)) {
		// the usual case (a deflated zip entry): the same stream, new content
		set_size(s, nullptr, static_cast<int>(data.size()));  // frees the old buffer, allocates a new one
		if (ok(s))
			memcpy(s->begin, data.data(), data.size());
		return s;  // (out of memory: the game sees a broken stream, as for a failed read)
	}
	// a mapped view of the pak (a stored entry): a new memory stream instead
	GameStream *m = static_cast<GameStream *>(reinterpret_cast<NewFn>(kGameNew)(sizeof(GameStream)));
	if (!m)
		return s;
	reinterpret_cast<CtorFn>(kMemoryStreamCtor)(m, nullptr);
	set_size(m, nullptr, static_cast<int>(data.size()));
	GameStream *unused = m;
	if (ok(m)) {
		memcpy(m->begin, data.data(), data.size());
		unused = s;
	}
	reinterpret_cast<DeleteFn>(unused->vftable[0])(unused, nullptr, 1);
	return unused == s ? m : s;
}

bool g_music_on = false;     // [audio] original_music=1
LONG g_log_data_files = 0;  // [audio] log_data_files=N (testing): log the first N data files opened
volatile LONG g_data_files = 0;

// The index is built when the game opens its first file (the hook goes in when
// the DLL is loaded, too early for zlib and file reading).
CRITICAL_SECTION g_init_lock;
volatile LONG g_state = 0;  // 0 not built yet, 1 ready, 2 off

bool BuildAll()
{
	const DWORD start = GetTickCount();
	if (!LoadZlib()) {
		hooklog::Write("original music: zlib1.dll (inflate) not found in the game: off");
		return false;
	}
	std::string mod_paks;
	if (!BuildIndex(&mod_paks))
		return false;
	hooklog::Write("original music: on; mods: %s (%u data files, %u of them also in the original game); "
	               "indexed in %lu ms; music is set back as the game loads it",
	               mod_paks.c_str(), static_cast<unsigned>(g_mod_files.size()),
	               static_cast<unsigned>(g_originals.size()), GetTickCount() - start);
	return true;
}

bool Ready()
{
	if (g_state == 0) {
		EnterCriticalSection(&g_init_lock);
		if (g_state == 0)
			InterlockedExchange(&g_state, BuildAll() ? 1 : 2);
		LeaveCriticalSection(&g_init_lock);
	}
	return g_state == 1;
}

GameStream *__fastcall OpenFileHook(void *vfs, void *edx, const GameString *path)
{
	GameStream *s = g_open_next(vfs, edx, path);
	if (!s || (s->flags & 1) || !path || !path->begin || path->end <= path->begin)
		return s;
	if (g_log_data_files > 0 && InterlockedIncrement(&g_data_files) <= g_log_data_files)
		hooklog::Write("data file %ld: %.*s (%d bytes)", g_data_files, static_cast<int>(path->end - path->begin),
		               path->begin, static_cast<int>(s->end - s->begin));
	if (!g_music_on || !Ready())
		return s;
	const std::string key = Normalize(path->begin, path->end);
	if (!IsXdb(key) || key.find(':') != std::string::npos)
		return s;
	const unsigned long long hash = Hash(key);
	if (!IsModFile(hash) || !s->begin || s->end < s->begin || s->pos != s->begin)
		return s;  // a file of the original game (or not freshly opened): nothing to do
	const std::string mod(s->begin, s->end);
	const bool music_folder = key.compare(0, 16, "sounds/_(music)/") == 0;
	if (!music_folder) {
		// only files that mention music (<AdventureMusic/>, even empty, has the word)
		bool mentions = false;
		for (size_t i = 0; !mentions && i + 5 <= mod.size(); ++i)
			mentions = (mod[i] | 0x20) == 'm' && _strnicmp(&mod[i], "music", 5) == 0;
		if (!mentions)
			return s;
	}
	std::string original;
	const Original *o = FindOriginal(hash);
	if (o && o->entry.name != key)
		o = nullptr;  // (two names with the same hash: cannot really happen)
	const bool has_original = o && ReadOriginal(*o, &original);
	if (o && !has_original) {
		hooklog::Write("original music: cannot read the original %s: left as it is", key.c_str());
		return s;
	}
	std::string patched, note;
	std::vector<music::Change> changes;
	const music::Result r =
		music::MakeOriginal(key, mod, has_original ? &original : nullptr, &patched, &changes, &note);
	std::string shown(path->begin, path->end);
	std::replace(shown.begin(), shown.end(), '\\', '/');
	if (!shown.empty() && shown[0] == '/')
		shown.erase(0, 1);
	if (!note.empty() && InterlockedIncrement(&g_notes) <= kMaxNotes)
		hooklog::Write("original music: %s: %s: those left as they are", shown.c_str(), note.c_str());
	if (r != music::Result::Patched)
		return s;
	s = WithContent(s, patched);
	LogPatched(shown, changes);
	return s;
}

// ---- log_music: which music files the game starts ----

typedef int(__stdcall *StreamOpenFn)(const char *name, unsigned mode, int offset, int length);
StreamOpenFn g_stream_open_next = nullptr;
volatile LONG g_streams = 0;
const LONG kMaxStreamLines = 300;

int __stdcall StreamOpenHook(const char *name, unsigned mode, int offset, int length)
{
	const int stream = g_stream_open_next(name, mode, offset, length);
	const LONG n = InterlockedIncrement(&g_streams);
	if (n <= kMaxStreamLines) {
		if (mode & 0x8000)  // FSOUND_LOADMEMORY: name is the data
			hooklog::Write("music file %ld: (from memory, %d bytes)%s", n, length, stream ? "" : " (failed)");
		else
			hooklog::Write("music file %ld: %s%s", n, name ? name : "?", stream ? "" : " (failed to open)");
	}
	return stream;
}

bool g_installed = false;  // our function is in the file system table
bool g_early = false;      // ... since the DLL was loaded

void IniPath(wchar_t *ini)
{
	// open-homm5.ini next to the game exe (open-homm5-<instance>.ini for the
	// robot's copies, see hooklog::PathNextToExe)
	hooklog::PathNextToExe(L"open-homm5", L".ini", ini, MAX_PATH);
}

void Install(bool early)
{
	if (g_installed)
		return;
	wchar_t ini[MAX_PATH];
	IniPath(ini);
	g_music_on = GetPrivateProfileIntW(L"audio", L"original_music", 0, ini) != 0;
	g_log_data_files = static_cast<LONG>(GetPrivateProfileIntW(L"audio", L"log_data_files", 0, ini));
	if (!g_music_on && g_log_data_files <= 0)
		return;  // nothing to do: nothing installed
	InitializeCriticalSection(&g_init_lock);
	g_open_next = reinterpret_cast<OpenFn>(hookpatch::PatchPointer(kWinVfsOpenSlot, kWinVfsOpen, &OpenFileHook));
	if (!g_open_next) {
		hooklog::Write("original music: the game's file system is not as expected (%08lx): off", kWinVfsOpenSlot);
		return;
	}
	g_installed = true;
	g_early = early;
}

}  // namespace

void InstallOriginalMusicEarly()
{
	Install(true);
}

void ApplyOriginalMusicFromSettings()
{
	wchar_t ini[MAX_PATH];
	IniPath(ini);
	if (GetPrivateProfileIntW(L"audio", L"log_music", 0, ini)) {
		g_stream_open_next = reinterpret_cast<StreamOpenFn>(
			hookpatch::PatchImport("fmod.dll", "_FSOUND_Stream_Open@16", reinterpret_cast<void *>(&StreamOpenHook)));
		hooklog::Write(g_stream_open_next ? "music files: logging the music files the game opens (log_music=1)"
		                                  : "music files: fmod.dll!FSOUND_Stream_Open not imported: no log");
	}
	if (!g_installed)
		Install(false);  // not at DLL load (e.g. the Steam-wrapped exe is still encrypted then)
	if (!g_music_on) {
		hooklog::Write("original music: off (the music of the installed mod, as without the add-on)");
		return;
	}
	if (g_installed && !g_early)
		hooklog::Write("original music: installed late: files the game read during its splash screen (the heroes) "
		               "keep the mod's music");
}

}  // namespace mods
