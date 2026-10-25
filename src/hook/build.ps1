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

$sources = "dllmain.cpp", "log.cpp", "game_build.cpp", "hooks.cpp", "ai_trace.cpp", "robot.cpp", "robot_input.cpp", "robot_endturn.cpp", "robot_trace.cpp", "robot_menu.cpp", "robot_screen.cpp", "robot_ui.cpp", "robot_lobby.cpp", "robot_net.cpp", "robot_profile.cpp", "robot_paths.cpp", "robot_clock.cpp", "patch.cpp", "..\mods\net_tuning.cpp", "..\mods\lan_discovery.cpp", "..\mods\coop_guard.cpp", "..\mods\lockstep.cpp", "..\mods\rmg_teams.cpp", "..\mods\rmg_templates.cpp", "..\mods\fast_paths.cpp", "..\mods\fast_paths_relax.cpp", "..\mods\fast_paths_search.cpp", "..\mods\fast_paths_expand.cpp", "..\mods\fast_paths_ashore.cpp", "..\mods\original_music.cpp", "..\mods\original_music_xdb.cpp"
# Source paths are relative to src\hook (the build runs there): with the full
# path of a deep folder, the command line grew past cmd's 8191 characters.
$flags = "/nologo /O2 /MT /W4 /WX /EHsc /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN"
# /GL + /LTCG: whole-program optimization of our own code (the AI pathfinding
# mod calls small helpers across files thousands of times per search)
$dll = "cl $flags /GL /LD $($sources -join ' ') /Fo`"$out\\`" /Fe`"$out\dinput8.dll`" /link /LTCG /DEF:dinput8.def /MAP:`"$out\dinput8.map`" user32.lib dxguid.lib iphlpapi.lib"
$cmd = "call `"$vcvars`" x86 >nul 2>&1 && cd /d `"$src`" && $dll"
if ($Test) {
    $cmd += " && cl $flags test\proxy_test.cpp /Fo`"$out\\`" /Fe`"$out\proxy_test.exe`" dxguid.lib"
    # calling-convention test of the AI hook: debug build with runtime stack checks (/RTCs)
    $tflags = "/nologo /Od /RTCs /MTd /W4 /WX /EHsc /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAI_TRACE_TEST"
    # the "original music" mod's text work (no game needed)
    $cmd += " && cl $flags test\original_music_test.cpp ..\mods\original_music_xdb.cpp /Fo`"$out\\`" /Fe`"$out\original_music_test.exe`""
    $cmd += " && cl $tflags test\ai_trace_test.cpp ai_trace.cpp log.cpp robot.cpp robot_input.cpp robot_endturn.cpp robot_trace.cpp robot_menu.cpp robot_screen.cpp robot_ui.cpp robot_lobby.cpp robot_net.cpp robot_profile.cpp robot_paths.cpp robot_clock.cpp patch.cpp ..\mods\fast_paths.cpp ..\mods\fast_paths_relax.cpp ..\mods\fast_paths_search.cpp ..\mods\fast_paths_expand.cpp ..\mods\fast_paths_ashore.cpp game_build.cpp /Fo`"$out\\`" /Fe`"$out\ai_trace_test.exe`" dxguid.lib user32.lib"
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
    } finally { Pop-Location }
}
