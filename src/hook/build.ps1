# Build the hook DLL (build/hook/dinput8.dll) and its tests, with a modern
# MSVC (32-bit). The DLL does not need to byte-match anything, so any VS 2019+
# toolset works. Usage:  powershell -File src\hook\build.ps1 [-Test]
param([switch]$Test)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$src = Join-Path $repo "src\hook"
$out = Join-Path $repo "build\hook"
New-Item -ItemType Directory -Force $out | Out-Null

$vcvars = $env:HOOK_VCVARS
if (-not $vcvars) {
    $candidates = @(
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat")
    $vcvars = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $vcvars) { throw "no Visual Studio found; set HOOK_VCVARS to vcvarsall.bat" }

$sources = "dllmain.cpp", "chain.cpp", "log.cpp", "old_names.cpp", "game_build.cpp", "hooks.cpp", "ai_trace.cpp", "robot.cpp", "robot_input.cpp", "robot_endturn.cpp", "robot_trace.cpp", "robot_menu.cpp", "robot_screen.cpp", "robot_ui.cpp", "robot_lobby.cpp", "robot_net.cpp", "robot_profile.cpp", "robot_paths.cpp", "robot_clock.cpp", "patch.cpp", "d3d9_hook.cpp", "watchdog.cpp", "crash.cpp", "..\mods\net_tuning.cpp", "..\mods\lan_discovery.cpp", "..\mods\coop_guard.cpp", "..\mods\lockstep.cpp", "..\mods\ai_pacing.cpp", "..\mods\autosave.cpp", "..\mods\notify.cpp", "..\mods\rmg_teams.cpp", "..\mods\rmg_templates.cpp", "..\mods\fast_paths.cpp", "..\mods\fast_paths_relax.cpp", "..\mods\fast_paths_search.cpp", "..\mods\fast_paths_expand.cpp", "..\mods\fast_paths_ashore.cpp", "..\mods\original_music.cpp", "..\mods\original_music_xdb.cpp", "..\mods\player_list.cpp", "..\mods\player_list_game.cpp", "..\mods\player_list_draw.cpp", "..\mods\rmg_maker.cpp", "..\mods\rmg_recipe.cpp", "..\mods\rmg_join.cpp", "..\mods\camera.cpp", "..\mods\languages.cpp", "..\mods\languages_text.cpp", "dinput_hook.cpp", "input_mute.cpp", "overlay.cpp", "overlay_input.cpp", "overlay_textures.cpp", "overlay_cursor.cpp", "overlay_test_panel.cpp", "texts.cpp", "texts_logic.cpp", "..\mods\rmg_builder.cpp", "..\mods\rmg_builder_data.cpp", "..\mods\rmg_builder_widgets.cpp", "..\mods\rmg_screen.cpp", "..\mods\rmg_factions.cpp", "..\mods\audio_volume.cpp", "..\mods\edge_sky.cpp", "..\mods\wheel.cpp", "..\mods\wheel_logic.cpp", "..\mods\host_rules.cpp", "..\mods\host_rules_logic.cpp", "..\mods\host_rules_panel.cpp", "..\mods\host_rules_bonus.cpp", "..\mods\host_rules_icons.cpp", "..\mods\host_rules_classes.cpp", "..\mods\hero_skins.cpp", "..\mods\hero_skins_logic.cpp", "..\mods\hero_skins_panel.cpp", "..\mods\parallel_ai.cpp", "..\mods\turn_undo.cpp","..\mods\turn_undo_logic.cpp", "..\mods\coop_features.cpp", "..\mods\coop_features_logic.cpp", "..\mods\endless_turns.cpp", "..\mods\mmh55_files.cpp", "..\mods\join_time.cpp", "..\mods\lobby_hero_tips.cpp", "..\mods\skill_wheel.cpp", "..\mods\skill_wheel_logic.cpp", "..\mods\script_rhythm.cpp", "..\mods\anim_throttle.cpp", "..\mods\frame_limit.cpp", "..\mods\spectate.cpp", "..\mods\spectate_logic.cpp", "..\mods\vertex_pool.cpp", "..\mods\quick_resume.cpp", "..\mods\resume_transfer_logic.cpp", "..\mods\resume_transfer.cpp", "..\mods\steam_spike.cpp", "..\mods\steam_spike_logic.cpp", "..\mods\update_check.cpp", "..\mods\update_check_logic.cpp", "..\mods\settings_table.cpp", "..\mods\settings_ini.cpp", "..\mods\settings_menu_data.cpp", "..\mods\settings_menu.cpp", "..\mods\settings_reload.cpp", "..\mods\menu_scene.cpp", "..\mods\menu_scene_logic.cpp", "..\mods\virtual_lan.cpp", "..\mods\virtual_lan_logic.cpp", "..\mods\virtual_lan_loopback.cpp", "..\mods\virtual_lan_steam.cpp", "..\mods\steam_runtime.cpp", "..\mods\online_ui.cpp", "..\mods\online_ui_logic.cpp", "..\mods\online_join.cpp"
# Source paths are relative to src\hook (the build runs there): with the full
# path of a deep folder, the command line grew past cmd's 8191 characters.
# Dear ImGui (third_party\imgui, for the player list overlay): the same settings
# for its files and for ours. NDEBUG: its internal checks never stop the game.
$imgui_defs = "/DNDEBUG /DIMGUI_DISABLE_OBSOLETE_FUNCTIONS /DIMGUI_DISABLE_DEMO_WINDOWS /DIMGUI_DISABLE_DEBUG_TOOLS /DIMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS /DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS /Ithird_party\imgui"
# Our texts' English, built into the DLL (texts.cpp includes it): the bytes of
# src\mods\texts\en.txt as a C list, build\hook\texts_en.inc. A missing or broken
# texts file in the game then never leaves a panel without words.
$en_txt = Join-Path $repo "src\mods\texts\en.txt"
$en_bytes = [IO.File]::ReadAllBytes($en_txt)
$en_hex = [BitConverter]::ToString($en_bytes) -split '-' | ForEach-Object { "0x$_," }
$en_lines = for ($i = 0; $i -lt $en_hex.Count; $i += 24) { ($en_hex[$i..([Math]::Min($i + 23, $en_hex.Count - 1))] -join '') }
Set-Content -Encoding ascii (Join-Path $out "texts_en.inc") (@("// generated by src\hook\build.ps1 from src\mods\texts\en.txt ($($en_bytes.Count) bytes): do not edit") + $en_lines)
# /utf-8: our files are UTF-8. Without it, cl reads them in Windows' code page and an
# accent typed in an L"..." text became two letters in the game (lobby tooltips, 0.2.7).
# /I build\hook: texts_en.inc.
$flags = "/nologo /O2 /MT /W4 /WX /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN $imgui_defs /I`"$out`""
# ImGui itself is built apart, at its authors' warning level (not /W4 /WX).
$imgui_out = Join-Path $out "imgui"
New-Item -ItemType Directory -Force $imgui_out | Out-Null
$imgui_sources = "third_party\imgui\imgui.cpp", "third_party\imgui\imgui_draw.cpp", "third_party\imgui\imgui_tables.cpp", "third_party\imgui\imgui_widgets.cpp", "third_party\imgui\backends\imgui_impl_dx9.cpp"
$imgui_objs = ($imgui_sources | ForEach-Object { "`"$imgui_out\" + [IO.Path]::GetFileNameWithoutExtension($_) + ".obj`"" }) -join ' '
$imgui = "cl /nologo /O2 /MT /W3 /EHsc /std:c++17 /utf-8 $imgui_defs /c $($imgui_sources -join ' ') /Fo`"$imgui_out\\`""
# The DLL's version information (dinput8.rc, linked in as dinput8.res). The text comes
# from version.h through the rc file's #include; rc.exe (found by vcvarsall, like cl)
# gets the three numbers cut from that same string.
$version_h = Get-Content -Raw (Join-Path $src "version.h")
$ver = [regex]::Match($version_h, '#define\s+OPEN_HOMM5_VERSION\s+"(\d+)\.(\d+)\.(\d+)')
if (-not $ver.Success) { throw "src\hook\version.h: OPEN_HOMM5_VERSION must start with major.minor.patch numbers, like ""0.3.0""" }
$res = "$out\dinput8.res"
$rc = "rc /nologo /DOH_VER_MAJOR=$($ver.Groups[1].Value) /DOH_VER_MINOR=$($ver.Groups[2].Value) /DOH_VER_PATCH=$($ver.Groups[3].Value) /Fo`"$res`" dinput8.rc"
# /GL + /LTCG: whole-program optimization of our own code (the AI pathfinding
# mod calls small helpers across files thousands of times per search)
# The DLL's long argument list goes in a response file: with every mod and its
# tests on one cmd line, the line grew past cmd's 8191 characters.
$dll_rsp = Join-Path $out "dinput8.rsp"
Set-Content -Encoding ascii $dll_rsp "$flags /GL /LD $($sources -join ' ') $imgui_objs `"$res`" /Fo`"$out\\`" /Fe`"$out\dinput8.dll`" /link /LTCG /DEF:dinput8.def /MAP:`"$out\dinput8.map`" user32.lib dxguid.lib iphlpapi.lib winmm.lib shell32.lib advapi32.lib winhttp.lib bcrypt.lib ws2_32.lib"
$dll = "cl @`"$dll_rsp`""
$cmd = "call `"$vcvars`" x86 >nul 2>&1 && cd /d `"$src`" && $imgui && $rc && $dll"
if ($Test) {
    # chain-loading: a stand-in for another mod's dinput8.dll (it exports DirectInput8Create undecorated, as the real one)
    $cmd += " && cl $flags /LD test\chain_dummy.cpp /Fo`"$out\\`" /Fe`"$out\chain_dummy.dll`" /link /EXPORT:DirectInput8Create=_DirectInput8Create@20"
    $cmd += " && cl $flags test\proxy_test.cpp chain.cpp log.cpp /Fo`"$out\\`" /Fe`"$out\proxy_test.exe`" dxguid.lib"
    # calling-convention test of the AI hook: debug build with runtime stack checks (/RTCs)
    $tflags = "/nologo /Od /RTCs /MTd /W4 /WX /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAI_TRACE_TEST"
    # the "original music" mod's text work (no game needed)
    $cmd += " && cl $flags test\original_music_test.cpp ..\mods\original_music_xdb.cpp /Fo`"$out\\`" /Fe`"$out\original_music_test.exe`""
    # the log's size limit: a big log becomes the old log at start-up
    $cmd += " && cl $flags test\log_test.cpp log.cpp /Fo`"$out\\`" /Fe`"$out\log_test.exe`""
    # the move from the add-on's old name (open-homm5.ini and the others become h5-together.*)
    $cmd += " && cl $flags test\old_names_test.cpp old_names.cpp log.cpp /Fo`"$out\\`" /Fe`"$out\old_names_test.exe`""
    # the "languages" mod's work on bytes: paths, text encodings, zip directories
    $cmd += " && cl $flags test\languages_test.cpp ..\mods\languages_text.cpp /Fo`"$out\\`" /Fe`"$out\languages_test.exe`""
    # the C++ template maker against the JavaScript one, and the recipe names
    $cmd += " && cl $flags test\rmg_maker_test.cpp ..\mods\rmg_maker.cpp /Fo`"$out\\`" /Fe`"$out\rmg_maker_test.exe`""
    $cmd += " && cl $flags test\rmg_recipe_test.cpp ..\mods\rmg_recipe.cpp ..\mods\rmg_maker.cpp ..\mods\rmg_join.cpp ..\mods\rmg_templates.cpp log.cpp old_names.cpp patch.cpp /Fo`"$out\\`" /Fe`"$out\rmg_recipe_test.exe`""
    # the template builder's data: presets, zone map, words, the native button's files
    $cmd += " && cl $flags test\rmg_builder_test.cpp ..\mods\rmg_builder_data.cpp ..\mods\rmg_recipe.cpp ..\mods\rmg_maker.cpp texts_logic.cpp /Fo`"$out\\`" /Fe`"$out\rmg_builder_test.exe`""
    # our texts: the texts file reader, placeholders, fallback, the language clues, and en.txt / fr.txt themselves
    $cmd += " && cl $flags test\texts_test.cpp texts_logic.cpp /Fo`"$out\\`" /Fe`"$out\texts_test.exe`""
    # the settings menu: the table of every setting against the shipped ini, the in-place ini writer, the Options screen's data
    $cmd += " && cl $flags test\settings_test.cpp ..\mods\settings_table.cpp ..\mods\settings_ini.cpp ..\mods\settings_menu_data.cpp texts_logic.cpp /Fo`"$out\\`" /Fe`"$out\settings_test.exe`""
    # the hero looks' rules: the choices file, the messages between PCs, who decides
    $cmd += " && cl $flags test\hero_skins_test.cpp ..\mods\hero_skins_logic.cpp /Fo`"$out\\`" /Fe`"$out\hero_skins_test.exe`""
    # undo End Turn's rules: the messages between PCs, when they count, when an undo is allowed
    $cmd += " && cl $flags test\turn_undo_test.cpp ..\mods\turn_undo_logic.cpp /Fo`"$out\\`" /Fe`"$out\turn_undo_test.exe`""
    # the co-op features' agreement: the hello, and who decides (script rhythm, parallel AI)
    $cmd += " && cl $flags test\mmh55_files_test.cpp /Fo`"$out\\`" /Fe`"$out\mmh55_files_test.exe`""
    $cmd += " && cl $flags test\coop_features_test.cpp ..\mods\coop_features_logic.cpp /Fo`"$out\\`" /Fe`"$out\coop_features_test.exe`""
    # the skill wheel's data: the game's two tables, a class's wheel
    $cmd += " && cl $flags test\skill_wheel_test.cpp ..\mods\skill_wheel_logic.cpp /Fo`"$out\\`" /Fe`"$out\skill_wheel_test.exe`""
    # following a friend's view: the messages between PCs, the glide, who follows whom
    $cmd += " && cl $flags test\spectate_test.cpp ..\mods\spectate_logic.cpp /Fo`"$out\\`" /Fe`"$out\spectate_test.exe`""
    # quick resume: sending the resume save to a player who lacks it (messages, CRC, a lossy line)
    $cmd += " && cl $flags test\resume_transfer_test.cpp ..\mods\resume_transfer_logic.cpp /Fo`"$out\\`" /Fe`"$out\resume_transfer_test.exe`""
    # two presentation mods' pure work: the volume curve and the light file edit
    $cmd += " && cl $flags test\audio_edge_test.cpp /Fo`"$out\\`" /Fe`"$out\audio_edge_test.exe`""
    # the main menu scene mod's pure work: the setting, the draw, what a scene needs, the edits of two small data files
    $cmd += " && cl $flags test\menu_scene_test.cpp ..\mods\menu_scene_logic.cpp /Fo`"$out\\`" /Fe`"$out\menu_scene_test.exe`""
    # the overlay's modal input rules (what the game gets while a panel is open)
    $cmd += " && cl $flags test\input_mute_test.cpp input_mute.cpp /Fo`"$out\\`" /Fe`"$out\input_mute_test.exe`""
    # the mouse wheel fix for Wine: steps turned into positions, and its check
    $cmd += " && cl $flags test\wheel_test.cpp ..\mods\wheel_logic.cpp /Fo`"$out\\`" /Fe`"$out\wheel_test.exe`""
    # the host rules: H55-Settings.lua read and served with other values, the start script's edits, the carried texts, the hello's codes
    $cmd += " && cl $flags test\host_rules_test.cpp ..\mods\host_rules_logic.cpp /Fo`"$out\\`" /Fe`"$out\host_rules_test.exe`""
    # the update check's version numbers, GitHub's answer and the once-a-day rule (no network; no
    # "update" in the exe's name: Windows would take it for an installer and ask for admin rights)
    $cmd += " && cl $flags test\new_release_test.cpp ..\mods\update_check_logic.cpp /Fo`"$out\\`" /Fe`"$out\new_release_test.exe`""
    # the Steam spike's pure work: its setting, log names, test messages, round trips
    $cmd += " && cl $flags test\steam_spike_test.cpp ..\mods\steam_spike_logic.cpp /Fo`"$out\\`" /Fe`"$out\steam_spike_test.exe`""
    # the virtual LAN's pure work: fake addresses, the 2-byte header, where each packet goes, hosting detection, the loopback datagrams
    $cmd += " && cl $flags test\virtual_lan_test.cpp ..\mods\virtual_lan_logic.cpp /Fo`"$out\\`" /Fe`"$out\virtual_lan_test.exe`""
    # the online UI's pure work: the LAN screen's lines, the invite list's order, Steam's +connect_lobby, the words in en.txt / fr.txt
    $cmd += " && cl $flags test\online_ui_test.cpp ..\mods\online_ui_logic.cpp ..\mods\virtual_lan_logic.cpp texts_logic.cpp /Fo`"$out\\`" /Fe`"$out\online_ui_test.exe`""
    # our Steamworks declarations against Valve's SDK headers, only where the SDK is (ref\ is never
    # committed; a worktree has none, so STEAMWORKS_SDK can name an unpacked SDK folder)
    $sdk_dirs = @()
    if ($env:STEAMWORKS_SDK) { $sdk_dirs += Get-Item $env:STEAMWORKS_SDK -ErrorAction SilentlyContinue }
    $sdk_dirs += Get-ChildItem (Join-Path $repo "ref") -Directory -Filter "steamworks_sdk_*" -ErrorAction SilentlyContinue | Sort-Object Name -Descending
    $sdk = $sdk_dirs | Where-Object { $_ -and (Test-Path (Join-Path $_.FullName "sdk\public\steam\steam_api.h")) } | Select-Object -First 1
    if ($sdk) {
        $sdk_inc = Join-Path $sdk.FullName "sdk\public"
        $cmd += " && cl /nologo /W3 /EHsc /std:c++17 /D_CRT_SECURE_NO_WARNINGS /I`"$sdk_inc`" test\steam_abi_check.cpp /Fo`"$out\\`" /Fe`"$out\steam_abi_check.exe`""
    }
    # the hang watchdog on a thread of its own that freezes on purpose (own object folder: built with WATCHDOG_TEST)
    $wd_out = Join-Path $out "watchdog_test"
    New-Item -ItemType Directory -Force $wd_out | Out-Null
    $cmd += " && cl $flags /DWATCHDOG_TEST test\watchdog_test.cpp watchdog.cpp log.cpp patch.cpp game_build.cpp /Fo`"$wd_out\\`" /Fe`"$out\watchdog_test.exe`" user32.lib"
    # the crash report: real crashes in a child process (own object folder: built with CRASH_TEST)
    $crash_out = Join-Path $out "crash_test"
    New-Item -ItemType Directory -Force $crash_out | Out-Null
    $cmd += " && cl $flags /DCRASH_TEST test\crash_test.cpp crash.cpp watchdog.cpp log.cpp patch.cpp game_build.cpp /Fo`"$crash_out\\`" /Fe`"$out\crash_test.exe`" user32.lib"
    $cmd += " && cl $tflags test\ai_trace_test.cpp ai_trace.cpp ..\mods\ai_pacing.cpp log.cpp robot.cpp robot_input.cpp robot_endturn.cpp robot_trace.cpp robot_menu.cpp robot_screen.cpp robot_ui.cpp robot_lobby.cpp robot_net.cpp robot_profile.cpp robot_paths.cpp robot_clock.cpp patch.cpp d3d9_hook.cpp watchdog.cpp ..\mods\fast_paths.cpp ..\mods\fast_paths_relax.cpp ..\mods\fast_paths_search.cpp ..\mods\fast_paths_expand.cpp ..\mods\fast_paths_ashore.cpp game_build.cpp dinput_hook.cpp /Fo`"$out\\`" /Fe`"$out\ai_trace_test.exe`" dxguid.lib user32.lib iphlpapi.lib"
}
# vcvarsall prints a harmless vswhere warning on stderr (silenced above);
# PowerShell 5.1 would turn any native stderr line into an error.
$ErrorActionPreference = "Continue"
# One command per line of a batch file: with every test, the && chain grew past
# cmd's 8191 characters per command line.
$bat = Join-Path $out "build.cmd"
Set-Content -Encoding ascii $bat ((($cmd -split ' && ') | ForEach-Object { "$_`r`nif errorlevel 1 exit /b 1" }) -join "`r`n")
cmd /c "`"$bat`" 2>&1"
if ($LASTEXITCODE -ne 0) { throw "build failed" }
$ErrorActionPreference = "Stop"
Write-Host "built $out\dinput8.dll"

if ($Test) {
    Push-Location $out
    try {
        Remove-Item -ErrorAction SilentlyContinue h5-together.log
        & (Join-Path $out "proxy_test.exe") (Join-Path $out "dinput8.dll") (Join-Path $out "chain_dummy.dll")
        if ($LASTEXITCODE -ne 0) { throw "smoke test failed" }
        Get-Content h5-together.log
        Remove-Item -ErrorAction SilentlyContinue h5-together.log
        & (Join-Path $out "ai_trace_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "AI trace test failed" }
        & (Join-Path $out "original_music_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "original music test failed" }
        & (Join-Path $out "languages_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "languages test failed" }
        & (Join-Path $out "log_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "log test failed" }
        & (Join-Path $out "old_names_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "old names test failed" }
        # Node writes what the C++ template maker must build (and runs the
        # JavaScript maker's own checks); the two test exes compare.
        if (-not (Get-Command node -ErrorAction SilentlyContinue)) { throw "Node.js is needed for the template maker's parity test" }
        $parity = Join-Path $repo "build\rmg_parity"
        & node (Join-Path $repo "tools\rmg\test_template_maker.js") --expected $parity
        if ($LASTEXITCODE -ne 0) { throw "template maker test (JavaScript) failed" }
        & (Join-Path $out "rmg_maker_test.exe") $parity
        if ($LASTEXITCODE -ne 0) { throw "template maker parity test failed" }
        & (Join-Path $out "rmg_recipe_test.exe") $parity (Join-Path $repo "src\mods\rmg\templates") (Join-Path $repo "tools\rmg\x1_fingerprints.txt")
        if ($LASTEXITCODE -ne 0) { throw "recipe name test failed" }
        & (Join-Path $out "rmg_builder_test.exe") (Join-Path $repo "src\mods\rmg\templates") (Join-Path $repo "src\mods\texts")
        if ($LASTEXITCODE -ne 0) { throw "template builder test failed" }
        & (Join-Path $out "texts_test.exe") (Join-Path $repo "src\mods\texts")
        if ($LASTEXITCODE -ne 0) { throw "texts test failed" }
        # every text key the code uses is in en.txt (and used), the translations have no other keys and the same {names}
        if (-not (Get-Command python -ErrorAction SilentlyContinue)) { throw "Python is needed for the texts check (tools\lang\ui_texts.py)" }
        & python (Join-Path $repo "tools\lang\ui_texts.py") check
        if ($LASTEXITCODE -ne 0) { throw "texts check failed (tools\lang\ui_texts.py check)" }
        & (Join-Path $out "settings_test.exe") (Join-Path $repo "tools\package\files\h5-together.ini") (Join-Path $repo "src\mods\texts")
        if ($LASTEXITCODE -ne 0) { throw "settings menu test failed" }
        & (Join-Path $out "hero_skins_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "hero skins test failed" }
        & (Join-Path $out "turn_undo_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "undo End Turn test failed" }
        & (Join-Path $out "coop_features_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "co-op features test failed" }
        & (Join-Path $out "skill_wheel_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "skill wheel test failed" }
        & (Join-Path $out "mmh55_files_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "MMH5.5 files test failed" }
        & (Join-Path $out "spectate_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "spectate test failed" }
        & (Join-Path $out "resume_transfer_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "resume save transfer test failed" }
        & (Join-Path $out "audio_edge_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "volume curve / edge sky test failed" }
        & (Join-Path $out "menu_scene_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "menu scene test failed" }
        & (Join-Path $out "input_mute_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "input mute test failed" }
        & (Join-Path $out "wheel_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "mouse wheel test failed" }
        & (Join-Path $out "host_rules_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "host rules test failed" }
        & (Join-Path $out "new_release_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "update check test failed" }
        & (Join-Path $out "steam_spike_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "Steam spike test failed" }
        & (Join-Path $out "virtual_lan_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "virtual LAN test failed" }
        & (Join-Path $out "online_ui_test.exe") (Join-Path $repo "src\mods\texts")
        if ($LASTEXITCODE -ne 0) { throw "online UI test failed" }
        if ($sdk) {
            & (Join-Path $out "steam_abi_check.exe")
            if ($LASTEXITCODE -ne 0) { throw "Steam declarations check failed" }
        } else { Write-Host "Steam declarations check skipped: no ref\steamworks_sdk_* here" }
        Remove-Item -ErrorAction SilentlyContinue h5-together.log
        & (Join-Path $out "watchdog_test.exe")
        if ($LASTEXITCODE -ne 0) { Get-Content h5-together.log; throw "watchdog test failed" }
        Remove-Item -ErrorAction SilentlyContinue h5-together.log
        & (Join-Path $out "crash_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "crash report test failed" }
    } finally { Pop-Location }
}
