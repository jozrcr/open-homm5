# open-homm5 installer for Windows. Runs on Windows PowerShell 5.1.
#
# Keep this file plain ASCII: PowerShell 5.1 reads a file without a byte-order
# mark as ANSI, so any accent would be garbled on a French Windows.
#
# Normal use: double-click install.bat. Options, for tests and odd setups:
#   -GameDir <folder>   use this game folder (or its bin folder), skip the search
#   -SteamDir <folder>  look in this Steam folder instead of the one in the registry
#   -NoPause            do not wait for Enter at the end
#
# Exit codes: 0 done, 3 refused (a dinput8.dll that is not ours is there),
# 4 game not found, 5 copy failed, 6 unexpected error, 7 package incomplete.
param(
    [string]$GameDir = "",
    [string]$SteamDir = "",
    [switch]$NoPause,
    # Tests only: pretend these are the network profiles, "name|category;name|category".
    [string]$TestProfiles = ""
)

$ErrorActionPreference = "Stop"

function Wait-AndExit([int]$Code) {
    if (-not $NoPause) {
        Write-Host ""
        try { [void](Read-Host "Press Enter to close this window") } catch { }
    }
    exit $Code
}

# ---- shared with uninstall.ps1: finding the game (keep both copies identical) ----
$Marker = "open-homm5 hook DLL"
$GameFolderName = "Heroes of Might and Magic 5 Tribes of the East"

# True if the file is the dinput8.dll of open-homm5 (it contains our marker text).
function Test-IsOurs([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $text = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
    return $text.Contains($Marker)
}

# Takes the game folder or its bin folder. Returns the bin folder, or $null if
# neither holds MMH55_Utility_64.exe or H5_Game.exe.
function Get-BinFolder([string]$Folder) {
    if (-not $Folder) { return $null }
    $Folder = $Folder.Trim().Trim('"').Trim()
    if (-not $Folder) { return $null }
    try {
        foreach ($candidate in @((Join-Path $Folder "bin"), $Folder)) {
            if ((Test-Path -LiteralPath (Join-Path $candidate "MMH55_Utility_64.exe")) -or
                (Test-Path -LiteralPath (Join-Path $candidate "H5_Game.exe"))) {
                return $candidate
            }
        }
    } catch { }
    return $null
}

function Get-SteamPath {
    if ($SteamDir) { return $SteamDir }
    try {
        $p = (Get-ItemProperty -Path "HKCU:\Software\Valve\Steam" -Name "SteamPath" -ErrorAction Stop).SteamPath
        if ($p) { return ($p -replace '/', '\') }
    } catch { }
    return $null
}

# Steam's own folder plus every library folder listed in libraryfolders.vdf.
function Get-LibraryFolders([string]$Steam) {
    $libs = New-Object System.Collections.ArrayList
    $vdf = Join-Path $Steam "steamapps\libraryfolders.vdf"
    if (Test-Path -LiteralPath $vdf) {
        foreach ($line in (Get-Content -LiteralPath $vdf -Encoding UTF8)) {
            if ($line -match '^\s*"path"\s+"(.+)"\s*$') {
                [void]$libs.Add(($Matches[1] -replace '\\\\', '\'))
            } elseif ($line -match '^\s*"\d+"\s+"([^"]*[\\/:][^"]*)"\s*$') {
                # older Steam: "1"  "D:\\SteamLibrary"
                [void]$libs.Add(($Matches[1] -replace '\\\\', '\'))
            }
        }
    }
    # Steam's own folder is a library too (the list normally has it already).
    $listed = $false
    foreach ($l in $libs) { if ($l.TrimEnd('\') -ieq $Steam.TrimEnd('\')) { $listed = $true } }
    if (-not $listed) { $libs.Insert(0, $Steam) }
    return $libs
}

# Every bin folder of the game that Steam knows about (no duplicates).
function Find-GameBins {
    $found = New-Object System.Collections.ArrayList
    $steam = Get-SteamPath
    if (-not $steam) { return @() }
    foreach ($lib in (Get-LibraryFolders $steam)) {
        $bin = Get-BinFolder (Join-Path $lib ("steamapps\common\" + $GameFolderName))
        if ($bin) {
            $dup = $false
            foreach ($f in $found) { if ($f -ieq $bin) { $dup = $true } }
            if (-not $dup) { [void]$found.Add($bin) }
        }
    }
    return @($found)
}

# Returns the bin folder of the game to work on, or $null (cancelled or not found).
function Select-GameBin {
    if ($GameDir) {
        $bin = Get-BinFolder $GameDir
        if (-not $bin) {
            Write-Host "That is not the game folder: there is no MMH55_Utility_64.exe or H5_Game.exe in" -ForegroundColor Red
            Write-Host "  $GameDir" -ForegroundColor Red
            Write-Host "  (or in its bin folder)." -ForegroundColor Red
        }
        return $bin
    }
    $found = @(Find-GameBins)
    if ($found.Count -eq 1) {
        Write-Host "Found the game through Steam."
        return $found[0]
    }
    if ($found.Count -gt 1) {
        Write-Host "The game is in several places:"
        for ($i = 0; $i -lt $found.Count; $i++) { Write-Host ("  {0}) {1}" -f ($i + 1), $found[$i]) }
        Write-Host "Type the number of the one to use, or paste another game folder."
        Write-Host "Press Enter alone to cancel."
    } else {
        Write-Host "I could not find the game through Steam."
        Write-Host "Paste the game folder here (the one that holds the bin folder), or its bin folder."
        Write-Host "Press Enter alone to cancel."
    }
    for ($attempt = 0; $attempt -lt 5; $attempt++) {
        $answer = $null
        try { $answer = Read-Host "Game folder" } catch { }
        if ($null -eq $answer) { return $null }
        $answer = $answer.Trim().Trim('"').Trim("'").Trim()
        if ($answer -eq "") { return $null }
        if ($found.Count -gt 1 -and $answer -match '^\d+$') {
            $n = [int]$answer
            if ($n -ge 1 -and $n -le $found.Count) { return $found[$n - 1] }
            Write-Host "There is no number $n in the list."
            continue
        }
        $bin = Get-BinFolder $answer
        if ($bin) { return $bin }
        Write-Host "No MMH55_Utility_64.exe or H5_Game.exe there. Try again, or press Enter to cancel."
    }
    return $null
}
# ---- end of the shared block ----

# Names of ZeroTier networks that Windows treats as Public. Only looks, changes nothing.
function Get-PublicZeroTier {
    $names = @()
    try {
        if ($TestProfiles) {
            foreach ($item in $TestProfiles.Split(";")) {
                $parts = $item.Split("|")
                if ($parts.Count -ge 2 -and $parts[0] -like "*ZeroTier*" -and $parts[1] -eq "Public") { $names += $parts[0] }
            }
        } else {
            $adapters = @(Get-NetAdapter -ErrorAction Stop | Where-Object {
                $_.InterfaceDescription -like "*ZeroTier*" -or $_.Name -like "*ZeroTier*" })
            foreach ($p in @(Get-NetConnectionProfile -ErrorAction Stop)) {
                $isZeroTier = $p.InterfaceAlias -like "*ZeroTier*"
                foreach ($a in $adapters) { if ($a.InterfaceIndex -eq $p.InterfaceIndex) { $isZeroTier = $true } }
                if ($isZeroTier -and ([string]$p.NetworkCategory -eq "Public")) { $names += $p.InterfaceAlias }
            }
        }
    } catch { }
    return $names
}

try {
    Write-Host ""
    Write-Host "open-homm5 installer"
    Write-Host "--------------------"

    $srcDll = Join-Path $PSScriptRoot "dinput8.dll"
    $srcIni = Join-Path $PSScriptRoot "open-homm5.ini"
    if (-not (Test-Path -LiteralPath $srcDll) -or -not (Test-Path -LiteralPath $srcIni)) {
        Write-Host "dinput8.dll and open-homm5.ini must be in the same folder as this script." -ForegroundColor Red
        Write-Host "Please extract the whole zip first (right-click the zip, Extract All)," -ForegroundColor Red
        Write-Host "then run install.bat from the new folder. Nothing was installed." -ForegroundColor Red
        Wait-AndExit 7
    }
    if (-not (Test-IsOurs $srcDll)) {
        Write-Host "The dinput8.dll in this package is not the open-homm5 one. The download may be damaged." -ForegroundColor Red
        Write-Host "Nothing was installed." -ForegroundColor Red
        Wait-AndExit 7
    }

    $bin = Select-GameBin
    if (-not $bin) {
        Write-Host ""
        Write-Host "No game folder, so nothing was installed."
        Wait-AndExit 4
    }
    Write-Host "Game folder: $bin"

    $dstDll = Join-Path $bin "dinput8.dll"
    $dstIni = Join-Path $bin "open-homm5.ini"

    # Never overwrite a dinput8.dll that belongs to something else.
    $dllWas = "new"
    if (Test-Path -LiteralPath $dstDll) {
        if (-not (Test-IsOurs $dstDll)) {
            Write-Host ""
            Write-Host "Stopped. There is already a file called dinput8.dll in the game folder," -ForegroundColor Red
            Write-Host "and it is not the open-homm5 one (another add-on may use it)." -ForegroundColor Red
            Write-Host "I did not change anything. If you want open-homm5, move that file away" -ForegroundColor Red
            Write-Host "yourself and run this installer again." -ForegroundColor Red
            Wait-AndExit 3
        }
        $same = (Get-FileHash -LiteralPath $srcDll -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $dstDll -Algorithm SHA256).Hash
        if ($same) { $dllWas = "same" } else { $dllWas = "older" }
    }

    try {
        Copy-Item -LiteralPath $srcDll -Destination $dstDll -Force
        # A zip from the web marks its files as "downloaded"; take that mark off the copy.
        try { Unblock-File -LiteralPath $dstDll } catch { }
        $iniWas = "kept"
        if (-not (Test-Path -LiteralPath $dstIni)) {
            Copy-Item -LiteralPath $srcIni -Destination $dstIni
            $iniWas = "new"
        }
    } catch {
        Write-Host ""
        Write-Host "Could not copy the files into the game folder:" -ForegroundColor Red
        Write-Host ("  " + $_.Exception.Message) -ForegroundColor Red
        Write-Host "If the game is running, close it and try again. If Windows says access is" -ForegroundColor Red
        Write-Host "denied, right-click install.bat and choose Run as administrator." -ForegroundColor Red
        Wait-AndExit 5
    }

    Write-Host ""
    Write-Host "Done. What I did:" -ForegroundColor Green
    if ($dllWas -eq "new") { Write-Host "  - added dinput8.dll (the add-on)" }
    elseif ($dllWas -eq "same") { Write-Host "  - dinput8.dll was already installed and up to date" }
    else { Write-Host "  - replaced the older open-homm5 dinput8.dll with this version" }
    if ($iniWas -eq "new") { Write-Host "  - added open-homm5.ini (settings, all at their defaults)" }
    else { Write-Host "  - kept your existing open-homm5.ini, with your settings" }
    Write-Host "  - did not change or replace any file of the game itself"
    if (-not (Test-Path -LiteralPath (Join-Path $bin "MMH55_Utility_64.exe"))) {
        Write-Host ""
        Write-Host "Note: there is no MMH55_Utility_64.exe in this folder. The add-on is made for" -ForegroundColor Yellow
        Write-Host "MMH5.5 (the co-op version of the game). Install MMH5.5 first." -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "How to check: start the game as you do for co-op games (GameRanger, or"
    Write-Host "MMH55_Utility_64.exe). A file named open-homm5.log then appears in:"
    Write-Host "  $bin"
    Write-Host "If you see it, the add-on is running. Everyone in the same co-op game should install it."
    Write-Host "To remove it later, run uninstall.bat."

    $publicNets = @(Get-PublicZeroTier)
    if ($publicNets.Count -gt 0) {
        Write-Host ""
        Write-Host ("Heads-up: Windows treats your ZeroTier network (" + ($publicNets -join ", ") + ") as Public.") -ForegroundColor Yellow
        Write-Host "The game's firewall rule only works on Private networks, so friends may not see your game." -ForegroundColor Yellow
        Write-Host "To fix it: Settings > Network and Internet > that ZeroTier network > Network profile type: Private." -ForegroundColor Yellow
        Write-Host "(I did not change anything in Windows.)" -ForegroundColor Yellow
    }

    Wait-AndExit 0
} catch {
    Write-Host ""
    Write-Host "Something unexpected went wrong:" -ForegroundColor Red
    Write-Host ("  " + $_.Exception.Message) -ForegroundColor Red
    Write-Host "You can send this text to the project page (see README.txt)." -ForegroundColor Red
    Wait-AndExit 6
}
