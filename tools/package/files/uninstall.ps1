# H5 Together uninstaller for Windows. Runs on Windows PowerShell 5.1.
#
# Removes only what install.ps1 put in the game's bin folder:
#   dinput8.dll (only if it is ours), h5-together.log (and h5-together.old.log), h5-together.ini (after asking),
#   and the random map templates of this package (the OH-*.xdb files whose names are in
#   this package's h5-together\templates folder), the language files of this package
#   (h5-together\texts, the same names as in the package), Valve's steam_api.dll in
#   h5-together (only if this package has one and the file there is byte for byte the
#   same; another copy stays), then the folders if they are empty.
# The same files under the add-on's old name (open-homm5, until 0.2.7) go too.
# Nothing else is touched: a template or a translation of your own stays.
#
# Keep this file plain ASCII (see install.ps1 for why).
#
# It finds the game the way install.ps1 does (Steam, GOG, MMH5.5's own entry in Windows; with
# none or several it asks), shows what it found, and asks you to close the game if it runs.
# Options, for tests and odd setups:
#   -GameDir <folder>   use this game folder (or its bin folder), skip the search
#   -SteamDir <folder>  look in this Steam folder instead of the one in the registry
#   -NoPause            do not wait for Enter at the end
#   -TestRegistry <key>   look for GOG and MMH5.5's installer in this registry key (laid out
#                         like HKLM:\SOFTWARE) instead of in HKLM; tests only
#   -TestPicker <folder>  B answers this folder, or "cancel", instead of opening the window
#   -TestKnown55u "<sha256>=<name>;..."  count these 55u.dll files as known versions
#
# Exit codes: 0 done, 4 game not found or cancelled, 5 could not delete a file,
# 6 unexpected error, 8 the game is running and was not closed.
param(
    [string]$GameDir = "",
    [string]$SteamDir = "",
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

try {
    Write-Host ""
    Write-Host "H5 Together uninstaller"
    Write-Host "-----------------------"

    $bin = Select-GameBin
    if (-not $bin) {
        Write-Host ""
        Write-Host "No game folder, so nothing was removed."
        Wait-AndExit 4
    }
    Write-GameInfo (Get-GameInfo $bin) -NoWarnings
    # The game must be closed: Windows will not let a loaded DLL be deleted.
    if (-not (Confirm-GameClosed $bin)) {
        Write-Host ""
        Write-Host "The game is still running, so nothing was removed. Close it and run uninstall.bat again." -ForegroundColor Red
        Wait-AndExit 8
    }
    Write-Host ""
    Write-Host "Removing ..."

    $failed = $false

    $dll = Join-Path $bin "dinput8.dll"
    if (-not (Test-Path -LiteralPath $dll)) {
        Write-Host "  - no dinput8.dll here, nothing to remove"
    } elseif (-not (Test-IsOurs $dll)) {
        Write-Host "  - dinput8.dll is NOT the H5 Together one, so I left it alone" -ForegroundColor Yellow
    } else {
        try {
            Remove-Item -LiteralPath $dll -Force
            Write-Host "  - removed dinput8.dll"
        } catch {
            Write-Host ("  - could not remove dinput8.dll: " + $_.Exception.Message) -ForegroundColor Red
            Write-Host "    If the game is running, close it and run this again." -ForegroundColor Red
            $failed = $true
        }
    }

    # The logs, under the add-on's name and its old one (open-homm5, until 0.2.7). The log is
    # kept to 5 MB: when it got bigger, the game kept the last full one as h5-together.old.log.
    # (and h5-together-resume.txt: which save quick resume offers to a player who lacks it)
    $logs = @("h5-together.log", "h5-together.old.log", "open-homm5.log", "open-homm5.old.log", "h5-together-resume.txt")
    if (-not (Test-Path -LiteralPath (Join-Path $bin "h5-together.log")) -and -not (Test-Path -LiteralPath (Join-Path $bin "open-homm5.log"))) {
        Write-Host "  - no h5-together.log here"
    }
    foreach ($name in $logs) {
        $log = Join-Path $bin $name
        if (-not (Test-Path -LiteralPath $log)) { continue }
        try {
            Remove-Item -LiteralPath $log -Force
            Write-Host "  - removed $name"
        } catch {
            Write-Host ("  - could not remove " + $name + ": " + $_.Exception.Message) -ForegroundColor Red
            $failed = $true
        }
    }

    # The settings file, and the copies of it that the installer keeps when it adds new
    # settings (.bak, .bak2, .bak3), under both names. One question for all of them.
    $inis = @(@("h5-together.ini", "open-homm5.ini") | Where-Object { Test-Path -LiteralPath (Join-Path $bin $_) -PathType Leaf })
    $backups = @()
    foreach ($base in @("h5-together.ini", "open-homm5.ini")) {
        $backups += @(@(".bak", ".bak2", ".bak3") | ForEach-Object { Join-Path $bin ($base + $_) } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    }
    if ($inis.Count -eq 0 -and $backups.Count -eq 0) {
        Write-Host "  - no h5-together.ini here"
    } else {
        $iniText = $inis -join " and "
        if ($inis.Count -eq 0) {
            $copied = (Split-Path -Leaf $backups[0]) -replace '\.bak\d?$', ''
            $question = "Also delete the old copies of your settings ($copied.bak)? [y/N]"; $what = "the old copies of $copied"
        }
        elseif ($inis.Count -gt 1) { $question = "Also delete your settings files $iniText, and their old copies (.bak)? [y/N]"; $what = "$iniText and their old copies" }
        elseif ($backups.Count -gt 0) { $question = "Also delete your settings file $iniText and its old copies (.bak)? [y/N]"; $what = "$iniText and its old copies" }
        else { $question = "Also delete your settings file $iniText? [y/N]"; $what = $iniText }
        $answer = $null
        try { $answer = Read-Host $question } catch { }
        if ($answer -and $answer.Trim() -match '^(y|yes|o|oui)$') {
            $files = @($inis | ForEach-Object { Join-Path $bin $_ }) + $backups
            foreach ($f in $files) {
                $name = Split-Path -Leaf $f
                try {
                    Remove-Item -LiteralPath $f -Force
                    Write-Host "  - removed $name"
                } catch {
                    Write-Host ("  - could not remove " + $name + ": " + $_.Exception.Message) -ForegroundColor Red
                    $failed = $true
                }
            }
        } else {
            Write-Host "  - kept $what"
        }
    }

    # The original game's 4 GB exe that the installer made (install.ps1, Install-Original4Gb):
    # only that name, and only when it is the 2008 game's exe.
    $orig4 = Join-Path $bin "H5_Original_4GB.exe"
    if ((Test-Path -LiteralPath $orig4 -PathType Leaf) -and (Test-Exe2008 $orig4)) {
        try {
            Remove-Item -LiteralPath $orig4 -Force
            Write-Host "  - removed H5_Original_4GB.exe (the original game's 4 GB exe that the installer made)"
        } catch {
            Write-Host ("  - could not remove H5_Original_4GB.exe: " + $_.Exception.Message) -ForegroundColor Red
            $failed = $true
        }
    }

    # Valve's steam_api.dll (online play over Steam): only if this package has it, and only
    # if the file in the game is byte for byte that one (the same SHA-256). Another copy
    # (another version, put there by hand) stays. A package without it leaves it alone.
    # Before the language files and the templates, so that the folder h5-together can go
    # once they are gone too.
    $ourDir = Join-Path $bin "h5-together"
    $srcSteam = Join-Path $PSScriptRoot "h5-together\steam_api.dll"
    $dstSteam = Join-Path $ourDir "steam_api.dll"
    if ((Test-Path -LiteralPath $srcSteam -PathType Leaf) -and (Test-Path -LiteralPath $dstSteam -PathType Leaf)) {
        $hash = Get-Sha256Hex $dstSteam
        if ($hash -match '^[0-9a-f]{64}$' -and $hash -eq (Get-Sha256Hex $srcSteam)) {
            try {
                Remove-Item -LiteralPath $dstSteam -Force
                Write-Host "  - removed steam_api.dll (Valve's file for online play over Steam)"
                if (@(Get-ChildItem -LiteralPath $ourDir -Force).Count -eq 0) { Remove-Item -LiteralPath $ourDir -Force }
            } catch {
                Write-Host ("  - could not remove steam_api.dll: " + $_.Exception.Message) -ForegroundColor Red
                $failed = $true
            }
        } else {
            Write-Host "  - kept h5-together\steam_api.dll: it is not the copy this package installs (another version?), so I left it alone" -ForegroundColor Yellow
        }
    }

    # The language files: only the names this package holds; a translation of the
    # player's own (another name) stays. Before the templates, so that the folder
    # h5-together can go once both are empty.
    $srcTexts = Join-Path $PSScriptRoot "h5-together\texts"
    $txtNames = @()
    if (Test-Path -LiteralPath $srcTexts) {
        $txtNames = @(Get-ChildItem -LiteralPath $srcTexts -File | Where-Object { $_.Extension -ieq ".txt" } | ForEach-Object { $_.Name })
    }
    $ourDir = Join-Path $bin "h5-together"
    $dstTexts = Join-Path $ourDir "texts"
    if (Test-Path -LiteralPath $dstTexts) {
        if ($txtNames.Count -eq 0) {
            Write-Host "  - this folder has no list of our language files, so I left h5-together\texts alone" -ForegroundColor Yellow
        } else {
            $removed = 0
            foreach ($n in $txtNames) {
                $p = Join-Path $dstTexts $n
                if (Test-Path -LiteralPath $p -PathType Leaf) {
                    try {
                        Remove-Item -LiteralPath $p -Force
                        $removed++
                    } catch {
                        Write-Host ("  - could not remove " + $n + ": " + $_.Exception.Message) -ForegroundColor Red
                        $failed = $true
                    }
                }
            }
            Write-Host ("  - removed {0} language files (h5-together\texts)" -f $removed)
            $left = @(Get-ChildItem -LiteralPath $dstTexts -Force)
            if ($left.Count -gt 0) {
                Write-Host ("  - kept {0} other file(s) in h5-together\texts (not ours)" -f $left.Count)
            } else {
                try {
                    Remove-Item -LiteralPath $dstTexts -Force
                    if (@(Get-ChildItem -LiteralPath $ourDir -Force).Count -eq 0) { Remove-Item -LiteralPath $ourDir -Force }
                    Write-Host "  - removed the empty folder h5-together\texts"
                } catch {
                    Write-Host ("  - could not remove the empty folder: " + $_.Exception.Message) -ForegroundColor Red
                    $failed = $true
                }
            }
        }
    }

    # The random map templates: only the names this package holds, never another file. In
    # our folder, and in its old name (open-homm5, until 0.2.7) if that is still there.
    $srcTemplates = Join-Path $PSScriptRoot "h5-together\templates"
    $tplNames = @()
    if (Test-Path -LiteralPath $srcTemplates) {
        $tplNames = @(Get-ChildItem -LiteralPath $srcTemplates -File | Where-Object { $_.Extension -ieq ".xdb" } | ForEach-Object { $_.Name })
    }
    $anyTemplates = $false
    foreach ($folder in @("h5-together", "open-homm5")) {
        $dstDir = Join-Path $bin $folder
        $dstTemplates = Join-Path $dstDir "templates"
        if (-not (Test-Path -LiteralPath $dstTemplates)) { continue }
        $anyTemplates = $true
        $from = ""
        if ($folder -ne "h5-together") { $from = " from $folder\templates (the folder's old name)" }
        if ($tplNames.Count -eq 0) {
            Write-Host "  - this folder has no list of our templates, so I left $folder\templates alone" -ForegroundColor Yellow
            continue
        }
        $removed = 0
        foreach ($n in $tplNames) {
            $p = Join-Path $dstTemplates $n
            if (Test-Path -LiteralPath $p -PathType Leaf) {
                try {
                    Remove-Item -LiteralPath $p -Force
                    $removed++
                } catch {
                    Write-Host ("  - could not remove " + $n + ": " + $_.Exception.Message) -ForegroundColor Red
                    $failed = $true
                }
            }
        }
        Write-Host ("  - removed {0} random map templates{1}" -f $removed, $from)
        $left = @(Get-ChildItem -LiteralPath $dstTemplates -Force)
        if ($left.Count -gt 0) {
            Write-Host ("  - kept {0} other file(s) in {1}\templates (not ours)" -f $left.Count, $folder)
        } else {
            try {
                Remove-Item -LiteralPath $dstTemplates -Force
                if (@(Get-ChildItem -LiteralPath $dstDir -Force).Count -eq 0) { Remove-Item -LiteralPath $dstDir -Force }
                Write-Host "  - removed the empty folder $folder\templates"
            } catch {
                Write-Host ("  - could not remove the empty folder: " + $_.Exception.Message) -ForegroundColor Red
                $failed = $true
            }
        }
    }
    if (-not $anyTemplates) { Write-Host "  - no random map templates here" }

    Write-Host ""
    if ($failed) {
        Write-Host "Some files could not be removed (see above)." -ForegroundColor Red
        Wait-AndExit 5
    }
    Write-Host "Done: H5 Together is removed. No other file was touched." -ForegroundColor Green
    if (Test-Path -LiteralPath (Join-Path $bin "dinput8_next.dll") -PathType Leaf) {
        Write-Host ""
        Write-Host "dinput8_next.dll is still there: another add-on that H5 Together loaded. To keep using"
        Write-Host "it without H5 Together, rename it back to dinput8.dll."
    }
    Write-Host "To put it back later, run install.bat again."
    Wait-AndExit 0
} catch {
    Write-Host ""
    Write-Host "Something unexpected went wrong:" -ForegroundColor Red
    Write-Host ("  " + $_.Exception.Message) -ForegroundColor Red
    Wait-AndExit 6
}
