// The player list overlay: the heroes' portraits.
//
// A hero's portrait is the FaceTexture of its game data (NDb::SAdvMapHeroShared
// +0x12c, an NDb::STexture), whose DestName (+0x40) is the .dds file in the
// game's data, e.g. "UI/H5A1/Icons/Heroes/Portrets128x128/Giovanni.(Texture).dds"
// (player_list_game.cpp finds that path). We read the file through the game's
// own file system, so MMH5.5's paks count as they do for the game: its
// "open file" (NVFS::CWinVFS vtable slot 7, 0x009e0020) is wrapped only to
// learn the file system object, and called to read the .dds. Then the picture
// becomes a Direct3D texture of ours (D3DPOOL_MANAGED: it survives a device
// reset), once per file.
#include "player_list_internal.h"

#include <d3d9.h>
#include <string.h>

#include <string>

#include "../hook/log.h"
#include "../hook/patch.h"

namespace mods {
namespace plist {
namespace {

const DWORD kWinVfsOpenSlot = 0x00e6651c;  // NVFS::CWinVFS vftable 0x00e66500, slot 7: OpenFile
const DWORD kWinVfsOpen = 0x009e0020;      // __thiscall(CWinVFS *, const nstl::string *path), ret 4

struct GameString {  // nstl::string: first char, one past the last, end of the allocation
	const char *begin;
	const char *end;
	const char *cap;
};
struct GameStream {  // CDataStream: the whole file in memory from begin to end
	void **vftable;  // slot 0: scalar deleting destructor (this, flags)
	char *begin;
	char *cap;
	char *end;
	char *pos;
	DWORD flags;  // bit 0: broken (e.g. no such file)
};
typedef GameStream *(__fastcall *OpenFn)(void *vfs, void *edx, const GameString *path);
typedef void(__fastcall *DeleteFn)(GameStream *self, void *edx, int flags);

OpenFn g_open_next = nullptr;
void *volatile g_vfs = nullptr;

GameStream *__fastcall OpenFileHook(void *vfs, void *edx, const GameString *path)
{
	g_vfs = vfs;
	return g_open_next(vfs, edx, path);
}

}  // namespace

bool ReadGameFile(const char *path, std::string *out)
{
	void *vfs = g_vfs;
	if (!vfs || !g_open_next)
		return false;
	const size_t n = strlen(path);
	const GameString name = {path, path + n, path + n};
	GameStream *s = g_open_next(vfs, nullptr, &name);
	if (!s)
		return false;
	const bool ok = !(s->flags & 1) && s->begin && s->end > s->begin;
	if (ok)
		out->assign(s->begin, s->end);
	reinterpret_cast<DeleteFn>(s->vftable[0])(s, nullptr, 1);
	return ok;
}

namespace {

// --- .dds files -------------------------------------------------------------------------
DWORD Dw(const std::string &d, size_t at)
{
	return at + 4 <= d.size() ? *reinterpret_cast<const DWORD *>(d.data() + at) : 0;
}

int MaskShift(DWORD mask)
{
	int shift = 0;
	while (mask && !(mask & 1)) {
		mask >>= 1;
		++shift;
	}
	return shift;
}

// The first picture of a .dds file as a texture of the device, or nullptr.
// Uncompressed 32/24-bit pictures and DXT1/3/5 (the formats of the game's icons).
IDirect3DTexture9 *TextureFromDds(IDirect3DDevice9 *device, const std::string &d, const char *path)
{
	if (d.size() < 128 || d.compare(0, 4, "DDS ") != 0) {
		hooklog::Write("player list: %s is not a .dds file", path);
		return nullptr;
	}
	const UINT height = Dw(d, 12), width = Dw(d, 16);
	const DWORD pf_flags = Dw(d, 80), fourcc = Dw(d, 84), bits = Dw(d, 88);
	const DWORD rmask = Dw(d, 92), gmask = Dw(d, 96), bmask = Dw(d, 100), amask = Dw(d, 104);
	if (!width || !height || width > 2048 || height > 2048)
		return nullptr;
	const char *pixels = d.data() + 128;
	const size_t available = d.size() - 128;
	D3DFORMAT format = D3DFMT_A8R8G8B8;
	size_t row_bytes = 0, rows = height;
	if (pf_flags & 0x4) {  // DDPF_FOURCC
		if (fourcc != MAKEFOURCC('D', 'X', 'T', '1') && fourcc != MAKEFOURCC('D', 'X', 'T', '3') &&
		    fourcc != MAKEFOURCC('D', 'X', 'T', '5')) {
			hooklog::Write("player list: %s: picture format %.4s not supported", path, reinterpret_cast<const char *>(&fourcc));
			return nullptr;
		}
		format = static_cast<D3DFORMAT>(fourcc);
		row_bytes = ((width + 3) / 4) * (fourcc == MAKEFOURCC('D', 'X', 'T', '1') ? 8 : 16);
		rows = (height + 3) / 4;
	}
	else if ((pf_flags & 0x40) && (bits == 32 || bits == 24)) {  // DDPF_RGB
		row_bytes = width * (bits / 8);
	}
	else {
		hooklog::Write("player list: %s: picture format not supported (flags %lx, %lu bits)", path, pf_flags, bits);
		return nullptr;
	}
	if (row_bytes * rows > available)
		return nullptr;
	IDirect3DTexture9 *texture = nullptr;
	if (FAILED(device->CreateTexture(width, height, 1, 0, format, D3DPOOL_MANAGED, &texture, nullptr)) || !texture)
		return nullptr;
	D3DLOCKED_RECT locked;
	if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
		texture->Release();
		return nullptr;
	}
	for (size_t y = 0; y < rows; ++y) {
		BYTE *dst = static_cast<BYTE *>(locked.pBits) + y * locked.Pitch;
		const BYTE *src = reinterpret_cast<const BYTE *>(pixels) + y * row_bytes;
		if (format != D3DFMT_A8R8G8B8) {
			memcpy(dst, src, row_bytes);
			continue;
		}
		// any channel order -> A8R8G8B8
		const int bytes = static_cast<int>(bits / 8);
		const int rs = MaskShift(rmask), gs = MaskShift(gmask), bs = MaskShift(bmask), as = MaskShift(amask);
		for (UINT x = 0; x < width; ++x) {
			DWORD v = 0;
			memcpy(&v, src + x * bytes, bytes);
			const DWORD r = (v & rmask) >> rs, g = (v & gmask) >> gs, b = (v & bmask) >> bs;
			const DWORD a = (pf_flags & 0x1) && amask ? (v & amask) >> as : 0xff;
			reinterpret_cast<DWORD *>(dst)[x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}
	texture->UnlockRect(0);
	return texture;
}

// --- the cache ----------------------------------------------------------------------------
struct Face {
	char path[160];
	IDirect3DTexture9 *texture;
};
const int kFaces = 48;
Face g_faces[kFaces];
int g_count = 0;
IDirect3DDevice9 *g_device = nullptr;
bool g_installed = false;

}  // namespace

bool InstallPortraits()
{
	if (g_installed)
		return true;
	g_open_next = reinterpret_cast<OpenFn>(hookpatch::PatchPointer(kWinVfsOpenSlot, kWinVfsOpen, &OpenFileHook));
	if (!g_open_next) {
		hooklog::Write("player list: the game's file system is not as expected: no portraits");
		return false;
	}
	g_installed = true;
	return true;
}

void *Portrait(IDirect3DDevice9 *device, const char *path, int *loads_left)
{
	if (!g_installed || !path[0])
		return nullptr;
	if (device != g_device) {
		ReleasePortraits();
		g_device = device;
	}
	for (int i = 0; i < g_count; ++i)
		if (strcmp(g_faces[i].path, path) == 0)
			return g_faces[i].texture;  // (nullptr: it could not be read)
	if (*loads_left <= 0 || g_count >= kFaces)
		return nullptr;  // later (a few per frame)
	--*loads_left;
	Face &f = g_faces[g_count++];
	lstrcpynA(f.path, path, sizeof(f.path));
	f.texture = nullptr;
	std::string data;
	if (ReadGameFile(path, &data))
		f.texture = TextureFromDds(device, data, path);
	else
		hooklog::Write("player list: cannot read %s", path);
	return f.texture;
}

void ReleasePortraits()
{
	for (int i = 0; i < g_count; ++i)
		if (g_faces[i].texture)
			g_faces[i].texture->Release();
	g_count = 0;
	g_device = nullptr;
}

}  // namespace plist
}  // namespace mods
