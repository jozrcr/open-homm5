# open-homm5 uninstaller for Windows. Runs on Windows PowerShell 5.1.
#
# Removes only what install.ps1 put in the game's bin folder:
#   dinput8.dll (only if it is ours), open-homm5.log, open-homm5.ini (after asking),
#   and the random map templates of this package (the OH-*.xdb files whose names are in
#   this package's open-homm5\templates folder), then the folders if they are empty.
# Nothing else is touched: a template of your own stays.
#
# Keep this file plain ASCII (see install.ps1 for why).
#
# Options, for tests and odd setups:
#   -GameDir <folder>   use this game folder (or its bin folder), skip the search
#   -SteamDir <folder>  look in this Steam folder instead of the one in the registry
#   -NoPause            do not wait for Enter at the end
#
# Exit codes: 0 done, 4 game not found, 5 could not delete a file, 6 unexpected error.
param(
    [string]$GameDir = "",
    [string]$SteamDir = "",
    [switch]$NoPause
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

try {
    Write-Host ""
    Write-Host "open-homm5 uninstaller"
    Write-Host "----------------------"

    $bin = Select-GameBin
    if (-not $bin) {
        Write-Host ""
        Write-Host "No game folder, so nothing was removed."
        Wait-AndExit 4
    }
    Write-Host "Game folder: $bin"
    Write-Host ""

    $failed = $false

    $dll = Join-Path $bin "dinput8.dll"
    if (-not (Test-Path -LiteralPath $dll)) {
        Write-Host "  - no dinput8.dll here, nothing to remove"
    } elseif (-not (Test-IsOurs $dll)) {
        Write-Host "  - dinput8.dll is NOT the open-homm5 one, so I left it alone" -ForegroundColor Yellow
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

    $log = Join-Path $bin "open-homm5.log"
    if (Test-Path -LiteralPath $log) {
        try {
            Remove-Item -LiteralPath $log -Force
            Write-Host "  - removed open-homm5.log"
        } catch {
            Write-Host ("  - could not remove open-homm5.log: " + $_.Exception.Message) -ForegroundColor Red
            $failed = $true
        }
    } else {
        Write-Host "  - no open-homm5.log here"
    }

    $ini = Join-Path $bin "open-homm5.ini"
    if (Test-Path -LiteralPath $ini) {
        $answer = $null
        try { $answer = Read-Host "Also delete your settings file open-homm5.ini? [y/N]" } catch { }
        if ($answer -and $answer.Trim() -match '^(y|yes|o|oui)$') {
            try {
                Remove-Item -LiteralPath $ini -Force
                Write-Host "  - removed open-homm5.ini"
            } catch {
                Write-Host ("  - could not remove open-homm5.ini: " + $_.Exception.Message) -ForegroundColor Red
                $failed = $true
            }
        } else {
            Write-Host "  - kept open-homm5.ini"
        }
    } else {
        Write-Host "  - no open-homm5.ini here"
    }

    # The random map templates: only the names this package holds, never another file.
    $srcTemplates = Join-Path $PSScriptRoot "open-homm5\templates"
    $dstDir = Join-Path $bin "open-homm5"
    $dstTemplates = Join-Path $dstDir "templates"
    $tplNames = @()
    if (Test-Path -LiteralPath $srcTemplates) {
        $tplNames = @(Get-ChildItem -LiteralPath $srcTemplates -File | Where-Object { $_.Extension -ieq ".xdb" } | ForEach-Object { $_.Name })
    }
    if (-not (Test-Path -LiteralPath $dstTemplates)) {
        Write-Host "  - no random map templates here"
    } elseif ($tplNames.Count -eq 0) {
        Write-Host "  - this folder has no list of our templates, so I left open-homm5\templates alone" -ForegroundColor Yellow
    } else {
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
        Write-Host ("  - removed {0} random map templates" -f $removed)
        $left = @(Get-ChildItem -LiteralPath $dstTemplates -Force)
        if ($left.Count -gt 0) {
            Write-Host ("  - kept {0} other file(s) in open-homm5\templates (not ours)" -f $left.Count)
        } else {
            try {
                Remove-Item -LiteralPath $dstTemplates -Force
                if (@(Get-ChildItem -LiteralPath $dstDir -Force).Count -eq 0) { Remove-Item -LiteralPath $dstDir -Force }
                Write-Host "  - removed the empty folder open-homm5\templates"
            } catch {
                Write-Host ("  - could not remove the empty folder: " + $_.Exception.Message) -ForegroundColor Red
                $failed = $true
            }
        }
    }

    Write-Host ""
    if ($failed) {
        Write-Host "Some files could not be removed (see above)." -ForegroundColor Red
        Wait-AndExit 5
    }
    Write-Host "Done. No other file was touched." -ForegroundColor Green
    Wait-AndExit 0
} catch {
    Write-Host ""
    Write-Host "Something unexpected went wrong:" -ForegroundColor Red
    Write-Host ("  " + $_.Exception.Message) -ForegroundColor Red
    Wait-AndExit 6
}
