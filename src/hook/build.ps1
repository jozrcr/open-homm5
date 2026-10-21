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

$sources = "dllmain.cpp", "log.cpp", "game_build.cpp", "hooks.cpp", "ai_trace.cpp", "robot.cpp", "robot_input.cpp", "robot_endturn.cpp", "robot_trace.cpp" | ForEach-Object { "`"$src\$_`"" }
$flags = "/nologo /O2 /MT /W4 /WX /EHsc /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN"
$dll = "cl $flags /LD $($sources -join ' ') /Fo`"$out\\`" /Fe`"$out\dinput8.dll`" /link /DEF:`"$src\dinput8.def`" user32.lib dxguid.lib"
$cmd = "call `"$vcvars`" x86 >nul 2>&1 && $dll"
if ($Test) {
    $cmd += " && cl $flags `"$src\test\proxy_test.cpp`" /Fo`"$out\\`" /Fe`"$out\proxy_test.exe`" dxguid.lib"
    # calling-convention test of the AI hook: debug build with runtime stack checks (/RTCs)
    $tflags = "/nologo /Od /RTCs /MTd /W4 /WX /EHsc /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAI_TRACE_TEST"
    $cmd += " && cl $tflags `"$src\test\ai_trace_test.cpp`" `"$src\ai_trace.cpp`" `"$src\log.cpp`" `"$src\robot.cpp`" `"$src\robot_input.cpp`" `"$src\robot_endturn.cpp`" `"$src\robot_trace.cpp`" `"$src\game_build.cpp`" /Fo`"$out\\`" /Fe`"$out\ai_trace_test.exe`" dxguid.lib user32.lib"
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
    } finally { Pop-Location }
}
