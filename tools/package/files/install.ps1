# H5 Together installer for Windows. Runs on Windows PowerShell 5.1.
#
# Keep this file plain ASCII: PowerShell 5.1 reads a file without a byte-order
# mark as ANSI, so any accent would be garbled on a French Windows.
#
# Normal use: double-click install.bat. It looks for the game (Steam, GOG, MMH5.5's own
# entry in Windows), shows what it found (the folder, the language, the MMH5.5 version),
# asks you to close the game if it runs, and copies the add-on in. With no game found, or
# several, it asks: a number, B for a window to choose the folder in, or a pasted folder.
# Options, for tests and odd setups:
#   -GameDir <folder>   use this game folder (or its bin folder), skip the search
#   -SteamDir <folder>  look in this Steam folder instead of the one in the registry
#   -NoPause            do not wait for Enter at the end
#   -TestRegistry <key>   look for GOG and MMH5.5's installer in this registry key (laid out
#                         like HKLM:\SOFTWARE) instead of in HKLM; tests only
#   -TestPicker <folder>  B answers this folder, or "cancel", instead of opening the window
#   -TestKnown55u "<sha256>=<name>;..."  count these 55u.dll files as known versions
#
# The add-on's language files (h5-together\texts\*.txt: its panels' words, English and
# French) go to bin\h5-together\texts: ours are overwritten, any other file (a translation
# of the player's own) stays.
#
# If the package has Valve's steam_api.dll (h5-together\steam_api.dll: online play over
# Steam, off until the player turns it on), it goes to bin\h5-together\steam_api.dll, over
# any copy there (that folder is ours). A package without it changes nothing there.
#
# An existing h5-together.ini is refreshed: it becomes this package's file with the
# player's values in it (the old one is kept as h5-together.ini.bak; see Merge-Settings).
# The files of the add-on's old name (open-homm5, until 0.2.7) get the new name first,
# the folder bin\open-homm5 included (see Move-OldNames).
#
# Exit codes: 0 done, 3 refused (a dinput8.dll that is not ours is there),
# 4 game not found or cancelled, 5 copy failed, 6 unexpected error, 7 package incomplete,
# 8 the game is running and was not closed.
param(
    [string]$GameDir = "",
    [string]$SteamDir = "",
    [switch]$NoPause,
    # MMH5.5's installer file (MMH55_RC19k.exe...), to make the original game's 4 GB exe from
    # (else it is looked for in this folder, Downloads and the Desktop).
    [string]$Mmh55Installer = "",
    # Never offer to fetch the original game's exe from MMH5.5's release on GitHub (also the
    # environment variable H5_TOGETHER_NO_DOWNLOAD=1).
    [switch]$NoDownload,
    # Tests only (see above).
    [string]$TestRegistry = "",
    [string]$TestPicker = "",
    [string]$TestKnown55u = "",
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

# ---- the original 2008 game with 4 GB of memory: bin\H5_Original_4GB.exe ----
# The original game's exe can use only 2 GB of memory, too little for big maps at a high
# resolution (the game crashes when it runs out); MMH5.5's exes are marked to use 4 GB. Steam's
# copy of the 2008 exe (H5_GameOLD.exe after the GameRanger guide) is protected by Steam and
# refuses any change. MMH5.5's installer carries the 2008 exe without Steam's protection, stored
# as it is: it is copied out of that file (nothing is run or installed), its header gets the
# 4 GB mark, and it is written as bin\H5_Original_4GB.exe. A copy of the game's own exe without
# Steam's protection (GOG, or the H5_Game.exe that MMH5.5 RC19k leaves) is used the same way.
$Original4GbName = "H5_Original_4GB.exe"

# The file length of the 2008 game's exe without Steam's protection whose header starts this
# byte array (at least 1 KB), else 0. (The 2008 date, 32-bit, the unprotected image size: Steam's
# protected one is 0x00E2D000.)
function Get-Exe2008Length([byte[]]$Head) {
    if ($Head.Length -lt 0x400 -or $Head[0] -ne 0x4D -or $Head[1] -ne 0x5A) { return 0 }
    $pe = [BitConverter]::ToInt32($Head, 0x3C)
    if ($pe -lt 0x40 -or $pe -gt 0x300) { return 0 }
    if ([BitConverter]::ToUInt32($Head, $pe) -ne 0x4550 -or [BitConverter]::ToUInt16($Head, $pe + 4) -ne 0x14C) { return 0 }
    if ([BitConverter]::ToUInt32($Head, $pe + 8) -ne $PeDate2008) { return 0 }
    if ([BitConverter]::ToUInt32($Head, $pe + 24 + 56) -ne 0x00DD9000) { return 0 }
    $sections = [BitConverter]::ToUInt16($Head, $pe + 6)
    $table = $pe + 24 + [BitConverter]::ToUInt16($Head, $pe + 20)
    if ($sections -lt 1 -or $table + 40 * $sections -gt $Head.Length) { return 0 }
    $end = [long]0
    for ($i = 0; $i -lt $sections; $i++) {
        $at = $table + 40 * $i
        $e = [long][BitConverter]::ToUInt32($Head, $at + 20) + [long][BitConverter]::ToUInt32($Head, $at + 16)
        if ($e -gt $end) { $end = $e }
    }
    if ($end -lt 0x400 -or $end -gt 64MB) { return 0 }
    return [int]$end
}

# These bytes are the 2008 game's own exe (it loads um.dll, not MMH5.5's 55u.dll).
function Test-Exe2008Bytes([byte[]]$Exe) {
    $head = New-Object byte[] ([Math]::Min(0x400, $Exe.Length))
    [Array]::Copy($Exe, $head, $head.Length)
    if ((Get-Exe2008Length $head) -ne $Exe.Length) { return $false }
    $text = $Latin1.GetString($Exe)
    return ($text.Contains("um.dll") -and -not $text.Contains("55u.dll"))
}

# The 2008 exe inside a bigger file (MMH5.5's installer, whose files are stored as they are):
# its bytes, else $null. Looks at every "MZ" in the file, 64 MB at a time.
function Find-Exe2008In([string]$Path) {
    $chunk = 64MB
    $fs = [System.IO.File]::OpenRead($Path)
    try {
        $buf = New-Object byte[] ($chunk + 0x400)
        $pos = [long]0
        while ($pos -lt $fs.Length) {
            $fs.Position = $pos
            $n = 0
            while ($n -lt $buf.Length) { $r = $fs.Read($buf, $n, $buf.Length - $n); if ($r -le 0) { break }; $n += $r }
            if ($n -le 0) { break }
            $text = $Latin1.GetString($buf, 0, $n)
            $i = $text.IndexOf("MZ", [StringComparison]::Ordinal)
            while ($i -ge 0 -and $i -lt $chunk -and $i + 0x400 -le $n) {
                $head = New-Object byte[] 0x400
                [Array]::Copy($buf, $i, $head, 0, 0x400)
                $len = Get-Exe2008Length $head
                if ($len -gt 0 -and $pos + $i + $len -le $fs.Length) {
                    $exe = New-Object byte[] $len
                    $fs.Position = $pos + $i
                    $got = 0
                    while ($got -lt $len) { $r = $fs.Read($exe, $got, $len - $got); if ($r -le 0) { break }; $got += $r }
                    if ($got -eq $len -and (Test-Exe2008Bytes $exe)) { return ,$exe }
                }
                $i = $text.IndexOf("MZ", $i + 2, [StringComparison]::Ordinal)
            }
            $pos += $chunk
        }
    } finally { $fs.Close() }
    return $null
}

# MMH5.5's own release on GitHub holds the same 2008 exe in a zip (manual_setup_RC19k.zip,
# 820 MB): only its piece is fetched (an HTTP range: about 5.4 MB), unpacked, and checked
# against the 2008 exe's SHA-256 before it is used. Read from the zip's table of contents on
# 2026-11-06 (tools/package/README.md). If MMH5.5 changes that file, the check fails and the
# installer says so: the player then uses MMH5.5's installer file as before.
$Mmh55ZipUrl = "https://github.com/Might-Magic-Heroes-5-5/MMH55/releases/download/RC19k/manual_setup_RC19k.zip"
$Mmh55ZipExe = @{ Offset = [long]2988923; Packed = 5401151; Size = 13089792
                  Sha256 = "6502a8c4cd0e03e9ce43b474bcb4a85acb8b8bf5050d56317e93c8f13fff50bd" }

# The 2008 exe from MMH5.5's release on GitHub: its bytes (checked), else an exception.
function Get-Exe2008FromGitHub {
    try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch { }
    $from = $Mmh55ZipExe.Offset
    $req = [System.Net.HttpWebRequest]::Create($Mmh55ZipUrl)
    $req.AddRange($from, $from + 30 + 1024 + $Mmh55ZipExe.Packed)
    $req.UserAgent = "H5 Together installer"
    $req.Timeout = 60000
    $req.ReadWriteTimeout = 120000
    $resp = $req.GetResponse()
    try {
        $buf = New-Object System.IO.MemoryStream
        $resp.GetResponseStream().CopyTo($buf)
        $data = $buf.ToArray()
    } finally { $resp.Close() }
    if ($data.Length -lt 30 -or [BitConverter]::ToUInt32($data, 0) -ne 0x04034B50) { throw "the downloaded piece is not the zip's file (MMH5.5 may have changed its release)" }
    $n = [BitConverter]::ToUInt16($data, 26)
    $start = 30 + $n + [BitConverter]::ToUInt16($data, 28)
    if ($Latin1.GetString($data, 30, $n) -ne "bin/H5_Game.exe" -or [BitConverter]::ToUInt16($data, 8) -ne 8 -or
        $start + $Mmh55ZipExe.Packed -gt $data.Length) { throw "the downloaded piece is not the 2008 exe (MMH5.5 may have changed its release)" }
    $in = New-Object System.IO.MemoryStream ($data, $start, $Mmh55ZipExe.Packed)
    $inflate = New-Object System.IO.Compression.DeflateStream ($in, [System.IO.Compression.CompressionMode]::Decompress)
    $out = New-Object System.IO.MemoryStream
    $inflate.CopyTo($out)
    $exe = $out.ToArray()
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $hex = -join ($sha.ComputeHash($exe) | ForEach-Object { $_.ToString("x2") })
    if ($exe.Length -ne $Mmh55ZipExe.Size -or $hex -ne $Mmh55ZipExe.Sha256) { throw "the downloaded exe is not the expected 2008 exe (its SHA-256 differs)" }
    return ,$exe
}

# MMH5.5's installer files (MMH55*.exe, over 100 MB) in this package's folder, Downloads, the Desktop.
function Find-Mmh55Installers {
    $places = @($PSScriptRoot, (Split-Path -Parent $PSScriptRoot))
    if ($env:USERPROFILE) { $places += @((Join-Path $env:USERPROFILE "Downloads"), (Join-Path $env:USERPROFILE "Desktop")) }
    try { $places += [Environment]::GetFolderPath("Desktop") } catch { }
    $found = @()
    foreach ($place in ($places | Select-Object -Unique)) {
        if (-not $place -or -not (Test-Path -LiteralPath $place -PathType Container)) { continue }
        $found += @(Get-ChildItem -LiteralPath $place -Filter "MMH55*.exe" -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Length -gt 100MB })
    }
    return @($found | Sort-Object LastWriteTime -Descending | ForEach-Object { $_.FullName } | Select-Object -Unique)
}

# Makes bin\H5_Original_4GB.exe if it can. A hashtable: State (kept, made, none, failed), From, Why.
function Install-Original4Gb([string]$Bin, [string]$Given) {
    $dst = Join-Path $Bin $Original4GbName
    if (Test-Path -LiteralPath $dst -PathType Leaf) { return @{ State = "kept"; From = ""; Why = "" } }
    $exe = $null
    $from = ""
    # a copy of the game's own 2008 exe without Steam's protection, already in the game folder
    foreach ($name in @("H5_Game.exe", "H5_GameOLD.exe")) {
        $p = Join-Path $Bin $name
        if ($null -eq $exe -and (Test-Path -LiteralPath $p -PathType Leaf) -and (Get-Item -LiteralPath $p).Length -le 64MB) {
            $bytes = [System.IO.File]::ReadAllBytes($p)
            if (Test-Exe2008Bytes $bytes) { $exe = $bytes; $from = $name }
        }
    }
    # else MMH5.5's installer file
    $sources = @()
    if ($Given) { $sources += $Given } else { $sources += @(Find-Mmh55Installers) }
    foreach ($src in $sources) {
        if ($null -ne $exe) { break }
        if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { return @{ State = "failed"; From = $src; Why = "the file was not found" } }
        Write-Host ("Looking for the original game's exe in {0} ..." -f (Split-Path -Leaf $src))
        try { $exe = Find-Exe2008In $src } catch { return @{ State = "failed"; From = $src; Why = $_.Exception.Message } }
        if ($null -ne $exe) { $from = Split-Path -Leaf $src }
    }
    if ($null -eq $exe) {
        if ($sources.Count -gt 0) { return @{ State = "failed"; From = ($sources -join ", "); Why = "the original game's exe is not in it" } }
        # else, with the player's yes: MMH5.5's release on GitHub (about 5.4 MB)
        if ($NoDownload -or $env:H5_TOGETHER_NO_DOWNLOAD) { return @{ State = "none"; From = ""; Why = "" } }
        Write-Host ""
        Write-Host "The original 2008 game (without MMH5.5) with 4 GB of memory: I can make bin\$Original4GbName."
        Write-Host "Its exe comes from MMH5.5's own release on GitHub (only that file: about 5.4 MB; MMH5.5"
        Write-Host "itself is not installed). Skip it if you only play MMH5.5."
        $answer = $null
        try { $answer = Read-Host "Fetch it? Y = yes, N = no" } catch { }
        if ($null -eq $answer -or ($answer.Trim() -ne "" -and $answer.Trim().ToLowerInvariant() -notin @("y", "yes", "o", "oui"))) {
            return @{ State = "none"; From = ""; Why = "" }
        }
        Write-Host "Fetching the original game's exe from MMH5.5's release on GitHub ..."
        try { $exe = Get-Exe2008FromGitHub } catch { return @{ State = "failed"; From = "MMH5.5's release on GitHub"; Why = $_.Exception.Message } }
        $from = "MMH5.5's release on GitHub, checked"
    }
    $pe = [BitConverter]::ToInt32($exe, 0x3C)
    $exe[$pe + 22] = $exe[$pe + 22] -bor 0x20  # IMAGE_FILE_LARGE_ADDRESS_AWARE: up to 4 GB
    try { [System.IO.File]::WriteAllBytes($dst, $exe) } catch { return @{ State = "failed"; From = $from; Why = $_.Exception.Message } }
    return @{ State = "made"; From = $from; Why = "" }
}

# ---- the settings file: refresh it from this package's h5-together.ini ----
# install.sh does exactly the same (merge_ini); test_scripts.py checks that both give
# the same bytes. The rules:
# - the result is this package's h5-together.ini (every setting, commented out), with
#   its line endings;
# - each "key=value" line of the player's file fills the line ";key=..." of the same
#   [section] of the new file: it becomes "key=value". Names ignore upper/lower case
#   (as the game's ini reader does); spaces around the key and the value go;
# - the player's other lines with content (a setting this version does not list, a
#   second copy of one) go, as they were, at the end of their section under
#   "; kept from your previous file:". A section the new file lacks goes at the end;
# - the player's own comments and blank lines are not carried over (the backup,
#   h5-together.ini.bak, has them).
# Files are read and written one byte = one character (Latin-1), so nothing is
# re-encoded: an ASCII file stays ASCII. ($Latin1 is in the shared block.)
$IniBlank = [char[]]@([char]32, [char]9)
$KeptHeading = "; kept from your previous file:"

# Lower case for A to Z only (as awk does in install.sh).
function Get-AsciiLower([string]$Text) {
    $a = $Text.ToCharArray()
    for ($i = 0; $i -lt $a.Length; $i++) {
        $c = [int]$a[$i]
        if ($c -ge 65 -and $c -le 90) { $a[$i] = [char]($c + 32) }
    }
    return (-join $a)
}

function New-OrdinalTable { return (New-Object System.Collections.Hashtable ([StringComparer]::Ordinal)) }

# The lines of a file without their line ends: a UTF-8 byte-order mark at the start
# goes, and one CR before each LF. Crlf: some line ended with CR LF.
function Read-IniLines([byte[]]$Bytes) {
    $start = 0
    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) { $start = 3 }
    $text = $Latin1.GetString($Bytes, $start, $Bytes.Length - $start)
    $lines = New-Object System.Collections.Generic.List[string]
    $crlf = $false
    if ($text.Length -gt 0) {
        $parts = $text.Split([char]10)
        $n = $parts.Length
        if ($text[$text.Length - 1] -eq [char]10) { $n-- }
        for ($i = 0; $i -lt $n; $i++) {
            $l = $parts[$i]
            if ($l.EndsWith([string][char]13)) { $l = $l.Substring(0, $l.Length - 1); $crlf = $true }
            $lines.Add($l)
        }
    }
    return @{ Lines = $lines; Crlf = $crlf }
}

# What one line is. Kind: blank, section (Name), comment (Key if it reads ";key=..."),
# setting (Key, Value) or other. Text: the line without spaces at its ends.
function Get-IniLine([string]$Line) {
    $t = $Line.Trim($IniBlank)
    $r = @{ Kind = "other"; Text = $t; Name = ""; Key = ""; Value = "" }
    if ($t.Length -eq 0) { $r.Kind = "blank"; return $r }
    $c = $t[0]
    $p = $t.IndexOf([char]']')
    if ($c -eq [char]'[' -and $p -gt 0) {
        $r.Kind = "section"
        $r.Name = $t.Substring(1, $p - 1).Trim($IniBlank)
        return $r
    }
    if ($c -eq [char]';' -or $c -eq [char]'#') {
        $r.Kind = "comment"
        if ($t -cmatch '^;([A-Za-z0-9_]+)=') { $r.Key = $Matches[1] }
        return $r
    }
    $p = $t.IndexOf([char]'=')
    if ($p -gt 0) {
        $r.Kind = "setting"
        $r.Key = $t.Substring(0, $p).Trim($IniBlank)
        $r.Value = $t.Substring($p + 1).Trim($IniBlank)
    }
    return $r
}

function Add-KeptLines($Out, [string]$Heading, $Lines, [string]$Eol) {
    if ($null -eq $Lines -or $Lines.Count -eq 0) { return }
    [void]$Out.Append($Eol)
    if ($Heading) { [void]$Out.Append($Heading).Append($Eol) }
    [void]$Out.Append($KeptHeading).Append($Eol)
    foreach ($l in $Lines) { [void]$Out.Append($l).Append($Eol) }
}

# The refreshed settings file (Bytes) from the player's file and this package's.
# Settings: the player's "key=value" lines; New: settings of the package's file that
# the player's file does not mention; Unknown: player's settings the package's file
# does not list. $null if the player's file is not plain text (it holds zero bytes,
# for example saved as "Unicode" by Notepad): then it is left as it is.
function Merge-Settings([byte[]]$PlayerBytes, [byte[]]$PackageBytes) {
    # (PowerShell names ignore upper/lower case: $old here is not $Old)
    if ([Array]::IndexOf($PlayerBytes, [byte]0) -ge 0) { return $null }
    $tpl = Read-IniLines $PackageBytes
    $old = Read-IniLines $PlayerBytes
    $eol = "`n"
    if ($tpl.Crlf) { $eol = "`r`n" }

    # The package's file: each line's section, the settings, each section's last line.
    $lineSec = New-Object System.Collections.Generic.List[string]
    $slotKey = @{}
    $slots = New-OrdinalTable
    $known = New-OrdinalTable
    $lastLine = New-OrdinalTable
    $sec = ""
    $known[""] = $true
    for ($i = 0; $i -lt $tpl.Lines.Count; $i++) {
        $p = Get-IniLine $tpl.Lines[$i]
        if ($p.Kind -eq "section") { $sec = Get-AsciiLower $p.Name; $known[$sec] = $true }
        $lineSec.Add($sec)
        if ($p.Kind -ne "blank") { $lastLine[$sec] = $i }
        if (($p.Kind -eq "comment" -and $p.Key -ne "") -or ($p.Kind -eq "setting" -and $p.Key -cmatch '^[A-Za-z0-9_]+$')) {
            $k = $sec + "`n" + (Get-AsciiLower $p.Key)
            if (-not $slots.ContainsKey($k)) { $slots[$k] = $true; $slotKey[$i] = $p.Key }
        }
    }

    # The player's file: the values, and the lines to keep as they are.
    $fill = New-OrdinalTable
    $seen = New-OrdinalTable
    $kept = New-OrdinalTable
    $heading = New-OrdinalTable
    $order = New-Object System.Collections.Generic.List[string]
    $settings = 0
    $unknown = 0
    $sec = ""
    foreach ($line in $old.Lines) {
        $p = Get-IniLine $line
        if ($p.Kind -eq "blank") { continue }
        if ($p.Kind -eq "section") {
            $sec = Get-AsciiLower $p.Name
            if (-not $heading.ContainsKey($sec)) { $heading[$sec] = $p.Text; $order.Add($sec) }
            continue
        }
        if ($p.Kind -eq "comment") {
            if ($p.Key -ne "") { $seen[$sec + "`n" + (Get-AsciiLower $p.Key)] = $true }
            continue
        }
        if ($p.Kind -eq "setting") {
            $settings++
            $k = $sec + "`n" + (Get-AsciiLower $p.Key)
            $seen[$k] = $true
            if ($slots.ContainsKey($k)) {
                if (-not $fill.ContainsKey($k)) { $fill[$k] = $p.Value; continue }
            } else {
                $unknown++
            }
        }
        if (-not $kept.ContainsKey($sec)) { $kept[$sec] = New-Object System.Collections.Generic.List[string] }
        $kept[$sec].Add($p.Text)
    }
    $new = 0
    foreach ($k in $slots.Keys) { if (-not $seen.ContainsKey($k)) { $new++ } }

    # The new file.
    $out = New-Object System.Text.StringBuilder
    if (-not $lastLine.ContainsKey("")) { Add-KeptLines $out $null $kept[""] $eol }
    for ($i = 0; $i -lt $tpl.Lines.Count; $i++) {
        $line = $tpl.Lines[$i]
        $s = $lineSec[$i]
        if ($slotKey.ContainsKey($i)) {
            $k = $s + "`n" + (Get-AsciiLower $slotKey[$i])
            if ($fill.ContainsKey($k)) { $line = $slotKey[$i] + "=" + $fill[$k] }
        }
        [void]$out.Append($line).Append($eol)
        if ($lastLine.ContainsKey($s) -and $lastLine[$s] -eq $i) { Add-KeptLines $out $null $kept[$s] $eol }
    }
    foreach ($s in $order) {
        if (-not $known.ContainsKey($s)) { Add-KeptLines $out $heading[$s] $kept[$s] $eol }
    }
    return @{ Bytes = $Latin1.GetBytes($out.ToString()); Settings = $settings; New = $new; Unknown = $unknown }
}

# Keeps the last three copies of the settings file: .bak (the newest), .bak2, .bak3.
function Save-IniBackup([string]$Path) {
    $b1 = $Path + ".bak"
    $b2 = $Path + ".bak2"
    $b3 = $Path + ".bak3"
    if (Test-Path -LiteralPath $b2) {
        if (Test-Path -LiteralPath $b3) { Remove-Item -LiteralPath $b3 -Force }
        Move-Item -LiteralPath $b2 -Destination $b3
    }
    if (Test-Path -LiteralPath $b1) { Move-Item -LiteralPath $b1 -Destination $b2 }
    Copy-Item -LiteralPath $Path -Destination $b1
}

# "1 new setting", "2 new settings"
function Get-Count([int]$N, [string]$One, [string]$Many) {
    if ($N -eq 1) { return "1 $One" }
    return "$N $Many"
}

# ---- the add-on's old name: until 0.2.7 it was called open-homm5 ----
# Its files in bin had that name: open-homm5.ini (and its .bak copies), open-homm5-skins.ini
# (the hero looks), open-homm5.log and open-homm5.old.log, and the folder open-homm5 (our
# templates, the French packs in lang\fr, anything the player put there). They get the new
# name, so nothing the player set or added is lost. install.sh does the same (move_old_names).
#   - a file moves only if the new name is not there yet; else it stays as it is (the new
#     one is the one used), and the summary says so;
#   - the last log becomes h5-together.old.log (the older log) while there is no log of the
#     new name yet; the other old logs are removed (ours, and older than that);
#   - the folder is renamed; if h5-together is there too, what it lacks is moved into it,
#     and our templates in the old folder go (this install puts them in the new one).

# Moves what the folder $From holds into the folder $To: what $To lacks is moved, a folder
# both have is merged the same way, a file both have stays in $From. Then $From is removed
# if it is empty. Returns how many files or folders stayed in $From.
function Merge-Folder([string]$From, [string]$To) {
    $left = 0
    foreach ($item in @(Get-ChildItem -LiteralPath $From -Force)) {
        $dst = Join-Path $To $item.Name
        if (-not (Test-Path -LiteralPath $dst)) {
            Move-Item -LiteralPath $item.FullName -Destination $dst
        } elseif ($item.PSIsContainer -and (Test-Path -LiteralPath $dst -PathType Container)) {
            $left += Merge-Folder $item.FullName $dst
        } else {
            $left++
        }
    }
    if (@(Get-ChildItem -LiteralPath $From -Force).Count -eq 0) { Remove-Item -LiteralPath $From -Force }
    return $left
}

# Gives the old files in $Bin their new names. $Templates: the names of this package's
# templates. Returns Moved (what got the new name), Left (old files left as they are) and
# FolderLeft (files of the old folder that the new one also has).
function Move-OldNames([string]$Bin, $Templates) {
    $r = @{ Moved = New-Object System.Collections.ArrayList; Left = New-Object System.Collections.ArrayList; FolderLeft = 0 }
    foreach ($ext in @(".ini", "-skins.ini")) {
        $old = Join-Path $Bin ("open-homm5" + $ext)
        $new = Join-Path $Bin ("h5-together" + $ext)
        if (-not (Test-Path -LiteralPath $old -PathType Leaf)) { continue }
        if (Test-Path -LiteralPath $new) { [void]$r.Left.Add("open-homm5" + $ext); continue }
        Move-Item -LiteralPath $old -Destination $new
        [void]$r.Moved.Add("open-homm5" + $ext)
    }
    # the installer's copies of the settings follow the settings file (not when it stayed)
    if (-not $r.Left.Contains("open-homm5.ini")) {
        foreach ($ext in @(".bak", ".bak2", ".bak3")) {
            $old = Join-Path $Bin ("open-homm5.ini" + $ext)
            $new = Join-Path $Bin ("h5-together.ini" + $ext)
            if ((Test-Path -LiteralPath $old -PathType Leaf) -and -not (Test-Path -LiteralPath $new)) { Move-Item -LiteralPath $old -Destination $new }
        }
    }
    $oldLog = Join-Path $Bin "open-homm5.log"
    if (Test-Path -LiteralPath $oldLog -PathType Leaf) {
        $older = Join-Path $Bin "h5-together.old.log"
        if (-not (Test-Path -LiteralPath (Join-Path $Bin "h5-together.log")) -and -not (Test-Path -LiteralPath $older)) {
            Move-Item -LiteralPath $oldLog -Destination $older
            [void]$r.Moved.Add("open-homm5.log")
        } else {
            Remove-Item -LiteralPath $oldLog -Force
        }
    }
    $oldOldLog = Join-Path $Bin "open-homm5.old.log"
    if (Test-Path -LiteralPath $oldOldLog -PathType Leaf) { Remove-Item -LiteralPath $oldOldLog -Force }
    $oldDir = Join-Path $Bin "open-homm5"
    $newDir = Join-Path $Bin "h5-together"
    if (Test-Path -LiteralPath $oldDir -PathType Container) {
        if (-not (Test-Path -LiteralPath $newDir)) {
            Move-Item -LiteralPath $oldDir -Destination $newDir
        } else {
            foreach ($name in $Templates) {
                $p = Join-Path $oldDir ("templates\" + $name)
                if (Test-Path -LiteralPath $p -PathType Leaf) { Remove-Item -LiteralPath $p -Force }
            }
            $r.FolderLeft = Merge-Folder $oldDir $newDir
        }
        [void]$r.Moved.Add("open-homm5")
    }
    return $r
}

try {
    Write-Host ""
    Write-Host "H5 Together installer"
    Write-Host "---------------------"
    Write-Host "This adds H5 Together to Heroes of Might and Magic V. It never changes the game's own files."

    $srcDll = Join-Path $PSScriptRoot "dinput8.dll"
    $srcIni = Join-Path $PSScriptRoot "h5-together.ini"
    $srcTemplates = Join-Path $PSScriptRoot "h5-together\templates"
    $templateFiles = @()
    if (Test-Path -LiteralPath $srcTemplates) {
        $templateFiles = @(Get-ChildItem -LiteralPath $srcTemplates -File | Where-Object { $_.Extension -ieq ".xdb" })
    }
    $srcTexts = Join-Path $PSScriptRoot "h5-together\texts"
    $textFiles = @()
    if (Test-Path -LiteralPath $srcTexts) {
        $textFiles = @(Get-ChildItem -LiteralPath $srcTexts -File | Where-Object { $_.Extension -ieq ".txt" })
    }
    # Valve's steam_api.dll: only in some packages (GitHub's build has no copy of it).
    $srcSteam = Join-Path $PSScriptRoot "h5-together\steam_api.dll"
    $hasSteam = Test-Path -LiteralPath $srcSteam -PathType Leaf
    if (-not (Test-Path -LiteralPath $srcDll) -or -not (Test-Path -LiteralPath $srcIni) -or $templateFiles.Count -eq 0 -or $textFiles.Count -eq 0) {
        Write-Host "dinput8.dll, h5-together.ini and the folders h5-together\templates and h5-together\texts" -ForegroundColor Red
        Write-Host "must be next to this script." -ForegroundColor Red
        Write-Host "Please extract the whole zip first (right-click the zip, Extract All)," -ForegroundColor Red
        Write-Host "then run install.bat from the new folder. Nothing was installed." -ForegroundColor Red
        Wait-AndExit 7
    }
    if (-not (Test-IsOurs $srcDll)) {
        Write-Host "The dinput8.dll in this package is not the H5 Together one. The download may be damaged." -ForegroundColor Red
        Write-Host "Nothing was installed." -ForegroundColor Red
        Wait-AndExit 7
    }

    $addon = Get-AddonVersion $srcDll
    if (-not $addon) { $addon = "H5 Together" }
    $version = ($addon -split ' ')[-1]
    Write-Host "This package: $addon"
    Write-Host ""

    $bin = Select-GameBin
    if (-not $bin) {
        Write-Host ""
        Write-Host "No game folder, so nothing was installed."
        Wait-AndExit 4
    }
    $game = Get-GameInfo $bin
    # The add-on's panels follow the game's language (Steam's setting): say which one they will use.
    $notes = @()
    if ($game.Language) {
        $codes = @{ "english" = "en"; "french" = "fr"; "german" = "de"; "russian" = "ru"; "polish" = "pl";
                    "spanish" = "es"; "italian" = "it"; "czech" = "cs"; "hungarian" = "hu" }
        $name = $game.Language
        if ($LanguageNames.ContainsKey($name)) { $name = $LanguageNames[$name] }
        if ($codes.ContainsKey($game.Language) -and (Test-Path -LiteralPath (Join-Path $srcTexts ($codes[$game.Language] + ".txt")))) {
            $notes += "H5 Together has its panels in $name, so that is what you will see."
        } else {
            $notes += "H5 Together has no texts in $name yet: its panels will be in English (translations are welcome, see README.txt)."
        }
    }
    Write-GameInfo $game $notes

    $dstDll = Join-Path $bin "dinput8.dll"
    $dstIni = Join-Path $bin "h5-together.ini"
    $dstTemplates = Join-Path $bin "h5-together\templates"
    $dstTexts = Join-Path $bin "h5-together\texts"

    # Never overwrite a dinput8.dll that belongs to something else.
    $dllWas = "new"
    if (Test-Path -LiteralPath $dstDll) {
        if (-not (Test-IsOurs $dstDll)) {
            Write-Host ""
            Write-Host "Stopped. There is already a file called dinput8.dll in the game folder," -ForegroundColor Red
            Write-Host "and it is not the H5 Together one (another add-on may use it)." -ForegroundColor Red
            Write-Host "I did not change anything. To use H5 Together and that add-on together," -ForegroundColor Red
            Write-Host "rename that file to dinput8_next.dll (H5 Together then loads it, and the" -ForegroundColor Red
            Write-Host "log says ""chained""), or move it away; then run this installer again." -ForegroundColor Red
            Wait-AndExit 3
        }
        $same = (Get-Sha256Hex $srcDll) -eq (Get-Sha256Hex $dstDll)
        if ($same) { $dllWas = "same" } else { $dllWas = "older" }
    }

    # The game must be closed: Windows will not let a loaded DLL be replaced.
    if (-not (Confirm-GameClosed $bin)) {
        Write-Host ""
        Write-Host "The game is still running, so nothing was installed. Close it and run install.bat again." -ForegroundColor Red
        Wait-AndExit 8
    }
    Write-Host ""
    Write-Host "Installing ..."

    # The files of the old name (open-homm5, until 0.2.7) get the new name first, so that
    # the settings refresh and the templates below work on them.
    try {
        $renamed = Move-OldNames $bin @($templateFiles | ForEach-Object { $_.Name })
    } catch {
        Write-Host ""
        Write-Host "Could not give your open-homm5 files their new name (h5-together):" -ForegroundColor Red
        Write-Host ("  " + $_.Exception.Message) -ForegroundColor Red
        Write-Host "If the game is running, close it and try again. Nothing else was changed." -ForegroundColor Red
        Wait-AndExit 5
    }

    try {
        Copy-Item -LiteralPath $srcDll -Destination $dstDll -Force
        # A zip from the web marks its files as "downloaded"; take that mark off the copy.
        try { Unblock-File -LiteralPath $dstDll } catch { }
        $iniWas = "old"
        if (-not (Test-Path -LiteralPath $dstIni)) {
            Copy-Item -LiteralPath $srcIni -Destination $dstIni
            $iniWas = "new"
        }
        # Our random map templates: overwrite our own OH-*.xdb files, leave every other file alone.
        [void](New-Item -ItemType Directory -Path $dstTemplates -Force)
        $ours = @{}
        $tplReplaced = 0
        foreach ($f in $templateFiles) {
            $ours[$f.Name] = $true
            $dst = Join-Path $dstTemplates $f.Name
            if (Test-Path -LiteralPath $dst) { $tplReplaced++ }
            Copy-Item -LiteralPath $f.FullName -Destination $dst -Force
        }
        $tplOthers = @(Get-ChildItem -LiteralPath $dstTemplates -File | Where-Object { -not $ours.ContainsKey($_.Name) }).Count
        # Our language files: the same way (a translation of the player's own stays).
        [void](New-Item -ItemType Directory -Path $dstTexts -Force)
        $oursTxt = @{}
        $txtReplaced = 0
        foreach ($f in $textFiles) {
            $oursTxt[$f.Name] = $true
            $dst = Join-Path $dstTexts $f.Name
            if (Test-Path -LiteralPath $dst) { $txtReplaced++ }
            Copy-Item -LiteralPath $f.FullName -Destination $dst -Force
        }
        $txtOthers = @(Get-ChildItem -LiteralPath $dstTexts -File | Where-Object { -not $oursTxt.ContainsKey($_.Name) }).Count
        # Valve's steam_api.dll, if this package has it: over any copy there (the folder is ours).
        if ($hasSteam) {
            $dstSteam = Join-Path $bin "h5-together\steam_api.dll"
            Copy-Item -LiteralPath $srcSteam -Destination $dstSteam -Force
            try { Unblock-File -LiteralPath $dstSteam } catch { }
        }
    } catch {
        Write-Host ""
        Write-Host "Could not copy the files into the game folder:" -ForegroundColor Red
        Write-Host ("  " + $_.Exception.Message) -ForegroundColor Red
        Write-Host "If the game is running, close it and try again. If Windows says access is" -ForegroundColor Red
        Write-Host "denied, right-click install.bat and choose Run as administrator." -ForegroundColor Red
        Wait-AndExit 5
    }

    # The player's settings file: add this version's settings, keep the player's values.
    $iniError = ""
    if ($iniWas -eq "old") {
        try {
            $oldBytes = [System.IO.File]::ReadAllBytes($dstIni)
            $merged = Merge-Settings $oldBytes ([System.IO.File]::ReadAllBytes($srcIni))
            if ($null -eq $merged) {
                $iniWas = "unreadable"
            } elseif ([string]::Equals($Latin1.GetString($merged.Bytes), $Latin1.GetString($oldBytes))) {
                $iniWas = "current"
            } else {
                Save-IniBackup $dstIni
                [System.IO.File]::WriteAllBytes($dstIni, $merged.Bytes)
                $iniWas = "refreshed"
            }
        } catch {
            $iniWas = "failed"
            $iniError = $_.Exception.Message
        }
    }

    # The original 2008 game with 4 GB of memory (bin\H5_Original_4GB.exe), if it can be made.
    $original = @{ State = "failed"; From = ""; Why = "" }
    try { $original = Install-Original4Gb $bin $Mmh55Installer } catch { $original.Why = $_.Exception.Message }

    Write-Host ""
    Write-Host "Done. What I did:" -ForegroundColor Green
    if ($renamed.Moved.Count -gt 0) {
        $what = @()
        if ($renamed.Moved.Contains("open-homm5.ini")) { $what += "the settings" }
        if ($renamed.Moved.Contains("open-homm5-skins.ini")) { $what += "the hero looks" }
        if ($renamed.Moved.Contains("open-homm5.log")) { $what += "the log" }
        if ($renamed.Moved.Contains("open-homm5")) { $what += "the folder" }
        Write-Host ("  - open-homm5 is now called H5 Together: your files got the new name, h5-together (" + ($what -join ", ") + ")")
    }
    foreach ($name in $renamed.Left) {
        Write-Host ("  - kept {0} as it is: there is already a {1}, which is the one used" -f $name, ($name -replace '^open-homm5', 'h5-together')) -ForegroundColor Yellow
    }
    if ($renamed.FolderLeft -gt 0) {
        Write-Host ("  - left {0} file(s) in the folder open-homm5: the folder h5-together has them too" -f $renamed.FolderLeft) -ForegroundColor Yellow
    }
    if ($dllWas -eq "new") { Write-Host "  - added dinput8.dll (the add-on)" }
    elseif ($dllWas -eq "same") { Write-Host "  - dinput8.dll was already installed and up to date" }
    else { Write-Host "  - replaced the older H5 Together dinput8.dll with this version" }
    if ($iniWas -eq "new") { Write-Host "  - added h5-together.ini (settings, all at their defaults)" }
    elseif ($iniWas -eq "refreshed") {
        $newText = Get-Count $merged.New "new setting" "new settings"
        Write-Host ("  - settings file refreshed: {0} of your settings kept, {1} available (see the comments)" -f $merged.Settings, $newText)
        Write-Host "    (your previous file is saved as h5-together.ini.bak)"
        if ($merged.Unknown -gt 0) {
            $unknownText = Get-Count $merged.Unknown "setting" "settings"
            Write-Host ("    ({0} not listed in this version: kept at the end of their section, under ""kept from your previous file"")" -f $unknownText)
        }
    }
    elseif ($iniWas -eq "current") {
        if ($merged.Settings -gt 0) { Write-Host ("  - settings file already up to date: {0} of your settings kept" -f $merged.Settings) }
        else { Write-Host "  - settings file already up to date (every setting at its default)" }
    }
    elseif ($iniWas -eq "unreadable") {
        Write-Host "  - kept h5-together.ini as it is: it is not a plain text file, so I could not add the new settings" -ForegroundColor Yellow
    }
    else {
        Write-Host ("  - could not refresh h5-together.ini: " + $iniError) -ForegroundColor Yellow
        Write-Host "    If the game is running, close it and run the installer again." -ForegroundColor Yellow
    }
    Write-Host ("  - installed {0} random map templates in h5-together\templates (they are in the game's random map list)" -f $templateFiles.Count)
    if ($tplReplaced -gt 0) { Write-Host ("    ({0} of them replaced our files from an earlier install)" -f $tplReplaced) }
    if ($tplOthers -gt 0) { Write-Host ("    (left the {0} other file(s) in that folder alone)" -f $tplOthers) }
    Write-Host "    Everyone in a multiplayer game needs the same templates: install the same version."
    $codes = ($textFiles | ForEach-Object { $_.BaseName }) -join ", "
    Write-Host ("  - installed {0} language files in h5-together\texts ({1}): the words of the add-on's panels" -f $textFiles.Count, $codes)
    if ($txtReplaced -gt 0) { Write-Host ("    ({0} of them replaced our files from an earlier install)" -f $txtReplaced) }
    if ($txtOthers -gt 0) { Write-Host ("    (left the {0} other file(s) in that folder alone)" -f $txtOthers) }
    if ($hasSteam) {
        Write-Host "  - installed Valve's steam_api.dll in h5-together (online play over Steam, off until you turn it on in Options)"
    }
    if ($original.State -eq "made") {
        Write-Host ("  - made {0}: the original 2008 game, with 4 GB of memory (its exe, from {1})" -f $Original4GbName, $original.From)
        Write-Host "    To play the original game without MMH5.5, double-click it (Steam running for online play)."
    } elseif ($original.State -eq "kept") {
        Write-Host ("  - {0} (the original game with 4 GB of memory) is already there" -f $Original4GbName)
    }
    Write-Host "  - did not change or replace any file of the game itself"
    if ($original.State -eq "none") {
        Write-Host ""
        Write-Host "Tip: to play the original game (without MMH5.5) with 4 GB of memory, H5 Together can make"
        Write-Host ("bin\{0} from MMH5.5's installer file (MMH55_RC19k.exe, from MMH5.5's ModDB page: download it," -f $Original4GbName)
        Write-Host "you do not need to install it). Put it in your Downloads folder and run install.bat again."
    } elseif ($original.State -eq "failed") {
        Write-Host ""
        Write-Host ("Could not make {0} (the original game with 4 GB) from {1}: {2}" -f $Original4GbName, $original.From, $original.Why) -ForegroundColor Yellow
    }
    if ($game.Mmh55 -eq "missing" -and -not $game.Has2008) {
        Write-Host ""
        Write-Host "Reminder: neither MMH5.5 nor the original 2008 game's exe was found in this game folder." -ForegroundColor Yellow
        Write-Host "H5 Together does nothing with Steam's 2025 rebuild: install MMH5.5 first." -ForegroundColor Yellow
    } elseif ($game.Mmh55 -eq "untested") {
        Write-Host ""
        Write-Host "Reminder: this MMH5.5 is a version we have not tested. If H5 Together seems to do nothing," -ForegroundColor Yellow
        Write-Host "h5-together.log (see below) tells why." -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "All done: $addon is installed." -ForegroundColor Green
    Write-Host ""
    Write-Host "What to do next:"
    if (Test-Path -LiteralPath (Join-Path $bin $Original4GbName)) {
        Write-Host "  1. Start the original game: bin\$Original4GbName (a double-click, with Steam running; it has"
        Write-Host "     4 GB of memory). For MMH5.5, start MMH55_Utility_64.exe (or GameRanger) as usual."
    } else {
        Write-Host "  1. Start the game as you do for co-op games (GameRanger, or MMH55_Utility_64.exe)."
    }
    Write-Host "  2. Then look in the game's bin folder for the file h5-together.log:"
    Write-Host "       $bin"
    Write-Host "     If its first line says ""H5 Together hook DLL $version loaded"", the add-on is running."
    Write-Host "  3. Everyone in the same co-op game should install the same version."
    Write-Host ""
    Write-Host "To change a setting: in the game, Options > H5 Together (or press F3)."
    Write-Host "Or open h5-together.ini in that folder: every line says what it does."
    Write-Host "To remove H5 Together, run uninstall.bat. If something goes wrong, run bug-report.bat."

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
