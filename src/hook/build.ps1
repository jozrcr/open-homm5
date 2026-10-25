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

$sources = "dllmain.cpp", "log.cpp", "game_build.cpp", "hooks.cpp", "ai_trace.cpp", "robot.cpp", "robot_input.cpp", "robot_endturn.cpp", "robot_trace.cpp", "robot_menu.cpp", "robot_screen.cpp", "robot_ui.cpp", "robot_lobby.cpp", "robot_net.cpp", "robot_profile.cpp", "robot_paths.cpp", "robot_clock.cpp", "patch.cpp", "d3d9_hook.cpp", "watchdog.cpp", "..\mods\net_tuning.cpp", "..\mods\lan_discovery.cpp", "..\mods\coop_guard.cpp", "..\mods\lockstep.cpp", "..\mods\ai_pacing.cpp", "..\mods\autosave.cpp", "..\mods\notify.cpp", "..\mods\rmg_teams.cpp", "..\mods\rmg_templates.cpp", "..\mods\fast_paths.cpp", "..\mods\fast_paths_relax.cpp", "..\mods\fast_paths_search.cpp", "..\mods\fast_paths_expand.cpp", "..\mods\fast_paths_ashore.cpp", "..\mods\original_music.cpp", "..\mods\original_music_xdb.cpp", "..\mods\player_list.cpp", "..\mods\player_list_game.cpp", "..\mods\player_list_draw.cpp", "..\mods\rmg_maker.cpp", "..\mods\rmg_recipe.cpp", "..\mods\rmg_join.cpp", "..\mods\camera.cpp", "..\mods\languages.cpp", "..\mods\languages_text.cpp", "dinput_hook.cpp", "input_mute.cpp", "overlay.cpp", "overlay_input.cpp", "overlay_textures.cpp", "overlay_test_panel.cpp", "..\mods\rmg_builder.cpp", "..\mods\rmg_builder_data.cpp", "..\mods\rmg_builder_widgets.cpp", "..\mods\rmg_screen.cpp", "..\mods\rmg_factions.cpp"
# Source paths are relative to src\hook (the build runs there): with the full
# path of a deep folder, the command line grew past cmd's 8191 characters.
# Dear ImGui (third_party\imgui, for the player list overlay): the same settings
# for its files and for ours. NDEBUG: its internal checks never stop the game.
$imgui_defs = "/DNDEBUG /DIMGUI_DISABLE_OBSOLETE_FUNCTIONS /DIMGUI_DISABLE_DEMO_WINDOWS /DIMGUI_DISABLE_DEBUG_TOOLS /DIMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS /DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS /Ithird_party\imgui"
$flags = "/nologo /O2 /MT /W4 /WX /EHsc /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN $imgui_defs"
# ImGui itself is built apart, at its authors' warning level (not /W4 /WX).
$imgui_out = Join-Path $out "imgui"
New-Item -ItemType Directory -Force $imgui_out | Out-Null
$imgui_sources = "third_party\imgui\imgui.cpp", "third_party\imgui\imgui_draw.cpp", "third_party\imgui\imgui_tables.cpp", "third_party\imgui\imgui_widgets.cpp", "third_party\imgui\backends\imgui_impl_dx9.cpp"
$imgui_objs = ($imgui_sources | ForEach-Object { "`"$imgui_out\" + [IO.Path]::GetFileNameWithoutExtension($_) + ".obj`"" }) -join ' '
$imgui = "cl /nologo /O2 /MT /W3 /EHsc /std:c++17 /utf-8 $imgui_defs /c $($imgui_sources -join ' ') /Fo`"$imgui_out\\`""
# /GL + /LTCG: whole-program optimization of our own code (the AI pathfinding
# mod calls small helpers across files thousands of times per search)
# The DLL's long argument list goes in a response file: with every mod and its
# tests on one cmd line, the line grew past cmd's 8191 characters.
$dll_rsp = Join-Path $out "dinput8.rsp"
Set-Content -Encoding ascii $dll_rsp "$flags /GL /LD $($sources -join ' ') $imgui_objs /Fo`"$out\\`" /Fe`"$out\dinput8.dll`" /link /LTCG /DEF:dinput8.def /MAP:`"$out\dinput8.map`" user32.lib dxguid.lib iphlpapi.lib winmm.lib shell32.lib advapi32.lib"
$dll = "cl @`"$dll_rsp`""
$cmd = "call `"$vcvars`" x86 >nul 2>&1 && cd /d `"$src`" && $imgui && $dll"
if ($Test) {
    $cmd += " && cl $flags test\proxy_test.cpp /Fo`"$out\\`" /Fe`"$out\proxy_test.exe`" dxguid.lib"
    # calling-convention test of the AI hook: debug build with runtime stack checks (/RTCs)
    $tflags = "/nologo /Od /RTCs /MTd /W4 /WX /EHsc /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAI_TRACE_TEST"
    # the "original music" mod's text work (no game needed)
    $cmd += " && cl $flags test\original_music_test.cpp ..\mods\original_music_xdb.cpp /Fo`"$out\\`" /Fe`"$out\original_music_test.exe`""
    # the "languages" mod's work on bytes: paths, text encodings, zip directories
    $cmd += " && cl $flags test\languages_test.cpp ..\mods\languages_text.cpp /Fo`"$out\\`" /Fe`"$out\languages_test.exe`""
    # the C++ template maker against the JavaScript one, and the recipe names
    $cmd += " && cl $flags test\rmg_maker_test.cpp ..\mods\rmg_maker.cpp /Fo`"$out\\`" /Fe`"$out\rmg_maker_test.exe`""
    $cmd += " && cl $flags test\rmg_recipe_test.cpp ..\mods\rmg_recipe.cpp ..\mods\rmg_maker.cpp ..\mods\rmg_join.cpp ..\mods\rmg_templates.cpp log.cpp patch.cpp /Fo`"$out\\`" /Fe`"$out\rmg_recipe_test.exe`""
    # the template builder's data: presets, zone map, words, the native button's files
    $cmd += " && cl $flags test\rmg_builder_test.cpp ..\mods\rmg_builder_data.cpp ..\mods\rmg_recipe.cpp ..\mods\rmg_maker.cpp /Fo`"$out\\`" /Fe`"$out\rmg_builder_test.exe`""
    # the overlay's modal input rules (what the game gets while a panel is open)
    $cmd += " && cl $flags test\input_mute_test.cpp input_mute.cpp /Fo`"$out\\`" /Fe`"$out\input_mute_test.exe`""
    # the hang watchdog on a thread of its own that freezes on purpose (own object folder: built with WATCHDOG_TEST)
    $wd_out = Join-Path $out "watchdog_test"
    New-Item -ItemType Directory -Force $wd_out | Out-Null
    $cmd += " && cl $flags /DWATCHDOG_TEST test\watchdog_test.cpp watchdog.cpp log.cpp patch.cpp game_build.cpp /Fo`"$wd_out\\`" /Fe`"$out\watchdog_test.exe`" user32.lib"
    $cmd += " && cl $tflags test\ai_trace_test.cpp ai_trace.cpp ..\mods\ai_pacing.cpp log.cpp robot.cpp robot_input.cpp robot_endturn.cpp robot_trace.cpp robot_menu.cpp robot_screen.cpp robot_ui.cpp robot_lobby.cpp robot_net.cpp robot_profile.cpp robot_paths.cpp robot_clock.cpp patch.cpp d3d9_hook.cpp watchdog.cpp ..\mods\fast_paths.cpp ..\mods\fast_paths_relax.cpp ..\mods\fast_paths_search.cpp ..\mods\fast_paths_expand.cpp ..\mods\fast_paths_ashore.cpp game_build.cpp dinput_hook.cpp /Fo`"$out\\`" /Fe`"$out\ai_trace_test.exe`" dxguid.lib user32.lib"
}
# vcvarsall prints a harmless vswhere warning on stderr (silenced above);
# PowerShell 5.1 would turn any native stderr line into an error.
$ErrorActionPreference = "Continue"
cmd /c "$cmd 2>&1"
if ($LASTEXITCODE -ne 0) { throw "build failed" }
$ErrorActionPreference = "Stop"
Write-Host "built $out\dinput8.dll"

if ($Test) {
    Push-Location $out
    try {
        Remove-Item -ErrorAction SilentlyContinue open-homm5.log
        & (Join-Path $out "proxy_test.exe") (Join-Path $out "dinput8.dll")
        if ($LASTEXITCODE -ne 0) { throw "smoke test failed" }
        Get-Content open-homm5.log
        Remove-Item -ErrorAction SilentlyContinue open-homm5.log
        & (Join-Path $out "ai_trace_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "AI trace test failed" }
        & (Join-Path $out "original_music_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "original music test failed" }
        & (Join-Path $out "languages_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "languages test failed" }
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
        & (Join-Path $out "rmg_builder_test.exe") (Join-Path $repo "src\mods\rmg\templates")
        if ($LASTEXITCODE -ne 0) { throw "template builder test failed" }
        & (Join-Path $out "input_mute_test.exe")
        if ($LASTEXITCODE -ne 0) { throw "input mute test failed" }
        Remove-Item -ErrorAction SilentlyContinue open-homm5.log
        & (Join-Path $out "watchdog_test.exe")
        if ($LASTEXITCODE -ne 0) { Get-Content open-homm5.log; throw "watchdog test failed" }
        Remove-Item -ErrorAction SilentlyContinue open-homm5.log
    } finally { Pop-Location }
}
