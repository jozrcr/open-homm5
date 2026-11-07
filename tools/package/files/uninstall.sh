#!/usr/bin/env bash
# H5 Together uninstaller for Linux (Steam / Proton).
#
# Removes only what install.sh put in the game's bin folder:
#   dinput8.dll (only if it is ours), h5-together.log (and h5-together.old.log), h5-together.ini (after asking),
#   and the random map templates of this package (the OH-*.xdb files whose names are in
#   this package's h5-together/templates folder), the language files of this package
#   (h5-together/texts, the same names as in the package), Valve's steam_api.dll in
#   h5-together (only if this package has one and the file there is byte for byte the
#   same; another copy stays), then the folders if they are empty.
# The same files under the add-on's old name (open-homm5, until 0.2.7) go too.
# Nothing else is touched: a template or a translation of your own stays. Steam's launch options are
# yours to clear by hand.
#
# Normal use:   bash uninstall.sh
# It finds the game the way install.sh does (with none or several it asks), shows what it
# found, and asks you to close the game if it runs.
# Options, for tests and odd setups:
#   --game-dir <folder>   use this game folder (or its bin folder), skip the search
#   --steam-dir <folder>  look in this Steam folder only
#   --test-known-55u "<sha256>=<name>;..."   count these 55u.dll files as known versions (tests)
#
# Exit codes: 0 done, 4 game not found or cancelled, 5 could not delete a file,
# 8 the game is running and was not closed.

[ -n "${BASH_VERSION:-}" ] || { echo "Please run this with bash:  bash uninstall.sh"; exit 2; }

MARKER='H5 Together hook DLL'
OLD_MARKER='open-homm5 hook DLL'   # the add-on's name until 0.2.7
GAME_FOLDER='Heroes of Might and Magic 5 Tribes of the East'

game_dir_arg=""
steam_dir_arg=""
test_known_55u_arg=""
while [ $# -gt 0 ]; do
    case "$1" in
        --game-dir)  game_dir_arg="${2:-}"; shift ;;
        --steam-dir) steam_dir_arg="${2:-}"; shift ;;
        --test-known-55u) test_known_55u_arg="${2:-}"; shift ;;
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

echo
echo "H5 Together uninstaller"
echo "-----------------------"

if ! choose_game_bin; then
    echo
    echo "No game folder, so nothing was removed."
    exit 4
fi
game_info "$bin"
gi_quiet=1
write_game_info
# The game must be closed: a program that has the add-on's file loaded must not have it deleted.
if ! confirm_game_closed; then
    echo
    echo "The game is still running, so nothing was removed. Close it and run uninstall.sh again."
    exit 8
fi
echo
echo "Removing ..."
echo

failed=0

dll="$(find_ci "$bin" dinput8.dll)"
if [ -z "$dll" ]; then
    echo "  - no dinput8.dll here, nothing to remove"
elif ! is_ours "$dll"; then
    echo "  - dinput8.dll is NOT the H5 Together one, so I left it alone"
elif rm -f "$dll"; then
    echo "  - removed dinput8.dll"
else
    echo "  - could not remove dinput8.dll. If the game is running, close it and run this again."
    failed=1
fi

# The logs, under the add-on's name and its old one (open-homm5, until 0.2.7). The log is
# kept to 5 MB: when it got bigger, the game kept the last full one as h5-together.old.log.
if [ -z "$(find_ci "$bin" h5-together.log)" ] && [ -z "$(find_ci "$bin" open-homm5.log)" ]; then
    echo "  - no h5-together.log here"
fi
for name in h5-together.log h5-together.old.log open-homm5.log open-homm5.old.log h5-together-resume.txt; do
    log="$(find_ci "$bin" "$name")"
    [ -n "$log" ] || continue
    if rm -f "$log"; then
        echo "  - removed $(basename "$log")"
    else
        echo "  - could not remove $(basename "$log")"
        failed=1
    fi
done

# The settings file, and the copies of it that the installer keeps when it adds new
# settings (.bak, .bak2, .bak3), under both names. One question for all of them.
inis=()
backups=()
for base in h5-together.ini open-homm5.ini; do
    f="$(find_ci "$bin" "$base")"
    [ -n "$f" ] && [ -f "$f" ] && inis+=("$f")
    for ext in bak bak2 bak3; do
        f="$(find_ci "$bin" "$base.$ext")"
        [ -n "$f" ] && [ -f "$f" ] && backups+=("$f")
    done
done
if [ "${#inis[@]}" -eq 0 ] && [ "${#backups[@]}" -eq 0 ]; then
    echo "  - no h5-together.ini here"
else
    ini_text=""
    for f in "${inis[@]}"; do ini_text="$ini_text and $(basename "$f")"; done
    ini_text="${ini_text# and }"
    if [ "${#inis[@]}" -eq 0 ]; then
        copied="$(basename "${backups[0]}")"
        copied="${copied%.*}"
        question="Also delete the old copies of your settings ($copied.bak)? [y/N] "; what="the old copies of $copied"
    elif [ "${#inis[@]}" -gt 1 ]; then
        question="Also delete your settings files $ini_text, and their old copies (.bak)? [y/N] "; what="$ini_text and their old copies"
    elif [ "${#backups[@]}" -gt 0 ]; then
        question="Also delete your settings file $ini_text and its old copies (.bak)? [y/N] "; what="$ini_text and its old copies"
    else
        question="Also delete your settings file $ini_text? [y/N] "; what="$ini_text"
    fi
    answer=""
    read -r -p "$question" answer || answer=""
    case "$answer" in
        y|Y|yes|YES|Yes|o|O|oui|OUI|Oui)
            for f in "${inis[@]}" "${backups[@]}"; do
                if rm -f "$f"; then
                    echo "  - removed $(basename "$f")"
                else
                    echo "  - could not remove $(basename "$f")"
                    failed=1
                fi
            done
            ;;
        *) echo "  - kept $what" ;;
    esac
fi

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The original game's 4 GB exe that the installer made (install.sh): only that name, and only
# when it is the 2008 game's exe.
p="$(find_ci "$bin" H5_Original_4GB.exe)"
if [ -n "$p" ] && [ -f "$p" ] && exe_2008 "$p"; then
    if rm -f "$p"; then
        echo "  - removed H5_Original_4GB.exe (the original game's 4 GB exe that the installer made)"
    else
        echo "  - could not remove H5_Original_4GB.exe"
        failed=1
    fi
fi

# Valve's steam_api.dll (online play over Steam): only if this package has it, and only if
# the file in the game is byte for byte that one (the same SHA-256). Another copy (another
# version, put there by hand) stays. A package without it leaves it alone. Before the
# language files and the templates, so that the folder h5-together can go once they are
# gone too.
our_dir="$(find_ci "$bin" h5-together)"
if [ -f "$here/h5-together/steam_api.dll" ] && [ -n "$our_dir" ] && [ -d "$our_dir" ]; then
    p="$(find_ci "$our_dir" steam_api.dll)"
    if [ -n "$p" ] && [ -f "$p" ]; then
        if ! cmp -s "$here/h5-together/steam_api.dll" "$p"; then
            echo "  - kept h5-together/$(basename "$p"): it is not the copy this package installs (another version?), so I left it alone"
        elif rm -f "$p"; then
            echo "  - removed steam_api.dll (Valve's file for online play over Steam)"
            rmdir "$our_dir" 2>/dev/null   # only if nothing else is in it
        else
            echo "  - could not remove steam_api.dll"
            failed=1
        fi
    fi
fi

# The language files: only the names this package holds; a translation of the player's
# own (another name) stays. Before the templates, so that the folder h5-together can go
# once both are empty.
txt_names=()
for f in "$here/h5-together/texts"/*.txt; do
    [ -f "$f" ] && txt_names+=("$(basename "$f")")
done
our_dir="$(find_ci "$bin" h5-together)"
txt_sub=""
[ -n "$our_dir" ] && [ -d "$our_dir" ] && txt_sub="$(find_ci "$our_dir" texts)"
if [ -n "$txt_sub" ] && [ -d "$txt_sub" ]; then
    if [ "${#txt_names[@]}" -eq 0 ]; then
        echo "  - this folder has no list of our language files, so I left h5-together/texts alone"
    else
        removed=0
        for name in "${txt_names[@]}"; do
            p="$(find_ci "$txt_sub" "$name")"
            [ -n "$p" ] && [ -f "$p" ] || continue
            if rm -f "$p"; then
                removed=$((removed + 1))
            else
                echo "  - could not remove $name"
                failed=1
            fi
        done
        echo "  - removed $removed language files (h5-together/texts)"
        if [ -n "$(ls -A "$txt_sub")" ]; then
            echo "  - kept the other file(s) in h5-together/texts (not ours)"
        elif rmdir "$txt_sub"; then
            rmdir "$our_dir" 2>/dev/null   # only if nothing else is in it
            echo "  - removed the empty folder h5-together/texts"
        else
            echo "  - could not remove the empty folder h5-together/texts"
            failed=1
        fi
    fi
fi

# The random map templates: only the names this package holds, never another file. In
# our folder, and in its old name (open-homm5, until 0.2.7) if that is still there.
tpl_names=()
for f in "$here/h5-together/templates"/*.xdb; do
    [ -f "$f" ] && tpl_names+=("$(basename "$f")")
done
any_templates=0
for folder in h5-together open-homm5; do
    tpl_dir="$(find_ci "$bin" "$folder")"
    tpl_sub=""
    [ -n "$tpl_dir" ] && [ -d "$tpl_dir" ] && tpl_sub="$(find_ci "$tpl_dir" templates)"
    [ -n "$tpl_sub" ] && [ -d "$tpl_sub" ] || continue
    any_templates=1
    from=""
    [ "$folder" = h5-together ] || from=" from $folder/templates (the folder's old name)"
    if [ "${#tpl_names[@]}" -eq 0 ]; then
        echo "  - this folder has no list of our templates, so I left $folder/templates alone"
        continue
    fi
    removed=0
    for name in "${tpl_names[@]}"; do
        p="$(find_ci "$tpl_sub" "$name")"
        [ -n "$p" ] && [ -f "$p" ] || continue
        if rm -f "$p"; then
            removed=$((removed + 1))
        else
            echo "  - could not remove $name"
            failed=1
        fi
    done
    echo "  - removed $removed random map templates$from"
    if [ -n "$(ls -A "$tpl_sub")" ]; then
        echo "  - kept the other file(s) in $folder/templates (not ours)"
    elif rmdir "$tpl_sub"; then
        rmdir "$tpl_dir" 2>/dev/null   # only if nothing else is in it
        echo "  - removed the empty folder $folder/templates"
    else
        echo "  - could not remove the empty folder $folder/templates"
        failed=1
    fi
done
[ "$any_templates" -eq 1 ] || echo "  - no random map templates here"

echo
if [ "$failed" -ne 0 ]; then
    echo "Some files could not be removed (see above)."
    exit 5
fi
echo "Done: H5 Together is removed. No other file was touched."
echo "To put it back later, run: bash install.sh"
echo "If you set Steam launch options for this game, clear them by hand (Properties > General)."
if [ -n "$(find_ci "$bin" dinput8_next.dll)" ]; then
    echo
    echo "dinput8_next.dll is still there: another add-on that H5 Together loaded. To keep using"
    echo "it without H5 Together, rename it back to dinput8.dll."
fi
exit 0
