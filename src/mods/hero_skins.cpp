// Mod: choose your hero's look ("skins"). Plan and findings: docs/plans/hero-skins.md.
//
// A player picks, for a hero, the look of another hero of the same faction
// (h5-together-skins.ini: Godric=Freyda). The hero then shows that hero's
// figures: on the adventure map, in battle and on the hero screen. Its
// portrait, class, skills and everything the rules use stay its own.
//
// What the game does (vanilla 3.1 exe, the same in MMH55_Utility):
//   - Every hero's data file (NDb::SAdvMapHeroShared, vtable 0x00e6d450) names
//     its figures: HeroCharacterArena +0x13c (battle, and the hero screen's 3D
//     figure), HeroCharacterArenaMelee +0x144, HeroCharacterAdventure +0x14c
//     (the map), the map animations AnimSet +0x4c (used when the map figure has
//     none of its own), HeroIndividualCamera +0x154 (the hero screen). Each is a
//     CDBPtr: the object at +0, a "resolved" byte at +4; the objects are NDb
//     resources with a reference count at +8.
//   - The hero object (NWorld::CHero, its IHero part, vtable 0x00e7e484) keeps
//     copies of the battle and map figures, at +0xb0 and +0xb8, which go into
//     saves. IHero slots 0x248 (0x00b531c0) and 0x24c (0x00b531d0) return them;
//     the figure code calls those slots: the map figure (NWorld::CAdvMapHero
//     slot 5, 0x008df730), the hero screen (0x006c7030), battles (0x00bc2010).
//   - The map figure: 0x008df730 takes slot 0x24c, the colour model for the
//     owner (0x008bae00), the figure's map animations, and gives them to the
//     map object's "set model" (0x00ce1160). The map object's constructors
//     (0x008e23a0, 0x008e2640) call it directly (0x008e2624, 0x008e28ab).
//   - Building a map figure makes new world objects (the robot saw two new
//     object numbers per rebuild, 2026-11-01) and marks others as changed
//     (0x0050c020). In a multiplayer game every PC must do that at the same
//     point of the game, or the PCs' object numbers and world checksums part.
//   - The list of all heroes is the game's named list "HEROES_ANY" (0x00bcb710
//     finds it; its CDBPtr entries at +0x44..+0x48, as the start-up check
//     0x00837590 reads them).
//
// What this file does:
//   1. Wraps IHero slots 0x248 and 0x24c (two vtable cells): for a hero with a
//      look, they answer the look hero's figures (its data file's +0x13c and
//      +0x14c); otherwise the game's answer. The hero object keeps its own.
//   2. Puts the look hero's ArenaMelee, AnimSet and camera references into the
//      hero's data object (game data, not the world), in memory only, with
//      counted references; the originals are kept and put back.
//   3. Notes every map figure as the game builds it (the vtable cell and the
//      two constructor calls of 0x008df730), holding a weak reference as the
//      game's own pointers do (the object's virtual base +8, as 0x00b530f0).
//   4. Single player and hot seat: after the adventure controller's frame
//      update (0x0060895e, also used by notify.cpp), rebuilds the figures whose
//      look changed. Nothing else depends on this PC there.
//   5. Multiplayer: no rebuild from frames. Each PC sends its looks to the
//      others right after its second segment report, as world-checksum messages
//      with check number -1 (as the co-op guard's hello: the game ignores them,
//      0x007c9410): 'k' a header (player number, the hero list's fingerprint),
//      'K' one per choice. A message counts only if it came before its sender's
//      report of segment kSendBy + 1, so every PC counts the same ones. At the
//      start of segment kDecideAt (the segment runner's call 0x007c9a67, the
//      same point of the game on every PC: every PC has reported that segment,
//      so every message that counts has arrived), every PC applies the same
//      rule: if every PC sent its looks in time with the same game data, the
//      looks are on for this game, and every PC rebuilds the same figures in
//      the same order (by object number); otherwise every hero keeps its look,
//      on every PC. From then on the game builds new figures with the looks,
//      at the same moments on every PC. A choice changed later (the panel)
//      counts on every PC, its sender too, at the segment skins::ChoiceCountsAt
//      gives for the sender's report count (hero_skins_logic.h).
//   6. The panel and the hero screen's Look button are for the hero the hero
//      screen shows, else for the selected hero (PanelTarget).
// Who decides a look (hero_skins_logic.cpp, Decide): the hero's owner, if
// human: in single player and hot seat this PC's file for every human; in
// multiplayer each player's own file. Computer players' heroes keep their looks.
//
// Settings, h5-together.ini [skins]:
//   skins=1           0 = nothing is installed
//   show_others=1     0 = no looks in multiplayer games (this PC takes no part,
//                     so nobody's looks show there; single player is unchanged)
//   trace=0           1 = log each figure built and each caller of the two slots
//   test_ai_heroes=0  testing: computer players' heroes take this PC's looks
//   live_hero_screen=1  0 = an open hero screen shows a new look only when opened again
// Choices: h5-together-skins.ini, [skins] section, hero=look by InternalName.
#include "hero_skins.h"

#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hero_skins_internal.h"
#include "hero_skins_logic.h"

#include "../hook/log.h"
#include "../hook/overlay.h"
#include "../hook/patch.h"

namespace mods {
namespace {

using skins::Catalog;
using skins::Choices;

// ---- the game (vanilla 3.1 layout, also MMH55_Utility) ----
const DWORD kHeroVftable = 0x00e7e484;     // NWorld::CHero's IHero part
const DWORD kArenaCell = kHeroVftable + 0x248;
const DWORD kArena = 0x00b531c0;           // add ecx, 0xb0; jmp 0x004f0dc0
const DWORD kMapCell = kHeroVftable + 0x24c;
const DWORD kMap = 0x00b531d0;             // add ecx, 0xb8; jmp 0x004f0dc0
const DWORD kHeroShared = 0xd0;            // IHero: its data file (CDBPtr, the object first)
const DWORD kHeroOwner = 0x128;            // IHero: its owner (IPlayer *), slot 0 (0x00b5aca0)
const DWORD kPlayerVftable = 0x00e1a09c;   // NWorld::CPlayer's IPlayer part
const DWORD kPlayerHuman = 0x74;           // 0 = human
const DWORD kPlayerNumber = 0x78;          // 1..8
const DWORD kSharedVftable = 0x00e6d450;   // NDb::SAdvMapHeroShared
const DWORD kSharedName = 0xec;            // InternalName (narrow string)
const DWORD kSharedTown = 0x164;           // TownType
const DWORD kSharedArena = 0x13c;
const DWORD kSharedMelee = 0x144;
const DWORD kSharedMap = 0x14c;
const DWORD kSharedCamera = 0x154;
const DWORD kSharedAnimSet = 0x4c;
const DWORD kFigureCell = 0x00e544b8;      // NWorld::CAdvMapHero (vtable 0x00e544a4) slot 5
const DWORD kFigure = 0x008df730;
const DWORD kFigureSites[] = {0x008e2624, 0x008e28ab};  // its two constructors
const DWORD kFigureHero = 0x158;           // CAdvMapHero: its hero (IHero *)
const DWORD kObjectNumber = 0x08;          // CAdvMapHero: its world object number (0x0050c020 pushes it)
const DWORD kHeroesAnyText = 0x00e498c0;   // "HEROES_ANY"
const DWORD kNamedList = 0x00bcb710;       // __fastcall(const string *): the list, or 0
const DWORD kListBegin = 0x44, kListEnd = 0x48;
const DWORD kGetBase = 0x004a5020;         // CDBPtr<SAdvMapObjectBaseShared>::Get, __fastcall(ptr)
const DWORD kGetCharacter = 0x004f0dc0;    // CDBPtr<SCharacter>::Get
const DWORD kGetAnimSet = 0x00680240;      // CDBPtr<SAnimSet>::Get
const DWORD kGetModel = 0x00453ff0;        // CDBPtr<SModel>::Get
const DWORD kCharacterModel = 0x44;        // SCharacter: its model
const DWORD kResourcePath = 0x20;          // an NDb resource: its own file (narrow string; its CDBID is at +0xc)
const DWORD kSharedFace = 0x12c;           // FaceTexture (CDBPtr<STexture>)
const DWORD kSharedNameFile = 0x16c;       // the text file of its name (narrow string; docs/subsystems/players.md)
const DWORD kTextureVftable = 0x00e90318;  // NDb::STexture
const DWORD kTextureFile = 0x40;           // DestName: the .dds in the game's data
const DWORD kPlayerSelected = 0x1d0;       // IPlayer: its selected hero (slot 0x0c, 0x004fcc40)
// The hero screen (7. below; docs/plans/hero-skins.md, "The hero screen's 3D figure").
const DWORD kHeroScreenVftable = 0x00e3584c;  // NUI::CHeroScreen2 (also MMH5.5's HeroScreen3 layout)
const DWORD kHeroScreenHero = 0x1b0;       // the hero it shows (IHero *)
const DWORD kHeroScreenShown = 0x15a;      // 1 once its update showed it (0 while it loads)
const DWORD kHeroScreenClosing = 0x160;    // set: its update closes it
const DWORD kHeroScreenPage = 0x708;
const DWORD kHeroScreenDialogs = 0x14c;    // a list head: not empty, its close (0x006d0960) refuses
const DWORD kHeroScreenPending = 0x720;    // an object: its close asks "are you sure" instead
const DWORD kHeroScreenMode = 0x7dc;       // 0: a hero of its owner's, alone (the only mode with the 3D figure
                                           // that its "next hero" serves; 0x006c05e0, 0x006c49c0)
const DWORD kHeroScreenSwitch = 0x006c4880;  // __thiscall(screen, hero): closes it and queues a new one for the hero
const DWORD kHeroScreenFigureCall = 0x006c710b;  // the hero screen's 3D figure (0x006c7030) asks slot 0x248 (trace only)
const DWORD kReleaseResource = 0x008c6150; // __fastcall(object): frees it when both counts are 0
const DWORD kUpdateCall = 0x0060895e;      // in 0x00608930: call 0x006082f0 (the adventure controller's frame)
const DWORD kUpdate = 0x006082f0;          // __thiscall, 5 stack arguments
const DWORD kControllerVftable = 0x00e29a2c;
const DWORD kControllerWorld = 0x18;
const DWORD kControllerLocal = 0x3c;       // this PC's player (IPlayer *)
const DWORD kMpStepCell = 0x00e43f1c + 0x10;  // NWorld::CMultiPlayerTransceiver slot 4
const DWORD kMpStep = 0x007ca990;
const DWORD kSpStepCell = 0x00e43c54 + 0x10;  // NWorld::CSinglePlayerTransceiver slot 4
const DWORD kSpStep = 0x007c7b20;
const DWORD kCheckCall = 0x007ca8d1;       // in the message pump 0x007ca7c0: call 0x007c9410
const DWORD kCheckHandler = 0x007c9410;
const DWORD kSegmentCall = 0x007c9a67;     // in the segment runner 0x007c98e0, before a segment's commands
const DWORD kSegmentTime = 0x007cc680;     // __thiscall(transceiver + 8, DWORD game time), ret 4
const DWORD kCrcCmdVftable = 0x00e43db0;   // NWorld::CCrcTestCmd, 0x2c bytes
const DWORD kGameNew = 0x00878fd0;
const DWORD kReleaseStrong = 0x008c61d0;   // __thiscall(object, mask)
// Transceiver fields (as the co-op guard reads them)
const DWORD kNet = 0x34, kReports = 0x3c, kReportsEnd = 0x40, kMe = 0x54;

// Multiplayer timing (in segments, see the top of this file and hero_skins_logic.h)
using skins::kSendAt;
using skins::kSendBy;
using skins::kDecideAt;

// The code whose layout this file relies on; checked before anything is changed.
struct Code {
	DWORD address;
	BYTE bytes[24];
	int size;
};
const Code kCode[] = {
	{kArena, {0x81, 0xc1, 0xb0, 0x00, 0x00, 0x00, 0xe9, 0xf5, 0xdb, 0x99, 0xff}, 11},
	{kMap, {0x81, 0xc1, 0xb8, 0x00, 0x00, 0x00, 0xe9, 0xe5, 0xdb, 0x99, 0xff}, 11},
	{0x00b5aca0, {0x8b, 0x81, 0x28, 0x01, 0x00, 0x00, 0xc3}, 7},         // IHero slot 0: the owner at +0x128
	{0x00b5ae40, {0x56, 0x8d, 0xb1, 0xd0, 0x00, 0x00, 0x00}, 7},         // IHero slot 0x34: the data file at +0xd0
	{0x00720e90, {0x8b, 0x41, 0x78, 0xc3}, 4},                           // IPlayer slot 0x2c: the number at +0x78
	{0x00500d60, {0x33, 0xc0, 0x39, 0x41, 0x74, 0x0f, 0x94, 0xc0, 0xc3}, 9},  // IPlayer slot 0x184: human = +0x74 == 0
	{kFigure, {0x53, 0x56, 0x8b, 0xf1, 0x8b, 0x8e, 0x58, 0x01, 0x00, 0x00, 0x8b, 0x01, 0x57, 0xff, 0x10}, 15},
	{0x008df752, {0xff, 0x90, 0x4c, 0x02, 0x00, 0x00}, 6},               // ... call [eax+0x24c]
	{0x008df7d1, {0x8b, 0xce, 0x5e, 0x5b, 0xe9, 0x46, 0xc8, 0xc2, 0xff}, 9},  // ... jmp 0x0050c020 (mark changed)
	{0x0050c020, {0x56, 0x8b, 0xf1, 0x83, 0x7e, 0x08, 0x00}, 7},         // its object number at +8
	// the data file's fields (its reader 0x00a0c3b0; the map objects' part 0x00a07e70)
	{0x00a0c3ea, {0x8d, 0x87, 0xec, 0x00, 0x00, 0x00}, 6},               // InternalName
	{0x00a0c4cc, {0x8d, 0x87, 0x3c, 0x01, 0x00, 0x00, 0x50, 0x50, 0x6a, 0x00, 0x68, 0x60, 0xd9, 0xe6, 0x00}, 15},
	{0x00a0c4e4, {0x8d, 0x87, 0x44, 0x01, 0x00, 0x00, 0x50, 0x50, 0x6a, 0x00, 0x68, 0x48, 0xd9, 0xe6, 0x00}, 15},
	{0x00a0c4f9, {0x8d, 0x87, 0x4c, 0x01, 0x00, 0x00, 0x50, 0x50, 0x6a, 0x00, 0x68, 0x30, 0xd9, 0xe6, 0x00}, 15},
	{0x00a0c50e, {0x8d, 0x87, 0x54, 0x01, 0x00, 0x00, 0x50, 0x50, 0x6a, 0x00, 0x68, 0x18, 0xd9, 0xe6, 0x00}, 15},
	{0x00a0c53b, {0x8d, 0x87, 0x64, 0x01, 0x00, 0x00, 0x50, 0x50, 0x6a, 0x00}, 10},  // TownType
	{0x00a07e8a, {0x8d, 0x47, 0x4c, 0x50, 0x50, 0x6a, 0x00, 0x68, 0x40, 0xba, 0xe1, 0x00}, 12},  // AnimSet
	{0x00e6d46c, {0xb0, 0xc3, 0xa0, 0x00}, 4},                           // the data file's reader cell
	// the list of all heroes, as the start-up check reads it
	{kHeroesAnyText, {'H', 'E', 'R', 'O', 'E', 'S', '_', 'A', 'N', 'Y', 0}, 11},
	{0x00837600, {0x8b, 0x4d, 0x48, 0x2b, 0x4d, 0x44}, 6},
	{0x00837620, {0x8b, 0x45, 0x44, 0x6a, 0x00, 0x68, 0x50, 0x8f, 0xf2, 0x00, 0x68, 0xe0, 0x8f, 0xf2, 0x00, 0x6a, 0x00,
	              0x8d, 0x0c, 0xf8, 0x32, 0xdb, 0xe8}, 23},
	{kNamedList, {0x51, 0x53, 0x55, 0x56, 0x8b, 0xe9, 0x57, 0x89, 0x6c, 0x24, 0x10, 0xe8}, 12},
	{kGetBase, {0x56, 0x8b, 0xf1, 0x80, 0x7e, 0x04, 0x00, 0x8b, 0x0d, 0x0c, 0x16, 0xf9, 0x00}, 13},
	{kGetCharacter, {0x56, 0x8b, 0xf1, 0x80, 0x7e, 0x04, 0x00, 0x8b, 0x0d, 0x78, 0x42, 0xfd, 0x00}, 13},
	{kGetAnimSet, {0x56, 0x8b, 0xf1, 0x80, 0x7e, 0x04, 0x00, 0x8b, 0x0d, 0xac, 0x20, 0xf9, 0x00}, 13},
	{kGetModel, {0x56, 0x8b, 0xf1, 0x80, 0x7e, 0x04, 0x00, 0x8b, 0x0d, 0x7c, 0x12, 0xf9, 0x00}, 13},
	{0x004fcc40, {0x8b, 0x81, 0xd0, 0x01, 0x00, 0x00, 0x85, 0xc0}, 8},   // IPlayer slot 0x0c: the selected hero
	{0x00e1a0a8, {0x40, 0xcc, 0x4f, 0x00}, 4},                           // ... its vtable cell
	{0x00a0c4a2, {0x8d, 0x87, 0x2c, 0x01, 0x00, 0x00, 0x50, 0x50, 0x6a, 0x00, 0x68, 0x88, 0xd9, 0xe6, 0x00}, 15},  // FaceTexture
	{0x00c7a69c, {0x8d, 0x47, 0x40}, 3},                                 // STexture: DestName at +0x40
	// the hero screen's "next hero" (0x006c49c0): push the hero; mov ecx,esi; call 0x006c4880
	// (with one hero, the hero itself), which closes the screen (0x006d0960) and, unless in
	// mode 1, queues a new one with the screen's own values and page (0x006c43d0, 0x00838c10)
	{0x006c4a61, {0x8b, 0x01, 0x50, 0x8b, 0xce, 0xe8, 0x15, 0xfe, 0xff, 0xff}, 10},
	{kHeroScreenSwitch, {0x83, 0xec, 0x34, 0x53, 0x55, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x48, 0x33, 0xdb, 0x3b, 0xfb, 0x8b}, 16},
	{0x006c48d5, {0xe8, 0x86, 0xc0, 0x00, 0x00}, 5},                     // call 0x006d0960 (close)
	{0x006c4909, {0x39, 0xae, 0xdc, 0x07, 0x00, 0x00}, 6},               // cmp [esi+0x7dc] (mode),1
	{0x006d0966, {0x8b, 0x86, 0x4c, 0x01, 0x00, 0x00, 0x39, 0x00, 0x74, 0x09}, 10},  // the close: the list at +0x14c,
	{0x006d0979, {0x8b, 0x86, 0x20, 0x07, 0x00, 0x00, 0x85, 0xc0}, 8},   // ... the object at +0x720,
	{0x006d0987, {0xf6, 0x40, 0x07, 0x80}, 4},                         // ... unless dying
	{0x006c49a8, {0xe8, 0x23, 0xfa, 0xff, 0xff, 0x8b, 0xc8, 0xe8, 0x5c, 0x42, 0x17, 0x00}, 12},
	{0x006c4a1e, {0x04, 0x00, 0x8b, 0x8e, 0xb0, 0x01, 0x00, 0x00}, 8},   // ... the hero at +0x1b0
	{0x006c05e0, {0x8b, 0x81, 0xdc, 0x07, 0x00, 0x00}, 6},               // the 3D figure in modes 0 and 5
	{0x006caee6, {0x80, 0xbe, 0x1c, 0xf9, 0xff, 0xff, 0x00}, 7},         // its update: closing at +0x160,
	{0x006caf05, {0x80, 0xbe, 0x16, 0xf9, 0xff, 0xff, 0x00}, 7},         // ... shown at +0x15a
	{kReleaseResource, {0x51, 0x83, 0x79, 0x08, 0x00, 0x75, 0x6b, 0xf7, 0x41, 0x04}, 10},
	// the adventure controller's frame (as notify.cpp checks it)
	{0x00608930, {0x56, 0x8b, 0xf1, 0x8b, 0x46, 0x3c}, 6},
	{0x006082f8, {0x8b, 0x4e, 0x18, 0x8b, 0x01, 0xff, 0x90, 0xa8, 0x00, 0x00}, 10},
	// the transceivers: own steps, the checksum handler, the segment runner's time call
	{kMpStep, {0x56, 0x8b, 0xf1, 0xe8, 0xf8, 0x1c, 0x21, 0x00}, 8},
	{kSpStep, {0x53, 0x55, 0x56, 0x57, 0x8b, 0xf9, 0x8b, 0x4f}, 8},
	{kCheckHandler, {0x83, 0xec, 0x60, 0x33, 0xc0, 0x39, 0x44, 0x24}, 8},
	{0x007c9a5f, {0x8d, 0x4e, 0x08, 0x53, 0x89, 0x4c, 0x24, 0x20}, 8},    // lea ecx,[esi+8]; push ebx; ... (then the call)
	{kSegmentTime, {0x53, 0x55, 0x8b, 0x6c, 0x24, 0x0c, 0x57, 0x55, 0x8b, 0xd9}, 10},
};

struct GameString {  // nstl::string: begin, end, end of the allocation
	const char *begin;
	const char *end;
	const char *cap;
};

typedef void *(__fastcall *SlotFn)(void *hero, void *edx);
typedef void(__fastcall *FigureFn)(void *figure, void *edx);
typedef void(__fastcall *UpdateFn)(void *controller, void *edx, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e);
typedef void(__fastcall *StepFn)(void *transceiver, void *edx);
typedef void(__fastcall *CheckFn)(void *transceiver, void *edx, int player, const BYTE *cmd);
typedef void(__fastcall *SegmentFn)(void *transceiver8, void *edx, DWORD time);
typedef void *(__fastcall *GetFn)(void *cdbptr);
typedef void *(__fastcall *ListFn)(const GameString *name);
typedef void(__fastcall *ReleaseFn)(void *object);

SlotFn g_arena_next = nullptr;
SlotFn g_map_next = nullptr;
FigureFn g_figure_cell_next = nullptr;
FigureFn g_figure_site_next = nullptr;
UpdateFn g_update_next = nullptr;
StepFn g_mp_step_next = nullptr;
StepFn g_sp_step_next = nullptr;
CheckFn g_check_next = nullptr;
SegmentFn g_segment_next = nullptr;

// ---- settings ----
bool g_disabled = false;  // part of the hooks could not go in: every hook only passes on
bool g_test_ai = false;   // testing ([skins] test_ai_heroes=1): computer players' heroes take this PC's looks too
bool g_show_others = true;
int g_trace = 0;
std::vector<std::pair<std::string, std::string>> g_entries;  // h5-together-skins.ini [skins]

// ---- state (g_lock) ----
CRITICAL_SECTION g_lock;
bool g_catalog_tried = false;
bool g_catalog_ok = false;
Catalog g_catalog;
std::vector<BYTE *> g_shared;                 // the data object of each catalog hero
std::unordered_map<const void *, int> g_index;  // data object -> catalog position
unsigned g_fingerprint = 0;
Choices g_local;
skins::Remotes g_remotes;

// The kind of game: set by the transceiver that steps (multiplayer wins).
enum Kind { kUnknown, kSingle, kMulti };
Kind g_kind = kUnknown;
const void *g_world = nullptr;  // the world of the controller's last frame
int g_single_frames = 0;        // frames since the game was found to be single player
DWORD g_frame_tick = 0;         // GetTickCount() of the controller's last frame
volatile DWORD g_step_tick = 0; // GetTickCount() of the last transceiver step (both kinds)
int g_local_player = 0;         // this PC's player number (controller +0x3c)
const BYTE *g_local_iplayer = nullptr;  // that player (IPlayer *)

// The hero's data object, with the look's references put in.
struct SwapState {
	int look = -1;
	BYTE saved[3][8];  // the hero's own references, in kSwapped order
};
std::vector<SwapState> g_applied;
struct Swapped {
	DWORD offset;
	DWORD getter;  // 0: copied as it is (the reader resolves it)
};
const Swapped kSwapped[] = {{kSharedAnimSet, kGetAnimSet}, {kSharedMelee, kGetCharacter}, {kSharedCamera, 0}};

// The map figures the game has built (weak references held).
struct Figure {
	BYTE *object;
	BYTE *hero;
	int built;  // the look it was built with (-1: its own)
};
std::vector<Figure> g_figures;
const size_t kMaxFigures = 1024;

// The current multiplayer game (one transceiver per game).
const int kMaxPcs = 16;
const void *g_transceiver = nullptr;
DWORD g_last_reports = 0;
bool g_sent = false;          // this PC sent its looks in time
bool g_header_ok[kMaxPcs];    // a header in time, with our game data, from each PC
DWORD g_segments = 0;         // segments the runner has started for this game
bool g_decided = false;
bool g_agreed = false;        // looks are on in this multiplayer game
// Choices changed during a multiplayer game: this PC's, waiting for its next
// step to be sent, and everybody's (this PC's own too), waiting for the
// segment at which every PC takes them in.
std::vector<DWORD> g_outgoing;
struct Pending {
	DWORD at;  // the segment
	skins::Message message;
};
std::vector<Pending> g_pending;

void NewGame(const void *transceiver);

// The hero screen's 3D figure after a look changed (7.).
bool g_live_screen = true;               // [skins] live_hero_screen
std::vector<int> g_screen_heroes;        // the heroes whose look changed (catalog positions), until the next frame
const char *g_screen_why = nullptr;
BYTE *g_screen_old = nullptr;            // the screen closed, until the new one is seen (never read)
BYTE *g_screen_hero = nullptr;           // its hero (compared only)
DWORD g_screen_since = 0;
DWORD g_screen_tries = 0;                // frames it waited for the screen to finish loading
void ServeHeroScreen();
char g_robot_screen[400] = {};  // a robot helper asked for (8.), done at the next adventure frame
void ServeRobot(const BYTE *controller);
void LogFigures(const char *why);

int g_logged = 0;  // log lines about figures and messages (capped)
const int kMaxLog = 300;
std::vector<DWORD> g_callers;  // trace: distinct callers of the two slots

DWORD Read(const void *base, DWORD offset)
{
	return *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(base) + offset);
}

bool Matches(DWORD address, const BYTE *bytes, int size)
{
	return memcmp(reinterpret_cast<const void *>(address), bytes, size) == 0;
}

struct Lock {
	Lock() { EnterCriticalSection(&g_lock); }
	~Lock() { LeaveCriticalSection(&g_lock); }
	Lock(const Lock &) = delete;
	Lock &operator=(const Lock &) = delete;
};

bool CanLog()
{
	if (g_logged >= kMaxLog)
		return false;
	++g_logged;
	return true;
}

// ---- game objects: the virtual base (its counts) of objects like CAdvMapHero ----
// vbptr at +4; the base at +4 + vbtable[1]: +4 strong count and flags (bit 31:
// being destroyed), +8 weak count (as 0x00b530f0 holds a pointer).
BYTE *VirtualBase(BYTE *object)
{
	const DWORD vbtable = Read(object, 4);
	return object + 4 + static_cast<LONG>(Read(reinterpret_cast<const void *>(vbtable), 4));
}

bool Dying(BYTE *object)
{
	return (VirtualBase(object)[7] & 0x80) != 0;
}

void AddWeak(BYTE *object)
{
	++*reinterpret_cast<DWORD *>(VirtualBase(object) + 8);
}

void ReleaseWeak(BYTE *object)
{
	BYTE *base = VirtualBase(object);
	DWORD *weak = reinterpret_cast<DWORD *>(base + 8);
	if (--*weak == 0)
		reinterpret_cast<ReleaseFn>(kReleaseResource)(base);
}

// ---- NDb resources: the reference count at +8 ----
void AddRef(void *resource)
{
	if (resource)
		++*reinterpret_cast<LONG *>(static_cast<BYTE *>(resource) + 8);
}

void ReleaseRef(void *resource)
{
	if (resource && --*reinterpret_cast<LONG *>(static_cast<BYTE *>(resource) + 8) == 0)
		reinterpret_cast<ReleaseFn>(kReleaseResource)(resource);
}

// ---- the catalog (the game's list of all heroes) ----
// Reads one list entry's hero; false if it is not a hero's data file.
bool ReadHero(BYTE *object, char *name, int size, DWORD *town)
{
	__try {
		if (!object || Read(object, 0) != kSharedVftable)
			return false;
		const char *b = reinterpret_cast<const char *>(Read(object, kSharedName));
		const char *e = reinterpret_cast<const char *>(Read(object, kSharedName + 4));
		if (!b || e <= b || e - b >= size)
			return false;
		memcpy(name, b, e - b);
		name[e - b] = 0;
		*town = Read(object, kSharedTown);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// Builds the catalog once (game thread, under g_lock): after the game's
// start-up check, every hero's data file is loaded.
bool EnsureCatalog()
{
	if (g_catalog_tried)
		return g_catalog_ok;
	g_catalog_tried = true;
	const char *text = reinterpret_cast<const char *>(kHeroesAnyText);
	const GameString name = {text, text + 10, text + 10};
	BYTE *list = static_cast<BYTE *>(reinterpret_cast<ListFn>(kNamedList)(&name));
	if (!list) {
		hooklog::Write("hero skins: the game's list of all heroes (HEROES_ANY) is not there: off");
		return false;
	}
	BYTE *begin = reinterpret_cast<BYTE *>(Read(list, kListBegin));
	BYTE *end = reinterpret_cast<BYTE *>(Read(list, kListEnd));
	const int count = begin && end > begin ? static_cast<int>((end - begin) / 8) : 0;
	int others = 0;
	for (int i = 0; i < count && i < 4096; ++i) {
		BYTE *object = static_cast<BYTE *>(reinterpret_cast<GetFn>(kGetBase)(begin + 8 * i));
		char hero_name[64];
		DWORD town = 0;
		if (!ReadHero(object, hero_name, sizeof(hero_name), &town)) {
			++others;
			continue;
		}
		if (g_index.count(object))
			continue;
		g_index[object] = static_cast<int>(g_catalog.size());
		g_catalog.push_back({hero_name, static_cast<unsigned>(town)});
		g_shared.push_back(object);
	}
	if (g_catalog.empty()) {
		hooklog::Write("hero skins: no hero in the game's list (%d entries): off", count);
		return false;
	}
	g_fingerprint = skins::Fingerprint(g_catalog);
	g_applied.resize(g_catalog.size());
	std::vector<std::string> notes;
	g_local = skins::ParseChoices(g_entries, g_catalog, &notes);
	hooklog::Write("hero skins: %d heroes in the game's list (fingerprint %04x%s); %d look(s) chosen in h5-together-skins.ini",
	               static_cast<int>(g_catalog.size()), g_fingerprint, others ? ", other entries skipped" : "",
	               static_cast<int>(g_local.size()));
	for (const std::string &n : notes)
		hooklog::Write("hero skins:   %s", n.c_str());
	g_catalog_ok = true;
	return true;
}

int HeroIndex(BYTE *hero)
{
	auto it = g_index.find(reinterpret_cast<const void *>(Read(hero, kHeroShared)));
	return it == g_index.end() ? -1 : it->second;
}

skins::Owner OwnerOf(BYTE *hero)
{
	skins::Owner o;
	const BYTE *player = reinterpret_cast<const BYTE *>(Read(hero, kHeroOwner));
	if (player && Read(player, 0) == kPlayerVftable) {
		o.present = true;
		o.human = Read(player, kPlayerHuman) == 0;
		o.number = static_cast<int>(Read(player, kPlayerNumber));
	}
	if (g_test_ai && o.present && !o.human && g_local_player > 0) {  // testing only
		o.human = true;
		o.number = g_local_player;
	}
	return o;
}

skins::Situation Now()
{
	skins::Situation s;
	// Multiplayer: only the decision taken at segment kDecideAt, the same on every
	// PC (nothing here may depend on this PC's frames).
	s.known = g_kind == kMulti ? g_agreed : g_kind == kSingle && g_world != nullptr;
	s.multiplayer = g_kind == kMulti;
	return s;
}

// The look `hero` wears now (-1: its own); *index = its catalog position.
int LookOf(BYTE *hero, int *index)
{
	*index = -1;
	if (!hero || !EnsureCatalog())
		return -1;
	*index = HeroIndex(hero);
	if (*index < 0)
		return -1;
	return skins::Decide(*index, OwnerOf(hero), Now(), g_local, g_remotes, g_catalog);
}

// Puts the look's references into the hero's data object, or its own back.
void EnsureFields(int index, int look)
{
	if (index < 0 || index >= static_cast<int>(g_applied.size()))
		return;
	SwapState &a = g_applied[index];
	if (a.look == look)
		return;
	BYTE *shared = g_shared[index];
	if (a.look >= 0) {  // put the hero's own back
		for (int i = 0; i < 3; ++i) {
			BYTE *field = shared + kSwapped[i].offset;
			void *current = reinterpret_cast<void *>(Read(field, 0));
			memcpy(field, a.saved[i], 8);
			ReleaseRef(current);  // the one this file put in (or what the game resolved it to)
		}
	}
	a.look = -1;
	if (look >= 0) {
		// The look hero's own references (its saved ones if it wears a look itself).
		SwapState &source = g_applied[look];
		for (int i = 0; i < 3; ++i) {
			BYTE *from = source.look >= 0 ? source.saved[i] : g_shared[look] + kSwapped[i].offset;
			BYTE *field = shared + kSwapped[i].offset;
			if (kSwapped[i].getter)
				reinterpret_cast<GetFn>(kSwapped[i].getter)(from);  // resolve the look's reference
			memcpy(a.saved[i], field, 8);  // the hero's own: its count now belongs to this file
			memcpy(field, from, 8);
			AddRef(reinterpret_cast<void *>(Read(from, 0)));
		}
		a.look = look;
	}
}

void NoteCaller(const char *slot, DWORD caller)
{
	if (g_trace < 1)
		return;
	for (DWORD c : g_callers)
		if (c == caller)
			return;
	if (g_callers.size() >= 64)
		return;
	g_callers.push_back(caller);
	hooklog::Write("hero skins: trace: slot %s called from %08lx", slot, caller);
}

// ---- 1. the hero's two figure slots ----
void *Answer(void *hero_ptr, void *edx, SlotFn next, DWORD field, const char *slot, DWORD caller)
{
	if (g_disabled)
		return next(hero_ptr, edx);
	BYTE *hero = static_cast<BYTE *>(hero_ptr);
	BYTE *look_field = nullptr;
	{
		Lock lock;
		NoteCaller(slot, caller);
		int index = -1;
		const int look = LookOf(hero, &index);
		EnsureFields(index, look);
		if (look >= 0)
			look_field = g_shared[look] + field;
		if (g_trace && caller == kHeroScreenFigureCall && index >= 0 && CanLog())
			hooklog::Write("hero skins: trace: the hero screen builds the 3D figure of %s (%s)", g_catalog[index].name.c_str(),
			               look >= 0 ? (g_catalog[look].name + "'s look").c_str() : "its own look");
	}
	if (look_field) {
		void *character = reinterpret_cast<GetFn>(kGetCharacter)(look_field);
		if (character)
			return character;
	}
	return next(hero_ptr, edx);
}

void *__fastcall ArenaHook(void *hero, void *edx)
{
	return Answer(hero, edx, g_arena_next, kSharedArena, "0x248 (battle)", reinterpret_cast<DWORD>(_ReturnAddress()));
}

void *__fastcall MapHook(void *hero, void *edx)
{
	return Answer(hero, edx, g_map_next, kSharedMap, "0x24c (map)", reinterpret_cast<DWORD>(_ReturnAddress()));
}

// ---- 3. the map figures ----
void Track(BYTE *object, bool from_constructor)
{
	if (g_disabled)
		return;
	BYTE *hero = reinterpret_cast<BYTE *>(Read(object, kFigureHero));
	if (!hero)
		return;
	Lock lock;
	// A map is loading (no controller frame and no transceiver step for a while;
	// a multiplayer game steps all the time, also behind the town screen):
	// forget the last game, so no stale rule picks a look. Every PC loads alike.
	const DWORD now = GetTickCount();
	if (from_constructor && now - g_frame_tick > 1500 && now - g_step_tick > 1500) {
		g_world = nullptr;
		g_kind = kUnknown;
		NewGame(nullptr);
	}
	int index = -1;
	const int look = LookOf(hero, &index);
	for (Figure &f : g_figures) {
		if (f.object == object) {
			f.hero = hero;
			f.built = look;
			return;
		}
	}
	for (size_t i = 0; i < g_figures.size(); ++i) {
		if (g_figures[i].hero == hero) {  // a new figure for this hero (hired again)
			ReleaseWeak(g_figures[i].object);
			g_figures.erase(g_figures.begin() + i);
			break;
		}
	}
	if (g_figures.size() >= kMaxFigures)
		return;
	AddWeak(object);
	g_figures.push_back({object, hero, look});
	if (g_trace && CanLog() && index >= 0)
		hooklog::Write("hero skins: trace: map figure of %s (%s)%s", g_catalog[index].name.c_str(),
		               look >= 0 ? g_catalog[look].name.c_str() : "its own look", from_constructor ? "" : ", rebuilt by the game");
}

void __fastcall FigureCellHook(void *object, void *edx)
{
	Track(static_cast<BYTE *>(object), false);
	g_figure_cell_next(object, edx);
}

void __fastcall FigureSiteHook(void *object, void *edx)
{
	Track(static_cast<BYTE *>(object), true);
	g_figure_site_next(object, edx);
}

// The figures whose look should change (under g_lock), dead ones dropped,
// sorted by world object number (the same order on every PC).
std::vector<BYTE *> FiguresToRebuild(size_t most)
{
	std::vector<std::pair<DWORD, BYTE *>> out;
	for (size_t i = 0; i < g_figures.size();) {
		Figure &f = g_figures[i];
		if (Dying(f.object) || reinterpret_cast<BYTE *>(Read(f.object, kFigureHero)) != f.hero) {
			ReleaseWeak(f.object);
			g_figures.erase(g_figures.begin() + i);
			continue;
		}
		int index = -1;
		const int look = LookOf(f.hero, &index);
		if (look != f.built && out.size() < most) {
			if (CanLog() && index >= 0)
				hooklog::Write("hero skins: %s: the map figure is rebuilt with %s", g_catalog[index].name.c_str(),
				               look >= 0 ? (g_catalog[look].name + "'s look").c_str() : "its own look");
			f.built = look;
			out.push_back({Read(f.object, kObjectNumber), f.object});
		}
		++i;
	}
	std::sort(out.begin(), out.end());
	std::vector<BYTE *> objects;
	for (const auto &p : out)
		objects.push_back(p.second);
	return objects;
}

// ---- 4. after each frame of the adventure controller ----
void AfterFrame(const BYTE *controller)
{
	if (g_disabled || !controller || Read(controller, 0) != kControllerVftable)
		return;
	std::vector<BYTE *> rebuild;
	{
		Lock lock;
		const void *world = reinterpret_cast<const void *>(Read(controller, kControllerWorld));
		if (world != g_world) {
			if (g_world || g_trace)
				hooklog::Write("hero skins: a new game world: the looks wait until the kind of game is known");
			g_world = world;
			if (g_kind != kMulti)  // a multiplayer game's state follows its segments only
				g_kind = kUnknown;
		}
		g_frame_tick = GetTickCount();
		const BYTE *local = reinterpret_cast<const BYTE *>(Read(controller, kControllerLocal));
		g_local_iplayer = local && Read(local, 0) == kPlayerVftable ? local : nullptr;
		g_local_player = g_local_iplayer ? static_cast<int>(Read(local, kPlayerNumber)) : 0;
		if (!g_catalog_ok && !g_catalog_tried)
			EnsureCatalog();
		// Single player only: a multiplayer game rebuilds at one segment, on every PC (5.).
		// A few frames first, so a multiplayer step can still say otherwise.
		if (g_kind == kSingle && ++g_single_frames > 30)
			rebuild = FiguresToRebuild(32);
	}
	for (BYTE *object : rebuild)
		g_figure_cell_next(object, nullptr);  // the game's own figure builder (the slots answer the look)
	ServeHeroScreen();
	ServeRobot(controller);
}

void __fastcall UpdateHook(void *controller, void *edx, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e)
{
	g_update_next(controller, edx, a, b, c, d, e);
	AfterFrame(static_cast<const BYTE *>(controller));
}

// ---- 5. multiplayer ----
// A world-checksum message with check number -1 and our value, to the other PCs
// (as the segment runner sends checksums, 0x007c9c74, and the co-op guard its hello).
void SendValue(const BYTE *transceiver, DWORD value)
{
	typedef void *(__cdecl * NewFn)(unsigned size);
	typedef void(__fastcall * SendFn)(void *net, void *edx, void *cmd);
	typedef void(__fastcall * StrongReleaseFn)(void *cmd, void *edx, DWORD mask);
	BYTE *cmd = static_cast<BYTE *>(reinterpret_cast<NewFn>(kGameNew)(0x2c));
	if (!cmd)
		return;
	memset(cmd, 0, 0x2c);
	*reinterpret_cast<DWORD *>(cmd) = kCrcCmdVftable;
	*reinterpret_cast<LONG *>(cmd + 0x0c) = -1;
	*reinterpret_cast<DWORD *>(cmd + 0x10) = value;
	*reinterpret_cast<DWORD *>(cmd + 4) += 1;
	void *net = reinterpret_cast<void *>(Read(transceiver, kNet));
	(*reinterpret_cast<SendFn *const *>(net))[7](net, nullptr, cmd);
	*reinterpret_cast<DWORD *>(cmd + 4) -= 1;
	if ((*reinterpret_cast<DWORD *>(cmd + 4) & 0xfffff) == 0)
		reinterpret_cast<StrongReleaseFn>(kReleaseStrong)(cmd, nullptr, 0xfffff);
}

// This PC's index and the number of PCs of a transceiver, or false.
bool Pcs(const BYTE *t, int *me, int *pcs)
{
	*me = static_cast<int>(Read(t, kMe));
	*pcs = static_cast<int>((Read(t, kReportsEnd) - Read(t, kReports)) / 4);
	return *pcs > 0 && *pcs <= kMaxPcs && *me >= 0 && *me < *pcs;
}

DWORD Reports(const BYTE *t, int pc)
{
	return reinterpret_cast<const DWORD *>(Read(t, kReports))[pc];
}

// A new multiplayer game (under g_lock): its own messages and decision.
void NewGame(const void *transceiver)
{
	g_transceiver = transceiver;
	g_last_reports = 0;
	g_sent = false;
	memset(g_header_ok, 0, sizeof(g_header_ok));
	g_segments = 0;
	g_decided = false;
	g_agreed = false;
	g_outgoing.clear();
	g_pending.clear();
	for (skins::Remote &r : g_remotes)
		r = skins::Remote();
}

void __fastcall MpStepHook(void *self, void *edx)
{
	g_mp_step_next(self, edx);
	g_step_tick = GetTickCount();
	if (g_disabled)
		return;
	const BYTE *t = static_cast<const BYTE *>(self);
	int me = 0, pcs = 0;
	if (!Pcs(t, &me, &pcs))
		return;
	const DWORD reports = Reports(t, me);
	std::vector<DWORD> values;
	{
		Lock lock;
		g_kind = kMulti;
		if (t != g_transceiver || reports < g_last_reports)
			NewGame(t);
		g_last_reports = reports;
		// A choice changed during the game (the panel): sent now, and taken in
		// by every PC (this one too) when ChoiceCountsAt says, the same rule the
		// others apply when it arrives (CheckHook).
		if (g_sent && !g_outgoing.empty()) {
			values.swap(g_outgoing);
			const DWORD at = skins::ChoiceCountsAt(reports);
			for (DWORD v : values) {
				if (at == 0)  // with this PC's first looks, before the decision
					skins::Receive(g_remotes, skins::Decode(v), g_catalog, g_fingerprint, nullptr);
				else
					g_pending.push_back({at, skins::Decode(v)});
			}
			hooklog::Write("hero skins: sent to the other PCs: %d changed look(s) (after this PC's report %lu; they count "
			               "at segment %lu)", static_cast<int>(values.size()), reports, at ? at : kDecideAt);
		}
		// Looks are sent once, between this PC's reports kSendAt and kSendBy;
		// show_others=0: this PC takes no part (it sends nothing).
		if (!values.empty() || g_sent || reports < kSendAt || reports > kSendBy || !g_show_others || g_local_player < 1 ||
		    g_local_player > skins::kMaxPlayers || !EnsureCatalog())
			goto send;
		if (g_catalog.size() > static_cast<size_t>(skins::kMaxCatalog)) {
			if (reports == kSendBy)
				hooklog::Write("hero skins: %d heroes do not fit in the messages: no looks in multiplayer games",
				               static_cast<int>(g_catalog.size()));
			return;
		}
		g_sent = true;
		g_outgoing.clear();  // the file's choices go now
		values.push_back(skins::EncodeHeader(g_local_player, g_fingerprint));
		for (const auto &c : g_local) {
			if (values.size() > 200)
				break;
			values.push_back(skins::EncodeChoice(g_local_player, c.first, c.second));
		}
		for (DWORD v : values)  // this PC takes its own in as the others do
			skins::Receive(g_remotes, skins::Decode(v), g_catalog, g_fingerprint, nullptr);
		hooklog::Write("hero skins: sent to the other PCs: %d look(s) for player %d (after this PC's report %lu)",
		               static_cast<int>(values.size()) - 1, g_local_player, reports);
	}
send:
	for (DWORD v : values)
		SendValue(t, v);
}

void __fastcall SpStepHook(void *self, void *edx)
{
	g_sp_step_next(self, edx);
	g_step_tick = GetTickCount();
	if (!g_disabled && g_kind == kUnknown) {  // a multiplayer step always wins
		Lock lock;
		if (g_kind == kUnknown) {
			g_kind = kSingle;
			g_single_frames = 0;
			if (g_trace)
				hooklog::Write("hero skins: trace: a single player (or hot seat) game");
		}
	}
}

void __fastcall CheckHook(void *self, void *edx, int pc, const BYTE *cmd)
{
	if (!g_disabled && cmd && *reinterpret_cast<const LONG *>(cmd + 0x0c) == -1) {
		const DWORD value = *reinterpret_cast<const DWORD *>(cmd + 0x10);
		const skins::Message m = skins::Decode(value);
		if (m.kind != skins::Message::kNone) {
			const BYTE *t = static_cast<const BYTE *>(self);
			Lock lock;
			if (t != g_transceiver)
				NewGame(t);
			int me = 0, pcs = 0;
			if (!Pcs(t, &me, &pcs) || pc < 0 || pc >= pcs || !EnsureCatalog())
				return;
			// Counted only if it came before its sender's report kSendBy + 1:
			// the same messages count on every PC.
			const DWORD sender_reports = Reports(t, pc);
			const DWORD at = skins::ChoiceCountsAt(sender_reports);
			if (at != 0) {
				// A choice changed during the game: every PC takes it in at the same segment.
				if (m.kind == skins::Message::kChoice && g_header_ok[pc]) {
					g_pending.push_back({at, m});
					if (CanLog())
						hooklog::Write("hero skins: from PC %d: a changed look for player %d (counts at segment %lu)", pc,
						               m.player, at);
				}
				else if (CanLog())
					hooklog::Write("hero skins: from PC %d: a message after its report %lu: too late, not counted", pc,
					               sender_reports);
				return;
			}
			std::string note;
			skins::Receive(g_remotes, m, g_catalog, g_fingerprint, &note);
			if (m.kind == skins::Message::kHeader)
				g_header_ok[pc] = m.player >= 1 && m.player <= skins::kMaxPlayers && g_remotes[m.player].same_data;
			if (!note.empty() && (m.kind == skins::Message::kHeader || g_trace) && CanLog())
				hooklog::Write("hero skins: from PC %d: %s", pc, note.c_str());
			return;  // ours: the game never sees it
		}
	}
	g_check_next(self, edx, pc, cmd);
}

// The start of each segment's commands (every PC runs the same segments in the
// same order): at kDecideAt, the decision and the rebuild, the same on every PC.
void __fastcall SegmentHook(void *transceiver8, void *edx, DWORD time)
{
	std::vector<BYTE *> rebuild;
	std::vector<int> changed_heroes;  // whose choice changed at this segment (catalog positions)
	if (!g_disabled) {
		const BYTE *t = static_cast<const BYTE *>(transceiver8) - 8;
		Lock lock;
		if (t != g_transceiver)
			NewGame(t);
		if (++g_segments == kDecideAt && !g_decided) {
			g_decided = true;
			int me = 0, pcs = 0;
			bool all = Pcs(t, &me, &pcs) && g_sent && g_catalog_ok;
			int missing = -1;
			for (int pc = 0; all && pc < pcs; ++pc)
				if (pc != me && !g_header_ok[pc]) {
					all = false;
					missing = pc;
				}
			g_agreed = all;
			if (g_agreed)
				hooklog::Write("hero skins: looks on for this game (all %d PCs sent theirs, with the same game data)", pcs);
			else if (!g_sent)
				hooklog::Write("hero skins: no looks in this game: this PC sent none (show_others=0, or too late)");
			else
				hooklog::Write("hero skins: no looks in this game: PC %d sent none in time (no H5 Together DLL, an old "
				               "one, other game data, or show_others=0)", missing);
			if (g_agreed && g_kind == kMulti)
				rebuild = FiguresToRebuild(kMaxFigures);
		}
		else if (g_decided && !g_pending.empty()) {
			// Changed choices whose segment has come, in the order they arrived
			// (each sender's in its own order: the same on every PC).
			bool changed = false;
			for (size_t i = 0; i < g_pending.size();) {
				if (g_pending[i].at > g_segments) {
					++i;
					continue;
				}
				if (g_agreed && g_catalog_ok) {
					std::string note;
					if (skins::Receive(g_remotes, g_pending[i].message, g_catalog, g_fingerprint, &note)) {
						changed = true;
						changed_heroes.push_back(g_pending[i].message.hero);
					}
					if (!note.empty() && CanLog())
						hooklog::Write("hero skins: segment %lu: %s", g_segments, note.c_str());
				}
				g_pending.erase(g_pending.begin() + i);
			}
			if (changed && g_kind == kMulti)
				rebuild = FiguresToRebuild(kMaxFigures);
		}
	}
	g_segment_next(transceiver8, edx, time);
	// After the game set the segment's time, before its commands: the same point on every PC.
	for (BYTE *object : rebuild)
		g_figure_cell_next(object, nullptr);
	// This PC's hero screen, if open on one of these heroes, opens again (7.):
	// the heroes whose map figure was rebuilt, and those whose choice changed,
	// also when no figure was rebuilt. In the owner's game of 2026-11-03 three
	// changes in a row rebuilt nothing on any PC (the hero had no map figure
	// then, likely in a town's garrison after a tavern visit), so the hero
	// screen stayed as it was and the Apply seemed to do nothing.
	if (!rebuild.empty() || !changed_heroes.empty()) {
		std::vector<int> heroes = changed_heroes;
		Lock lock;
		for (BYTE *object : rebuild) {
			const int index = HeroIndex(reinterpret_cast<BYTE *>(Read(object, kFigureHero)));
			if (index >= 0)
				heroes.push_back(index);
		}
		skins::RefreshHeroScreen(heroes, "a look changed at this segment");
	}
}

// ---- the panel's questions (hero_skins_internal.h) ----
bool CopyNarrow(const BYTE *object, DWORD offset, char *text, int size)
{
	__try {
		text[0] = 0;
		const char *b = reinterpret_cast<const char *>(Read(object, offset));
		const char *e = reinterpret_cast<const char *>(Read(object, offset + 4));
		if (!b || e <= b || e - b >= size)
			return false;
		memcpy(text, b, e - b);
		text[e - b] = 0;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		text[0] = 0;
		return false;
	}
}

std::string NarrowString(const BYTE *object, DWORD offset)
{
	char text[401];
	return CopyNarrow(object, offset, text, sizeof(text)) ? std::string(text) : std::string();
}

std::string Lower(std::string s)
{
	for (char &c : s)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return s;
}

// The model (an NDb resource) a hero's figure shows: data object + field -> SCharacter -> its model.
BYTE *FigureModel(int hero, DWORD field)
{
	BYTE *character = static_cast<BYTE *>(reinterpret_cast<GetFn>(kGetCharacter)(g_shared[hero] + field));
	return character ? static_cast<BYTE *>(reinterpret_cast<GetFn>(kGetModel)(character + kCharacterModel)) : nullptr;
}

std::string PortraitOf(int hero)
{
	const BYTE *texture = reinterpret_cast<const BYTE *>(Read(g_shared[hero], kSharedFace));
	if (!texture || Read(texture, 0) != kTextureVftable)
		return std::string();
	const std::string path = NarrowString(texture, kTextureFile);
	return path.size() > 4 && Lower(path.substr(path.size() - 4)) == ".dds" ? path : std::string();
}

std::string NameFileOf(int hero)
{
	std::string path = NarrowString(g_shared[hero], kSharedNameFile);
	if (path.size() < 5 || Lower(path.substr(path.size() - 4)) != ".txt")
		return std::string();
	while (!path.empty() && (path[0] == '/' || path[0] == '\\'))
		path.erase(0, 1);
	return path;
}

// ---- 7. the hero screen's 3D figure ----
// A shown hero screen never builds its 3D figure again (a try that made it do so
// crashed). So, after a look changed, the hero screen showing that hero is closed
// and opened again, the way its own "next hero" does it with a single hero
// (0x006c49c0 -> 0x006c4880(screen, hero)): it closes the screen and queues a new
// one with the same values and page. At the adventure controller's frame (the
// game's update), never inside drawing or input. Anything unexpected: nothing is
// done, and the screen shows the look when it is opened again.
BYTE *HeroScreenOnTop()
{
	char top[96];
	if (!overlay::TopScreen(top, sizeof(top)) || strcmp(top, ".?AVCHeroScreen2@NUI@@") != 0)
		return nullptr;
	BYTE *screen = static_cast<BYTE *>(overlay::TopScreenObject());
	return screen && Read(screen, 0) == kHeroScreenVftable ? screen : nullptr;
}

// "<hero> with <look>'s look, battle model <file>" (under g_lock).
std::string FigureShown(BYTE *hero)
{
	int index = -1;
	const int look = hero && !Dying(hero) ? LookOf(hero, &index) : -1;
	if (index < 0)
		return "a hero not in the list";
	const BYTE *model = FigureModel(look >= 0 ? look : index, kSharedArena);
	const std::string file = model ? NarrowString(model, kResourcePath) : std::string();
	return g_catalog[index].name + " with " + (look >= 0 ? g_catalog[look].name + "'s look" : std::string("its own look")) +
	       ", battle model " + (file.empty() ? "?" : file);
}

enum ScreenPhase { kScreenIdle, kScreenClosed, kScreenLoading };
ScreenPhase g_screen_phase = kScreenIdle;

void ServeHeroScreen()
{
	if (g_disabled)
		return;
	BYTE *screen = HeroScreenOnTop();
	if (g_screen_phase != kScreenIdle) {  // the screen was closed: wait for the new one, and log what it shows
		// The new one usually takes the old one's place in memory and may load
		// within one frame, so it is recognized by time: shown again, for the same
		// hero, at least half a second after the close.
		const DWORD since = GetTickCount() - g_screen_since;
		if (screen && !screen[kHeroScreenShown])
			g_screen_phase = kScreenLoading;
		BYTE *hero = screen ? reinterpret_cast<BYTE *>(Read(screen, kHeroScreenHero)) : nullptr;
		if (screen && screen[kHeroScreenShown] && hero == g_screen_hero && (since >= 500 || g_screen_phase == kScreenLoading)) {
			std::string what;
			{
				Lock lock;
				what = FigureShown(hero);
			}
			hooklog::Write("hero skins: the hero screen is open again after %lu ms (%s, page %lu, mode %lu): %s", since,
			               screen != g_screen_old ? "a new screen" : "in the same place", Read(screen, kHeroScreenPage),
			               Read(screen, kHeroScreenMode), what.c_str());
			g_screen_phase = kScreenIdle;
		}
		else if (GetTickCount() - g_screen_since > 10000) {
			hooklog::Write("hero skins: the hero screen was closed for the new look, but no new one showed within 10 s");
			g_screen_phase = kScreenIdle;
		}
		return;
	}
	std::vector<int> heroes;
	const char *why = nullptr;
	{
		Lock lock;
		if (g_screen_heroes.empty())
			return;
		if (screen && !screen[kHeroScreenShown] && !screen[kHeroScreenClosing] && ++g_screen_tries < 300)
			return;  // it is still loading: wait (up to about 300 frames)
		heroes.swap(g_screen_heroes);
		why = g_screen_why;
		g_screen_tries = 0;
	}
	if (!screen || !g_live_screen)
		return;  // no hero screen open (it shows the look when opened), or turned off
	BYTE *hero = reinterpret_cast<BYTE *>(Read(screen, kHeroScreenHero));
	const DWORD mode = Read(screen, kHeroScreenMode);
	int index = -1;
	std::string name;
	{
		Lock lock;
		index = hero && !Dying(hero) ? HeroIndex(hero) : -1;
		if (index >= 0)
			name = g_catalog[index].name;
	}
	if (index < 0 || std::find(heroes.begin(), heroes.end(), index) == heroes.end())
		return;  // it shows another hero
	// As its close (0x006d0960) would go: no list entry (it would refuse) and no
	// pending object (it would ask "are you sure" and keep the screen).
	const BYTE *dialogs = reinterpret_cast<const BYTE *>(Read(screen, kHeroScreenDialogs));
	const BYTE *pending = reinterpret_cast<const BYTE *>(Read(screen, kHeroScreenPending));
	const char *busy = mode != 0                                      ? "not its own-hero mode"
	                   : screen[kHeroScreenClosing]                   ? "closing"
	                   : !screen[kHeroScreenShown]                    ? "still loading"
	                   : !dialogs || Read(dialogs, 0) != reinterpret_cast<DWORD>(dialogs) ? "a dialog open"
	                   : pending && !(pending[7] & 0x80)              ? "a change waits for a confirmation"
	                                                                  : nullptr;
	if (busy) {
		hooklog::Write("hero skins: the hero screen is left as it is (mode %lu, %s): it shows the look when opened again",
		               mode, busy);
		return;
	}
	hooklog::Write("hero skins: the hero screen closes and opens again for %s (%s), page %lu, as its \"next hero\" does",
	               name.c_str(), why, Read(screen, kHeroScreenPage));
	g_screen_old = screen;
	g_screen_hero = hero;
	g_screen_since = GetTickCount();
	g_screen_phase = kScreenClosed;
	reinterpret_cast<void(__fastcall *)(void *, void *, void *)>(kHeroScreenSwitch)(screen, nullptr, hero);
}

// ---- 8. robot helpers (robot mode: the panel's robot commands) ----
// The screens' own message handlers, called as the game's message dispatch calls
// them (this = the screen, the message's number on the stack): the robot's msg|
// step is not taken by these screens' observers.
const DWORD kAdvScreenVftable = 0x00e230e4;   // NUI::CAdventureScreen
const DWORD kAdvScreenController = 0x5f4;     // its adventure controller (checked: the one whose frame this is)
const DWORD kTownScreenVftable = 0x00e2a64c;  // NUI::CTownScreen
struct Handler {
	const char *what;
	DWORD address;
	BYTE bytes[16];
	int size;
};
const Handler kAdvHeroAttributes = {"hero_attributes", 0x00599b30,  // push 1; call 0x00599ab0; mov al,1; ret 4
                                    {0x6a, 0x01, 0xe8, 0x79, 0xff, 0xff, 0xff, 0xb0, 0x01, 0xc2, 0x04, 0x00}, 12};
const Handler kAdvSelectTown = {"input_select_town", 0x0058c420,  // (message, the town's place in the list), ret 8
                                {0x56, 0x8b, 0xf1, 0x8b, 0x8e, 0xf4, 0x05, 0x00, 0x00, 0xe8, 0x52, 0x75}, 12};
const Handler kAdvShowTown = {"adv_show_town", 0x0058c320,  // (message), ret 4
                              {0x56, 0x8b, 0xf1, 0x8b, 0x8e, 0xf4, 0x05, 0x00, 0x00, 0xe8, 0x32, 0x72}, 12};
const Handler kTownHeroAttributes = {"hero_attributes", 0x006194b0,  // (message), ret 4: ... push 1; call 0x00619320
                                     {0x56, 0x8b, 0xf1, 0x8b, 0x86, 0x4c, 0x01, 0x00, 0x00, 0x39, 0x00, 0x75, 0x24}, 13};
// The hero screen's own arrows (its key handler 0x006c85b0: case 3 next, case 1 previous;
// the message argument is not read): the next / previous hero of the shown hero's owner,
// through 0x006c4880 (the screen closes and a new one opens for that hero).
const Handler kHeroScreenNext = {"next hero", 0x006c49c0,  // (message), ret 4
                                 {0x80, 0x3d, 0xb4, 0xf0, 0xfd, 0x00, 0x00, 0x56, 0x8b, 0xf1, 0x75, 0x21}, 12};
const Handler kHeroScreenPrev = {"previous hero", 0x006c4a80,
                                 {0x80, 0x3d, 0xb4, 0xf0, 0xfd, 0x00, 0x00, 0x56, 0x8b, 0xf1, 0x75, 0x21}, 12};
// The adventure controller's own script machine (+0x40, made where "console script
// commands" are allowed: single player). 0x00603b20 runs a text in it with its slot 0:
// mov ecx,[esi+0x40]; mov edx,[esp+8]; mov eax,[ecx]; add esp,4; push edx; call [eax].
const DWORD kControllerScript = 0x40;
const Code kScriptRunCode = {0x00603ba4, {0x8b, 0x4e, 0x40, 0x8b, 0x54, 0x24, 0x08, 0x8b, 0x01, 0x83, 0xc4, 0x04, 0x52, 0xff, 0x10}, 15};

// Robot only: a line of Lua in the controller's script machine, as the console's
// script commands run (e.g. SetObjectOwner, to give the robot's player a second hero).
void RunScript(const BYTE *controller, const char *code)
{
	BYTE *script = reinterpret_cast<BYTE *>(Read(controller, kControllerScript));
	if (!Matches(kScriptRunCode.address, kScriptRunCode.bytes, kScriptRunCode.size) || !script || Dying(script)) {
		hooklog::Write("hero skins: robot: no script machine in this game (or its code is not as expected): \"%s\" not run",
		               code);
		return;
	}
	__try {
		reinterpret_cast<void(__fastcall *)(void *, void *, const char *)>(Read(reinterpret_cast<const void *>(Read(script, 0)), 0))(
		    script, nullptr, code);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("hero skins: robot: the script \"%s\" crashed (caught)", code);
		return;
	}
	hooklog::Write("hero skins: robot: script run: %s", code);
}

bool CallHandler(const Handler &h, BYTE *screen, DWORD a, DWORD b, bool two)
{
	if (!Matches(h.address, h.bytes, h.size)) {
		hooklog::Write("hero skins: robot: the handler \"%s\" (%08lx) is not as expected: not called", h.what, h.address);
		return false;
	}
	__try {
		if (two)
			reinterpret_cast<void(__fastcall *)(void *, void *, DWORD, DWORD)>(h.address)(screen, nullptr, a, b);
		else
			reinterpret_cast<void(__fastcall *)(void *, void *, DWORD)>(h.address)(screen, nullptr, a);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hooklog::Write("hero skins: robot: the handler \"%s\" crashed (caught)", h.what);
		return false;
	}
	hooklog::Write("hero skins: robot: called the screen's \"%s\"", h.what);
	return true;
}

void ServeRobot(const BYTE *controller)
{
	char text[sizeof(g_robot_screen)];
	{
		Lock lock;
		if (!g_robot_screen[0])
			return;
		memcpy(text, g_robot_screen, sizeof(text));
		g_robot_screen[0] = 0;
	}
	char top[96] = {};
	overlay::TopScreen(top, sizeof(top));
	BYTE *screen = static_cast<BYTE *>(overlay::TopScreenObject());
	if (strncmp(text, "lua ", 4) == 0) {
		RunScript(controller, text + 4);
		return;
	}
	if (strcmp(text, "figures") == 0) {
		LogFigures("robot");
		return;
	}
	if (strcmp(text, "hero_screen_next") == 0 || strcmp(text, "hero_screen_prev") == 0) {
		BYTE *hero_screen = HeroScreenOnTop();
		if (!hero_screen) {
			hooklog::Write("hero skins: robot: \"%s\": no hero screen on top (%s)", text, top);
			return;
		}
		CallHandler(text[12] == 'n' ? kHeroScreenNext : kHeroScreenPrev, hero_screen, 0, 0, false);
		return;
	}
	if (strcmp(text, "town_hero_screen") == 0) {
		if (!screen || Read(screen, 0) != kTownScreenVftable) {
			hooklog::Write("hero skins: robot: \"%s\": no town screen on top (%s)", text, top);
			return;
		}
		CallHandler(kTownHeroAttributes, screen, 0, 0, false);
		return;
	}
	if (!screen || Read(screen, 0) != kAdvScreenVftable ||
	    reinterpret_cast<const BYTE *>(Read(screen, kAdvScreenController)) != controller) {
		hooklog::Write("hero skins: robot: \"%s\": no adventure screen of this controller on top (%s)", text, top);
		return;
	}
	if (strcmp(text, "hero_screen") == 0)
		CallHandler(kAdvHeroAttributes, screen, 0, 0, false);
	else if (strncmp(text, "town", 4) == 0 &&
	         CallHandler(kAdvSelectTown, screen, 0, static_cast<DWORD>(atoi(text + 4)), true))
		CallHandler(kAdvShowTown, screen, 0, 0, false);
}

// Robot only ("figures"): every map figure this PC knows, with the look it was
// built with and the look its hero wears now, and what this PC's player selected.
void LogFigures(const char *why)
{
	Lock lock;
	std::string line;
	for (const Figure &f : g_figures) {
		if (Dying(f.object) || reinterpret_cast<BYTE *>(Read(f.object, kFigureHero)) != f.hero)
			continue;
		int index = -1;
		const int look = LookOf(f.hero, &index);
		if (index < 0)
			continue;
		const skins::Owner o = OwnerOf(f.hero);
		char owner[48];
		_snprintf_s(owner, sizeof(owner), _TRUNCATE, "%s%d, faction %u", o.present ? "player " : "no owner ", o.number,
		            g_catalog[index].town);
		line += (line.empty() ? "" : "; ") + g_catalog[index].name + " (" + owner + "): " +
		        (look >= 0 ? g_catalog[look].name + "'s look" : std::string("its own look")) +
		        (f.built != look ? ", its figure still shows another" : "");
	}
	BYTE *selected = g_local_iplayer ? reinterpret_cast<BYTE *>(Read(g_local_iplayer, kPlayerSelected)) : nullptr;
	const int s = selected && !Dying(selected) ? HeroIndex(selected) : -1;
	hooklog::Write("hero skins: figures (%s): %s; selected hero: %s", why, line.empty() ? "none" : line.c_str(),
	               s >= 0 ? g_catalog[s].name.c_str() : "none");
}

// The hero the panel and the hero screen's Look button are for (under g_lock):
// the hero the hero screen shows when it is on top, else this PC's player's
// selected hero. The hero screen is not always about the selected hero: its own
// "next hero" / "previous hero" arrows and the town screen's portraits open it
// for other heroes, and the selection on the map stays where it was (found by the
// owner, 2026-11-03: "hero A change impacts hero B"). Null: no hero, or the
// screen shows a hero whose look this PC's player does not choose; *why says so.
BYTE *PanelTarget(const char **why)
{
	*why = "no selected hero of this PC's player";
	if (!g_local_iplayer)
		return nullptr;
	if (BYTE *screen = HeroScreenOnTop()) {
		BYTE *hero = reinterpret_cast<BYTE *>(Read(screen, kHeroScreenHero));
		*why = "the hero screen shows no hero";
		if (!hero || Dying(hero))
			return nullptr;
		// Whose choice counts (Decide): in single player and hot seat every human
		// owner's (this PC's file); in multiplayer each player's own.
		const BYTE *player = reinterpret_cast<const BYTE *>(Read(hero, kHeroOwner));
		const bool human = player && Read(player, 0) == kPlayerVftable && Read(player, kPlayerHuman) == 0;
		*why = "the hero screen shows a hero of another player (its owner chooses its look)";
		if (!human || (g_kind == kMulti && player != g_local_iplayer))
			return nullptr;
		return hero;
	}
	BYTE *hero = reinterpret_cast<BYTE *>(Read(g_local_iplayer, kPlayerSelected));
	return hero && !Dying(hero) ? hero : nullptr;
}

}  // namespace

namespace skins {

void RobotScreenCommand(const char *text)
{
	Lock lock;
	strncpy_s(g_robot_screen, text, _TRUNCATE);
}

std::string InternalName(int hero)
{
	Lock lock;
	return hero >= 0 && hero < static_cast<int>(g_catalog.size()) ? g_catalog[hero].name : std::string();
}

void RefreshHeroScreen(const std::vector<int> &heroes, const char *why)
{
	if (g_disabled || heroes.empty())
		return;
	Lock lock;  // done at the next adventure frame (ServeHeroScreen)
	for (int h : heroes)
		if (std::find(g_screen_heroes.begin(), g_screen_heroes.end(), h) == g_screen_heroes.end())
			g_screen_heroes.push_back(h);
	g_screen_why = why;
	g_screen_tries = 0;
}

bool PanelLookPortrait(std::string *dds)
{
	dds->clear();
	if (g_disabled || !g_local_iplayer || !g_catalog_ok)
		return false;
	Lock lock;
	const char *why = nullptr;
	BYTE *hero = PanelTarget(&why);
	if (!hero)
		return false;
	const int index = HeroIndex(hero);
	if (index < 0)
		return false;
	auto it = g_local.find(index);
	*dds = PortraitOf(it == g_local.end() ? index : it->second);
	return true;
}

std::string NameFile(int hero)
{
	Lock lock;
	return hero >= 0 && hero < static_cast<int>(g_catalog.size()) ? NameFileOf(hero) : std::string();
}

bool PanelHero(PanelData *out, const char **why)
{
	*out = PanelData();
	*why = "the looks are off";
	if (g_disabled || !g_local_iplayer)
		return false;
	Lock lock;
	if (!EnsureCatalog())
		return false;
	BYTE *hero = PanelTarget(why);
	if (!hero)
		return false;
	const int target = HeroIndex(hero);
	*why = "a hero not in the game's list of all heroes";
	if (target < 0)
		return false;
	out->hero = target;
	out->from_hero_screen = HeroScreenOnTop() != nullptr;
	out->internal = g_catalog[target].name;
	out->name_file = NameFileOf(target);
	out->portrait = PortraitOf(target);
	auto it = g_local.find(target);
	out->chosen = it == g_local.end() ? -1 : it->second;
	out->multiplayer = g_kind == kMulti;
	out->decided = g_decided;
	out->agreed = g_agreed;
	// The faction's looks: heroes grouped by the models of their map and battle figures.
	std::vector<std::pair<std::pair<BYTE *, BYTE *>, Look>> groups;
	for (int j = 0; j < static_cast<int>(g_catalog.size()); ++j) {
		if (g_catalog[j].town != g_catalog[target].town)
			continue;
		BYTE *map_model = FigureModel(j, kSharedMap);
		BYTE *battle_model = FigureModel(j, kSharedArena);
		if (!map_model || !battle_model)
			continue;
		const std::pair<BYTE *, BYTE *> key(map_model, battle_model);
		size_t g = 0;
		while (g < groups.size() && groups[g].first != key)
			++g;
		if (g == groups.size())
			groups.push_back({key, Look()});
		groups[g].second.members.push_back(j);
		if (j == target)
			groups[g].second.own = true;
	}
	for (auto &group : groups) {
		Look &look = group.second;
		// Its name: the panel's hero for its own look; else the hero the models are named after; else the first.
		const std::string paths = Lower(NarrowString(group.first.first, kResourcePath) + "|" +
		                                NarrowString(group.first.second, kResourcePath));
		look.hero = look.own ? target : look.members[0];
		for (int m : look.members)
			if (!look.own && paths.find(Lower(g_catalog[m].name)) != std::string::npos) {
				look.hero = m;
				break;
			}
		look.internal = g_catalog[look.hero].name;
		look.portrait = PortraitOf(look.hero);
		look.name_file = NameFileOf(look.hero);
		if (look.own)
			out->looks.insert(out->looks.begin(), look);
		else
			out->looks.push_back(look);
	}
	return true;
}

Applied ApplyLook(int hero, int look, std::string *note)
{
	Lock lock;
	if (!g_catalog_ok || hero < 0 || hero >= static_cast<int>(g_catalog.size()) ||
	    (look >= 0 && !LookAllowed(g_catalog, hero, look))) {
		*note = "not a look this hero may wear";
		return kNotApplied;
	}
	if (look == hero)
		look = -1;
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together-skins", L".ini", path, MAX_PATH);
	const std::wstring key(g_catalog[hero].name.begin(), g_catalog[hero].name.end());
	const std::wstring value = look >= 0 ? std::wstring(g_catalog[look].name.begin(), g_catalog[look].name.end()) : L"";
	if (!WritePrivateProfileStringW(L"skins", key.c_str(), look >= 0 ? value.c_str() : nullptr, path))
		hooklog::Write("hero skins: h5-together-skins.ini could not be written (error %lu)", GetLastError());
	if (look >= 0)
		g_local[hero] = look;
	else
		g_local.erase(hero);
	*note = g_catalog[hero].name + (look >= 0 ? " wears the look of " + g_catalog[look].name : " wears its own look");
	if (g_kind != kMulti)
		return kAppliedNow;  // the frame pass rebuilds it (single player, hot seat)
	if (g_decided && !g_agreed)
		return kSavedForNextGames;
	if (g_local_player >= 1)
		g_outgoing.push_back(EncodeChoice(g_local_player, hero, look));
	return kAppliedSoon;
}

void *ShownHero(bool *on_hero_screen)
{
	*on_hero_screen = false;
	BYTE *hero = nullptr;
	if (BYTE *screen = HeroScreenOnTop()) {
		*on_hero_screen = true;
		hero = reinterpret_cast<BYTE *>(Read(screen, kHeroScreenHero));
	}
	else if (g_local_iplayer)
		hero = reinterpret_cast<BYTE *>(Read(g_local_iplayer, kPlayerSelected));
	return hero && Read(hero, 0) == kHeroVftable && !Dying(hero) ? hero : nullptr;
}

}  // namespace skins

namespace {

// ---- settings ----
void ReadChoices()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together-skins", L".ini", path, MAX_PATH);
	std::vector<wchar_t> buffer(32768);
	const DWORD n = GetPrivateProfileSectionW(L"skins", buffer.data(), static_cast<DWORD>(buffer.size()), path);
	for (DWORD i = 0; i < n;) {
		const wchar_t *line = buffer.data() + i;
		const size_t len = wcslen(line);
		char text[256];
		const int k = WideCharToMultiByte(CP_UTF8, 0, line, static_cast<int>(len), text, sizeof(text) - 1, nullptr, nullptr);
		text[k > 0 ? k : 0] = 0;
		const char *eq = strchr(text, '=');
		if (eq)
			g_entries.push_back({std::string(text, eq - text), std::string(eq + 1)});
		i += static_cast<DWORD>(len) + 1;
	}
}

bool CodeAsExpected()
{
	for (const Code &c : kCode) {
		if (!Matches(c.address, c.bytes, c.size)) {
			hooklog::Write("hero skins: the game's code at %08lx is not as expected: off", c.address);
			return false;
		}
	}
	return true;
}

}  // namespace

void ReloadHeroSkinsSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together", L".ini", path, MAX_PATH);
	g_live_screen = GetPrivateProfileIntW(L"skins", L"live_hero_screen", 1, path) != 0;
	hooklog::Write("hero skins: from the settings menu: after Apply the hero screen %s",
	               g_live_screen ? "opens again with a new look" : "keeps its figure");
}

void ApplyHeroSkinsFromSettings()
{
	wchar_t path[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together", L".ini", path, MAX_PATH);
	if (!GetPrivateProfileIntW(L"skins", L"skins", 1, path)) {
		hooklog::Write("hero skins: off ([skins] skins=0)");
		return;
	}
	g_show_others = GetPrivateProfileIntW(L"skins", L"show_others", 1, path) != 0;
	g_trace = static_cast<int>(GetPrivateProfileIntW(L"skins", L"trace", 0, path));
	g_test_ai = GetPrivateProfileIntW(L"skins", L"test_ai_heroes", 0, path) != 0;
	g_live_screen = GetPrivateProfileIntW(L"skins", L"live_hero_screen", 1, path) != 0;
	ReadChoices();
	if (g_entries.empty() && !g_show_others) {
		hooklog::Write("hero skins: no look chosen and show_others=0: nothing installed");
		return;
	}
	if (!CodeAsExpected())
		return;
	InitializeCriticalSection(&g_lock);
	// Vtable cells and call sites; stacked hooks are fine (hookpatch checks what is there).
	g_arena_next = reinterpret_cast<SlotFn>(hookpatch::PatchPointer(kArenaCell, kArena, &ArenaHook));
	if (!g_arena_next) {
		hooklog::Write("hero skins: the hero's battle figure slot is not as expected: off");
		return;
	}
	g_map_next = reinterpret_cast<SlotFn>(hookpatch::PatchPointer(kMapCell, kMap, &MapHook));
	if (!g_map_next) {
		g_disabled = true;  // the battle slot stays wrapped, and only passes on
		hooklog::Write("hero skins: the hero's map figure slot is not as expected: off");
		return;
	}
	g_figure_cell_next = reinterpret_cast<FigureFn>(hookpatch::PatchPointer(kFigureCell, kFigure, &FigureCellHook));
	g_figure_site_next = reinterpret_cast<FigureFn>(hookpatch::RedirectCalls(kFigureSites, 2, kFigure, &FigureSiteHook));
	g_update_next = reinterpret_cast<UpdateFn>(hookpatch::RedirectCall(kUpdateCall, kUpdate, &UpdateHook));
	g_sp_step_next = reinterpret_cast<StepFn>(hookpatch::PatchPointer(kSpStepCell, kSpStep, &SpStepHook));
	g_mp_step_next = reinterpret_cast<StepFn>(hookpatch::PatchPointer(kMpStepCell, kMpStep, &MpStepHook));
	g_check_next = reinterpret_cast<CheckFn>(hookpatch::RedirectCall(kCheckCall, kCheckHandler, &CheckHook));
	g_segment_next = reinterpret_cast<SegmentFn>(hookpatch::RedirectCall(kSegmentCall, kSegmentTime, &SegmentHook));
	if (!g_figure_cell_next || !g_figure_site_next || !g_update_next || !g_sp_step_next || !g_mp_step_next ||
	    !g_check_next || !g_segment_next) {
		g_disabled = true;  // the hooks that went in stay, and only pass on
		hooklog::Write("hero skins: part of the game's code is not as expected (figure %d/%d, frame %d, steps %d/%d, "
		               "messages %d, segments %d): the looks stay the game's",
		               g_figure_cell_next != nullptr, g_figure_site_next != nullptr, g_update_next != nullptr,
		               g_sp_step_next != nullptr, g_mp_step_next != nullptr, g_check_next != nullptr,
		               g_segment_next != nullptr);
		return;
	}
	InstallHeroSkinsPanel();
	hooklog::Write("hero skins: on; %d line(s) in h5-together-skins.ini (read when the game's hero list is); multiplayer "
	               "looks %s; an open hero screen %s%s%s",
	               static_cast<int>(g_entries.size()), g_show_others ? "when every PC takes part" : "off (show_others=0)",
	               g_live_screen ? "opens again with a new look" : "keeps its figure (live_hero_screen=0)", g_trace ? "; trace" : "", g_test_ai ? "; TESTING: computer players' heroes take this PC's looks" : "");
}

}  // namespace mods
