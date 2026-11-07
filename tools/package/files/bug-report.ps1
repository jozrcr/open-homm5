# H5 Together bug report helper for Windows. Runs on Windows PowerShell 5.1.
#
# Makes h5-together-bug-report-<date>.zip on your Desktop, to attach to a bug report on
# GitHub. It holds what we need to look into a problem and nothing else:
#   h5-together.log and h5-together.old.log   what the add-on wrote
#   h5-together.ini, h5-together-skins.ini    your settings
#   the same four under the add-on's old name (open-homm5, until 0.2.7), if they are there
#   the newest crash report, if there is one: h5-together-crash-<date>-<time>.txt (what
#                                             crashed) and its .dmp (a small minidump; it holds
#                                             part of the game's memory: -NoDump leaves it out)
#   system.txt                                the add-on, MMH5.5 and Windows versions, a few file
#                                             sizes and hashes, the other DLLs in the game folder
# No saves, no maps, no game files. It sends nothing anywhere: the zip is yours to look at
# (it is a normal zip) and to attach yourself. The log lists player names and LAN addresses.
#
# It finds the game the way install.ps1 does (and shows what it found). It works when the game
# does not start, and when the add-on is not installed (then there is no log, and the report
# says so).
#
# Keep this file plain ASCII (see install.ps1 for why).
#
# Normal use: double-click bug-report.bat. Options, for tests and odd setups:
#   -GameDir <folder>   use this game folder (or its bin folder), skip the search
#   -SteamDir <folder>  look in this Steam folder instead of the one in the registry
#   -OutDir <folder>    put the zip here instead of on the Desktop
#   -NoDump             leave the crash report's .dmp out (its .txt still goes in)
#   -NoPause            do not wait for Enter at the end, and do not open the folder
#   -TestRegistry <key>   look for GOG and MMH5.5's installer in this registry key (laid out
#                         like HKLM:\SOFTWARE) instead of in HKLM; tests only
#   -TestPicker <folder>  B answers this folder, or "cancel", instead of opening the window
#   -TestKnown55u "<sha256>=<name>;..."  count these 55u.dll files as known versions
#
# Exit codes: 0 done, 4 game not found or cancelled, 5 could not write the zip, 6 unexpected error.
param(
    [string]$GameDir = "",
    [string]$SteamDir = "",
    [string]$OutDir = "",
    [switch]$NoDump,
    [switch]$NoPause,
    # Tests only (see above).
    [string]$TestRegistry = "",
    [string]$TestPicker = "",
    [string]$TestKnown55u = ""
)

$ErrorActionPreference = "Stop"

function Wait-AndExit([int]$Code) {
    if (-not $NoPause) {
        Write-Host ""
        try { [void](Read-Host "Press Enter to close this window") } catch { }
    }
    exit $Code
}

# ---- shared with uninstall.ps1 and bug-report.ps1: finding the game (keep all copies identical) ----
# Finding the game: Steam (its libraries and its file for the game), GOG and MMH5.5's own
# entry in Windows' registry, then a numbered choice, a folder picker window or a typed
# path. Describing it: the folder, Steam's language, the exes, MMH5.5 and its version.
# Checking that it is not running. Test hooks (the options of the scripts): -SteamDir,
# -TestRegistry (a registry key to look in, with the same layout as HKLM:\SOFTWARE),
# -TestPicker (a folder, or "cancel": no window opens), -TestKnown55u ("sha256=name;...").
$Marker = "H5 Together hook DLL"
$OldMarker = "open-homm5 hook DLL"  # the add-on's name until 0.2.7
$GameFolderName = "Heroes of Might and Magic 5 Tribes of the East"
$SteamAppId = "15370"                # Tribes of the East on Steam
$CoopExes = @("MMH55_Utility_64.exe", "MMH55_Utility.exe")   # MMH5.5's co-op exes
$GameProcesses = @("H5_Game", "H5_GameOLD", "MMH55_Utility_64", "MMH55_Utility", "MMH55_64", "MMH55")
$Latin1 = [System.Text.Encoding]::GetEncoding(28591)
Add-Type -AssemblyName System.IO.Compression

# True if the file is the dinput8.dll of H5 Together, or of open-homm5 (0.2.7 and
# older): it contains our marker text.
function Test-IsOurs([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $text = $Latin1.GetString($bytes)
    return ($text.Contains($Marker) -or $text.Contains($OldMarker))
}

# The name and version a dinput8.dll of ours says in its text, or $null: "H5 Together 0.3.0"
# from "H5 Together hook DLL 0.3.0 loaded" ("open-homm5 0.2.7": the add-on's old name).
function Get-AddonVersion([string]$Path) {
    try {
        $text = $Latin1.GetString([System.IO.File]::ReadAllBytes($Path))
        $m = [regex]::Match($text, '(H5 Together|open-homm5) hook DLL (\S+) loaded')
        if ($m.Success) { return ($m.Groups[1].Value + " " + $m.Groups[2].Value) }
    } catch { }
    return $null
}

# SHA-256 of a file as lower case hex, read so that a file in use is fine.
function Get-Sha256Hex([string]$Path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $fs = $null
    try {
        $fs = [System.IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
        return ([System.BitConverter]::ToString($sha.ComputeHash($fs))).Replace("-", "").ToLowerInvariant()
    } catch {
        return "(could not be read)"
    } finally {
        if ($fs) { $fs.Dispose() }
        $sha.Dispose()
    }
}

# The text of scripts/H55-Version.lua in MMH55-Index.pak (a zip file), or $null.
function Get-Mmh55VersionScript([string]$Pak) {
    $fs = $null
    $zip = $null
    try {
        $fs = [System.IO.File]::Open($Pak, 'Open', 'Read', 'ReadWrite')
        $zip = New-Object System.IO.Compression.ZipArchive($fs, [System.IO.Compression.ZipArchiveMode]::Read)
        foreach ($e in $zip.Entries) {
            if ($e.FullName -ieq "scripts/H55-Version.lua") {
                $reader = New-Object System.IO.StreamReader($e.Open())
                try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
            }
        }
    } catch {
    } finally {
        if ($zip) { $zip.Dispose() } elseif ($fs) { $fs.Dispose() }
    }
    return $null
}

# 55u.dll is MMH5.5's runtime patcher, and it changes with every release; its hash tells
# the versions apart (docs/plans/mmh55-rc19k.md). H55_Version says "19e" in RC19k too.
$Known55u = @{
    "a6de006f9492f1e8af6eae19619b5a345536c443a5f2e4bb5e1e26947988085b" = "RC19d"
    "a28663da081b6e23f0c2fa4000117d0b5d1457ca3314e3735bca215c1d5d304d" = "RC19k"
}
if ($TestKnown55u) {
    foreach ($item in $TestKnown55u.Split(";")) {
        $kv = $item.Split("=")
        if ($kv.Count -eq 2) { $Known55u[$kv[0].Trim().ToLowerInvariant()] = $kv[1].Trim() }
    }
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

# The value of a "key"  "value" line in one of Steam's .acf files (the first), or $null.
function Get-AcfValue([string]$Path, [string]$Key) {
    try {
        foreach ($line in (Get-Content -LiteralPath $Path -Encoding UTF8)) {
            if ($line -match ('^\s*"' + [regex]::Escape($Key) + '"\s+"([^"]*)"\s*$')) { return ($Matches[1] -replace '\\\\', '\') }
        }
    } catch { }
    return $null
}

# Adds a bin folder to the list unless it is there already (the same folder written another way).
function Add-UniqueBin($List, [string]$Bin) {
    if (-not $Bin) { return }
    $norm = $Bin
    try { $norm = [System.IO.Path]::GetFullPath($Bin) } catch { }
    $norm = $norm.TrimEnd('\')
    foreach ($f in $List) {
        $other = $f
        try { $other = [System.IO.Path]::GetFullPath($f) } catch { }
        if ($other.TrimEnd('\') -ieq $norm) { return }
    }
    [void]$List.Add($Bin)
}

# Game folders that Windows itself knows, besides Steam: GOG's list of games, and the entry
# of MMH5.5's own installer (it names the folder it installed into, whatever the copy).
function Get-RegistryGameFolders {
    $roots = @("HKLM:\SOFTWARE\WOW6432Node", "HKLM:\SOFTWARE")
    if ($TestRegistry) { $roots = @($TestRegistry) }
    $dirs = New-Object System.Collections.ArrayList
    foreach ($root in $roots) {
        try {
            foreach ($key in @(Get-ChildItem -LiteralPath ($root + "\GOG.com\Games") -ErrorAction Stop)) {
                $p = (Get-ItemProperty -LiteralPath $key.PSPath -ErrorAction Stop).path
                if ($p) { [void]$dirs.Add([string]$p) }
            }
        } catch { }
        try {
            $key = $root + "\Microsoft\Windows\CurrentVersion\Uninstall\{CEC93C99-EB3D-4EBE-B0BE-AE228E5B47AE}_is1"
            $p = (Get-ItemProperty -LiteralPath $key -ErrorAction Stop).InstallLocation
            if ($p) { [void]$dirs.Add([string]$p) }
        } catch { }
    }
    return @($dirs)
}

# Every bin folder of the game that Steam, GOG or MMH5.5's installer knows about (no
# duplicates). A folder only counts if it holds one of the game's exes.
function Find-GameBins {
    $found = New-Object System.Collections.ArrayList
    $steam = Get-SteamPath
    if ($steam) {
        foreach ($lib in (Get-LibraryFolders $steam)) {
            # Steam's file for the game says which folder it is in; the usual name is the fallback
            $names = @($GameFolderName)
            $installDir = Get-AcfValue (Join-Path $lib ("steamapps\appmanifest_" + $SteamAppId + ".acf")) "installdir"
            if ($installDir) { $names = @($installDir, $GameFolderName) }
            foreach ($n in $names) { Add-UniqueBin $found (Get-BinFolder (Join-Path $lib ("steamapps\common\" + $n))) }
        }
    }
    foreach ($dir in (Get-RegistryGameFolders)) { Add-UniqueBin $found (Get-BinFolder $dir) }
    return @($found)
}

$LanguageNames = @{ "english" = "English"; "french" = "French"; "german" = "German"; "russian" = "Russian";
                    "polish" = "Polish"; "spanish" = "Spanish"; "italian" = "Italian"; "czech" = "Czech"; "hungarian" = "Hungarian" }

# The build date in a program's header (seconds since 1970), or 0. The 2008 game's exe and
# MMH5.5's exes made from it say 2008-06-04 13:58:55; Steam's 2025 rebuild says 2025.
$PeDate2008 = 1212587935
function Get-PeDate([string]$Path) {
    try {
        $fs = [System.IO.File]::OpenRead($Path)
        try {
            $buf = New-Object byte[] 1024
            $n = $fs.Read($buf, 0, 1024)
            if ($n -lt 64) { return 0 }
            $pe = [BitConverter]::ToInt32($buf, 60)
            if ($pe -lt 0 -or $pe + 12 -gt $n -or [BitConverter]::ToUInt32($buf, $pe) -ne 0x4550) { return 0 }
            return [BitConverter]::ToUInt32($buf, $pe + 8)
        } finally { $fs.Close() }
    } catch { return 0 }
}

# The 2008 game's own exe (not MMH5.5's: those have the same date but load MMH5.5's 55u.dll).
function Test-Exe2008([string]$Path) {
    if ((Get-PeDate $Path) -ne $PeDate2008) { return $false }
    try { $text = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($Path)) } catch { return $false }
    return -not $text.Contains("55u.dll")
}

# What we can tell about a game, from its bin folder. Only looks. A hashtable:
#   Bin, Root (the game folder), Store ("Steam", "GOG" or ""), Language (Steam's name for it, or ""),
#   Exes (each: Name, Role = coop | copy | 2008 | own), Has2008 (the 2008 game's exe is there),
#   Mmh55 (ok | untested | missing), Mmh55Name ("RC19k", or ""), Mmh55Version (H55_Version, or "").
function Get-GameInfo([string]$Bin) {
    $root = Split-Path -Parent $Bin
    $info = @{ Bin = $Bin; Root = $root; Store = ""; Language = ""; Exes = @(); Has2008 = $false; Mmh55 = "missing"; Mmh55Name = ""; Mmh55Version = "" }
    # Steam keeps the game in <library>\steamapps\common\<name>, and its file for the game next to that
    $common = Split-Path -Parent $root
    if ($common -and (Split-Path -Leaf $common) -ieq "common") {
        $manifest = Join-Path (Split-Path -Parent $common) ("appmanifest_" + $SteamAppId + ".acf")
        if (Test-Path -LiteralPath $manifest -PathType Leaf) {
            $info.Store = "Steam"
            $lang = Get-AcfValue $manifest "language"
            if ($lang) { $info.Language = $lang.ToLowerInvariant() }
        }
    }
    if (-not $info.Store -and (Test-Path -LiteralPath $root -PathType Container) -and
        @(Get-ChildItem -LiteralPath $root -Filter "goggame-*.info" -File -ErrorAction SilentlyContinue).Count -gt 0) {
        $info.Store = "GOG"
    }
    # The exes. H5_Game.exe is a copy of the co-op exe when the player followed the GameRanger
    # guide, which keeps the 2008 game's own exe as H5_GameOLD.exe.
    $exes = New-Object System.Collections.ArrayList
    $coop = @()
    foreach ($name in $CoopExes) {
        $p = Join-Path $Bin $name
        if (Test-Path -LiteralPath $p -PathType Leaf) { $coop += $p; [void]$exes.Add(@{ Name = $name; Role = "coop" }) }
    }
    foreach ($name in @("H5_Game.exe", "H5_GameOLD.exe", "H5_Original_4GB.exe")) {
        $p = Join-Path $Bin $name
        if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { continue }
        $role = "own"
        $size = (Get-Item -LiteralPath $p).Length
        foreach ($c in $coop) {
            if ($role -eq "own" -and (Get-Item -LiteralPath $c).Length -eq $size -and (Get-Sha256Hex $c) -eq (Get-Sha256Hex $p)) { $role = "copy" }
        }
        if ($role -eq "own" -and (Test-Exe2008 $p)) { $role = "2008"; $info.Has2008 = $true }
        [void]$exes.Add(@{ Name = $name; Role = $role })
    }
    $info.Exes = @($exes)
    # MMH5.5 and its version: the version file in its pak, and which 55u.dll it has
    $pak = Join-Path $root "data\MMH55-Index.pak"
    if (Test-Path -LiteralPath $pak -PathType Leaf) {
        $lua = Get-Mmh55VersionScript $pak
        if ($lua) {
            $m = [regex]::Match($lua, 'H55_Version\s*=\s*"([^"]*)"')
            if ($m.Success) { $info.Mmh55Version = $m.Groups[1].Value }
        }
    }
    $u55 = Join-Path $Bin "55u.dll"
    if (Test-Path -LiteralPath $u55 -PathType Leaf) {
        $h = Get-Sha256Hex $u55
        if ($Known55u.ContainsKey($h)) { $info.Mmh55Name = $Known55u[$h] }
    }
    if ($coop.Count -gt 0) {
        if ($info.Mmh55Name) { $info.Mmh55 = "ok" } else { $info.Mmh55 = "untested" }
    }
    return $info
}

# The one-line description of MMH5.5 in a game's info.
function Get-Mmh55Text($Info) {
    if ($Info.Mmh55 -eq "ok") { return ($Info.Mmh55Name + " (tested with H5 Together)") }
    if ($Info.Mmh55 -eq "untested") {
        $v = "a version we have not tested"
        if ($Info.Mmh55Version) { $v += " (its version file says " + $Info.Mmh55Version + ")" }
        return $v
    }
    if ($Info.Has2008) { return "not found (H5 Together works with the original 2008 game)" }
    return "not found"
}

# Shows what we found, in plain words, with a warning where H5 Together may not work.
# Notes: more lines to show with the facts. NoWarnings: facts only (for the uninstaller).
function Write-GameInfo($Info, [string[]]$Notes = @(), [switch]$NoWarnings) {
    $tested = (($Known55u.Values | Sort-Object -Unique) -join " and ")
    Write-Host ""
    Write-Host "Your game"
    Write-Host ("  Folder:     " + $Info.Root)
    if ($Info.Store) { Write-Host ("  Found in:   " + $Info.Store) }
    if ($Info.Language) {
        $lang = $Info.Language
        if ($LanguageNames.ContainsKey($lang)) { $lang = $LanguageNames[$lang] }
        Write-Host ("  Language:   " + $lang + " (Steam's setting for the game)")
    }
    $color = "Green"
    if ($Info.Mmh55 -ne "ok" -and -not ($Info.Mmh55 -eq "missing" -and $Info.Has2008)) { $color = "Yellow" }
    Write-Host ("  MMH5.5:     " + (Get-Mmh55Text $Info)) -ForegroundColor $color
    $label = "  Game exes:  "
    foreach ($e in $Info.Exes) {
        $text = "MMH5.5's co-op exe: H5 Together works with it"
        if ($e.Role -eq "copy") { $text = "a copy of the co-op exe (made for GameRanger): works too" }
        if ($e.Role -eq "2008") {
            if ($Info.Mmh55 -eq "missing") { $text = "the original 2008 game: H5 Together works with it" }
            else { $text = "the original 2008 game: works too (H5 Together hides MMH5.5 from it)" }
        }
        if ($e.Role -eq "own") { $text = "not the 2008 game (Steam's 2025 update?): H5 Together does nothing with it" }
        Write-Host ($label + $e.Name.PadRight(21) + " " + $text)
        $label = "              "
    }
    foreach ($n in $Notes) { Write-Host ("  " + $n) }
    if ($NoWarnings) { return }
    if ($Info.Mmh55 -eq "missing" -and -not $Info.Has2008) {
        Write-Host ""
        Write-Host "Warning: MMH5.5 was not found in this folder (there is no MMH55_Utility_64.exe), nor the" -ForegroundColor Yellow
        Write-Host "original 2008 game's exe. Since July 2025, Steam installs a rebuild of the game that" -ForegroundColor Yellow
        Write-Host "H5 Together does not know. The files can be copied anyway; they start working with" -ForegroundColor Yellow
        Write-Host "MMH5.5, or with the 2008 game's exe." -ForegroundColor Yellow
    } elseif ($Info.Mmh55 -eq "untested") {
        Write-Host ""
        Write-Host "Warning: this MMH5.5 is a version we have not tested (tested: $tested)." -ForegroundColor Yellow
        Write-Host "H5 Together may switch itself off to stay safe; the game then runs as it always did," -ForegroundColor Yellow
        Write-Host "and h5-together.log says why." -ForegroundColor Yellow
    }
}

# Names of the game's programs that are running from this bin folder (a program whose
# folder Windows does not tell us counts too: better to ask than to fail halfway).
function Get-RunningGame([string]$Bin) {
    $names = New-Object System.Collections.ArrayList
    $here = $Bin
    try { $here = [System.IO.Path]::GetFullPath($Bin) } catch { }
    $here = $here.TrimEnd('\')
    foreach ($p in @(Get-Process -Name $GameProcesses -ErrorAction SilentlyContinue)) {
        $path = $null
        try { $path = $p.Path } catch { }
        $inside = $true
        if ($path) { $inside = ((Split-Path -Parent $path).TrimEnd('\') -ieq $here) }
        if ($inside -and -not $names.Contains($p.ProcessName + ".exe")) { [void]$names.Add($p.ProcessName + ".exe") }
    }
    return @($names)
}

# While the game runs from this bin folder, asks the player to close it (Windows will not let a
# DLL that a program has loaded be replaced or deleted). Returns $true to go on, $false to stop.
function Confirm-GameClosed([string]$Bin) {
    for ($i = 0; $i -lt 20; $i++) {
        $running = @(Get-RunningGame $Bin)
        if ($running.Count -eq 0) { return $true }
        Write-Host ""
        Write-Host ("The game is running (" + ($running -join ", ") + "). Windows does not let me change the add-on's file while it runs.") -ForegroundColor Yellow
        Write-Host "Please close the game, then press Enter here to go on."
        Write-Host "(Type S and Enter to go on anyway, or Q and Enter to stop.)"
        $answer = $null
        try { $answer = Read-Host "Game closed? Press Enter" } catch { }
        if ($null -eq $answer) { return $false }
        $answer = $answer.Trim().ToLowerInvariant()
        if ($answer -eq "q") { return $false }
        if ($answer -eq "s") { return $true }
    }
    return $false
}

# A window to choose the folder in. Returns the folder, or $null if the window was closed.
function Show-FolderPicker {
    if ($TestPicker) {
        if ($TestPicker -eq "cancel") { return $null }
        return $TestPicker
    }
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $owner = New-Object System.Windows.Forms.Form
        $owner.TopMost = $true
        $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
        $dialog.Description = "Choose the folder of Heroes of Might and Magic V (the one that has the bin folder in it)."
        $dialog.ShowNewFolderButton = $false
        try {
            if ($dialog.ShowDialog($owner) -eq [System.Windows.Forms.DialogResult]::OK) { return $dialog.SelectedPath }
        } finally {
            $dialog.Dispose()
            $owner.Dispose()
        }
    } catch { }
    return $null
}

function Write-NotTheGame([string]$Folder) {
    Write-Host "That is not the game folder: I found no bin folder with H5_Game.exe or MMH55_Utility_64.exe in" -ForegroundColor Red
    Write-Host "  $Folder" -ForegroundColor Red
    Write-Host "Choose the folder that holds the game's bin and data folders." -ForegroundColor Red
}

# Returns the bin folder of the game to work on, or $null (cancelled or not found).
function Select-GameBin {
    if ($GameDir) {
        $bin = Get-BinFolder $GameDir
        if (-not $bin) { Write-NotTheGame $GameDir }
        return $bin
    }
    Write-Host "Looking for Heroes of Might and Magic V ..."
    $found = @(Find-GameBins)
    if ($found.Count -eq 1) {
        Write-Host "Found the game."
        return $found[0]
    }
    if ($found.Count -gt 1) {
        Write-Host "The game is in several places:"
        for ($i = 0; $i -lt $found.Count; $i++) {
            $g = Get-GameInfo $found[$i]
            $what = @()
            if ($g.Store) { $what += $g.Store }
            $what += ("MMH5.5: " + (Get-Mmh55Text $g))
            Write-Host ("  {0}) {1}" -f ($i + 1), $g.Root)
            Write-Host ("       " + ($what -join "; "))
        }
        Write-Host "Type the number of the one to use. Or type B to choose another folder in a window,"
        Write-Host "or paste another game folder."
    } else {
        Write-Host "I could not find the game on this PC (I looked in Steam and GOG)."
        Write-Host "Type B and press Enter to choose its folder in a window,"
        Write-Host "or paste the game folder here (the one that holds the bin folder)."
    }
    Write-Host "Press Enter alone to cancel."
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
        if ($answer -ieq "b") {
            $answer = Show-FolderPicker
            if (-not $answer) {
                Write-Host "No folder chosen. Type B to try again, paste a folder, or press Enter to cancel."
                continue
            }
        }
        $bin = Get-BinFolder $answer
        if ($bin) { return $bin }
        Write-NotTheGame $answer
        Write-Host "Try again (B for the window, or paste a folder), or press Enter to cancel."
    }
    return $null
}
# ---- end of the shared block ----

# The path with your home folder written as ~, so the report does not carry your user name.
function Get-ShortPath([string]$Path) {
    $profile_ = $env:USERPROFILE
    if ($profile_ -and $Path.StartsWith($profile_, [System.StringComparison]::OrdinalIgnoreCase)) {
        return "~" + $Path.Substring($profile_.Length)
    }
    return $Path
}

function Format-Size([long]$Bytes) {
    if ($Bytes -ge 1048576) { return ("{0:N1} MB" -f ($Bytes / 1048576.0)) }
    if ($Bytes -ge 1024) { return ("{0:N0} KB" -f ($Bytes / 1024.0)) }
    return "$Bytes bytes"
}

# Where the zip goes: -OutDir, else the Desktop (also when OneDrive moved it), else your home folder.
function Get-OutFolder {
    if ($OutDir) {
        [void](New-Item -ItemType Directory -Path $OutDir -Force)
        return (Resolve-Path -LiteralPath $OutDir).Path
    }
    foreach ($special in @("Desktop", "UserProfile")) {
        $p = [Environment]::GetFolderPath($special)
        if ($p -and (Test-Path -LiteralPath $p -PathType Container)) { return $p }
    }
    return $PSScriptRoot
}

try {
    Write-Host ""
    Write-Host "H5 Together bug report"
    Write-Host "----------------------"

    $bin = Select-GameBin
    if (-not $bin) {
        Write-Host ""
        Write-Host "No game folder, so no bug report was made."
        Wait-AndExit 4
    }
    Write-GameInfo (Get-GameInfo $bin)
    $root = Split-Path -Parent $bin

    # The files that go in the zip: only these, and only the ones that exist (the last four:
    # the add-on's old name, until 0.2.7, for an install that was not updated yet).
    $pack = New-Object System.Collections.ArrayList
    foreach ($name in @("h5-together.log", "h5-together.old.log", "h5-together.ini", "h5-together-skins.ini",
                        "open-homm5.log", "open-homm5.old.log", "open-homm5.ini", "open-homm5-skins.ini")) {
        $p = Join-Path $bin $name
        if (Test-Path -LiteralPath $p -PathType Leaf) { [void]$pack.Add(@($name, $p)) }
    }
    $hasLog = $false
    foreach ($item in $pack) { if ($item[0] -eq "h5-together.log" -or $item[0] -eq "open-homm5.log") { $hasLog = $true } }
    # The add-on's newest crash report (h5-together-crash-<date>-<time>.txt and its .dmp, next to
    # the log; the names sort as the time does). The add-on keeps the 3 newest.
    $crashes = @(Get-ChildItem -LiteralPath $bin -File -ErrorAction SilentlyContinue |
                 Where-Object { $_.Name -match '^h5-together-crash-\d{8}-\d{6}\.txt$' } | Sort-Object Name)
    $crashText = $null
    $crashDump = $null
    if ($crashes.Count -gt 0) {
        $crashText = $crashes[$crashes.Count - 1].Name
        [void]$pack.Add(@($crashText, $crashes[$crashes.Count - 1].FullName))
        $dmpName = $crashText.Substring(0, $crashText.Length - 4) + ".dmp"
        $dmpPath = Join-Path $bin $dmpName
        if (Test-Path -LiteralPath $dmpPath -PathType Leaf) {
            $crashDump = $dmpName
            if (-not $NoDump) { [void]$pack.Add(@($dmpName, $dmpPath)) }
        }
    }

    # system.txt
    $t = New-Object System.Collections.Generic.List[string]
    $t.Add("H5 Together bug report")
    $t.Add("Made by bug-report.ps1 on " + [DateTime]::Now.ToString("yyyy-MM-dd HH:mm:ss", [Globalization.CultureInfo]::InvariantCulture) + ". Nothing was sent anywhere.")
    $t.Add("")
    $t.Add("Add-on")
    $dll = Join-Path $bin "dinput8.dll"
    if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) {
        $t.Add("  dinput8.dll in the game folder: none (the add-on is not installed here)")
    } elseif (-not (Test-IsOurs $dll)) {
        $t.Add("  dinput8.dll in the game folder: NOT the H5 Together one (another add-on uses that name)")
    } else {
        $v = Get-AddonVersion $dll
        if (-not $v) { $v = "an older build of the add-on, version not written in it" }
        $props = (Get-Item -LiteralPath $dll).VersionInfo
        $t.Add("  dinput8.dll in the game folder: " + $v + " (file properties: " + $(if ($props.ProductVersion) { $props.ProductVersion } else { "no version information" }) + ")")
    }
    $mine = Join-Path $PSScriptRoot "dinput8.dll"
    if (Test-Path -LiteralPath $mine -PathType Leaf) {
        $v = Get-AddonVersion $mine
        if (-not $v) { $v = "unknown" }
        $t.Add("  the package this script came from: " + $v)
    }
    foreach ($name in @("h5-together.nohooks", "open-homm5.nohooks")) {
        $t.Add("  " + $name + " (switches the hooks off): " + $(if (Test-Path -LiteralPath (Join-Path $bin $name)) { "present" } else { "not there" }))
    }
    $t.Add("")
    $t.Add("System")
    try {
        $cv = Get-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion" -ErrorAction Stop
        # (Windows 11 still says "Windows 10" in this registry value; its builds start at 22000)
        $winName = $cv.ProductName
        if ([int]$cv.CurrentBuild -ge 22000) { $winName = $winName -replace '^Windows 10', 'Windows 11' }
        $t.Add("  Windows: " + $winName + " " + $cv.DisplayVersion + ", build " + $cv.CurrentBuild + "." + $cv.UBR)
    } catch {
        $t.Add("  Windows: " + [Environment]::OSVersion.VersionString)
    }
    $t.Add("  64-bit Windows: " + [Environment]::Is64BitOperatingSystem)
    $t.Add("  language: " + (Get-UICulture).Name + " (formats: " + (Get-Culture).Name + ")")
    $t.Add("  PowerShell: " + $PSVersionTable.PSVersion)
    $t.Add("  game folder: " + (Get-ShortPath $bin))
    $t.Add("")
    $t.Add("MMH5.5")
    $pak = Join-Path $root "data\MMH55-Index.pak"
    if (Test-Path -LiteralPath $pak -PathType Leaf) {
        $lua = Get-Mmh55VersionScript $pak
        $m = $null
        if ($lua) { $m = [regex]::Match($lua, 'H55_Version\s*=\s*"([^"]*)"') }
        if ($m -and $m.Success) {
            $t.Add("  H55_Version in data\MMH55-Index.pak: " + $m.Groups[1].Value)
        } else {
            $t.Add("  data\MMH55-Index.pak is there, but I could not read the version from it")
        }
    } else {
        $t.Add("  data\MMH55-Index.pak: not found (MMH5.5 is not installed here, or the game folder is not the usual one)")
    }
    $u55 = Join-Path $bin "55u.dll"
    if (Test-Path -LiteralPath $u55 -PathType Leaf) {
        $h = Get-Sha256Hex $u55
        $label = "a version we have not tested"
        if ($Known55u.ContainsKey($h)) { $label = $Known55u[$h] }
        $t.Add("  55u.dll: " + $label + " (sha256 " + $h + ")")
    } else {
        $t.Add("  55u.dll: not found")
    }
    $t.Add("")
    $t.Add("Game exes in the bin folder (size, file version, sha256)")
    $anyExe = $false
    foreach ($name in @("H5_Game.exe", "H5_GameOLD.exe", "MMH55_Utility_64.exe", "MMH55_Utility.exe", "MMH55_64.exe", "MMH55.exe")) {
        $p = Join-Path $bin $name
        if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { continue }
        $anyExe = $true
        $fv = (Get-Item -LiteralPath $p).VersionInfo.FileVersion
        if (-not $fv) { $fv = "no version" }
        $t.Add("  " + $name + "  " + (Get-Item -LiteralPath $p).Length + " bytes  " + $fv + "  " + (Get-Sha256Hex $p))
    }
    if (-not $anyExe) { $t.Add("  none found") }
    $t.Add("")
    $t.Add("Other .dll and .asi files in the bin folder (name, size)")
    $others = @(Get-ChildItem -LiteralPath $bin -File | Where-Object { $_.Extension -ieq ".dll" -or $_.Extension -ieq ".asi" } | Sort-Object Name)
    foreach ($f in $others) { $t.Add("  " + $f.Name + "  " + $f.Length) }
    if ($others.Count -eq 0) { $t.Add("  none") }
    $t.Add("")
    $t.Add("Crash reports of the add-on (written next to the log when the game crashes)")
    if (-not $crashText) {
        $t.Add("  none")
    } else {
        $dumpNote = " (no .dmp with it)"
        if ($crashDump -and $NoDump) { $dumpNote = " (its .dmp left out: -NoDump)" } elseif ($crashDump) { $dumpNote = " and " + $crashDump }
        $t.Add("  " + $crashes.Count + " kept; the newest is in this zip: " + $crashText + $dumpNote)
    }
    $t.Add("")
    $t.Add("Files in this zip")
    foreach ($item in $pack) { $t.Add("  " + $item[0] + "  " + (Get-Item -LiteralPath $item[1]).Length + " bytes") }
    if (-not $hasLog) { $t.Add("  (there is no h5-together.log in the game folder: the game was not started since the add-on was installed, or the add-on is not installed)") }
    $t.Add("  system.txt  (this file)")
    $systemText = ($t -join "`r`n") + "`r`n"

    # The zip.
    $stamp = [DateTime]::Now.ToString("yyyy-MM-dd-HHmm", [Globalization.CultureInfo]::InvariantCulture)
    try {
        $outFolder = Get-OutFolder
    } catch {
        Write-Host ""
        Write-Host "Could not use the folder for the bug report:" -ForegroundColor Red
        Write-Host ("  " + $_.Exception.Message) -ForegroundColor Red
        Write-Host "You can run it again with -OutDir <a folder you can write in>." -ForegroundColor Red
        Wait-AndExit 5
    }
    $zipPath = Join-Path $outFolder ("h5-together-bug-report-" + $stamp + ".zip")
    $zipFile = $null
    $zip = $null
    try {
        $zipFile = New-Object System.IO.FileStream($zipPath, [System.IO.FileMode]::Create)
        $zip = New-Object System.IO.Compression.ZipArchive($zipFile, [System.IO.Compression.ZipArchiveMode]::Create)
        foreach ($item in $pack) {
            $entry = $zip.CreateEntry($item[0], [System.IO.Compression.CompressionLevel]::Optimal)
            # shared read: the game may be running and writing its log
            $in = [System.IO.File]::Open($item[1], 'Open', 'Read', 'ReadWrite')
            try {
                $out = $entry.Open()
                try { $in.CopyTo($out) } finally { $out.Dispose() }
            } finally { $in.Dispose() }
        }
        $entry = $zip.CreateEntry("system.txt", [System.IO.Compression.CompressionLevel]::Optimal)
        $out = $entry.Open()
        try {
            $bytes = (New-Object System.Text.UTF8Encoding($true)).GetPreamble() + (New-Object System.Text.UTF8Encoding($false)).GetBytes($systemText)
            $out.Write($bytes, 0, $bytes.Length)
        } finally { $out.Dispose() }
        $zip.Dispose()
        $zip = $null
    } catch {
        $problem = $_.Exception.Message
        if ($zip) { try { $zip.Dispose() } catch { } } elseif ($zipFile) { try { $zipFile.Dispose() } catch { } }
        try { Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue } catch { }
        Write-Host ""
        Write-Host "Could not write the bug report:" -ForegroundColor Red
        Write-Host ("  " + $problem) -ForegroundColor Red
        Write-Host "You can run it again with -OutDir <folder>, or just attach h5-together.log from the bin folder." -ForegroundColor Red
        Wait-AndExit 5
    }

    Write-Host ""
    Write-Host "Done. The bug report is this file:" -ForegroundColor Green
    Write-Host "  $zipPath"
    Write-Host ("  ({0}; it holds: {1}, system.txt)" -f (Format-Size (Get-Item -LiteralPath $zipPath).Length), (($pack | ForEach-Object { $_[0] }) -join ", "))
    if (-not $hasLog) {
        Write-Host ""
        Write-Host "There is no h5-together.log in the game folder, so the report only tells about your installation." -ForegroundColor Yellow
        Write-Host "(Either the add-on is not installed here, or the game was not started since.)" -ForegroundColor Yellow
    }
    if ($crashText) {
        Write-Host ""
        if ($crashDump -and -not $NoDump) {
            Write-Host ("It holds the newest crash report too: " + $crashText + " and " + $crashDump + ".")
            Write-Host "The .dmp holds part of the game's memory at the crash (player names, folder names)."
            Write-Host "To leave it out, run this again with -NoDump."
        } else {
            Write-Host ("It holds the newest crash report too: " + $crashText + ".")
        }
    }
    Write-Host ""
    Write-Host "Attach that file to your report on GitHub: https://github.com/jozrcr/h5-together/issues/new/choose"
    Write-Host "Nothing was sent anywhere. The log lists player names and LAN addresses; the zip is a normal"
    Write-Host "zip file, so you can open it and look before you send it."
    if (-not $NoPause) {
        try { Start-Process -FilePath "explorer.exe" -ArgumentList ("/select,`"" + $zipPath + "`"") } catch { }
    }

    Wait-AndExit 0
} catch {
    Write-Host ""
    Write-Host "Something unexpected went wrong:" -ForegroundColor Red
    Write-Host ("  " + $_.Exception.Message) -ForegroundColor Red
    Write-Host "You can send this text to the project page (see README.txt)." -ForegroundColor Red
    Wait-AndExit 6
}
