// The overlay (see overlay.h): the clients, the ImGui frame, the fonts, the
// screen on top, the panels' language and our keys.
//
// How it draws: d3d9_hook.cpp calls us just before each frame is shown
// (IDirect3DDevice9::Present); we draw with Dear ImGui (third_party/imgui, its
// DirectX 9 renderer) between our own BeginScene/EndScene, only on frames where
// a client wants to draw. On a device reset (fullscreen switch, alt-tab) ImGui's
// Direct3D objects are released before and made again after; the pictures live
// in D3DPOOL_MANAGED. When no client has drawn for 10 s (main menu, a battle...),
// every Direct3D object of ours goes, so that we hold nothing of the game's
// device when it is not needed (ImGui's renderer also holds a reference to it).
#include "overlay.h"

#include <d3d9.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "d3d9_hook.h"
#include "log.h"
#include "overlay_internal.h"
#include "robot.h"
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/backends/imgui_impl_dx9.h"
#include "../mods/languages.h"

namespace overlay {
namespace {

// --- the clients -----------------------------------------------------------------------
struct Slot {
	Client client;
	bool on;      // false once it crashed
	bool wanted;  // this frame
};
const int kMaxClients = 8;
Slot g_clients[kMaxClients];
int g_client_count = 0;
bool g_early = false;      // the Direct3D import went in at DLL load
bool g_installed = false;  // the frame and reset callbacks are in

void Crashed(Slot &s, const char *where)
{
	s.on = false;
	s.wanted = false;
	hooklog::Write("overlay: %s crashed in %s (caught): it is turned off", s.client.name, where);
	internal::CloseModalOf(s.client.name, "its panel crashed");
}

// (Separate functions: __try cannot sit with C++ objects that need unwinding.)
bool CallWanted(Slot *s)
{
	__try {
		return s->client.wanted();
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		Crashed(*s, "wanted");
		return false;
	}
}

void CallPrepare(Slot *s, const Frame *f)
{
	if (!s->client.prepare)
		return;
	__try {
		s->client.prepare(*f);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		Crashed(*s, "prepare");
	}
}

void CallDraw(Slot *s, const Frame *f)
{
	__try {
		s->client.draw(*f);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		Crashed(*s, "draw");
	}
}

// --- fonts -----------------------------------------------------------------------------
struct FontSlot {
	float px;
	ImFont *font;
	DWORD wanted_at;  // GetTickCount() when a client last asked for it
};
const int kMaxFonts = 8;
FontSlot g_fonts[kMaxFonts];  // the fonts in ImGui's atlas now
int g_font_count = 0;
float g_wanted[kMaxFonts];  // asked for this frame
int g_wanted_count = 0;

// A font of the system with Latin and Cyrillic letters: Tahoma Bold (the
// closest to the game's own "System" font, the resource bar's numbers; Wine
// has it too), Segoe UI Bold, Arial Bold; ImGui's own font if none is there.
// The game's own fonts are bitmaps (UI/Fonts/*.(Texture).dds with a glyph
// table in Bin/Fonts, format not known yet).
ImFont *AddFont(float px)
{
	static const ImWchar kRanges[] = {
		0x0020, 0x00ff,  // Basic Latin, Latin-1
		0x0100, 0x017f,  // Latin Extended-A
		0x0400, 0x052f,  // Cyrillic
		0x2000, 0x206f,  // punctuation (dashes, the ellipsis)
		0,
	};
	static const wchar_t *const kNames[] = {L"\\Fonts\\tahomabd.ttf", L"\\Fonts\\segoeuib.ttf", L"\\Fonts\\arialbd.ttf"};
	ImGuiIO &io = ImGui::GetIO();
	wchar_t dir[MAX_PATH];
	const UINT n = GetWindowsDirectoryW(dir, MAX_PATH);
	for (int i = 0; i < 3 && n; ++i) {
		wchar_t wide[MAX_PATH];
		lstrcpynW(wide, dir, MAX_PATH);
		lstrcpynW(wide + n, kNames[i], MAX_PATH - n);
		if (GetFileAttributesW(wide) == INVALID_FILE_ATTRIBUTES)
			continue;
		char utf8[MAX_PATH * 3];
		WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr);
		ImFontConfig config;
		config.OversampleH = 2;
		if (ImFont *font = io.Fonts->AddFontFromFileTTF(utf8, px, &config, kRanges)) {
			hooklog::Write("overlay: font %ls, %.0f px", wide, px);
			return font;
		}
	}
	ImFontConfig config;
	config.SizePixels = px;
	hooklog::Write("overlay: no system font found: ImGui's own font, %.0f px", px);
	return io.Fonts->AddFontDefault(&config);
}

int FontIndex(float px)
{
	for (int i = 0; i < g_font_count; ++i)
		if (g_fonts[i].px == px)
			return i;
	return -1;
}

// After the clients' prepare: every font asked for is in the atlas, or the
// atlas is made again (with the fonts asked for in the last 5 s).
void UpdateFonts()
{
	const DWORD now = GetTickCount();
	bool missing = false;
	for (int i = 0; i < g_wanted_count; ++i) {
		const int at = FontIndex(g_wanted[i]);
		if (at < 0)
			missing = true;
		else
			g_fonts[at].wanted_at = now;
	}
	if (!missing)
		return;
	float keep[kMaxFonts];
	int count = 0;
	for (int i = 0; i < g_wanted_count && count < kMaxFonts; ++i)
		keep[count++] = g_wanted[i];
	for (int i = 0; i < g_font_count && count < kMaxFonts; ++i) {
		bool listed = false;
		for (int j = 0; j < count; ++j)
			listed = listed || keep[j] == g_fonts[i].px;
		if (!listed && now - g_fonts[i].wanted_at < 5000)
			keep[count++] = g_fonts[i].px;
	}
	ImGui::GetIO().Fonts->Clear();
	g_font_count = 0;
	for (int i = 0; i < count; ++i)
		g_fonts[g_font_count++] = {keep[i], AddFont(keep[i]), now};
	ImGui_ImplDX9_InvalidateDeviceObjects();  // the font texture is made again
}

// --- Dear ImGui --------------------------------------------------------------------------
IDirect3DDevice9 *g_imgui_device = nullptr;  // the device ImGui's renderer works with
LARGE_INTEGER g_freq, g_last;

bool EnsureImGui(IDirect3DDevice9 *device)
{
	if (!ImGui::GetCurrentContext()) {
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO &io = ImGui::GetIO();
		io.IniFilename = nullptr;  // no imgui.ini next to the game
		io.LogFilename = nullptr;
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
		QueryPerformanceFrequency(&g_freq);
		QueryPerformanceCounter(&g_last);
		hooklog::Write("overlay: Dear ImGui %s ready", ImGui::GetVersion());
	}
	if (g_imgui_device != device) {
		if (g_imgui_device) {
			ImGui_ImplDX9_Shutdown();  // a new device: the old one's objects go
			internal::ReleaseTextures();
		}
		g_imgui_device = nullptr;
		if (!ImGui_ImplDX9_Init(device))
			return false;
		g_imgui_device = device;
		hooklog::Write("overlay: drawing on device %p", device);
	}
	return true;
}

// The back buffer's size (what the panels are laid out in).
bool BackBufferSize(IDirect3DDevice9 *device, float *w, float *h)
{
	IDirect3DSurface9 *back = nullptr;
	if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back)
		return false;
	D3DSURFACE_DESC desc = {};
	const HRESULT hr = back->GetDesc(&desc);
	back->Release();
	if (FAILED(hr) || !desc.Width || !desc.Height)
		return false;
	*w = static_cast<float>(desc.Width);
	*h = static_cast<float>(desc.Height);
	return true;
}

void ReleaseDeviceObjects()
{
	if (!g_imgui_device)
		return;
	ImGui_ImplDX9_Shutdown();
	internal::ReleaseTextures();
	g_imgui_device = nullptr;
}

// --- the frame ---------------------------------------------------------------------------
bool DrawFrame(IDirect3DDevice9 *device)
{
	if (device->TestCooperativeLevel() != D3D_OK)
		return false;  // lost (fullscreen, alt-tab): nothing can be drawn now
	float w, h;
	if (!BackBufferSize(device, &w, &h) || !EnsureImGui(device))
		return false;
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	const float dt = static_cast<float>(now.QuadPart - g_last.QuadPart) / static_cast<float>(g_freq.QuadPart);
	g_last = now;
	const Frame frame = {device, w, h, dt > 0.0f && dt < 1.0f ? dt : 1.0f / 60.0f};

	g_wanted_count = 0;
	for (int i = 0; i < g_client_count; ++i)
		if (g_clients[i].wanted)
			CallPrepare(&g_clients[i], &frame);
	UpdateFonts();

	ImGuiIO &io = ImGui::GetIO();
	io.DeltaTime = frame.delta;
	io.DisplaySize = ImVec2(w, h);
	internal::FeedImGui(w, h);
	ImGui_ImplDX9_NewFrame();
	ImGui::NewFrame();
	for (int i = 0; i < g_client_count; ++i)
		if (g_clients[i].wanted)
			CallDraw(&g_clients[i], &frame);
	ImGui::Render();
	if (SUCCEEDED(device->BeginScene())) {
		// The pictures have mipmaps: use them (ImGui's renderer sets the other
		// filters), and give the game back its own setting afterwards.
		DWORD mip_filter = D3DTEXF_NONE;
		const bool got = SUCCEEDED(device->GetSamplerState(0, D3DSAMP_MIPFILTER, &mip_filter));
		if (got)  // (a "pure" device cannot tell: then leave it as it is)
			device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
		ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
		if (got)
			device->SetSamplerState(0, D3DSAMP_MIPFILTER, mip_filter);
		device->EndScene();
	}
	if (internal::ModalOpen())
		internal::ModalDrawn();
	return true;
}

void OnPresent(IDirect3DDevice9 *device)
{
	static DWORD last_drawn = 0;
	internal::ModalFrameStart();
	bool any = false;
	for (int i = 0; i < g_client_count; ++i) {
		Slot &s = g_clients[i];
		s.wanted = s.on && CallWanted(&s);
		any = any || s.wanted;
	}
	if (any && DrawFrame(device))
		last_drawn = GetTickCount();
	else if (g_imgui_device && GetTickCount() - last_drawn > 10000)
		ReleaseDeviceObjects();
}

void OnReset(IDirect3DDevice9 *device, bool after, bool ok)
{
	(void)ok;
	if (!after && device == g_imgui_device)
		ImGui_ImplDX9_InvalidateDeviceObjects();  // made again on the next frame
}

// --- the game's screens --------------------------------------------------------------------
// A list at [0x00fe719c] (read by 0x008385a0); its last entry (+8) is the
// screen on top. Its RTTI class tells which screen it is.
const DWORD kTopScreenGetter = 0x008385a0;
const BYTE kTopScreenGetterBytes[] = {0x51, 0x33, 0xd2, 0x89, 0x14, 0x24, 0xa1, 0x9c, 0x71, 0xfe, 0x00};
const DWORD kScreenList = 0x00fe719c;
int g_screens_ok = -1;  // the getter's bytes: -1 not checked yet

DWORD Read(const void *base, DWORD offset)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(base) + offset);
}

// The class name from the object's RTTI (".?AVCAdventureScreen@NUI@@"), or "".
// The exe has no ASLR: its RTTI data lies between 0x00400000 and 0x01200000.
const char *ClassName(const void *object)
{
	const DWORD vtable = Read(object, 0);
	if (vtable < 0x00401000 || vtable > 0x01200000)
		return "";
	const DWORD locator = Read(reinterpret_cast<const void *>(vtable), static_cast<DWORD>(-4));
	if (locator < 0x00401000 || locator > 0x01200000)
		return "";
	const DWORD type = Read(reinterpret_cast<const void *>(locator), 12);
	if (type < 0x00401000 || type > 0x01200000)
		return "";
	return reinterpret_cast<const char *>(type + 8);
}

bool ReadTopScreen(char *class_name, int size)
{
	__try {
		const DWORD *sentinel = *reinterpret_cast<const DWORD *const *>(kScreenList);
		if (!sentinel || sentinel[0] == reinterpret_cast<DWORD>(sentinel))
			return false;
		const void *screen = reinterpret_cast<const void *>(Read(reinterpret_cast<const void *>(sentinel[1]), 8));
		if (!screen)
			return false;
		lstrcpynA(class_name, ClassName(screen), size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		class_name[0] = 0;
		return false;
	}
	return class_name[0] != 0;
}

}  // namespace

namespace internal {

bool GameInFront()
{
	HWND window = d3dhook::DeviceWindow();
	const HWND front = GetForegroundWindow();
	return window && front &&
	       (front == window || GetAncestor(front, GA_ROOTOWNER) == GetAncestor(window, GA_ROOTOWNER));
}

}  // namespace internal

void InstallEarly()
{
	if (!g_early)
		g_early = d3dhook::Install(false);
}

bool AddClient(const Client &client)
{
	if (!g_installed) {
		if (!d3dhook::Install(!g_early))
			return false;
		internal::InstallTextures();
		d3dhook::AddPresentCallback(&OnPresent, 0);  // the robot's screenshots come after (they show the overlay)
		d3dhook::AddResetCallback(&OnReset);
		g_installed = true;
	}
	if (g_client_count >= kMaxClients)
		return false;
	g_clients[g_client_count++] = {client, true, false};
	return true;
}

void WantFont(float px)
{
	for (int i = 0; i < g_wanted_count; ++i)
		if (g_wanted[i] == px)
			return;
	if (g_wanted_count < kMaxFonts)
		g_wanted[g_wanted_count++] = px;
}

ImFont *Font(float px)
{
	ImFont *best = nullptr;
	float distance = 1e9f;
	for (int i = 0; i < g_font_count; ++i) {
		const float d = g_fonts[i].px > px ? g_fonts[i].px - px : px - g_fonts[i].px;
		if (d < distance) {
			distance = d;
			best = g_fonts[i].font;
		}
	}
	return best ? best : ImGui::GetFont();
}

bool TopScreen(char *class_name, int size)
{
	class_name[0] = 0;
	if (g_screens_ok < 0)
		g_screens_ok = memcmp(reinterpret_cast<const void *>(kTopScreenGetter), kTopScreenGetterBytes,
		                      sizeof(kTopScreenGetterBytes)) == 0;
	if (!g_screens_ok)
		return false;
	return ReadTopScreen(class_name, size);
}

void *TopScreenObject()
{
	if (g_screens_ok < 0)
		g_screens_ok = memcmp(reinterpret_cast<const void *>(kTopScreenGetter), kTopScreenGetterBytes,
		                      sizeof(kTopScreenGetterBytes)) == 0;
	if (!g_screens_ok)
		return nullptr;
	__try {
		const DWORD *sentinel = *reinterpret_cast<const DWORD *const *>(kScreenList);
		if (!sentinel || sentinel[0] == reinterpret_cast<DWORD>(sentinel))
			return nullptr;
		// The list holds a part of the screen object (one of its bases); its
		// RTTI locator says where that part sits in the whole object.
		const BYTE *part = reinterpret_cast<const BYTE *>(Read(reinterpret_cast<const void *>(sentinel[1]), 8));
		if (!part)
			return nullptr;
		const DWORD vtable = Read(part, 0);
		if (vtable < 0x00401000 || vtable > 0x01200000)
			return nullptr;
		const DWORD locator = Read(reinterpret_cast<const void *>(vtable), static_cast<DWORD>(-4));
		if (locator < 0x00401000 || locator > 0x01200000)
			return nullptr;
		return const_cast<BYTE *>(part - Read(reinterpret_cast<const void *>(locator), 4));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

Language PanelLanguage()
{
	static int language = -1;  // found once
	if (language >= 0)
		return static_cast<Language>(language);
	wchar_t path[MAX_PATH], text[32];
	hooklog::PathNextToExe(L"open-homm5", L".ini", path, MAX_PATH);
	GetPrivateProfileStringW(L"ui", L"language", L"auto", text, 32, path);
	if (_wcsicmp(text, L"en") == 0 || _wcsicmp(text, L"fr") == 0) {
		language = _wcsicmp(text, L"fr") == 0 ? kFrench : kEnglish;
		return static_cast<Language>(language);
	}
	// The game's language: French when the languages mod gives the game French
	// texts ([lang] texts=fr); else a text of the original game, the red
	// player's name ("Joueur rouge" / "Red player"); else the language of Windows.
	if (mods::TextsChoice() == mods::Texts::French) {
		hooklog::Write("overlay: French words, as the game's texts ([lang] texts=fr)");
		language = kFrench;
		return static_cast<Language>(language);
	}
	std::string data;
	if (ReadGameFile("Text/Game/Player/Colors/red_player.txt", &data) && data.size() >= 4) {
		const wchar_t *w = reinterpret_cast<const wchar_t *>(data.data());
		const std::wstring words(w, data.size() / 2);
		const bool french = words.find(L"Joueur") != std::wstring::npos;
		hooklog::Write("overlay: the game's texts are %s", french ? "French" : "not French: English");
		language = french ? kFrench : kEnglish;
	}
	else {
		const bool french = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;
		hooklog::Write("overlay: the game's language is unknown: %s, as Windows", french ? "French" : "English");
		language = french ? kFrench : kEnglish;
	}
	return static_cast<Language>(language);
}

bool MouseInBackBuffer(float w, float h, float *x, float *y)
{
	HWND window = d3dhook::DeviceWindow();
	POINT p;
	RECT client;
	if (!window || !GetCursorPos(&p) || !ScreenToClient(window, &p) || !GetClientRect(window, &client) ||
	    client.right <= 0 || client.bottom <= 0)
		return false;
	*x = p.x * w / client.right;
	*y = p.y * h / client.bottom;
	return true;
}

HWND GameWindow()
{
	return d3dhook::DeviceWindow();
}

bool KeyPressed(int vk, bool *was_down)
{
	if (!vk)
		return false;
	const bool from_robot = robot::TakeOverlayKey(vk);
	const bool down = internal::GameInFront() && (GetAsyncKeyState(vk) & 0x8000) != 0;
	const bool pressed = from_robot || (down && !*was_down);
	*was_down = down;
	return pressed;
}

int ParseKey(const wchar_t *text, int fallback)
{
	if (_wcsicmp(text, L"none") == 0 || text[0] == L'0')
		return 0;
	if ((text[0] == L'F' || text[0] == L'f') && _wtoi(text + 1) >= 1 && _wtoi(text + 1) <= 12)
		return VK_F1 + _wtoi(text + 1) - 1;
	return fallback;
}

}  // namespace overlay
