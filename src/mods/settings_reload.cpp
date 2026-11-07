// The settings menu's live settings (see settings_reload.h): which mod reads which again.
#include "settings_reload.h"

#include <string.h>

#include "autosave.h"
#include "camera.h"
#include "spectate.h"
#include "frame_limit.h"
#include "hero_skins_internal.h"
#include "host_rules_internal.h"
#include "lan_discovery.h"
#include "notify.h"
#include "player_list.h"
#include "rmg_builder.h"
#include "rmg_teams.h"
#include "skill_wheel.h"

#include "../hook/log.h"
#include "../hook/watchdog.h"

namespace mods {
namespace settings {

void Reload(const char *section, const char *key)
{
	const auto is = [&](const char *s, const char *k) { return strcmp(section, s) == 0 && strcmp(key, k) == 0; };
	if (is("coop", "share_view") || is("coop", "watch_battles"))
		ReloadSpectateSettings();
	if (is("notify", "sound") || is("notify", "popup"))
		ReloadNotifySettings();
	else if (is("ui", "player_list_key") || is("ui", "player_list_top") || is("ui", "player_list_left"))
		ReloadPlayerListSettings();
	else if (is("rules", "key") || is("rules", "button"))
		rules::ReloadPanelSettings();
	else if (is("skins", "key") || is("skins", "hero_screen_button"))
		ReloadHeroSkinsPanelSettings();
	else if (is("skins", "live_hero_screen"))
		ReloadHeroSkinsSettings();
	else if (is("ui", "skill_wheel") || is("ui", "skill_wheel_key"))
		ReloadSkillWheelSettings();
	else if (is("rmg", "builder_key"))
		ReloadRmgBuilderSettings();
	else if (is("rmg", "teams"))
		ReloadRmgTeamsSettings();
	else if (is("save", "compression") || is("save", "autosave_every"))
		ReloadAutosaveSettings();
	else if (is("net", "lan_all_adapters"))
		ReloadLanDiscoverySettings();
	else if (strcmp(section, "camera") == 0)
		ReloadCameraSettings();
	else if (is("video", "fps_limit"))
		ReloadFrameLimitSettings();
	else if (is("debug", "hang_seconds"))
		watchdog::ReloadSettings();
	else
		hooklog::Write("settings: [%s] %s has no reload (it waits for a restart)", section, key);
}

}  // namespace settings
}  // namespace mods
