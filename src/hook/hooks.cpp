#include "hooks.h"

#include <windows.h>

#include "game_build.h"
#include "log.h"
#include "watchdog.h"
#include "../mods/ai_pacing.h"
#include "../mods/autosave.h"
#include "../mods/coop_guard.h"
#include "../mods/fast_paths.h"
#include "../mods/lan_discovery.h"
#include "../mods/lockstep.h"
#include "../mods/net_tuning.h"
#include "../mods/notify.h"
#include "../mods/original_music.h"
#include "../mods/player_list.h"
#include "../mods/rmg_teams.h"
#include "../mods/rmg_templates.h"

namespace hooks {

void InstallAiTrace();

namespace {

// Escape hatch for testing: an empty file "open-homm5.nohooks" next to the
// exe keeps the DLL in forwarding-only mode.
bool NoHooks()
{
	wchar_t path[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
	while (n > 0 && path[n - 1] != L'\\')
		--n;
	lstrcpynW(path + n, L"open-homm5.nohooks", MAX_PATH - n);
	return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

void InstallEarly()
{
	// Only data-table hooks that must be in place before the game reads its
	// data, and only in a build we know (an unwrapped exe: the Steam-wrapped
	// one is still encrypted now, and gets them with InstallAll()).
	if (NoHooks() || !IsHookable(DetectGameBuild()))
		return;
	mods::InstallOriginalMusicEarly();
	mods::InstallPlayerListEarly();  // the Direct3D import, before the game makes its device
}

void InstallAll()
{
	if (NoHooks()) {
		hooklog::Write("open-homm5.nohooks found: hooks disabled");
		return;
	}
	watchdog::Install();  // first: the others register their state for its reports
	InstallAiTrace();
	mods::ApplyNetTuningFromSettings();
	mods::ApplyFastPathsFromSettings();
	mods::ApplyLanDiscoveryFromSettings();
	mods::ApplyCoopGuardFromSettings();
	mods::ApplyLockstepFromSettings();
	mods::ApplyAiPacingFromSettings();
	mods::ApplyAutosaveFromSettings();
	mods::ApplyNotifyFromSettings();
	mods::ApplyRmgTeamsFromSettings();
	mods::ApplyRmgTemplatesFromSettings();
	mods::ApplyOriginalMusicFromSettings();
	mods::ApplyPlayerListFromSettings();
}

}  // namespace hooks
