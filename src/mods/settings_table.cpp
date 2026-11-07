// Every setting of h5-together.ini (see settings_table.h). The order is the
// shipped file's; the menu shows its rows in this order within each group.
// Checked against the code's reads (GetPrivateProfile* in src/hook and
// src/mods) on 2026-11-02, as appendix A of docs/plans/community-release.md.
#include "settings_table.h"

#include <stdlib.h>
#include <string.h>

namespace mods {
namespace settings {
namespace {

#define COUNT(a) static_cast<int>(sizeof(a) / sizeof((a)[0]))

const Value kNoneKey = {"none", "settings.value.none"};

// --- the menu's choices ----------------------------------------------------------------------
const Value kListTop[] = {{"0", nullptr},  {"2", nullptr},  {"4", nullptr},  {"6", nullptr},  {"8", nullptr},  {"10", nullptr},
                          {"12", nullptr}, {"14", nullptr}, {"16", nullptr}, {"18", nullptr}, {"20", nullptr}, {"22", nullptr},
                          {"24", nullptr}, {"26", nullptr}, {"28", nullptr}, {"30", nullptr}, {"32", nullptr}, {"34", nullptr},
                          {"36", nullptr}, {"38", nullptr}, {"40", nullptr}, {"42", nullptr}, {"44", nullptr}, {"46", nullptr},
                          {"48", nullptr}, {"50", nullptr}, {"55", nullptr}, {"60", nullptr}, {"65", nullptr}, {"70", nullptr},
                          {"75", nullptr}, {"80", nullptr}, {"85", nullptr}, {"90", nullptr}};
const Value kListLeft[] = {{"0", nullptr},  {"1", nullptr},  {"2", nullptr},  {"3", nullptr},  {"4", nullptr},  {"5", nullptr},
                           {"6", nullptr},  {"8", nullptr},  {"10", nullptr}, {"12", nullptr}, {"15", nullptr}, {"20", nullptr},
                           {"25", nullptr}, {"30", nullptr}, {"40", nullptr}, {"50", nullptr}, {"60", nullptr}, {"70", nullptr},
                           {"80", nullptr}, {"90", nullptr}};
const Value kZoom[] = {{"100", nullptr}, {"125", nullptr}, {"150", nullptr}, {"175", nullptr}, {"200", nullptr},
                       {"225", nullptr}, {"250", nullptr}, {"275", nullptr}, {"300", nullptr}, {"325", nullptr},
                       {"350", nullptr}, {"375", nullptr}, {"400", nullptr}};
const Value kStartZoom[] = {{"", "settings.value.game_start_zoom"},
                            {"10", nullptr}, {"15", nullptr}, {"20", nullptr}, {"30", nullptr}, {"40", nullptr},
                            {"50", nullptr}, {"60", nullptr}, {"70", nullptr}, {"100", nullptr}, {"150", nullptr}};
const Value kWheelFix[] = {{"auto", "settings.value.wheel_auto"}, {"1", "settings.value.wheel_always"}, {"0", "settings.value.off"}};
const Value kEdgeSky[] = {{"game", nullptr}, {"none", nullptr}};
const Value kFpsLimit[] = {{"0", "settings.value.off"}, {"30", nullptr},  {"60", nullptr},  {"90", nullptr}, {"120", nullptr},
                           {"144", nullptr}, {"165", nullptr}, {"180", nullptr}, {"240", nullptr}};
const Value kVsync[] = {{"game", "settings.value.vsync_game"}, {"1", "settings.value.vsync_on"}, {"0", "settings.value.off"}};
const Value kMenuScene[] = {{"default", "settings.value.menu_default"}, {"toe", "settings.value.menu_toe"},
                            {"homm5", "settings.value.menu_homm5"}, {"random", "settings.value.menu_random"}};          // a check box: ticked = none (black)
const Value kVolumeCurve[] = {{"game", nullptr}, {"wide", nullptr}};      // ticked = wide
const Value kLanguages[] = {{"auto", "settings.value.language_auto"}, {"en", nullptr}, {"fr", nullptr}};  // + the files found
const Value kGameTexts[] = {{"game", "settings.value.texts_game"}, {"fr", "settings.value.texts_fr"}};
const Value kTeams[] = {{"", "settings.value.teams_map"}, {"humans_vs_ai", "settings.value.teams_humans"},
                        {"pairs", "settings.value.teams_pairs"}};
const Value kSteps[] = {{"50", nullptr}, {"100", nullptr}, {"200", "settings.value.steps_game"}};
const Value kViewDelay[] = {{"50", nullptr},  {"60", nullptr},  {"80", nullptr},  {"100", nullptr}, {"120", nullptr},
                            {"150", nullptr}, {"200", nullptr}, {"250", nullptr}, {"300", nullptr}, {"400", nullptr}};
const Value kCompression[] = {{"-1", "settings.value.compression_game"}, {"1", "settings.value.compression_fast"},
                              {"2", nullptr}, {"3", nullptr}, {"4", nullptr}, {"5", nullptr}, {"6", nullptr},
                              {"7", nullptr}, {"8", nullptr}, {"9", "settings.value.compression_small"}};
const Value kEveryDays[] = {{"1", "settings.value.every_day"}, {"2", nullptr}, {"3", nullptr}, {"4", nullptr},
                            {"5", nullptr}, {"6", nullptr}, {"7", "settings.value.every_week"}};
const Value kHangSeconds[] = {{"10", nullptr}, {"15", nullptr}, {"20", nullptr},  {"30", nullptr},  {"45", nullptr}, {"60", nullptr},
                              {"90", nullptr}, {"120", nullptr}, {"180", nullptr}, {"300", nullptr}, {"600", nullptr}};
const Value kChain[] = {{"dinput8_next.dll", "settings.value.chain_next"}, {"none", "settings.value.chain_none"}};
const Value kOnlineMode[] = {{"off", "settings.value.online_off"}, {"steam", "settings.value.online_steam"}};  // (+ loopback: tests)

// --- the table -----------------------------------------------------------------------------------
// FILE_ONLY: in the file only. HIDDEN: read by the code, not in the shipped file. ROW: a row of
// the menu, with its words (whole keys: tools/lang/ui_texts.py check looks for them in the code).
// A new setting: one line here, its line in tools/package/files/h5-together.ini and, for a
// row, its words in src/mods/texts/en.txt and fr.txt (test/settings_test.cpp checks all three).
#define FILE_ONLY(sec, key, def, kind, lo, hi, who, when) \
	{sec, key, def, kind, lo, hi, nullptr, 0, nullptr, who, when, kFileOnly, kNone, nullptr, nullptr, true}
#define HIDDEN(sec, key, def, kind, lo, hi) \
	{sec, key, def, kind, lo, hi, nullptr, 0, nullptr, kTester, kRestart, kFileOnly, kNone, nullptr, nullptr, false}
#define ROW(sec, key, def, kind, lo, hi, values, count, unit, who, when, group, control, text, help) \
	{sec, key, def, kind, lo, hi, values, count, unit, who, when, group, control, text, help, true}

}  // namespace

const Setting kSettings[] = {
	// [net]
	ROW("net", "tuning", "1", kBool, 0, 1, nullptr, 0, nullptr, kGroup, kRestart, kAdvanced, kCheck,
	    "settings.net.tuning", "settings.net.tuning.help"),
	FILE_ONLY("net", "keepalive_ms", "10", kInt, 1, 1000, kTester, kRestart),
	FILE_ONLY("net", "send_interval_ms", "2", kInt, 0, 50, kTester, kRestart),
	FILE_ONLY("net", "batched_segments", "0", kInt, 0, 3, kTester, kRestart),
	FILE_ONLY("net", "join_timeout_s", "300", kInt, 0, 600, kPlayer, kRestart),  // join_time.cpp
	FILE_ONLY("video", "background", "2", kInt, 0, 2, kPlayer, kRestart),  // join_time.cpp: 2 = under Linux/Wine only
	ROW("net", "lan_all_adapters", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kLive, kAdvanced, kCheck,
	    "settings.net.lan_all_adapters", "settings.net.lan_all_adapters.help"),
	// a text box the game's Options screen does not have: shown as a line of the Advanced group, set in the file
	{"net", "lan_peers", "", kText, 0, 0, nullptr, 0, nullptr, kPlayer, kLive, kAdvanced, kNone, "settings.net.lan_peers",
	 "settings.net.lan_peers.help", true},
	FILE_ONLY("net", "drain_reports", "1", kBool, 0, 1, kTester, kRestart),
	ROW("net", "view_delay_ms", "50", kInt, 50, 400, kViewDelay, COUNT(kViewDelay), "settings.unit.ms", kGroup, kRestart,
	    kAdvanced, kList,
	    "settings.net.view_delay_ms", "settings.net.view_delay_ms.help"),
	FILE_ONLY("net", "trace_lockstep", "1", kBool, 0, 1, kTester, kRestart),
	FILE_ONLY("net", "catch_up_ms", "600", kInt, 150, 5000, kTester, kRestart),
	FILE_ONLY("net", "scale_pacing", "1", kBool, 0, 1, kTester, kRestart),
	FILE_ONLY("net", "catch_up_behind", "1", kBool, 0, 1, kTester, kRestart),
	FILE_ONLY("net", "net_sleep_ms", "1", kInt, 1, 10, kTester, kRestart),
	// [coop]
	ROW("coop", "guard", "1", kBool, 0, 1, nullptr, 0, nullptr, kGroup, kRestart, kAdvanced, kCheck,
	    "settings.coop.guard", "settings.coop.guard.help"),
	// fast_segments and segment_ms: one row of the menu (segment_ms's, "200" = fast_segments=0)
	{"coop", "fast_segments", "1", kBool, 0, 1, nullptr, 0, nullptr, kGroup, kRestart, kAdvanced, kNone, nullptr, nullptr, true},
	ROW("coop", "resume_save", "1", kBool, 0, 1, nullptr, 0, nullptr, kHost, kRestart, kHostGroup, kCheck,
	    "settings.coop.resume_save", "settings.coop.resume_save.help"),
	ROW("coop", "undo_end_turn", "1", kBool, 0, 1, nullptr, 0, nullptr, kHost, kNextGame, kHostGroup, kCheck,
	    "settings.coop.undo_end_turn", "settings.coop.undo_end_turn.help"),
	ROW("coop", "endless_turns", "1", kBool, 0, 1, nullptr, 0, nullptr, kHost, kNextGame, kHostGroup, kCheck,
	    "settings.coop.endless_turns", "settings.coop.endless_turns.help"),  // the 2008 game only (endless_turns.cpp)
	ROW("coop", "script_rhythm", "1", kBool, 0, 1, nullptr, 0, nullptr, kHost, kNextGame, kHostGroup, kCheck,
	    "settings.coop.script_rhythm", "settings.coop.script_rhythm.help"),
	ROW("coop", "segment_ms", "50", kChoice, 0, 0, kSteps, COUNT(kSteps), "settings.unit.ms", kGroup, kRestart, kAdvanced, kList,
	    "settings.coop.segment_ms", "settings.coop.segment_ms.help"),
	// [ai]
	ROW("ai", "fast_paths", "1", kBool, 0, 1, nullptr, 0, nullptr, kHost, kRestart, kAdvanced, kCheck,
	    "settings.ai.fast_paths", "settings.ai.fast_paths.help"),
	FILE_ONLY("ai", "remember_checks", "1", kBool, 0, 1, kTester, kRestart),
	FILE_ONLY("ai", "fast_queue", "1", kBool, 0, 1, kTester, kRestart),
	FILE_ONLY("ai", "port_relax", "1", kBool, 0, 1, kTester, kRestart),
	FILE_ONLY("ai", "port_search", "1", kBool, 0, 1, kTester, kRestart),
	FILE_ONLY("ai", "port_expand", "1", kBool, 0, 1, kTester, kRestart),
	FILE_ONLY("ai", "fast_ashore", "1", kBool, 0, 1, kTester, kRestart),
	FILE_ONLY("ai", "ashore_check", "0", kBool, 0, 1, kTester, kRestart),
	ROW("ai", "parallel_turns", "1", kBool, 0, 1, nullptr, 0, nullptr, kHost, kNextGame, kHostGroup, kCheck,
	    "settings.ai.parallel_turns", "settings.ai.parallel_turns.help"),
	FILE_ONLY("ai", "one_decision_per_frame", "1", kBool, 0, 1, kTester, kRestart),
	FILE_ONLY("ai", "max_expansions", "0", kInt, 0, 1000000, kTester, kRestart),
	// [save]
	ROW("save", "fast_save_list", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kAdvanced, kCheck,
	    "settings.save.fast_save_list", "settings.save.fast_save_list.help"),
	ROW("save", "game_names", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kAdvanced, kCheck,
	    "settings.save.game_names", "settings.save.game_names.help"),
	FILE_ONLY("save", "trace_autosave", "1", kBool, 0, 1, kTester, kRestart),
	ROW("save", "compression", "1", kInt, -1, 9, kCompression, COUNT(kCompression), nullptr, kPlayer, kLive, kAdvanced, kList,
	    "settings.save.compression", "settings.save.compression.help"),
	ROW("save", "autosave_every", "1", kInt, 1, 1000, kEveryDays, COUNT(kEveryDays), "settings.unit.days", kGroup, kLive,
	    kAdvanced, kList,
	    "settings.save.autosave_every", "settings.save.autosave_every.help"),
	// [notify]
	ROW("notify", "turn", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kInterface, kCheck,
	    "settings.notify.turn", "settings.notify.turn.help"),
	ROW("notify", "sound", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kLive, kInterface, kCheck,
	    "settings.notify.sound", "settings.notify.sound.help"),
	ROW("notify", "popup", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kLive, kInterface, kCheck,
	    "settings.notify.popup", "settings.notify.popup.help"),
	ROW("coop", "share_view", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kLive, kInterface, kCheck,
	    "settings.coop.share_view", "settings.coop.share_view.help"),
	ROW("coop", "watch_battles", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kLive, kInterface, kCheck,
	    "settings.coop.watch_battles", "settings.coop.watch_battles.help"),
	ROW("coop", "stay_after_defeat", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kLive, kInterface, kCheck,
	    "settings.coop.stay_after_defeat", "settings.coop.stay_after_defeat.help"),
	FILE_ONLY("notify", "trace", "0", kBool, 0, 1, kTester, kRestart),
	// [ui]
	ROW("ui", "player_list", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kInterface, kCheck,
	    "settings.ui.player_list", "settings.ui.player_list.help"),
	ROW("ui", "player_list_key", "F9", kKey, 0, 0, nullptr, 0, nullptr, kPlayer, kLive, kInterface, kList,
	    "settings.ui.player_list_key", "settings.ui.player_list_key.help"),
	ROW("ui", "player_list_top", "34", kInt, 0, 90, kListTop, COUNT(kListTop), "settings.unit.percent", kPlayer, kLive,
	    kInterface, kSlider,
	    "settings.ui.player_list_top", "settings.ui.player_list_top.help"),
	ROW("ui", "player_list_left", "2", kInt, 0, 100, kListLeft, COUNT(kListLeft), "settings.unit.percent", kPlayer, kLive,
	    kInterface, kSlider,
	    "settings.ui.player_list_left", "settings.ui.player_list_left.help"),
	ROW("ui", "language", "auto", kChoice, 0, 0, kLanguages, COUNT(kLanguages), nullptr, kPlayer, kRestart, kLanguage, kList,
	    "settings.ui.language", "settings.ui.language.help"),
	ROW("ui", "hero_tips", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kInterface, kCheck,
	    "settings.ui.hero_tips", "settings.ui.hero_tips.help"),
	ROW("ui", "skill_wheel", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kLive, kInterface, kCheck,
	    "settings.ui.skill_wheel", "settings.ui.skill_wheel.help"),  // the button: the 2008 game only (skill_wheel.cpp)
	ROW("ui", "skill_wheel_key", "none", kKey, 0, 0, nullptr, 0, nullptr, kPlayer, kLive, kInterface, kList,
	    "settings.ui.skill_wheel_key", "settings.ui.skill_wheel_key.help"),
	FILE_ONLY("ui", "trace_players", "0", kInt, 0, 2, kTester, kRestart),
	FILE_ONLY("ui", "player_list_test_rows", "0", kInt, 0, 16, kTester, kRestart),
	FILE_ONLY("ui", "overlay_test_panel", "0", kBool, 0, 1, kTester, kRestart),
	FILE_ONLY("ui", "overlay_test_key", "F10", kKey, 0, 0, kTester, kRestart),
	// [rmg]
	ROW("rmg", "teams", "", kChoice, 0, 0, kTeams, COUNT(kTeams), nullptr, kHost, kNextGame, kHostGroup, kList,
	    "settings.rmg.teams", "settings.rmg.teams.help"),
	ROW("rmg", "lobby_teams", "0", kBool, 0, 1, nullptr, 0, nullptr, kHost, kRestart, kHostGroup, kCheck,
	    "settings.rmg.lobby_teams", "settings.rmg.lobby_teams.help"),
	FILE_ONLY("rmg", "trace_teams", "1", kBool, 0, 1, kTester, kRestart),
	ROW("rmg", "recipe_teams", "1", kBool, 0, 1, nullptr, 0, nullptr, kHost, kRestart, kHostGroup, kCheck,
	    "settings.rmg.recipe_teams", "settings.rmg.recipe_teams.help"),
	ROW("rmg", "templates", "1", kBool, 0, 1, nullptr, 0, nullptr, kGroup, kRestart, kAdvanced, kCheck,
	    "settings.rmg.templates", "settings.rmg.templates.help"),
	FILE_ONLY("rmg", "recipes", "", kText, 0, 0, kPlayer, kRestart),
	ROW("rmg", "builder", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kRandomMaps, kCheck,
	    "settings.rmg.builder", "settings.rmg.builder.help"),
	ROW("rmg", "builder_key", "F11", kKey, 0, 0, nullptr, 0, nullptr, kPlayer, kLive, kRandomMaps, kList,
	    "settings.rmg.builder_key", "settings.rmg.builder_key.help"),
	FILE_ONLY("rmg", "builder_last", "", kText, 0, 0, kPanel, kWritten),
	ROW("rmg", "faction_terrain", "1", kBool, 0, 1, nullptr, 0, nullptr, kHost, kRestart, kHostGroup, kCheck,
	    "settings.rmg.faction_terrain", "settings.rmg.faction_terrain.help"),
	FILE_ONLY("rmg", "trace_factions", "0", kBool, 0, 1, kTester, kRestart),
	// [audio]
	ROW("audio", "original_music", "0", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kSound, kCheck,
	    "settings.audio.original_music", "settings.audio.original_music.help"),
	FILE_ONLY("audio", "log_music", "0", kBool, 0, 1, kTester, kRestart),
	ROW("audio", "volume_curve", "wide", kChoice, 0, 0, kVolumeCurve, COUNT(kVolumeCurve), nullptr, kPlayer, kRestart, kSound,
	    kCheck,
	    "settings.audio.volume_curve", "settings.audio.volume_curve.help"),
	FILE_ONLY("audio", "log_volume", "0", kInt, 0, 1000000, kTester, kRestart),
	// [lang]
	ROW("lang", "texts", "game", kChoice, 0, 0, kGameTexts, COUNT(kGameTexts), nullptr, kPlayer, kRestart, kLanguage, kList,
	    "settings.lang.texts", "settings.lang.texts.help"),
	FILE_ONLY("lang", "log_texts", "0", kInt, 0, 1000000, kTester, kRestart),
	// [camera]
	ROW("camera", "zoom_out", "100", kInt, 100, 400, kZoom, COUNT(kZoom), "settings.unit.percent", kPlayer, kLive, kCamera,
	    kSlider,
	    "settings.camera.zoom_out", "settings.camera.zoom_out.help"),
	ROW("camera", "combat_zoom_out", "100", kInt, 100, 400, kZoom, COUNT(kZoom), "settings.unit.percent", kPlayer, kLive,
	    kCamera, kSlider,
	    "settings.camera.combat_zoom_out", "settings.camera.combat_zoom_out.help"),
	ROW("camera", "start_zoom", "", kInt, 5, 400, kStartZoom, COUNT(kStartZoom), nullptr, kPlayer, kLive, kCamera, kList,
	    "settings.camera.start_zoom", "settings.camera.start_zoom.help"),
	ROW("camera", "wheel_fix", "auto", kChoice, 0, 0, kWheelFix, COUNT(kWheelFix), nullptr, kPlayer, kRestart, kCamera, kList,
	    "settings.camera.wheel_fix", "settings.camera.wheel_fix.help"),
	ROW("camera", "edge_sky", "game", kChoice, 0, 0, kEdgeSky, COUNT(kEdgeSky), nullptr, kPlayer, kRestart, kCamera, kCheck,
	    "settings.camera.edge_sky", "settings.camera.edge_sky.help"),
	ROW("camera", "anim_saver", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kCamera, kCheck,
	    "settings.camera.anim_saver", "settings.camera.anim_saver.help"),
	ROW("video", "fps_limit", "180", kInt, 0, 500, kFpsLimit, COUNT(kFpsLimit), "settings.unit.fps", kPlayer, kLive, kCamera, kList,
	    "settings.video.fps_limit", "settings.video.fps_limit.help"),
	ROW("video", "vsync", "game", kChoice, 0, 0, kVsync, COUNT(kVsync), nullptr, kPlayer, kRestart, kCamera, kList,
	    "settings.video.vsync", "settings.video.vsync.help"),
	ROW("video", "hd_textures", "0", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kCamera, kCheck,
	    "settings.video.hd_textures", "settings.video.hd_textures.help"),  // the 2008 game in an MMH5.5 folder (mmh55_files.cpp)
	HIDDEN("camera", "anim_every", "1", kInt, 0, 8),  // testing: 3D models animated on one frame in N (anim_throttle.cpp)
	FILE_ONLY("camera", "vertex_pool", "4", kInt, 1, 16, kTester, kRestart),  // the dynamic vertex pool N times as large (vertex_pool.cpp)
	HIDDEN("camera", "vb_trace", "0", kBool, 0, 1),  // research: why the models' vertices are built again (vertex_pool.cpp)
	FILE_ONLY("camera", "edge_sky_test_light", "", kText, 0, 0, kTester, kRestart),
	// [menu]
	ROW("menu", "scene", "default", kChoice, 0, 0, kMenuScene, COUNT(kMenuScene), nullptr, kPlayer, kRestart, kCamera, kList,
	    "settings.menu.scene", "settings.menu.scene.help"),
	// [skins]
	ROW("skins", "skins", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kInterface, kCheck,
	    "settings.skins.skins", "settings.skins.skins.help"),
	ROW("skins", "key", "F7", kKey, 0, 0, nullptr, 0, nullptr, kPlayer, kLive, kInterface, kList,
	    "settings.skins.key", "settings.skins.key.help"),
	ROW("skins", "hero_screen_button", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kLive, kInterface, kCheck,
	    "settings.skins.hero_screen_button", "settings.skins.hero_screen_button.help"),
	FILE_ONLY("skins", "hero_screen_button_x", "403", kInt, 0, 1024, kPlayer, kRestart),
	FILE_ONLY("skins", "hero_screen_button_y", "94", kInt, 0, 768, kPlayer, kRestart),
	ROW("skins", "live_hero_screen", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kLive, kAdvanced, kCheck,
	    "settings.skins.live_hero_screen", "settings.skins.live_hero_screen.help"),
	ROW("skins", "show_others", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kInterface, kCheck,
	    "settings.skins.show_others", "settings.skins.show_others.help"),
	FILE_ONLY("skins", "trace", "0", kBool, 0, 1, kTester, kRestart),
	// [rules]
	ROW("rules", "enabled", "1", kBool, 0, 1, nullptr, 0, nullptr, kGroup, kRestart, kAdvanced, kCheck,
	    "settings.rules.enabled", "settings.rules.enabled.help"),
	ROW("rules", "key", "F2", kKey, 0, 0, nullptr, 0, nullptr, kPlayer, kLive, kInterface, kList,
	    "settings.rules.key", "settings.rules.key.help"),
	ROW("rules", "button", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kLive, kInterface, kCheck,
	    "settings.rules.button", "settings.rules.button.help"),
#define RULE(key) {"rules", key, "", kText, 0, 0, nullptr, 0, nullptr, kHost, kNextGame, kRulesPanel, kNone, nullptr, nullptr, true}
	RULE("governance"), RULE("town_conversion"), RULE("extra_conversions"), RULE("town_gate"), RULE("mine_control"),
	RULE("dimension_doors"), RULE("ai_cheat"), RULE("mixed_stacks"), RULE("neutral_stack_size"), RULE("bank_difficulty"),
	RULE("negative_artifacts"), RULE("rpg_potions"), RULE("map_scripts"), RULE("competitive_towns"),
	RULE("mana_observatories"), RULE("moon_disc"), RULE("game_mode"), RULE("toe_bonuses"), RULE("class_skills"),
	RULE("class_chance"),
#undef RULE
	FILE_ONLY("rules", "trace_skills", "0", kBool, 0, 1, kTester, kRestart),
	FILE_ONLY("rules", "class_skills_single", "0", kBool, 0, 1, kTester, kRestart),
	FILE_ONLY("rules", "trace", "0", kBool, 0, 1, kTester, kRestart),
	// [debug]
	ROW("debug", "hang_watchdog", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kAdvanced, kCheck,
	    "settings.debug.hang_watchdog", "settings.debug.hang_watchdog.help"),
	ROW("debug", "hang_seconds", "20", kInt, 5, 600, kHangSeconds, COUNT(kHangSeconds), "settings.unit.seconds", kPlayer, kLive,
	    kAdvanced, kList,
	    "settings.debug.hang_seconds", "settings.debug.hang_seconds.help"),
	FILE_ONLY("debug", "hang_test", "0", kBool, 0, 1, kTester, kRestart),
	HIDDEN("debug", "spectate_probe", "0", kBool, 0, 1),  // research: 1 = the camera and the screens in the log, 2 = also watch the followed ally's battle (spectate.cpp)
	ROW("debug", "crash_report", "1", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kAdvanced, kCheck,
	    "settings.debug.crash_report", "settings.debug.crash_report.help"),
	FILE_ONLY("debug", "trace_wheel", "0", kBool, 0, 1, kTester, kRestart),
	// [update]
	ROW("update", "check", "0", kBool, 0, 1, nullptr, 0, nullptr, kPlayer, kRestart, kHelp, kCheck,
	    "settings.update.check", "settings.update.check.help"),
	FILE_ONLY("update", "last_check", "", kText, 0, 0, kPanel, kWritten),
	FILE_ONLY("update", "latest", "", kText, 0, 0, kPanel, kWritten),
	FILE_ONLY("update", "asked", "", kBool, 0, 1, kPanel, kWritten),  // the first-start question was answered
	// [chain]
	ROW("chain", "dll", "dinput8_next.dll", kChoice, 0, 0, kChain, COUNT(kChain), nullptr, kPlayer, kRestart, kAdvanced, kList,
	    "settings.chain.dll", "settings.chain.dll.help"),
	// [online]: the virtual LAN (off | steam | loopback, the robot's). Steam starts at the game's first DirectInput
	// call and the Winsock hooks go in when the DLL loads, so a change waits for a restart.
	ROW("online", "mode", "steam", kChoice, 0, 0, kOnlineMode, COUNT(kOnlineMode), nullptr, kGroup, kRestart, kOnline, kList,
	    "settings.online.mode", "settings.online.mode.help"),

	// read by the code, not in the shipped file (appendix B): for testing
	HIDDEN("audio", "log_data_files", "0", kInt, 0, 1000000),
	HIDDEN("debug", "hang_test_at", "0", kInt, 0, 100000),
	HIDDEN("debug", "hang_test_ai_step", "0", kInt, 0, 100000),
	HIDDEN("debug", "hang_test_frame_ms", "0", kInt, 0, 100000),
	HIDDEN("debug", "crash_test_at", "0", kInt, 0, 100000),
	HIDDEN("rules", "test_version", "", kText, 0, 0),
	HIDDEN("rmg", "templates_selftest", "0", kBool, 0, 1),
	HIDDEN("skins", "test_ai_heroes", "0", kBool, 0, 1),
	HIDDEN("skins", "test_button_on_map", "0", kBool, 0, 1),
	HIDDEN("update", "welcome_test", "0", kBool, 0, 1),  // the first-start question also in robot runs
	HIDDEN("online", "spike", "0", kText, 0, 0),
	HIDDEN("online", "app_id", "15370", kInt, 1, 0x7fffffff),
	HIDDEN("online", "spike_seconds", "", kInt, 1, 100000),
	HIDDEN("online", "spike_lobby", "friends", kText, 0, 0),
	HIDDEN("online", "spike_pings", "100", kInt, 0, 100000),
	HIDDEN("online", "spike_ping_bytes", "300", kInt, 0, 1200),
	HIDDEN("online", "loopback_port", "47100", kInt, 1, 65535),  // the virtual LAN's robot tests
	HIDDEN("online", "loopback_peers", "", kText, 0, 0),
	HIDDEN("online", "reliable", "1", kBool, 0, 1),  // 0 = Steam sends each message once, as UDP
	HIDDEN("online", "ui_test", "0", kBool, 0, 1),  // the online UI with made-up friends (robot screenshots)
	HIDDEN("online", "test_drop_join", "0", kBool, 0, 1),  // the virtual LAN loses our join requests (the game's timeout box)
};

#undef ROW
#undef FILE_ONLY
#undef HIDDEN

const int kSettingCount = COUNT(kSettings);

namespace {

bool SameText(const char *a, const char *b)
{
	return _stricmp(a, b) == 0;
}

std::string Trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t'))
		++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'))
		--b;
	return s.substr(a, b - a);
}

bool Number(const std::string &text, long *out)
{
	const std::string t = Trim(text);
	if (t.empty())
		return false;
	char *end = nullptr;
	const long v = strtol(t.c_str(), &end, 10);
	if (!end || *end)
		return false;
	*out = v;
	return true;
}

}  // namespace


const Value kKeyValues[] = {{"F1", nullptr}, {"F2", nullptr}, {"F3", nullptr},  {"F4", nullptr},  {"F5", nullptr},
                            {"F6", nullptr}, {"F7", nullptr}, {"F8", nullptr},  {"F9", nullptr},  {"F10", nullptr},
                            {"F11", nullptr}, {"F12", nullptr}, kNoneKey};
const int kKeyValueCount = COUNT(kKeyValues);
const char kCoopStepsGame[] = "200";

int PageOf(Group group)
{
	switch (group) {
	case kInterface:
	case kCamera:
	case kSound:
	case kLanguage:
	case kRandomMaps:
		return 1;
	case kOnline:
	case kHostGroup:
	case kAdvanced:
	case kHelp:
		return 2;
	default:
		return 0;
	}
}

const Setting *Find(const char *section, const char *key)
{
	for (int i = 0; i < kSettingCount; ++i)
		if (SameText(kSettings[i].section, section) && SameText(kSettings[i].key, key))
			return &kSettings[i];
	return nullptr;
}

bool Valid(const Setting &s, const std::string &raw)
{
	const std::string value = Trim(raw);
	if (value.find_first_of("\r\n") != std::string::npos)
		return false;
	long n = 0;
	switch (s.kind) {
	case kBool:
		return value == "0" || value == "1";
	case kInt:
		if (value.empty())
			return s.def[0] == 0;
		return Number(value, &n) && n >= s.low && n <= s.high;
	case kKey:
		for (const Value &v : kKeyValues)
			if (SameText(v.value, value.c_str()))
				return true;
		return false;
	case kChoice:
		for (int i = 0; i < s.value_count; ++i)
			if (SameText(s.values[i].value, value.c_str()))
				return true;
		if (SameText(s.section, "ui") && SameText(s.key, "language")) {  // any language with a texts file
			if (value.size() < 2 || value.size() > 12)
				return false;
			for (char c : value)
				if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
					return false;
			return value[0] >= 'a' && value[0] <= 'z';
		}
		if (SameText(s.section, "rmg") && SameText(s.key, "teams")) {  // a team for each player: 1,1,2,2
			for (char c : value)
				if (!((c >= '0' && c <= '9') || c == ',' || c == ' '))
					return false;
			return true;
		}
		if (SameText(s.section, "chain") && SameText(s.key, "dll"))  // another file name
			return !value.empty();
		if (SameText(s.section, "online") && SameText(s.key, "mode"))  // the robot's own tests
			return SameText(value.c_str(), "loopback");
		return false;
	case kText:
		return true;
	}
	return false;
}

bool SameValue(const Setting &s, const std::string &a_raw, const std::string &b_raw)
{
	const std::string a = Trim(a_raw).empty() ? std::string(s.def) : Trim(a_raw);
	const std::string b = Trim(b_raw).empty() ? std::string(s.def) : Trim(b_raw);
	long x = 0, y = 0;
	if ((s.kind == kBool || s.kind == kInt) && Number(a, &x) && Number(b, &y))
		return s.kind == kBool ? (x != 0) == (y != 0) : x == y;
	if (s.kind == kText)
		return a == b;
	return SameText(a.c_str(), b.c_str());
}

int ChoiceCount(const Setting &s)
{
	if (s.kind == kBool)
		return 2;
	if (s.kind == kKey)
		return kKeyValueCount;
	return s.value_count;
}

std::string ChoiceValue(const Setting &s, int index)
{
	if (index < 0 || index >= ChoiceCount(s))
		return s.def;
	if (s.kind == kBool)
		return index ? "1" : "0";
	if (s.kind == kKey)
		return kKeyValues[index].value;
	return s.values[index].value;
}

int ChoiceIndex(const Setting &s, const std::string &raw)
{
	const std::string value = Trim(raw).empty() ? std::string(s.def) : Trim(raw);
	const int count = ChoiceCount(s);
	for (int i = 0; i < count; ++i)
		if (SameValue(s, ChoiceValue(s, i), value))
			return i;
	long want = 0;
	if (s.kind != kInt || !Number(value, &want))
		return -1;
	// a number between the menu's steps: the nearest one
	int best = -1;
	long best_gap = 0;
	for (int i = 0; i < count; ++i) {
		long v = 0;
		if (!Number(s.values[i].value, &v))
			continue;
		const long gap = v > want ? v - want : want - v;
		if (best < 0 || gap < best_gap) {
			best = i;
			best_gap = gap;
		}
	}
	return best;
}

}  // namespace settings
}  // namespace mods
