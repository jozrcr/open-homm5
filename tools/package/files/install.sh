#!/usr/bin/env bash
# open-homm5 installer for Linux (Steam / Proton).
#
# Normal use, from the folder where you extracted the zip:   bash install.sh
# Options, for tests and odd setups:
#   --game-dir <folder>   use this game folder (or its bin folder), skip the search
#   --steam-dir <folder>  look in this Steam folder only
#
# It looks for Steam in ~/.steam/steam, ~/.local/share/Steam and the Flatpak
# folder, so a fake $HOME works for tests.
#
# Exit codes: 0 done, 3 refused (a dinput8.dll that is not ours is there),
# 4 game not found, 5 copy failed, 7 package incomplete.

[ -n "${BASH_VERSION:-}" ] || { echo "Please run this with bash:  bash install.sh"; exit 2; }

MARKER='open-homm5 hook DLL'
GAME_FOLDER='Heroes of Might and Magic 5 Tribes of the East'

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
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
echo "open-homm5 installer"
echo "--------------------"

src_dll="$here/dinput8.dll"
src_ini="$here/open-homm5.ini"
if [ ! -f "$src_dll" ] || [ ! -f "$src_ini" ]; then
    echo "dinput8.dll and open-homm5.ini must be in the same folder as this script."
    echo "Please extract the whole zip first, then run install.sh from the new folder."
    echo "Nothing was installed."
    exit 7
fi
if ! is_ours "$src_dll"; then
    echo "The dinput8.dll in this package is not the open-homm5 one. The download may be damaged."
    echo "Nothing was installed."
    exit 7
fi

if ! choose_game_bin; then
    echo
    echo "No game folder, so nothing was installed."
    exit 4
fi
echo "Game folder: $bin"

# Never overwrite a dinput8.dll that belongs to something else.
dst_dll="$bin/dinput8.dll"
dll_was="new"
existing="$(find_ci "$bin" dinput8.dll)"
if [ -n "$existing" ]; then
    if ! is_ours "$existing"; then
        echo
        echo "Stopped. There is already a file called dinput8.dll in the game folder,"
        echo "and it is not the open-homm5 one (another add-on may use it)."
        echo "I did not change anything. If you want open-homm5, move that file away"
        echo "yourself and run this installer again."
        exit 3
    fi
    dst_dll="$existing"
    if cmp -s "$src_dll" "$dst_dll"; then dll_was="same"; else dll_was="older"; fi
fi

dst_ini="$(find_ci "$bin" open-homm5.ini)"
ini_was="kept"
if ! cp -f "$src_dll" "$dst_dll"; then
    echo
    echo "Could not copy dinput8.dll into the game folder (see the message above)."
    echo "If the game is running, close it and try again."
    exit 5
fi
if [ -z "$dst_ini" ]; then
    if cp "$src_ini" "$bin/open-homm5.ini"; then
        ini_was="new"
    else
        echo "Could not copy open-homm5.ini (see the message above)."
        exit 5
    fi
fi

echo
echo "Done. What I did:"
case "$dll_was" in
    new)   echo "  - added dinput8.dll (the add-on)" ;;
    same)  echo "  - dinput8.dll was already installed and up to date" ;;
    older) echo "  - replaced the older open-homm5 dinput8.dll with this version" ;;
esac
if [ "$ini_was" = "new" ]; then
    echo "  - added open-homm5.ini (settings, all at their defaults)"
else
    echo "  - kept your existing open-homm5.ini, with your settings"
fi
echo "  - did not change or replace any file of the game itself"

if [ -z "$(find_ci "$bin" MMH55_Utility_64.exe)" ]; then
    echo
    echo "Note: there is no MMH55_Utility_64.exe in this folder. The add-on is made for"
    echo "MMH5.5 (the co-op version of the game). Install MMH5.5 first (see docs/linux.md)."
fi

cat <<'EOF'

One more step, in Steam (I do not touch Steam's settings myself):
right-click the game > Properties > General > Launch options, and paste:

    WINEDLLOVERRIDES="dinput8=n,b" %command%

This tells Wine (Proton) to load our dinput8.dll instead of its own.

Which exe starts? Steam starts H5_Game.exe, the plain game, and the add-on only
works with MMH5.5's exe, MMH55_Utility_64.exe (the co-op one). To make Steam start
that exe, paste this line instead of the one above:

    WINEDLLOVERRIDES="dinput8=n,b" bash -c 'exec "${@/H5_Game.exe/MMH55_Utility_64.exe}"' -- %command%

If it does not work, docs/linux.md (step 3) has another way: swap the two exe
files. https://github.com/jozrcr/open-homm5/blob/main/docs/linux.md

How to check: start the game from Steam. A file named open-homm5.log then appears in:
EOF
echo "  $bin"
echo "If you see it, the add-on is running. Everyone in the same co-op game should install it."
echo "To remove it later, run: bash uninstall.sh"
exit 0
