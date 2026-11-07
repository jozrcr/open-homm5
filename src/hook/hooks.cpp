#include "hooks.h"

#include <windows.h>

#include "crash.h"
#include "game_build.h"
#include "log.h"
#include "patch.h"
#include "overlay.h"
#include "watchdog.h"
#include "../mods/ai_pacing.h"
#include "../mods/anim_throttle.h"
#include "../mods/audio_volume.h"
#include "../mods/autosave.h"
#include "../mods/coop_features.h"
#include "../mods/camera.h"
#include "../mods/coop_guard.h"
#include "../mods/edge_sky.h"
#include "../mods/endless_turns.h"
#include "../mods/frame_limit.h"
#include "../mods/spectate.h"
#include "../mods/fast_paths.h"
#include "../mods/hero_skins.h"
#include "../mods/join_time.h"
#include "../mods/lobby_hero_tips.h"
#include "../mods/skill_wheel.h"
#include "../mods/turn_undo.h"
#include "../mods/vertex_pool.h"
#include "../mods/parallel_ai.h"
#include "../mods/quick_resume.h"
#include "../mods/hero_skins_internal.h"
#include "../mods/host_rules.h"
#include "../mods/lan_discovery.h"
#include "../mods/languages.h"
#include "../mods/lockstep.h"
#include "../mods/menu_scene.h"
#include "../mods/mmh55_files.h"
#include "../mods/net_tuning.h"
#include "../mods/notify.h"
#include "../mods/online_ui.h"
#include "../mods/original_music.h"
#include "../mods/player_list.h"
#include "../mods/rmg_builder.h"
#include "../mods/rmg_factions.h"
#include "../mods/rmg_teams.h"
#include "../mods/rmg_templates.h"
#include "../mods/script_rhythm.h"
#include "../mods/settings_menu.h"
#include "../mods/steam_spike.h"
#include "../mods/update_check.h"
#include "../mods/virtual_lan.h"
#include "../mods/wheel.h"

namespace hooks {

void InstallAiTrace();

namespace {

// Escape hatch for testing: an empty file "h5-together.nohooks" next to the
// exe keeps the DLL in forwarding-only mode ("open-homm5.nohooks" too: the
// add-on's old name, until 0.2.7).
bool NoHooks()
{
	wchar_t path[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
	while (n > 0 && path[n - 1] != L'\\')
		--n;
	const wchar_t *const names[] = {L"h5-together.nohooks", L"open-homm5.nohooks"};
	for (const wchar_t *name : names) {
		lstrcpynW(path + n, name, MAX_PATH - n);
		if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
			return true;
	}
	return false;
}

}  // namespace

void InstallNetEarly()
{
	// [online] mode (off by default): the virtual LAN's Winsock hooks go in before the
	// robot's network log (robot::Init), so they sit under it and that log shows the
	// game's packets as the game sends and gets them. Only in a build we know now (an
	// unwrapped exe); the Steam-wrapped one gets them from InstallAll().
	if (NoHooks() || !IsHookable(DetectGameBuild()))
		return;
	mods::InstallVirtualLan();
}

namespace {

// The data-table hooks that must be in place before the game reads its data.
void InstallEarlyHooks(const char *when)
{
	crash::Install(when);  // first: before the game's code runs
	mods::InstallLanguagesEarly();  // [lang] texts=fr, before the game reads its first text
	mods::InstallOriginalMusicEarly();
	mods::InstallMenuSceneEarly();  // [menu] scene: after original_music, so ours is the outer hook
	mods::InstallPlayerListEarly();  // the Direct3D import, before the game makes its device
	mods::InstallRmgBuilderEarly();
	mods::InstallHeroSkinsPanelEarly();  // the overlay's Direct3D import, for the looks' panel
	mods::InstallHostRulesEarly();  // its panel: the Direct3D import
	mods::InstallUpdateCheckEarly();  // [update] check=1: its notice in the main menu
	mods::InstallSettingsMenuEarly();  // our page in the Options screen: before the game reads its list
	mods::InstallOnlineUiEarly();  // [online] mode=steam: its lines and button are drawn by the overlay
	overlay::InstallTestPanelEarly();
}

// The Steam-wrapped exe (H5_GameOLD.exe, the 2008 game as Steam installed it): at the DLL's
// load its code is still encrypted, so the build cannot be checked yet. Steam's wrapper
// decrypts it and then jumps to the game's own start, whose C runtime first calls
// MSVCR71.dll's __set_app_type: from there, before the game reads any data, the early hooks
// go in (without this they came at the game's first DirectInput call, after the game had
// read its Options list: no H5 Together page, 2026-11-06).
typedef void(__cdecl *SetAppTypeFn)(int type);
SetAppTypeFn g_set_app_type_next = nullptr;

void __cdecl SetAppTypeHook(int type)
{
	g_set_app_type_next(type);
	static bool done = false;
	if (done)
		return;
	done = true;
	const GameBuild build = DetectGameBuild();
	if (IsHookable(build))
		InstallEarlyHooks("at the game's start, after Steam's wrapper, before the game's own crash filter");
	else
		hooklog::Write("hooks: at the game's start the exe is %s: no early hooks", GameBuildName(build));
}

// Testing: [debug] early_at_start=1 takes the wrapped exe's way in an unwrapped one (the robot
// cannot start the Steam-wrapped exe).
bool EarlyAtStartTest()
{
	wchar_t ini[MAX_PATH];
	hooklog::PathNextToExe(L"h5-together", L".ini", ini, MAX_PATH);
	return GetPrivateProfileIntW(L"debug", L"early_at_start", 0, ini) != 0;
}

}  // namespace

void InstallEarly()
{
	// Only data-table hooks that must be in place before the game reads its
	// data, and only in a build we know.
	if (NoHooks())
		return;
	// The 2008 exe in a folder with MMH5.5: its files hidden before the game looks for its
	// data. Also in the Steam-wrapped exe (it reads only the header and the import table).
	mods::InstallMmh55FilesEarly();
	if (IsHookable(DetectGameBuild()) && !EarlyAtStartTest()) {
		InstallEarlyHooks("from the DLL's load, before the game's own crash filter");
		return;
	}
	// not known yet: perhaps the Steam-wrapped exe, still encrypted (see SetAppTypeHook)
	g_set_app_type_next = reinterpret_cast<SetAppTypeFn>(
		hookpatch::PatchImport("MSVCR71.dll", "__set_app_type", reinterpret_cast<void *>(&SetAppTypeHook)));
	hooklog::Write(g_set_app_type_next ? "hooks: the exe cannot be checked yet (Steam's wrapper?): the early hooks wait for the "
	                                     "game's start"
	                                   : "hooks: the exe cannot be checked yet, and it has no __set_app_type import: the early "
	                                     "hooks wait for the game's first DirectInput call");
}

void InstallAll()
{
	if (NoHooks()) {
		hooklog::Write("h5-together.nohooks (or open-homm5.nohooks) found: hooks disabled");
		return;
	}
	crash::Install("at the game's first DirectInput call");  // (done at the DLL's load in an unwrapped exe)
	watchdog::Install();  // first: the others register their state for its reports
	mods::InstallVirtualLan();  // [online] mode (off by default); done already at the DLL's load in an unwrapped exe
	mods::StartVirtualLan();  // mode=steam: Steam starts here, early (before the LAN screen and the Direct3D device)
	mods::ApplySteamSpikeFromSettings();  // [online] spike (test only, off by default; not with mode=steam): early too
	InstallAiTrace();
	mods::ApplyNetTuningFromSettings();
	mods::ApplyJoinTimeFromSettings();  // [net] join_timeout_s: more time for a PC that joins (its map build)
	mods::ApplyFastPathsFromSettings();
	mods::ApplyLanDiscoveryFromSettings();  // (after InstallVirtualLan: its hooks stack on the virtual LAN's)
	mods::ApplyCoopGuardFromSettings();
	mods::ApplyLockstepFromSettings();
	mods::ApplyAiPacingFromSettings();
	mods::ApplyParallelAiFromSettings();  // [ai] parallel_turns (host's, off by default): acts when the co-op features agree
	mods::ApplyAutosaveFromSettings();
	mods::ApplyNotifyFromSettings();
	mods::ApplyRmgTeamsFromSettings();
	mods::ApplyRmgTemplatesFromSettings();
	mods::ApplyLanguagesFromSettings();
	mods::ApplyRmgFactionsFromSettings();
	mods::ApplyOriginalMusicFromSettings();
	mods::ApplyMenuSceneFromSettings();  // [menu] scene: after original_music, which it must stay outside of
	mods::ApplyPlayerListFromSettings();  // overlay clients: drawn in this order
	mods::ApplyRmgBuilderFromSettings();
	overlay::ApplyTestPanelFromSettings();
	mods::ApplyCameraFromSettings();
	mods::ApplyAnimThrottleFromSettings();  // [camera] anim_saver (on by default; anim_every: testing)
	mods::ApplyFrameLimitFromSettings();  // [video] fps_limit, vsync (off, the game's own by default)
	mods::ApplySpectateFromSettings();  // following a friend's view ([coop] share_view; a click in the player list)
	mods::ApplyVertexPoolFromSettings();  // [camera] vertex_pool (4: a bigger pool; vb_trace: research, hidden)
	mods::ApplyWheelFromSettings();  // its DirectInput filter goes first (order -10)
	mods::ApplyEdgeSkyFromSettings();  // [camera] edge_sky: before the first map reads its light
	mods::ApplyHostRulesFromSettings();  // [rules]: the host's rules, served as H55-Settings.lua
	mods::ApplyOnlineUiFromSettings();  // [online] mode=steam: Steam's lines on the LAN screen, Invite friends... (after the rules' button)
	mods::ApplyAudioVolumeFromSettings();
	mods::ApplyLobbyHeroTipsFromSettings();  // [ui] hero_tips: the waiting room's hero tooltip tells class, skills, specialty
	mods::ApplySkillWheelFromSettings();  // [ui] skill_wheel: the hero screen's button (the original game), a key
	mods::ApplyHeroSkinsFromSettings();  // after notify and the co-op guard: it stacks on their hooks
	mods::ApplyTurnUndoFromSettings();  // [coop] undo_end_turn (host's); after hero looks: it stacks on their hooks
	mods::InstallEndlessTurns();  // the 2008 game: endless simultaneous turns, one of the co-op features
	mods::ApplyCoopFeaturesFromSettings();  // the host's script_rhythm and parallel_turns, agreed at each game's start
	mods::ApplyScriptRhythmFromSettings();  // [coop] script_rhythm (host's, on by default); after the co-op features
	mods::ApplyQuickResumeFromSettings();  // [coop] resume_save (host's); after the autosave mod: it stacks on its save hook
	mods::ApplyUpdateCheckFromSettings();  // [update] check (off by default)
	mods::ApplySettingsMenuFromSettings();  // Options, H5 Together: Accept writes h5-together.ini; F3; the first-start question
	hooklog::Write("patch: %d hooks in the game's code (room for %d)", hookpatch::RecordsUsed(), hookpatch::RecordsCapacity());
}

void InstallUnknownBuild()
{
	if (NoHooks())
		return;
	mods::ApplySteamSpikeFromSettings();  // calls only Steam's library, no address in the game
}

}  // namespace hooks
