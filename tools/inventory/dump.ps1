# Dump disassembly + decompiled C (+ callers/callees) of functions to analysis/export/dump/.
# Usage:  powershell -File tools\inventory\dump.ps1 0086dd40 00867440 ...
# Takes about a minute (Ghidra start-up), so pass many addresses at once.
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Addresses)
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $env:JAVA_HOME) { $env:JAVA_HOME = "C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot" }
if (-not $env:GHIDRA_HOME) { $env:GHIDRA_HOME = "C:\tools\ghidra_12.1.4_PUBLIC" }
$env:GHIDRA_HEADLESS_MAXMEM = "8G"
$list = Join-Path $repo "analysis\export\dump_request.txt"
$Addresses | Set-Content -Encoding ascii $list
& "$env:GHIDRA_HOME\support\analyzeHeadless.bat" "$repo\analysis\ghidra" h5_31 -process H5_Game_31_vanilla.exe `
    -noanalysis -readOnly -scriptPath "$repo\tools\inventory\ghidra" `
    -postScript DumpFunctions.java "$repo\analysis\export\dump" "@$list" 2>&1 |
    Select-String "DumpFunctions.java>" | ForEach-Object { $_.Line }
