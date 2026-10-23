# List every reference (code or data) to the given addresses, with the function
# each one is in, into analysis/export/xrefs.txt.
# Usage:  powershell -File tools\inventory\xrefs.ps1 00e0a170 00e133b4 ...
# Takes about a minute (Ghidra start-up), so pass many addresses at once.
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Addresses)
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $env:JAVA_HOME) { $env:JAVA_HOME = "C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot" }
if (-not $env:GHIDRA_HOME) { $env:GHIDRA_HOME = "C:\tools\ghidra_12.1.4_PUBLIC" }
$env:GHIDRA_HEADLESS_MAXMEM = "8G"
$out = Join-Path $repo "analysis\export\xrefs.txt"
& "$env:GHIDRA_HOME\support\analyzeHeadless.bat" "$repo\analysis\ghidra" h5_31 -process H5_Game_31_vanilla.exe `
    -noanalysis -readOnly -scriptPath "$repo\tools\inventory\ghidra" `
    -postScript XrefsTo.java $out @Addresses 2>&1 |
    Select-String "XrefsTo.java>" | ForEach-Object { $_.Line }
Get-Content $out
