// Mod: the game's random map screen (NUI::CRMGScreen, vtable 0x00e41114) for
// the template builder (rmg_builder.cpp; docs/plans/in-game-generator.md,
// build steps 5 and 7).
//
// The native "Template..." button. The screen's window is built from UI files
// of the game's data (data.pak). When the game opens them through its file
// system (NVFS::CWinVFS slot 7, which also reads the paks), we serve:
//   UI/RMGScreen/MainPanel.(WindowSimpleShared).xdb   the game's file with one
//       more child, our button (only when the file is the one we know: its
//       fingerprint, rmg_builder_data.h; else the builder draws its own button);
//   UI/RMGScreen/OpenHomm5Builder.(WindowMSButton).xdb   a copy of the game's
//       Save button at x 340 (the free slot between Save and Back), enabled,
//       with our label and our reaction;
//   UI/RMGScreen/OpenHomm5Builder.(UISRunReaction).xdb   the Save button's
//       reaction, sending "oh_builder" instead of "save";
//   UI/RMGScreen/OpenHomm5Builder.txt, TTOpenHomm5Builder.txt   the label and
//       the tooltip, in the panel's language (UTF-16, as the game's texts).
// The button's reaction goes to the screen's command handler (0x0079a650,
// vtable cell 0x00e4110c: "create", "save", "back", "change_option"...; 0 for
// a command it does not know). We wrap that cell and open the builder on
// "oh_builder". Nothing on disk changes.
//
// "Use template" (ApplyToRmgScreen): the screen's options are objects in a
// vector at screen +0x1c0/+0x1c4 (made by 0x00799b60 from MMH5.5's
// InfoRMGScreen list). Each has its EP_* parameter at +0xc; a combo option
// (NUI::CRMGScreen::CComboOption, vtable 0x00e41070) has
//   slot 8 (+0x20)  is its check box ticked (else the game picks at random);
//   slot 9 (+0x24)  tick or untick it (one argument);
//   slot 10 (+0x28) the chosen item;
//   +0x28  its list control: slot 6 (+0x18) chooses an item (one argument);
//   +0x2c/+0x30  its items (pointers).
// The command "change_option" (0x00799500, ecx = the screen) reads every option
// into the screen's values (+0x264) and, when "ARMG Templates" (EP 6) is
// ticked, asks the generator for the templates that fit the size, levels and
// players (into screen +0x324/+0x328: one 0x20-byte database id per template)
// and refills that list. So: set size, levels and players, tick EP 6, run
// change_option, find our recipe in the list, choose it, run change_option
// again (the same list: it keeps the choice). The game then generates with our
// template as if the player had clicked.
#include "rmg_screen.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include <initializer_list>
#include <string>

#include "rmg_builder_data.h"
#include "rmg_recipe.h"
#include "rmg_templates.h"

#include "../hook/log.h"
#include "../hook/overlay.h"
#include "../hook/patch.h"

namespace mods {
namespace {

namespace data = rmg::builder;

// --- the game ---------------------------------------------------------------------------------
const DWORD kWinVfsOpenSlot = 0x00e6651c;  // NVFS::CWinVFS slot 7: __thiscall(vfs, const nstl::string *path), ret 4
const DWORD kWinVfsOpen = 0x009e0020;
const DWORD kReactSlot = 0x00e4110c;  // CRMGScreen's IProgrammedReactionsAndChecks (at +0x174), slot 0
const DWORD kReact = 0x0079a650;      // __thiscall(reactions, ?, const nstl::string *command, ?), ret 0xc
const DWORD kScreenVtable = 0x00e41114;
const DWORD kComboOptionVtable = 0x00e41070;
const DWORD kChangeOption = 0x00799500;  // __thiscall(screen): the command "change_option"
const BYTE kChangeOptionBytes[] = {0x81, 0xec, 0xa8, 0x00, 0x00, 0x00, 0x56, 0x57,
                                   0x8b, 0xf1, 0x8d, 0xbe, 0x64, 0x02, 0x00, 0x00};
const DWORD kOptions = 0x1c0;    // vector of option pointers: begin, end
const DWORD kTemplates = 0x324;  // vector of 0x20-byte database ids: begin, end
enum { kEpSize = 1, kEpPlayers = 4, kEpLevels = 5, kEpCustomTemplate = 6 };

struct GameString {  // nstl::string
	const char *begin;
	const char *end;
	const char *cap;
};
struct GameStream {  // CMemoryStream: the whole file in memory
	void **vftable;  // slot 0: scalar deleting destructor
	char *begin;
	char *cap;
	char *end;
	char *pos;
	DWORD flags;  // bit 0: broken
};
struct DbId {  // NDb's database id (rmg_join.cpp)
	GameString path;
	BYTE key_made;
	BYTE pad[3];
	DWORD hash;
	GameString key;
};
static_assert(sizeof(DbId) == 0x20, "NDb database id");

typedef GameStream *(__fastcall *OpenFn)(void *vfs, void *edx, const GameString *path);
typedef void(__fastcall *DeleteFn)(GameStream *self, void *edx, int flags);
typedef bool(__fastcall *ReactFn)(void *self, void *edx, void *a, const GameString *command, void *c);
OpenFn g_open_next = nullptr;
ReactFn g_react_next = nullptr;
void (*g_open_panel)() = nullptr;

// --- the served files ---------------------------------------------------------------------------
std::string g_button, g_reaction;  // made once, from the game's own files
volatile LONG g_native = 0;        // 1 once our main panel was served
volatile LONG g_told = 0;

std::string Key(const char *begin, const char *end)
{
	while (begin < end && (*begin == '/' || *begin == '\\'))
		++begin;
	std::string s(begin, end);
	for (char &c : s)
		c = c == '\\' ? '/' : (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	return s;
}

std::string Key(const char *path)
{
	return Key(path, path + strlen(path));
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

// A game file through the file system below us (the paks and the folder).
bool ReadBelow(void *vfs, const char *path, std::string *out)
{
	const size_t n = strlen(path);
	const GameString name = {path, path + n, path + n};
	GameStream *s = g_open_next(vfs, nullptr, &name);
	const bool ok = ReadStream(s, out);
	Delete(s);
	return ok;
}

void TellOnce(const char *why)
{
	if (InterlockedExchange(&g_told, 1) == 0)
		hooklog::Write("rmg screen: %s: no native \"Template...\" button (the builder draws its own)", why);
}

// The game opens the screen's main panel: ours, with the button, if every file is as we know it.
GameStream *ServeMainPanel(void *vfs, GameStream *original)
{
	std::string main, panel, save_button, save_reaction, button, reaction;
	if (!ReadStream(original, &main)) {
		TellOnce("the main panel's file is not readable");
		return original;
	}
	if (!data::MakeMainPanel(main, &panel)) {
		TellOnce("the main panel's file is not the one we know");
		return original;
	}
	if (!ReadBelow(vfs, data::kSaveButtonPath, &save_button) || !data::MakeButton(save_button, &button) ||
	    !ReadBelow(vfs, data::kSaveReactionPath, &save_reaction) || !data::MakeReaction(save_reaction, &reaction)) {
		TellOnce("the Save button's files are not the ones we know");
		return original;
	}
	if (InterlockedCompareExchange(&g_native, 0, 0) == 0) {
		g_button = button;
		g_reaction = reaction;
	}
	GameStream *ours = static_cast<GameStream *>(NewGameStream(panel));
	if (!ours)
		return original;
	Delete(original);
	if (InterlockedExchange(&g_native, 1) == 0)
		hooklog::Write("rmg screen: the random map screen gets our \"Template...\" button (its main panel, %u bytes, "
		               "with one more child)",
		               static_cast<unsigned>(panel.size()));
	return ours;
}

GameStream *__fastcall OpenHook(void *vfs, void *edx, const GameString *path)
{
	if (!path || !path->begin || path->end - path->begin < 20 || path->end - path->begin > 200)
		return g_open_next(vfs, edx, path);
	try {
		static const std::string main_key = Key(data::kMainPanelPath), button_key = Key(data::kButtonPath),
		                         reaction_key = Key(data::kReactionPath), label_key = Key(data::kLabelPath),
		                         tooltip_key = Key(data::kTooltipPath);
		const std::string key = Key(path->begin, path->end);
		if (key == main_key)
			return ServeMainPanel(vfs, g_open_next(vfs, edx, path));
		const bool ours = key == button_key || key == reaction_key || key == label_key || key == tooltip_key;
		if (!ours || !g_native)
			return g_open_next(vfs, edx, path);
		const data::Words &w = data::WordsFor(overlay::PanelLanguage());
		const std::string bytes = key == button_key     ? g_button
		                          : key == reaction_key ? g_reaction
		                          : key == label_key    ? data::GameText(w.native_button)
		                                                : data::GameText(w.native_tooltip);
		GameStream *s = g_open_next(vfs, edx, path);  // (none: these names are ours)
		GameStream *served = static_cast<GameStream *>(NewGameStream(bytes));
		if (!served)
			return s;
		Delete(s);
		return served;
	} catch (...) {  // out of memory: the game's own files
		return g_open_next(vfs, edx, path);
	}
}

bool __fastcall ReactHook(void *self, void *edx, void *a, const GameString *command, void *c)
{
	const size_t n = strlen(data::kCommand);
	if (command && command->begin && static_cast<size_t>(command->end - command->begin) == n &&
	    memcmp(command->begin, data::kCommand, n) == 0) {
		hooklog::Write("rmg screen: \"%s\" (the Template... button)", data::kCommand);
		if (g_open_panel)
			g_open_panel();
		return true;
	}
	return g_react_next(self, edx, a, command, c);
}

// --- "Use template" -----------------------------------------------------------------------------
typedef char(__fastcall *IsOnFn)(void *option, void *edx);
typedef void(__fastcall *SetOnFn)(void *option, void *edx, int on);
typedef int(__fastcall *IndexFn)(void *option, void *edx);
typedef void(__fastcall *ChooseFn)(void *control, void *edx, int index);
typedef void(__fastcall *ChangeOptionFn)(void *screen, void *edx);

DWORD At(const void *object, DWORD offset)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(object) + offset);
}

void **Vtable(const void *object)
{
	return *static_cast<void **const *>(object);
}

// The screen's combo option with this EP_* parameter, or nullptr.
void *FindOption(void *screen, int ep)
{
	void *const *begin = reinterpret_cast<void *const *>(At(screen, kOptions));
	void *const *end = reinterpret_cast<void *const *>(At(screen, kOptions + 4));
	if (!begin || end < begin || end - begin > 64)
		return nullptr;
	for (void *const *p = begin; p != end; ++p)
		if (*p && static_cast<int>(At(*p, 0xc)) == ep)
			return reinterpret_cast<DWORD>(Vtable(*p)) == kComboOptionVtable ? *p : nullptr;
	return nullptr;
}

int ItemCount(void *option)
{
	const DWORD begin = At(option, 0x2c), end = At(option, 0x30);
	return end >= begin ? static_cast<int>((end - begin) / 4) : 0;
}

bool IsOn(void *option)
{
	return reinterpret_cast<IsOnFn>(Vtable(option)[8])(option, nullptr) != 0;
}

int Chosen(void *option)
{
	return reinterpret_cast<IndexFn>(Vtable(option)[10])(option, nullptr);
}

// Ticks the option and chooses item `index` (-1: only tick it).
bool Set(void *option, int index)
{
	if (!IsOn(option))
		reinterpret_cast<SetOnFn>(Vtable(option)[9])(option, nullptr, 1);
	if (index >= 0) {
		void *control = reinterpret_cast<void *>(At(option, 0x28));
		if (!control || index >= ItemCount(option))
			return false;
		if (Chosen(option) != index)
			reinterpret_cast<ChooseFn>(Vtable(control)[6])(control, nullptr, index);
	}
	return IsOn(option) && (index < 0 || Chosen(option) == index);
}

// The game's calls, apart (__try cannot sit with C++ objects).
struct Options {
	void *size, *levels, *players, *custom;
};

bool SafeFind(void *screen, Options *o)
{
	__try {
		if (At(screen, 0) != kScreenVtable)
			return false;
		o->size = FindOption(screen, kEpSize);
		o->levels = FindOption(screen, kEpLevels);
		o->players = FindOption(screen, kEpPlayers);
		o->custom = FindOption(screen, kEpCustomTemplate);
		return o->size && o->levels && o->players && o->custom;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// For the log when the options are not as expected: the screen's vftable and
// each option's EP_* parameter and vftable.
void DescribeOptions(void *screen, char *out, size_t size)
{
	out[0] = 0;
	__try {
		_snprintf_s(out, size, _TRUNCATE, "screen %p vftable %08lx, options:", screen, At(screen, 0));
		void *const *begin = reinterpret_cast<void *const *>(At(screen, kOptions));
		void *const *end = reinterpret_cast<void *const *>(At(screen, kOptions + 4));
		for (void *const *p = begin; p && p < end && p < begin + 24; ++p) {
			char item[32];
			_snprintf_s(item, sizeof(item), _TRUNCATE, " %ld:%08lx", *p ? static_cast<long>(At(*p, 0xc)) : -1L,
			            *p ? At(*p, 0) : 0UL);
			strncat_s(out, size, item, _TRUNCATE);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		strncat_s(out, size, " (unreadable)", _TRUNCATE);
	}
}

// 0 ok, else the step that failed: 1 size, 2 levels, 3 players, 4 the template box, 5 a crash.
int SafeSetOptions(void *screen, const Options &o, int size, int levels, int players)
{
	__try {
		if (!Set(o.size, size))
			return 1;
		if (!Set(o.levels, levels))
			return 2;
		if (!Set(o.players, players))
			return 3;
		if (!Set(o.custom, -1))
			return 4;
		reinterpret_cast<ChangeOptionFn>(kChangeOption)(screen, nullptr);
		return 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 5;
	}
}

bool SafeChooseTemplate(void *screen, void *custom, int index)
{
	__try {
		if (!Set(custom, index))
			return false;
		reinterpret_cast<ChangeOptionFn>(kChangeOption)(screen, nullptr);
		return IsOn(custom) && Chosen(custom) == index;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool ReadString(const GameString *s, std::string *out)
{
	if (IsBadReadPtr(s, sizeof(GameString)) || !s->begin || s->end <= s->begin)
		return false;
	const size_t n = static_cast<size_t>(s->end - s->begin);
	if (n > 1024 || IsBadReadPtr(s->begin, n))
		return false;
	out->assign(s->begin, n);
	return true;
}

// Where our recipe is in the screen's template list (-1: not there); *count, *first: what is.
int FindTemplate(void *screen, const std::string &recipe, int *count, std::string *first)
{
	const BYTE *begin = reinterpret_cast<const BYTE *>(At(screen, kTemplates));
	const BYTE *end = reinterpret_cast<const BYTE *>(At(screen, kTemplates + 4));
	*count = 0;
	if (!begin || end < begin || (end - begin) % sizeof(DbId) || IsBadReadPtr(begin, end - begin))
		return -1;
	*count = static_cast<int>((end - begin) / sizeof(DbId));
	for (int i = 0; i < *count; ++i) {
		const DbId *id = reinterpret_cast<const DbId *>(begin + i * sizeof(DbId));
		std::string text, found;
		for (const GameString *s : {&id->path, &id->key}) {
			if (!ReadString(s, &text))
				continue;
			if (i == 0 && first->empty())
				*first = text;
			if (rmg::RecipeOfReference(text, &found) && found == recipe)
				return i;
		}
	}
	return -1;
}

}  // namespace

bool InstallRmgScreen(void (*open_panel)())
{
	g_open_panel = open_panel;
	g_react_next = reinterpret_cast<ReactFn>(hookpatch::PatchPointer(kReactSlot, kReact, &ReactHook));
	if (!g_react_next) {
		hooklog::Write("rmg screen: the random map screen's command handler is not as expected: no Template... button");
		return false;
	}
	g_open_next = reinterpret_cast<OpenFn>(hookpatch::PatchPointer(kWinVfsOpenSlot, kWinVfsOpen, &OpenHook));
	if (!g_open_next)
		hooklog::Write("rmg screen: the game's file system is not as expected: no native Template... button");
	return true;
}

bool RmgScreenHasNativeButton()
{
	return g_native != 0;
}

bool ApplyToRmgScreen(void *screen, const ScreenChoice &c, std::string *report)
{
	char line[256];
	if (!screen || memcmp(reinterpret_cast<const void *>(kChangeOption), kChangeOptionBytes, sizeof(kChangeOptionBytes)) != 0) {
		*report = "the random map screen's code is not as expected";
		return false;
	}
	Options o = {};
	if (!SafeFind(screen, &o)) {
		char options[600];
		DescribeOptions(screen, options, sizeof(options));
		*report = std::string("the screen's size, levels, players or template option was not found (") + options + ")";
		return false;
	}
	const int step = SafeSetOptions(screen, o, c.size_index, c.underground ? 1 : 0, c.players - 2);
	if (step) {
		static const char *const kWhat[] = {"", "the size", "the levels", "the players", "the template box", "a crash (caught)"};
		*report = std::string("the screen did not take ") + kWhat[step < 6 ? step : 5];
		return false;
	}
	int count = 0;
	std::string first;
	const int index = FindTemplate(screen, c.recipe, &count, &first);
	if (index < 0) {
		_snprintf_s(line, sizeof(line), _TRUNCATE, "%s is not in the screen's template list (%d templates fit%s%s)",
		            c.recipe.c_str(), count, count ? ", the first: " : "", first.c_str());
		*report = line;
		return false;
	}
	if (!SafeChooseTemplate(screen, o.custom, index)) {
		*report = "the screen did not take our template";
		return false;
	}
	_snprintf_s(line, sizeof(line), _TRUNCATE,
	            "size %s%s, %d players, \"ARMG Templates\" ticked, %s chosen (%d of %d that fit)",
	            rmg::kSizes[c.size_index].key, c.underground ? " with underground" : "", c.players, c.recipe.c_str(),
	            index + 1, count);
	*report = line;
	return true;
}

}  // namespace mods
