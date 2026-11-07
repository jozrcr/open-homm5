#!/usr/bin/env bash
# H5 Together bug report helper for Linux (Steam / Proton).
#
# Makes h5-together-bug-report-<date>.zip in the folder you run it from, to attach to a
# bug report on GitHub. It holds what we need to look into a problem and nothing else:
#   h5-together.log and h5-together.old.log   what the add-on wrote
#   h5-together.ini, h5-together-skins.ini    your settings
#   the same four under the add-on's old name (open-homm5, until 0.2.7), if they are there
#   the newest crash report, if there is one: h5-together-crash-<date>-<time>.txt (what
#                                             crashed) and its .dmp (a small minidump; it holds
#                                             part of the game's memory: --no-dump leaves it out)
#   system.txt                                the add-on, MMH5.5 and Linux versions, a few file
#                                             sizes and hashes, the other DLLs in the game folder
# No saves, no maps, no game files. It sends nothing anywhere: the zip is yours to look at
# (it is a normal zip) and to attach yourself. The log lists player names and LAN addresses.
#
# It finds the game the way install.sh does (and shows what it found). It works when the game
# does not start, and when the add-on is not installed (then there is no log, and the report
# says so).
#
# Normal use, from the folder where you extracted the package:   bash bug-report.sh
# Options, for tests and odd setups:
#   --game-dir <folder>   use this game folder (or its bin folder), skip the search
#   --steam-dir <folder>  look in this Steam folder only
#   --out-dir <folder>    put the report here instead of in the current folder
#   --tar                 make a .tar.gz instead of a .zip (for when there is no zip tool)
#   --no-dump             leave the crash report's .dmp out (its .txt still goes in)
#   --test-known-55u "<sha256>=<name>;..."   count these 55u.dll files as known versions (tests)
#
# The zip is made with "zip", or else with python3's zipfile module; if neither is there,
# a .tar.gz is made instead.
#
# Exit codes: 0 done, 4 game not found or cancelled, 5 could not write the report.

[ -n "${BASH_VERSION:-}" ] || { echo "Please run this with bash:  bash bug-report.sh"; exit 2; }

MARKER='H5 Together hook DLL'
OLD_MARKER='open-homm5 hook DLL'   # the add-on's name until 0.2.7
GAME_FOLDER='Heroes of Might and Magic 5 Tribes of the East'

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
game_dir_arg=""
steam_dir_arg=""
test_known_55u_arg=""
out_dir_arg=""
force_tar=0
no_dump=0
while [ $# -gt 0 ]; do
    case "$1" in
        --game-dir)  game_dir_arg="${2:-}"; shift ;;
        --steam-dir) steam_dir_arg="${2:-}"; shift ;;
        --test-known-55u) test_known_55u_arg="${2:-}"; shift ;;
        --out-dir)   out_dir_arg="${2:-}"; shift ;;
        --tar)       force_tar=1 ;;
        --no-dump)   no_dump=1 ;;
        -h|--help)   awk 'NR > 1 { if (/^#/) { sub(/^# ?/, ""); print } else exit }' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "Unknown option: $1 (try --help)"; exit 2 ;;
    esac
    shift
done

# ---- shared with uninstall.sh and bug-report.sh: finding the game (keep all copies identical) ----
# Finding the game (Steam in its usual places, then a number or a folder the player types),
# describing it (folder, Steam's language, exes, MMH5.5 and its version), checking that it is
# not running. Needs MARKER, OLD_MARKER, GAME_FOLDER and the options --game-dir, --steam-dir
# and --test-known-55u (<sha256>=<name>;..., tests only: a 55u.dll to count as a known version).
# Tests only too: H5_TOGETHER_PROC_DIR=<folder> is read instead of /proc to see what runs.
STEAM_APP_ID=15370   # Tribes of the East on Steam

# True if the file is the dinput8.dll of H5 Together, or of open-homm5 (0.2.7 and
# older): it contains our marker text.
is_ours() { grep -q -a -F -e "$MARKER" -e "$OLD_MARKER" -- "$1" 2>/dev/null; }

# Prints the first file or folder called $2 (ignoring upper/lower case) inside $1.
find_ci() { find "$1" -mindepth 1 -maxdepth 1 -iname "$2" -print 2>/dev/null | head -n 1; }

# Takes the game folder or its bin folder. Prints the bin folder, or fails if
# neither holds MMH55_Utility_64.exe or H5_Game.exe.
bin_of() {
    local folder="$1" candidate
    [ -d "$folder" ] || return 1
    for candidate in "$(find_ci "$folder" bin)" "$folder"; do
        [ -n "$candidate" ] && [ -d "$candidate" ] || continue
        if [ -n "$(find_ci "$candidate" MMH55_Utility_64.exe)" ] || [ -n "$(find_ci "$candidate" H5_Game.exe)" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

# SHA-256 of a file as lower case hex.
sha256_of() {
    # (sha256sum puts a "\" before the checksum when the file's path holds a backslash)
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" 2>/dev/null | awk '{sub(/^\\/, "", $1); print $1}'
    elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" 2>/dev/null | awk '{print $1}'
    elif command -v openssl >/dev/null 2>&1; then openssl dgst -sha256 "$1" 2>/dev/null | awk '{print $NF}'
    fi
}

# Prints a python that works ("python3" or "python"), or nothing. (On some systems the
# name only leads to a stub that tells you to install Python, so it is tried.)
find_python() {
    local p
    for p in python3 python; do
        if command -v "$p" >/dev/null 2>&1 && "$p" -c 'import zipfile' >/dev/null 2>&1; then
            printf '%s\n' "$p"
            return 0
        fi
    done
    return 1
}

# The name and version a dinput8.dll of ours says in its text: "H5 Together 0.3.0" from
# "H5 Together hook DLL 0.3.0 loaded" ("open-homm5 0.2.7": the add-on's old name).
addon_version() {
    grep -a -o -E '(H5 Together|open-homm5) hook DLL [^ ]+ loaded' "$1" 2>/dev/null | head -n 1 |
        sed -E 's/ hook DLL ([^ ]+) loaded$/ \1/'
}

# Prints the text of a file inside a zip file ($1), by the name inside ($2). Fails if it cannot.
zip_entry_text() {
    local py
    if command -v unzip >/dev/null 2>&1 && unzip -p "$1" "$2" 2>/dev/null; then
        return 0
    fi
    py="$(find_python)" || return 1
    "$py" -c 'import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
sys.stdout.write(z.read(sys.argv[2]).decode("latin-1"))' "$1" "$2" 2>/dev/null
}

# 55u.dll is MMH5.5's runtime patcher, and it changes with every release; its hash tells
# the versions apart (docs/plans/mmh55-rc19k.md). H55_Version says "19e" in RC19k too.
# Prints the version's name for the hash $1, or nothing if we have not tested that one.
KNOWN_55U_NAMES="RC19d and RC19k"
known_55u_name() {
    local item
    case "$1" in
        a6de006f9492f1e8af6eae19619b5a345536c443a5f2e4bb5e1e26947988085b) echo "RC19d" ;;
        a28663da081b6e23f0c2fa4000117d0b5d1457ca3314e3735bca215c1d5d304d) echo "RC19k" ;;
        *)
            [ -n "$1" ] && [ -n "$test_known_55u_arg" ] || return 0
            while IFS= read -r item; do
                [ "${item%%=*}" = "$1" ] && echo "${item#*=}"
            done < <(printf '%s\n' "$test_known_55u_arg" | tr ';' '\n')
            ;;
    esac
}

steam_roots() {
    local root
    if [ -n "$steam_dir_arg" ]; then
        printf '%s\n' "$steam_dir_arg"
        return
    fi
    for root in "$HOME/.steam/steam" \
                "$HOME/.local/share/Steam" \
                "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam" \
                "$HOME/snap/steam/common/.local/share/Steam"; do
        [ -d "$root/steamapps" ] && printf '%s\n' "$root"
    done
}

# Prints a Steam folder and every library folder listed in its libraryfolders.vdf.
library_folders() {
    local root="$1" vdf="$1/steamapps/libraryfolders.vdf"
    printf '%s\n' "$root"
    [ -f "$vdf" ] || return 0
    sed -n -E \
        -e 's/^[[:space:]]*"path"[[:space:]]+"(.*)"[[:space:]]*$/\1/p' \
        -e 's/^[[:space:]]*"[0-9]+"[[:space:]]+"([/].*)"[[:space:]]*$/\1/p' "$vdf" | sed 's/\\\\/\\/g'
}

# Prints the value of a "key"  "value" line of one of Steam's .acf files (the first one).
acf_value() {
    sed -n -E "s/^[[:space:]]*\"$2\"[[:space:]]+\"([^\"]*)\"[[:space:]]*\$/\1/p" "$1" 2>/dev/null | head -n 1
}

# Prints every bin folder of the game that Steam knows about (no duplicates). Steam's file for
# the game (appmanifest_15370.acf) says which folder it is in; the usual name is the fallback.
find_game_bins() {
    local root lib bin real name seen="|"
    while IFS= read -r root; do
        while IFS= read -r lib; do
            for name in "$(acf_value "$lib/steamapps/appmanifest_$STEAM_APP_ID.acf" installdir)" "$GAME_FOLDER"; do
                [ -n "$name" ] || continue
                bin="$(bin_of "$lib/steamapps/common/$name")" || continue
                real="$(cd "$bin" && pwd -P)"
                case "$seen" in *"|$real|"*) continue ;; esac
                seen="$seen$real|"
                printf '%s\n' "$real"
            done
        done < <(library_folders "$root")
    done < <(steam_roots)
}

# English name of a language, from Steam's name for it.
language_name() {
    case "$1" in
        english) echo "English" ;;   french) echo "French" ;;   german) echo "German" ;;
        russian) echo "Russian" ;;   polish) echo "Polish" ;;   spanish) echo "Spanish" ;;
        italian) echo "Italian" ;;   czech) echo "Czech" ;;     hungarian) echo "Hungarian" ;;
        *) echo "$1" ;;
    esac
}

# The build date in a program's header (seconds since 1970), or 0. The 2008 game's exe and
# MMH5.5's exes made from it say 2008-06-04 13:58:55; Steam's 2025 rebuild says 2025.
PE_DATE_2008=1212587935
pe_date() {
    local off
    off="$(od -An -tu4 -j 60 -N 4 "$1" 2>/dev/null | tr -d ' ')"
    if [ -z "$off" ] || [ "$(od -An -c -j "$off" -N 2 "$1" 2>/dev/null | tr -d ' ')" != "PE" ]; then echo 0; return; fi
    off="$(od -An -tu4 -j $((off + 8)) -N 4 "$1" 2>/dev/null | tr -d ' ')"
    echo "${off:-0}"
}

# The 2008 game's own exe (not MMH5.5's: those have the same date but load MMH5.5's 55u.dll).
exe_2008() {
    [ "$(pe_date "$1")" = "$PE_DATE_2008" ] && ! LC_ALL=C grep -q -a -F 55u.dll "$1"
}

# What we can tell about a game, from its bin folder $1. Only looks. Sets gi_bin, gi_root (the
# game folder), gi_store (Steam, GOG or nothing), gi_language (Steam's name for it, or nothing),
# gi_exes (each "name|role": coop, copy of the co-op exe, 2008 (the 2008 game's exe) or own),
# gi_has2008 (1: the 2008 game's exe is there), gi_mmh55 (ok, untested or missing),
# gi_mmh55_name (RC19k...) and gi_mmh55_version (H55_Version).
game_info() {
    local b="$1" common manifest f c name role pak u55 coop=()
    gi_bin="$b"; gi_root="$(dirname "$b")"; gi_store=""; gi_language=""; gi_exes=(); gi_has2008=0
    gi_mmh55="missing"; gi_mmh55_name=""; gi_mmh55_version=""
    # Steam keeps the game in <library>/steamapps/common/<name>, and its file for the game next to that
    common="$(dirname "$gi_root")"
    if [ "$(basename "$common" | tr 'A-Z' 'a-z')" = "common" ]; then
        manifest="$(find_ci "$(dirname "$common")" "appmanifest_$STEAM_APP_ID.acf")"
        if [ -n "$manifest" ] && [ -f "$manifest" ]; then
            gi_store="Steam"
            gi_language="$(acf_value "$manifest" language | tr 'A-Z' 'a-z')"
        fi
    fi
    if [ -z "$gi_store" ] && [ -n "$(find_ci "$gi_root" 'goggame-*.info')" ]; then gi_store="GOG"; fi
    # The exes. H5_Game.exe is a copy of the co-op exe when the player followed the GameRanger
    # guide, which keeps the 2008 game's own exe as H5_GameOLD.exe.
    for name in MMH55_Utility_64.exe MMH55_Utility.exe; do
        f="$(find_ci "$b" "$name")"
        if [ -n "$f" ] && [ -f "$f" ]; then coop+=("$f"); gi_exes+=("$name|coop"); fi
    done
    for name in H5_Game.exe H5_GameOLD.exe H5_Original_4GB.exe; do
        f="$(find_ci "$b" "$name")"
        if [ -z "$f" ] || [ ! -f "$f" ]; then continue; fi
        role="own"
        for c in "${coop[@]}"; do
            [ "$role" = "own" ] && cmp -s "$f" "$c" && role="copy"
        done
        if [ "$role" = "own" ] && exe_2008 "$f"; then role="2008"; gi_has2008=1; fi
        gi_exes+=("$name|$role")
    done
    # MMH5.5 and its version: the version file in its pak, and which 55u.dll it has
    pak="$(find_ci "$gi_root/data" MMH55-Index.pak)"
    if [ -n "$pak" ] && [ -f "$pak" ]; then
        gi_mmh55_version="$(zip_entry_text "$pak" scripts/H55-Version.lua |
            sed -n -E 's/.*H55_Version[[:space:]]*=[[:space:]]*"([^"]*)".*/\1/p' | head -n 1)"
    fi
    u55="$(find_ci "$b" 55u.dll)"
    if [ -n "$u55" ] && [ -f "$u55" ]; then gi_mmh55_name="$(known_55u_name "$(sha256_of "$u55")")"; fi
    if [ "${#coop[@]}" -gt 0 ]; then
        if [ -n "$gi_mmh55_name" ]; then gi_mmh55="ok"; else gi_mmh55="untested"; fi
    fi
}

# The one-line description of MMH5.5 for the game in gi_*.
mmh55_text() {
    case "$gi_mmh55" in
        ok) echo "$gi_mmh55_name (tested with H5 Together)" ;;
        untested)
            if [ -n "$gi_mmh55_version" ]; then echo "a version we have not tested (its version file says $gi_mmh55_version)"
            else echo "a version we have not tested"; fi
            ;;
        *)
            if [ "$gi_has2008" = "1" ]; then echo "not found (H5 Together works with the original 2008 game)"
            else echo "not found"; fi
            ;;
    esac
}

# Shows what we found (the game in gi_*), in plain words, with a warning where H5 Together
# may not work (gi_quiet=1: facts only, for the uninstaller). The arguments are more lines to
# show with the facts.
write_game_info() {
    local entry text label="  Game exes:  " note
    echo
    echo "Your game"
    echo "  Folder:     $gi_root"
    [ -n "$gi_store" ] && echo "  Found in:   $gi_store"
    if [ -n "$gi_language" ]; then
        echo "  Language:   $(language_name "$gi_language") (Steam's setting for the game)"
    fi
    echo "  MMH5.5:     $(mmh55_text)"
    for entry in "${gi_exes[@]}"; do
        case "${entry#*|}" in
            coop) text="MMH5.5's co-op exe: H5 Together works with it" ;;
            copy) text="a copy of the co-op exe (made for GameRanger): works too" ;;
            2008)
                if [ "$gi_mmh55" = "missing" ]; then text="the original 2008 game: H5 Together works with it"
                else text="the original 2008 game: works too (H5 Together hides MMH5.5 from it)"; fi
                ;;
            *)    text="not the 2008 game (Steam's 2025 update?): H5 Together does nothing with it" ;;
        esac
        printf '%s%-21s %s\n' "$label" "${entry%%|*}" "$text"
        label="              "
    done
    for note in "$@"; do echo "  $note"; done
    [ "${gi_quiet:-0}" = "1" ] && return 0
    if [ "$gi_mmh55" = "missing" ] && [ "$gi_has2008" != "1" ]; then
        echo
        echo "Warning: MMH5.5 was not found in this folder (there is no MMH55_Utility_64.exe), nor the"
        echo "original 2008 game's exe. Since July 2025, Steam installs a rebuild of the game that"
        echo "H5 Together does not know. The files can be copied anyway; they start working with"
        echo "MMH5.5, or with the 2008 game's exe."
    elif [ "$gi_mmh55" = "untested" ]; then
        echo
        echo "Warning: this MMH5.5 is a version we have not tested (tested: $KNOWN_55U_NAMES)."
        echo "H5 Together may switch itself off to stay safe; the game then runs as it always did,"
        echo "and h5-together.log says why."
    fi
}

# Prints the names of the game's programs that are running, one per line. Under Wine or Proton
# the command line of the game's process names its exe (C:\...\H5_Game.exe).
running_game() {
    cat "${H5_TOGETHER_PROC_DIR:-/proc}"/[0-9]*/cmdline 2>/dev/null | tr '\0' '\n' |
        grep -i -o -E '(^|[\\/])(H5_Game|H5_GameOLD|MMH55_Utility_64|MMH55_Utility|MMH55_64|MMH55)\.exe$' |
        sed -E 's/^[\\/]//' | sort -u
}

# While the game is running, asks the player to close it (a program that has the add-on's file
# loaded must not have it replaced). Succeeds to go on, fails to stop.
confirm_game_closed() {
    local running answer i
    for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
        running="$(running_game | paste -s -d, - | sed 's/,/, /g')"
        [ -n "$running" ] || return 0
        echo
        echo "The game is running ($running). I cannot change the add-on's file safely while it runs."
        echo "Please close the game, then press Enter here to go on."
        echo "(Type S and Enter to go on anyway, or Q and Enter to stop.)"
        read -r -p "Game closed? Press Enter: " answer || return 1
        case "$(printf '%s' "$answer" | tr 'A-Z' 'a-z')" in
            q) return 1 ;;
            s) return 0 ;;
        esac
    done
    return 1
}

not_the_game() {
    echo "That is not the game folder: I found no bin folder with H5_Game.exe or MMH55_Utility_64.exe in"
    echo "  $1"
    echo "Choose the folder that holds the game's bin and data folders."
}

# Sets $bin to the bin folder of the game to work on. Fails if cancelled or not found.
choose_game_bin() {
    local found=() line answer attempt n
    bin=""
    if [ -n "$game_dir_arg" ]; then
        bin="$(bin_of "$game_dir_arg")" && return 0
        not_the_game "$game_dir_arg"
        return 1
    fi
    echo "Looking for Heroes of Might and Magic V ..."
    while IFS= read -r line; do
        [ -n "$line" ] && found+=("$line")
    done < <(find_game_bins)
    if [ "${#found[@]}" -eq 1 ]; then
        echo "Found the game."
        bin="${found[0]}"
        return 0
    fi
    if [ "${#found[@]}" -gt 1 ]; then
        echo "The game is in several places:"
        n=1
        for line in "${found[@]}"; do
            game_info "$line"
            echo "  $n) $gi_root"
            echo "       ${gi_store:+$gi_store; }MMH5.5: $(mmh55_text)"
            n=$((n + 1))
        done
        echo "Type the number of the one to use, or paste another game folder."
    else
        echo "I could not find the game on this PC (I looked in Steam's usual places)."
        echo "Paste the game folder here (the one that holds the bin folder), or its bin folder."
    fi
    echo "Press Enter alone to cancel."
    for attempt in 1 2 3 4 5; do
        read -r -p "Game folder: " answer || return 1
        answer="${answer#\"}"; answer="${answer%\"}"
        answer="${answer#\'}"; answer="${answer%\'}"
        answer="${answer/#\~/$HOME}"
        [ -n "$answer" ] || return 1
        if [ "${#found[@]}" -gt 1 ] && [[ "$answer" =~ ^[0-9]+$ ]]; then
            if [ "$answer" -ge 1 ] && [ "$answer" -le "${#found[@]}" ]; then
                bin="${found[$((answer - 1))]}"
                return 0
            fi
            echo "There is no number $answer in the list."
            continue
        fi
        bin="$(bin_of "$answer")" && return 0
        not_the_game "$answer"
        echo "Try again (paste a folder), or press Enter to cancel."
    done
    return 1
}
# ---- end of the shared block ----

# A folder's path with your home folder written as ~, so the report does not carry your user name.
short_path() {
    local p h
    p="$(cd "$1" 2>/dev/null && pwd -P)" || p="$1"
    h="$(cd "$HOME" 2>/dev/null && pwd -P)" || h="$HOME"
    case "$p" in
        "$h"/*) printf '~%s\n' "${p#"$h"}" ;;
        *) printf '%s\n' "$p" ;;
    esac
}

echo
echo "H5 Together bug report"
echo "----------------------"

if ! choose_game_bin; then
    echo
    echo "No game folder, so no bug report was made."
    exit 4
fi
game_info "$bin"
write_game_info
root="$(dirname "$bin")"

# The folder for the report: --out-dir, else the current folder, else your home folder.
out_dir="$out_dir_arg"
if [ -z "$out_dir" ]; then
    out_dir="$PWD"
    [ -w "$out_dir" ] || out_dir="$HOME"
fi
if ! mkdir -p "$out_dir" 2>/dev/null || [ ! -w "$out_dir" ]; then
    echo
    echo "Cannot write in $out_dir. Run this again with:  bash bug-report.sh --out-dir <a folder you can write in>"
    exit 5
fi
out_dir="$(cd "$out_dir" && pwd)"

# The files that go in the report: only these, and only the ones that exist (the last four:
# the add-on's old name, until 0.2.7, for an install that was not updated yet).
pack_names=()
pack_paths=()
has_log=0
for name in h5-together.log h5-together.old.log h5-together.ini h5-together-skins.ini \
            open-homm5.log open-homm5.old.log open-homm5.ini open-homm5-skins.ini; do
    f="$(find_ci "$bin" "$name")"
    if [ -n "$f" ] && [ -f "$f" ]; then
        pack_names+=("$name")
        pack_paths+=("$f")
        case "$name" in h5-together.log|open-homm5.log) has_log=1 ;; esac
    fi
done
# The add-on's newest crash report (h5-together-crash-<date>-<time>.txt and its .dmp, next to
# the log; the names sort as the time does). The add-on keeps the 3 newest.
crash_count=0
crash_txt=""
crash_dmp=""
while IFS= read -r f; do
    [ -n "$f" ] || continue
    crash_count=$((crash_count + 1))
    crash_txt="$f"
done < <(find "$bin" -mindepth 1 -maxdepth 1 -type f -name 'h5-together-crash-*.txt' -print 2>/dev/null |
         grep -E '/h5-together-crash-[0-9]{8}-[0-9]{6}\.txt$' | sort)
if [ -n "$crash_txt" ]; then
    pack_names+=("$(basename "$crash_txt")")
    pack_paths+=("$crash_txt")
    if [ -f "${crash_txt%.txt}.dmp" ]; then
        crash_dmp="$(basename "${crash_txt%.txt}.dmp")"
        if [ "$no_dump" -eq 0 ]; then
            pack_names+=("$crash_dmp")
            pack_paths+=("${crash_txt%.txt}.dmp")
        fi
    fi
fi

stage="$(mktemp -d "${TMPDIR:-/tmp}/h5-together-report.XXXXXX")" || { echo "Cannot make a temporary folder."; exit 5; }
trap 'rm -rf "$stage"' EXIT

# system.txt
{
    echo "H5 Together bug report"
    echo "Made by bug-report.sh on $(date '+%Y-%m-%d %H:%M:%S'). Nothing was sent anywhere."
    echo
    echo "Add-on"
    dll="$(find_ci "$bin" dinput8.dll)"
    if [ -z "$dll" ]; then
        echo "  dinput8.dll in the game folder: none (the add-on is not installed here)"
    elif ! is_ours "$dll"; then
        echo "  dinput8.dll in the game folder: NOT the H5 Together one (another add-on uses that name)"
    else
        v="$(addon_version "$dll")"
        echo "  dinput8.dll in the game folder: ${v:-an older build of the add-on, version not written in it}"
    fi
    if [ -f "$here/dinput8.dll" ]; then
        v="$(addon_version "$here/dinput8.dll")"
        echo "  the package this script came from: ${v:-unknown}"
    fi
    for name in h5-together.nohooks open-homm5.nohooks; do
        if [ -n "$(find_ci "$bin" "$name")" ]; then
            echo "  $name (switches the hooks off): present"
        else
            echo "  $name (switches the hooks off): not there"
        fi
    done
    echo
    echo "System"
    echo "  $(uname -srm 2>/dev/null)"
    if [ -r /etc/os-release ]; then
        echo "  $(sed -n -E 's/^PRETTY_NAME="?([^"]*)"?$/\1/p' /etc/os-release | head -n 1)"
    fi
    echo "  language: ${LANG:-not set}"
    echo "  game folder: $(short_path "$bin")"
    echo
    echo "MMH5.5"
    pak="$(find_ci "$root/data" MMH55-Index.pak 2>/dev/null)"
    if [ -n "$pak" ] && [ -f "$pak" ]; then
        lua="$(zip_entry_text "$pak" scripts/H55-Version.lua)"
        ver="$(printf '%s\n' "$lua" | sed -n -E 's/.*H55_Version[[:space:]]*=[[:space:]]*"([^"]*)".*/\1/p' | head -n 1)"
        if [ -n "$ver" ]; then
            echo "  H55_Version in data/MMH55-Index.pak: $ver"
        else
            echo "  data/MMH55-Index.pak is there, but I could not read the version from it (no unzip or python3?)"
        fi
    else
        echo "  data/MMH55-Index.pak: not found (MMH5.5 is not installed here, or the game folder is not the usual one)"
    fi
    u55="$(find_ci "$bin" 55u.dll)"
    if [ -n "$u55" ]; then
        h="$(sha256_of "$u55")"
        n="$(known_55u_name "$h")"
        echo "  55u.dll: ${n:-a version we have not tested} (sha256 ${h:-unknown})"
    else
        echo "  55u.dll: not found"
    fi
    echo
    echo "Game exes in the bin folder (size, sha256)"
    any_exe=0
    for name in H5_Game.exe H5_GameOLD.exe MMH55_Utility_64.exe MMH55_Utility.exe MMH55_64.exe MMH55.exe; do
        f="$(find_ci "$bin" "$name")"
        [ -n "$f" ] && [ -f "$f" ] || continue
        any_exe=1
        echo "  $name  $(wc -c < "$f" | tr -d ' ') bytes  $(sha256_of "$f")"
    done
    [ "$any_exe" -eq 1 ] || echo "  none found"
    echo
    echo "Other .dll and .asi files in the bin folder (name, size)"
    any_dll=0
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        any_dll=1
        echo "  $(basename "$f")  $(wc -c < "$f" | tr -d ' ')"
    done < <(find "$bin" -mindepth 1 -maxdepth 1 -type f \( -iname '*.dll' -o -iname '*.asi' \) -print 2>/dev/null | sort)
    [ "$any_dll" -eq 1 ] || echo "  none"
    echo
    echo "Crash reports of the add-on (written next to the log when the game crashes)"
    if [ -z "$crash_txt" ]; then
        echo "  none"
    elif [ -z "$crash_dmp" ]; then
        echo "  $crash_count kept; the newest is in this zip: $(basename "$crash_txt") (no .dmp with it)"
    elif [ "$no_dump" -eq 1 ]; then
        echo "  $crash_count kept; the newest is in this zip: $(basename "$crash_txt") (its .dmp left out: --no-dump)"
    else
        echo "  $crash_count kept; the newest is in this zip: $(basename "$crash_txt") and $crash_dmp"
    fi
    echo
    echo "Files in this zip"
    for i in "${!pack_names[@]}"; do
        echo "  ${pack_names[$i]}  $(wc -c < "${pack_paths[$i]}" | tr -d ' ') bytes"
    done
    [ "$has_log" -eq 1 ] || echo "  (there is no h5-together.log in the game folder: the game was not started since the add-on was installed, or the add-on is not installed)"
    echo "  system.txt  (this file)"
} > "$stage/system.txt"

for i in "${!pack_names[@]}"; do
    cp "${pack_paths[$i]}" "$stage/${pack_names[$i]}" || { echo "Could not copy ${pack_names[$i]}."; exit 5; }
done

# The archive: zip, else python3's zipfile, else a .tar.gz.
stamp="$(date '+%Y-%m-%d-%H%M')"
base="h5-together-bug-report-$stamp"
files=("${pack_names[@]}" system.txt)
report=""
if [ "$force_tar" -eq 0 ] && command -v zip >/dev/null 2>&1; then
    if (cd "$stage" && rm -f "$out_dir/$base.zip" && zip -q -9 "$out_dir/$base.zip" "${files[@]}"); then report="$out_dir/$base.zip"; fi
fi
if [ -z "$report" ] && [ "$force_tar" -eq 0 ] && py="$(find_python)"; then
    if (cd "$stage" && rm -f "$out_dir/$base.zip" && "$py" -c 'import sys, zipfile
z = zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED)
for name in sys.argv[2:]:
    z.write(name, name)
z.close()' "$out_dir/$base.zip" "${files[@]}"); then report="$out_dir/$base.zip"; fi
fi
if [ -z "$report" ]; then
    if (cd "$stage" && tar -czf "$out_dir/$base.tar.gz" "${files[@]}"); then report="$out_dir/$base.tar.gz"; fi
fi
if [ -z "$report" ]; then
    echo
    echo "Could not make the report file in $out_dir."
    echo "You can attach h5-together.log from the game's bin folder instead:"
    echo "  $bin"
    exit 5
fi

echo
echo "Done. The bug report is this file:"
echo "  $report"
echo "  ($(wc -c < "$report" | tr -d ' ') bytes; it holds: $(IFS=,; echo "${pack_names[*]}" | sed 's/,/, /g'), system.txt)"
if [ "$has_log" -eq 0 ]; then
    echo
    echo "There is no h5-together.log in the game folder, so the report only tells about your installation."
    echo "(Either the add-on is not installed here, or the game was not started since.)"
fi
if [ -n "$crash_txt" ]; then
    echo
    if [ -n "$crash_dmp" ] && [ "$no_dump" -eq 0 ]; then
        echo "It holds the newest crash report too: $(basename "$crash_txt") and $crash_dmp."
        echo "The .dmp holds part of the game's memory at the crash (player names, folder names)."
        echo "To leave it out, run this again with --no-dump."
    else
        echo "It holds the newest crash report too: $(basename "$crash_txt")."
    fi
fi
echo
echo "Attach that file to your report on GitHub: https://github.com/jozrcr/h5-together/issues/new/choose"
echo "Nothing was sent anywhere. The log lists player names and LAN addresses; the file is a normal"
echo "archive, so you can open it and look before you send it."
exit 0
