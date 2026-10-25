#!/usr/bin/env bash
# open-homm5 uninstaller for Linux (Steam / Proton).
#
# Removes only what install.sh put in the game's bin folder:
#   dinput8.dll (only if it is ours), open-homm5.log, open-homm5.ini (after asking),
#   and the random map templates of this package (the OH-*.xdb files whose names are in
#   this package's open-homm5/templates folder), then the folders if they are empty.
# Nothing else is touched: a template of your own stays. Steam's launch options are
# yours to clear by hand.
#
# Normal use:   bash uninstall.sh
# Options, for tests and odd setups:
#   --game-dir <folder>   use this game folder (or its bin folder), skip the search
#   --steam-dir <folder>  look in this Steam folder only
#
# Exit codes: 0 done, 4 game not found, 5 could not delete a file.

[ -n "${BASH_VERSION:-}" ] || { echo "Please run this with bash:  bash uninstall.sh"; exit 2; }

MARKER='open-homm5 hook DLL'
GAME_FOLDER='Heroes of Might and Magic 5 Tribes of the East'

game_dir_arg=""
steam_dir_arg=""
while [ $# -gt 0 ]; do
    case "$1" in
        --game-dir)  game_dir_arg="${2:-}"; shift ;;
        --steam-dir) steam_dir_arg="${2:-}"; shift ;;
        -h|--help)   awk 'NR > 1 { if (/^#/) { sub(/^# ?/, ""); print } else exit }' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "Unknown option: $1 (try --help)"; exit 2 ;;
    esac
    shift
done

# ---- shared with uninstall.sh: finding the game (keep both copies identical) ----

# True if the file is the dinput8.dll of open-homm5 (it contains our marker text).
is_ours() { grep -q -a -F -- "$MARKER" "$1" 2>/dev/null; }

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

steam_roots() {
    local root
    if [ -n "$steam_dir_arg" ]; then
        printf '%s\n' "$steam_dir_arg"
        return
    fi
    for root in "$HOME/.steam/steam" \
                "$HOME/.local/share/Steam" \
                "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
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

# Prints every bin folder of the game that Steam knows about (no duplicates).
find_game_bins() {
    local root lib bin real seen="|"
    while IFS= read -r root; do
        while IFS= read -r lib; do
            bin="$(bin_of "$lib/steamapps/common/$GAME_FOLDER")" || continue
            real="$(cd "$bin" && pwd -P)"
            case "$seen" in *"|$real|"*) continue ;; esac
            seen="$seen$real|"
            printf '%s\n' "$real"
        done < <(library_folders "$root")
    done < <(steam_roots)
}

# Sets $bin to the bin folder of the game to work on. Fails if cancelled or not found.
choose_game_bin() {
    local found=() line answer attempt n
    bin=""
    if [ -n "$game_dir_arg" ]; then
        bin="$(bin_of "$game_dir_arg")" && return 0
        echo "That is not the game folder: there is no MMH55_Utility_64.exe or H5_Game.exe in"
        echo "  $game_dir_arg"
        echo "  (or in its bin folder)."
        return 1
    fi
    while IFS= read -r line; do
        [ -n "$line" ] && found+=("$line")
    done < <(find_game_bins)
    if [ "${#found[@]}" -eq 1 ]; then
        echo "Found the game through Steam."
        bin="${found[0]}"
        return 0
    fi
    if [ "${#found[@]}" -gt 1 ]; then
        echo "The game is in several places:"
        n=1
        for line in "${found[@]}"; do
            echo "  $n) $line"
            n=$((n + 1))
        done
        echo "Type the number of the one to use, or paste another game folder."
    else
        echo "I could not find the game through Steam."
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
        echo "No MMH55_Utility_64.exe or H5_Game.exe there. Try again, or press Enter to cancel."
    done
    return 1
}
# ---- end of the shared block ----

echo
echo "open-homm5 uninstaller"
echo "----------------------"

if ! choose_game_bin; then
    echo
    echo "No game folder, so nothing was removed."
    exit 4
fi
echo "Game folder: $bin"
echo

failed=0

dll="$(find_ci "$bin" dinput8.dll)"
if [ -z "$dll" ]; then
    echo "  - no dinput8.dll here, nothing to remove"
elif ! is_ours "$dll"; then
    echo "  - dinput8.dll is NOT the open-homm5 one, so I left it alone"
elif rm -f "$dll"; then
    echo "  - removed dinput8.dll"
else
    echo "  - could not remove dinput8.dll. If the game is running, close it and run this again."
    failed=1
fi

log="$(find_ci "$bin" open-homm5.log)"
if [ -z "$log" ]; then
    echo "  - no open-homm5.log here"
elif rm -f "$log"; then
    echo "  - removed open-homm5.log"
else
    echo "  - could not remove open-homm5.log"
    failed=1
fi

ini="$(find_ci "$bin" open-homm5.ini)"
if [ -z "$ini" ]; then
    echo "  - no open-homm5.ini here"
else
    answer=""
    read -r -p "Also delete your settings file open-homm5.ini? [y/N] " answer || answer=""
    case "$answer" in
        y|Y|yes|YES|Yes|o|O|oui|OUI|Oui)
            if rm -f "$ini"; then
                echo "  - removed open-homm5.ini"
            else
                echo "  - could not remove open-homm5.ini"
                failed=1
            fi
            ;;
        *) echo "  - kept open-homm5.ini" ;;
    esac
fi

# The random map templates: only the names this package holds, never another file.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tpl_names=()
for f in "$here/open-homm5/templates"/*.xdb; do
    [ -f "$f" ] && tpl_names+=("$(basename "$f")")
done
tpl_dir="$(find_ci "$bin" open-homm5)"
tpl_sub=""
[ -n "$tpl_dir" ] && [ -d "$tpl_dir" ] && tpl_sub="$(find_ci "$tpl_dir" templates)"
if [ -z "$tpl_sub" ] || [ ! -d "$tpl_sub" ]; then
    echo "  - no random map templates here"
elif [ "${#tpl_names[@]}" -eq 0 ]; then
    echo "  - this folder has no list of our templates, so I left open-homm5/templates alone"
else
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
    echo "  - removed $removed random map templates"
    if [ -n "$(ls -A "$tpl_sub")" ]; then
        echo "  - kept the other file(s) in open-homm5/templates (not ours)"
    elif rmdir "$tpl_sub"; then
        rmdir "$tpl_dir" 2>/dev/null   # only if nothing else is in it
        echo "  - removed the empty folder open-homm5/templates"
    else
        echo "  - could not remove the empty folder open-homm5/templates"
        failed=1
    fi
fi

echo
if [ "$failed" -ne 0 ]; then
    echo "Some files could not be removed (see above)."
    exit 5
fi
echo "Done. No other file was touched."
echo "If you set Steam launch options for this game, clear them by hand (Properties > General)."
exit 0
