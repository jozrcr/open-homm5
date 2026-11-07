#!/usr/bin/env bash
# H5 Together installer for Linux (Steam / Proton).
#
# Normal use, from the folder where you extracted the zip:   bash install.sh
# It looks for the game, shows what it found (the folder, the language, the MMH5.5 version),
# asks you to close the game if it runs, and copies the add-on in. With no game found, or
# several, it asks: a number, or a pasted folder.
# Options, for tests and odd setups:
#   --game-dir <folder>   use this game folder (or its bin folder), skip the search
#   --steam-dir <folder>  look in this Steam folder only
#   --mmh55-installer <file>  MMH5.5's installer file, to make the original game's 4 GB exe
#                         from (else looked for here, in Downloads and on the Desktop)
#   --no-download         never offer to fetch that exe from MMH5.5's release on GitHub
#                         (also H5_TOGETHER_NO_DOWNLOAD=1)
#   --test-known-55u "<sha256>=<name>;..."   count these 55u.dll files as known versions (tests)
#
# It looks for Steam in ~/.steam/steam, ~/.local/share/Steam, the Flatpak and the Snap
# folders, so a fake $HOME works for tests.
#
# It also copies the random map templates of this package (h5-together/templates/*.xdb)
# into <game bin>/h5-together/templates, and its language files (h5-together/texts/*.txt:
# the words of the add-on's panels) into <game bin>/h5-together/texts. It overwrites its
# own files there and never touches another file.
#
# If the package has Valve's steam_api.dll (h5-together/steam_api.dll: online play over
# Steam, off until the player turns it on), it goes to <game bin>/h5-together/steam_api.dll,
# over any copy there (that folder is ours). A package without it changes nothing there.
#
# An existing h5-together.ini is refreshed: it becomes this package's file with the
# player's values in it (the old one is kept as h5-together.ini.bak; see merge_ini).
# The files of the add-on's old name (open-homm5, until 0.2.7) get the new name first,
# the folder bin/open-homm5 included (see move_old_names).
#
# Exit codes: 0 done, 3 refused (a dinput8.dll that is not ours is there),
# 4 game not found or cancelled, 5 copy failed, 7 package incomplete,
# 8 the game is running and was not closed.

[ -n "${BASH_VERSION:-}" ] || { echo "Please run this with bash:  bash install.sh"; exit 2; }

MARKER='H5 Together hook DLL'
OLD_MARKER='open-homm5 hook DLL'   # the add-on's name until 0.2.7
GAME_FOLDER='Heroes of Might and Magic 5 Tribes of the East'

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
game_dir_arg=""
steam_dir_arg=""
mmh55_installer_arg=""
no_download=0
test_known_55u_arg=""
while [ $# -gt 0 ]; do
    case "$1" in
        --game-dir)  game_dir_arg="${2:-}"; shift ;;
        --steam-dir) steam_dir_arg="${2:-}"; shift ;;
        --mmh55-installer) mmh55_installer_arg="${2:-}"; shift ;;
        --no-download) no_download=1 ;;
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

# ---- the settings file: refresh it from this package's h5-together.ini ----
# install.ps1 does exactly the same (Merge-Settings, where the rules are written out);
# test_scripts.py checks that both give the same bytes. In short: the result is this
# package's file; each "key=value" of the player's file fills the ";key=" line of the
# same [section]; the player's other lines with content go at the end of their section
# under "; kept from your previous file:" (a section the new file lacks: at the end).
# Plain POSIX awk (Linux often has mawk, not gawk); LC_ALL=C: one byte = one character.
# BINMODE=3: the gawk of Git Bash on Windows (used by the tests) would otherwise drop
# the CRs as it reads and writes; other awks see an unused variable.

# Writes the refreshed file to $3 from the player's file $1 and the package's file $2.
# Prints: <player's settings> <new settings> <settings not listed in the new file>.
merge_ini() {
    OH_OLD="$1" OH_TPL="$2" OH_OUT="$3" LC_ALL=C awk -v BINMODE=3 '
    function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
    # Sets kind (blank, section, comment, setting, other), txt, name, key, val.
    function parse(line,    c, p) {
        txt = trim(line); name = ""; key = ""; val = ""
        if (txt == "") { kind = "blank"; return }
        c = substr(txt, 1, 1)
        p = index(txt, "]")
        if (c == "[" && p > 0) { kind = "section"; name = trim(substr(txt, 2, p - 2)); return }
        if (c == ";" || c == "#") {
            kind = "comment"
            if (match(txt, /^;[A-Za-z0-9_]+=/)) key = substr(txt, 2, RLENGTH - 2)
            return
        }
        p = index(txt, "=")
        if (p > 1) { kind = "setting"; key = trim(substr(txt, 1, p - 1)); val = trim(substr(txt, p + 1)); return }
        kind = "other"
    }
    # Reads a file into arr[1..n] without line ends (a UTF-8 byte-order mark at the
    # start goes, and one CR before each LF). Returns n, or -1 if it cannot be read.
    function readfile(path, arr,    n, line, r) {
        n = 0
        while ((r = (getline line < path)) > 0) {
            if (n == 0 && substr(line, 1, 3) == "\357\273\277") line = substr(line, 4)
            if (line ~ /\r$/) { line = substr(line, 1, length(line) - 1); had_cr = 1 }
            arr[++n] = line
        }
        close(path)
        return r < 0 ? -1 : n
    }
    function emit(s) { printf "%s%s", s, eol > out }
    function emit_kept(s, head,    j) {
        if (!(s in nkept)) return
        emit("")
        if (head != "") emit(head)
        emit("; kept from your previous file:")
        for (j = 1; j <= nkept[s]; j++) emit(kept[s, j])
    }
    BEGIN {
        out = ENVIRON["OH_OUT"]
        had_cr = 0
        nt = readfile(ENVIRON["OH_TPL"], tl)
        eol = had_cr ? "\r\n" : "\n"
        no = readfile(ENVIRON["OH_OLD"], ol)
        if (nt < 0 || no < 0) exit 1

        # the file of the package: the section of each line, the settings, the last line of each section
        sec = ""; known[""] = 1; nslots = 0
        for (i = 1; i <= nt; i++) {
            parse(tl[i])
            if (kind == "section") { sec = tolower(name); known[sec] = 1 }
            lsec[i] = sec
            if (kind != "blank") last[sec] = i
            if ((kind == "comment" && key != "") || (kind == "setting" && key ~ /^[A-Za-z0-9_]+$/)) {
                k = sec SUBSEP tolower(key)
                if (!(k in slot)) { slot[k] = 1; slotkey[i] = key; slotlist[++nslots] = k }
            }
        }

        # the file of the player: the values, and the lines to keep as they are
        sec = ""; nos = 0; settings = 0; unknown = 0
        for (i = 1; i <= no; i++) {
            parse(ol[i])
            if (kind == "blank") continue
            if (kind == "section") {
                sec = tolower(name)
                if (!(sec in heading)) { heading[sec] = txt; order[++nos] = sec }
                continue
            }
            if (kind == "comment") {
                if (key != "") seen[sec SUBSEP tolower(key)] = 1
                continue
            }
            if (kind == "setting") {
                settings++
                k = sec SUBSEP tolower(key)
                seen[k] = 1
                if (k in slot) {
                    if (!(k in fill)) { fill[k] = val; continue }
                } else {
                    unknown++
                }
            }
            kept[sec, ++nkept[sec]] = txt
        }
        new = 0
        for (j = 1; j <= nslots; j++) if (!(slotlist[j] in seen)) new++

        # the new file
        if (!("" in last)) emit_kept("", "")
        for (i = 1; i <= nt; i++) {
            line = tl[i]; s = lsec[i]
            if (i in slotkey) {
                k = s SUBSEP tolower(slotkey[i])
                if (k in fill) line = slotkey[i] "=" fill[k]
            }
            emit(line)
            if ((s in last) && last[s] == i) emit_kept(s, "")
        }
        for (j = 1; j <= nos; j++) if (!(order[j] in known)) emit_kept(order[j], heading[order[j]])
        close(out)
        print settings, new, unknown
        exit 0
    }'
}

# Keeps the last three copies of the settings file: .bak (the newest), .bak2, .bak3.
backup_ini() {
    if [ -e "$1.bak2" ]; then mv -f "$1.bak2" "$1.bak3" || return 1; fi
    if [ -e "$1.bak" ]; then mv -f "$1.bak" "$1.bak2" || return 1; fi
    cp -p "$1" "$1.bak"
}

# Refreshes the player's settings file $1 from the package's $2. Sets ini_was to
# refreshed, current (nothing to change), unreadable (not plain text: it holds zero
# bytes, for example a "Unicode" file) or failed, and ini_settings, ini_new, ini_unknown.
refresh_ini() {
    local tmp counts
    ini_was="failed"; ini_settings=0; ini_new=0; ini_unknown=0
    if ! tr -d '\000' < "$1" | cmp -s - "$1"; then
        ini_was="unreadable"
        return
    fi
    tmp="$(mktemp "${TMPDIR:-/tmp}/h5-together-ini.XXXXXX")" || return
    if counts="$(merge_ini "$1" "$2" "$tmp")" && read -r ini_settings ini_new ini_unknown <<< "$counts" && [ -n "$ini_unknown" ]; then
        if cmp -s "$tmp" "$1"; then
            ini_was="current"
        elif backup_ini "$1" && cat "$tmp" > "$1"; then
            ini_was="refreshed"
        fi
    fi
    rm -f "$tmp"
}

# "1 new setting", "2 new settings"
count_text() { if [ "$1" -eq 1 ]; then echo "1 $2"; else echo "$1 $3"; fi; }

# ---- the add-on's old name: until 0.2.7 it was called open-homm5 ----
# Its files in bin get the new name, with the same rules as Move-OldNames in install.ps1
# (read them there): the settings and their .bak copies, the hero looks, the last log
# (it becomes h5-together.old.log), and the folder open-homm5 (renamed, or merged into
# an h5-together folder that is already there). Names are found in any upper/lower case.
old_moved=" "      # what got the new name, for the summary: " open-homm5.ini open-homm5 ..."
old_left=()        # old files left as they are (the new name is there already)
old_folder_left=0  # files of the old folder that the new folder has too

# Gives the file $1 of bin the name $2, unless that name is there already.
move_old() {
    local old
    old="$(find_ci "$bin" "$1")"
    [ -n "$old" ] && [ -f "$old" ] || return 0
    if [ -n "$(find_ci "$bin" "$2")" ] || [ -e "$bin/$2" ]; then
        old_left+=("$1")
        return 0
    fi
    mv "$old" "$bin/$2" || return 1
    old_moved="$old_moved$1 "
}

# Moves what folder $1 holds into folder $2: what $2 lacks is moved, a folder both have is
# merged the same way, a file both have stays in $1. Then $1 goes if it is empty.
merge_folder() {
    local from="$1" to="$2" item name dst
    for item in "$from"/* "$from"/.[!.]* "$from"/..?*; do
        [ -e "$item" ] || [ -L "$item" ] || continue
        name="${item##*/}"
        dst="$(find_ci "$to" "$name")"
        [ -z "$dst" ] && [ -e "$to/$name" ] && dst="$to/$name"
        if [ -z "$dst" ]; then
            mv "$item" "$to/$name" || return 1
        elif [ -d "$item" ] && [ ! -L "$item" ] && [ -d "$dst" ]; then
            merge_folder "$item" "$dst" || return 1
        else
            old_folder_left=$((old_folder_left + 1))
        fi
    done
    rmdir "$from" 2>/dev/null   # only if nothing is left in it
    return 0
}

move_old_names() {
    local ext log old_dir new_dir old_tpl f p ini_stayed=0
    move_old open-homm5.ini h5-together.ini || return 1
    move_old open-homm5-skins.ini h5-together-skins.ini || return 1
    # the installer's copies of the settings follow the settings file (not when it stayed)
    for f in "${old_left[@]}"; do [ "$f" = open-homm5.ini ] && ini_stayed=1; done
    if [ "$ini_stayed" -eq 0 ]; then
        for ext in bak bak2 bak3; do
            f="$(find_ci "$bin" "open-homm5.ini.$ext")"
            if [ -n "$f" ] && [ -f "$f" ] && [ -z "$(find_ci "$bin" "h5-together.ini.$ext")" ]; then
                mv "$f" "$bin/h5-together.ini.$ext" || return 1
            fi
        done
    fi
    log="$(find_ci "$bin" open-homm5.log)"
    if [ -n "$log" ] && [ -f "$log" ]; then
        if [ -z "$(find_ci "$bin" h5-together.log)" ] && [ -z "$(find_ci "$bin" h5-together.old.log)" ]; then
            mv "$log" "$bin/h5-together.old.log" || return 1
            old_moved="${old_moved}open-homm5.log "
        else
            rm -f "$log" || return 1
        fi
    fi
    log="$(find_ci "$bin" open-homm5.old.log)"
    if [ -n "$log" ] && [ -f "$log" ]; then rm -f "$log" || return 1; fi
    old_dir="$(find_ci "$bin" open-homm5)"
    [ -n "$old_dir" ] && [ -d "$old_dir" ] || return 0
    new_dir="$(find_ci "$bin" h5-together)"
    if [ -z "$new_dir" ]; then
        mv "$old_dir" "$bin/h5-together" || return 1
    else
        # our templates in the old folder go: this install puts them in the new one
        old_tpl="$(find_ci "$old_dir" templates)"
        if [ -n "$old_tpl" ] && [ -d "$old_tpl" ]; then
            for f in "${tpl_files[@]}"; do
                p="$(find_ci "$old_tpl" "$(basename "$f")")"
                [ -n "$p" ] && [ -f "$p" ] && { rm -f "$p" || return 1; }
            done
        fi
        merge_folder "$old_dir" "$new_dir" || return 1
    fi
    old_moved="${old_moved}open-homm5 "
}

echo
echo "H5 Together installer"
echo "---------------------"
echo "This adds H5 Together to Heroes of Might and Magic V. It never changes the game's own files."

src_dll="$here/dinput8.dll"
src_ini="$here/h5-together.ini"
src_tpl="$here/h5-together/templates"
tpl_files=()
for f in "$src_tpl"/*.xdb; do
    [ -f "$f" ] && tpl_files+=("$f")
done
src_txt="$here/h5-together/texts"
txt_files=()
for f in "$src_txt"/*.txt; do
    [ -f "$f" ] && txt_files+=("$f")
done
if [ ! -f "$src_dll" ] || [ ! -f "$src_ini" ] || [ "${#tpl_files[@]}" -eq 0 ] || [ "${#txt_files[@]}" -eq 0 ]; then
    echo "dinput8.dll, h5-together.ini and the folders h5-together/templates and h5-together/texts"
    echo "must be next to this script."
    echo "Please extract the whole zip first, then run install.sh from the new folder."
    echo "Nothing was installed."
    exit 7
fi
if ! is_ours "$src_dll"; then
    echo "The dinput8.dll in this package is not the H5 Together one. The download may be damaged."
    echo "Nothing was installed."
    exit 7
fi

addon="$(addon_version "$src_dll")"
[ -n "$addon" ] || addon="H5 Together"
version="${addon##* }"
echo "This package: $addon"
echo

if ! choose_game_bin; then
    echo
    echo "No game folder, so nothing was installed."
    exit 4
fi
game_info "$bin"
# The add-on's panels follow the game's language (Steam's setting): say which one they will use.
note=""
if [ -n "$gi_language" ]; then
    case "$gi_language" in
        english) code=en ;; french) code=fr ;; german) code=de ;; russian) code=ru ;; polish) code=pl ;;
        spanish) code=es ;; italian) code=it ;; czech) code=cs ;; hungarian) code=hu ;; *) code="" ;;
    esac
    if [ -n "$code" ] && [ -f "$src_txt/$code.txt" ]; then
        note="H5 Together has its panels in $(language_name "$gi_language"), so that is what you will see."
    else
        note="H5 Together has no texts in $(language_name "$gi_language") yet: its panels will be in English (translations are welcome, see README.txt)."
    fi
fi
write_game_info ${note:+"$note"}

# Never overwrite a dinput8.dll that belongs to something else.
dst_dll="$bin/dinput8.dll"
dll_was="new"
existing="$(find_ci "$bin" dinput8.dll)"
if [ -n "$existing" ]; then
    if ! is_ours "$existing"; then
        echo
        echo "Stopped. There is already a file called dinput8.dll in the game folder,"
        echo "and it is not the H5 Together one (another add-on may use it)."
        echo "I did not change anything. To use H5 Together and that add-on together,"
        echo "rename that file to dinput8_next.dll (H5 Together then loads it, and the"
        echo "log says \"chained\"), or move it away; then run this installer again."
        exit 3
    fi
    dst_dll="$existing"
    if cmp -s "$src_dll" "$dst_dll"; then dll_was="same"; else dll_was="older"; fi
fi

# The game must be closed: a program that has the add-on's file loaded must not have it replaced.
if ! confirm_game_closed; then
    echo
    echo "The game is still running, so nothing was installed. Close it and run install.sh again."
    exit 8
fi
echo
echo "Installing ..."

# The files of the old name (open-homm5, until 0.2.7) get the new name first, so that the
# settings refresh and the templates below work on them.
if ! move_old_names; then
    echo
    echo "Could not give your open-homm5 files their new name, h5-together (see the message above)."
    echo "If the game is running, close it and try again."
    exit 5
fi

dst_ini="$(find_ci "$bin" h5-together.ini)"
ini_was="old"
if ! cp -f "$src_dll" "$dst_dll"; then
    echo
    echo "Could not copy dinput8.dll into the game folder (see the message above)."
    echo "If the game is running, close it and try again."
    exit 5
fi
if [ -z "$dst_ini" ]; then
    if cp "$src_ini" "$bin/h5-together.ini"; then
        ini_was="new"
    else
        echo "Could not copy h5-together.ini (see the message above)."
        exit 5
    fi
else
    # the player's settings file: add this version's settings, keep the player's values
    refresh_ini "$dst_ini" "$src_ini"
fi

# Our random map templates: overwrite our own OH-*.xdb files, leave every other file alone.
tpl_dir="$(find_ci "$bin" h5-together)"
[ -n "$tpl_dir" ] && [ -d "$tpl_dir" ] || tpl_dir="$bin/h5-together"
tpl_sub="$(find_ci "$tpl_dir" templates)"
[ -n "$tpl_sub" ] && [ -d "$tpl_sub" ] || tpl_sub="$tpl_dir/templates"
if ! mkdir -p "$tpl_sub"; then
    echo "Could not create the folder $tpl_sub (see the message above)."
    exit 5
fi
tpl_replaced=0
for f in "${tpl_files[@]}"; do
    name="$(basename "$f")"
    dst="$(find_ci "$tpl_sub" "$name")"
    if [ -n "$dst" ]; then tpl_replaced=$((tpl_replaced + 1)); else dst="$tpl_sub/$name"; fi
    if ! cp -f "$f" "$dst"; then
        echo "Could not copy $name into $tpl_sub (see the message above)."
        exit 5
    fi
done
tpl_total="$(find "$tpl_sub" -mindepth 1 -maxdepth 1 -type f | wc -l)"
tpl_others=$((tpl_total - ${#tpl_files[@]}))

# Our language files: the same way (a translation of the player's own stays).
txt_sub="$(find_ci "$tpl_dir" texts)"
[ -n "$txt_sub" ] && [ -d "$txt_sub" ] || txt_sub="$tpl_dir/texts"
if ! mkdir -p "$txt_sub"; then
    echo "Could not create the folder $txt_sub (see the message above)."
    exit 5
fi
txt_replaced=0
txt_codes=""
for f in "${txt_files[@]}"; do
    name="$(basename "$f")"
    txt_codes="$txt_codes, ${name%.txt}"
    dst="$(find_ci "$txt_sub" "$name")"
    if [ -n "$dst" ]; then txt_replaced=$((txt_replaced + 1)); else dst="$txt_sub/$name"; fi
    if ! cp -f "$f" "$dst"; then
        echo "Could not copy $name into $txt_sub (see the message above)."
        exit 5
    fi
done
txt_total="$(find "$txt_sub" -mindepth 1 -maxdepth 1 -type f | wc -l)"
txt_others=$((txt_total - ${#txt_files[@]}))

# Valve's steam_api.dll, if this package has it (GitHub's build has no copy of it): over any
# copy there (the folder is ours), also one under another spelling, which Wine would find too.
src_steam="$here/h5-together/steam_api.dll"
steam_installed=0
if [ -f "$src_steam" ]; then
    dst="$(find_ci "$tpl_dir" steam_api.dll)"
    [ -n "$dst" ] || dst="$tpl_dir/steam_api.dll"
    if ! cp -f "$src_steam" "$dst"; then
        echo "Could not copy steam_api.dll into $tpl_dir (see the message above)."
        exit 5
    fi
    steam_installed=1
fi

# ---- the original 2008 game with 4 GB of memory: bin/H5_Original_4GB.exe ----
# As install.ps1 (Install-Original4Gb): the original game's exe can use only 2 GB of memory,
# too little for big maps; Steam's copy refuses any change. MMH5.5's installer carries the 2008
# exe without Steam's protection, stored as it is: it is copied out of that file (nothing is run
# or installed) and its header gets the 4 GB mark. (Proton may give the game 4 GB by itself;
# the copy does no harm then.)
u16() { od -An -tu2 -j "$2" -N 2 "$1" 2>/dev/null | tr -d ' '; }
u32() { od -An -tu4 -j "$2" -N 4 "$1" 2>/dev/null | tr -d ' '; }

# exe_2008_length <file> <offset>: the length of the 2008 game's exe without Steam's protection
# whose header starts at that offset of the file, else 0.
exe_2008_length() {
    local f="$1" o="$2" pe secs opt table i at e end=0
    [ "$(od -An -c -j "$o" -N 2 "$f" 2>/dev/null | tr -d ' ')" = "MZ" ] || { echo 0; return; }
    pe="$(u32 "$f" $((o + 60)))"
    if [ -z "$pe" ] || [ "$pe" -lt 64 ] || [ "$pe" -gt 768 ]; then echo 0; return; fi
    [ "$(u32 "$f" $((o + pe)))" = "17744" ] || { echo 0; return; }            # "PE\0\0"
    [ "$(u16 "$f" $((o + pe + 4)))" = "332" ] || { echo 0; return; }           # 32-bit
    [ "$(u32 "$f" $((o + pe + 8)))" = "$PE_DATE_2008" ] || { echo 0; return; } # 2008-06-04
    [ "$(u32 "$f" $((o + pe + 80)))" = "14520320" ] || { echo 0; return; }      # 0x00DD9000: unprotected
    secs="$(u16 "$f" $((o + pe + 6)))"
    opt="$(u16 "$f" $((o + pe + 20)))"
    table=$((o + pe + 24 + opt))
    for ((i = 0; i < secs; i++)); do
        at=$((table + 40 * i))
        e=$(( $(u32 "$f" $((at + 20))) + $(u32 "$f" $((at + 16))) ))
        [ "$e" -gt "$end" ] && end=$e
    done
    if [ "$end" -lt 1024 ] || [ "$end" -gt 67108864 ]; then echo 0; return; fi
    echo "$end"
}

# The 2008 game's own exe (it loads um.dll, not MMH5.5's 55u.dll).
exe_2008_bytes() {
    LC_ALL=C grep -q -a -F um.dll "$1" && ! LC_ALL=C grep -q -a -F 55u.dll "$1"
}

# copy_exe_2008 <file> <out>: copies the 2008 exe found in <file> (MMH5.5's installer) to <out>.
copy_exe_2008() {
    local f="$1" out="$2" off len
    while IFS=: read -r off _; do
        len="$(exe_2008_length "$f" "$off")"
        [ "$len" -gt 0 ] || continue
        dd if="$f" of="$out" bs=1M iflag=skip_bytes,count_bytes skip="$off" count="$len" status=none 2>/dev/null || continue
        [ "$(wc -c < "$out" | tr -d ' ')" = "$len" ] && exe_2008_bytes "$out" && return 0
    done < <(LC_ALL=C grep -obUa $'MZ\x90' "$f" 2>/dev/null)   # (each exe header's first bytes; no -P: Git Bash refuses it)
    rm -f "$out"
    return 1
}

# The 4 GB mark (IMAGE_FILE_LARGE_ADDRESS_AWARE) in the exe $1's header.
set_4gb() {
    local pe at b
    pe="$(u32 "$1" 60)"
    at=$((pe + 22))
    b="$(od -An -tu1 -j "$at" -N 1 "$1" | tr -d ' ')"
    printf "$(printf '\\%03o' $((b | 32)))" | dd of="$1" bs=1 seek="$at" conv=notrunc status=none
}

# MMH5.5's own release on GitHub holds the same 2008 exe in a zip (as install.ps1,
# Get-Exe2008FromGitHub): only its piece is fetched (curl, an HTTP range: about 5.4 MB),
# unpacked (python3) and checked against the 2008 exe's SHA-256.
MMH55_ZIP_URL="https://github.com/Might-Magic-Heroes-5-5/MMH55/releases/download/RC19k/manual_setup_RC19k.zip"
MMH55_ZIP_OFFSET=2988923
MMH55_ZIP_PACKED=5401151
MMH55_ZIP_SHA256="6502a8c4cd0e03e9ce43b474bcb4a85acb8b8bf5050d56317e93c8f13fff50bd"

# fetch_exe_2008 <out>: the 2008 exe from MMH5.5's release on GitHub, checked; fails otherwise
# (fetch_why says why).
fetch_exe_2008() {
    local out="$1" piece n e start
    fetch_why=""
    if ! command -v curl > /dev/null 2>&1 || ! command -v python3 > /dev/null 2>&1; then
        fetch_why="it needs curl and python3"; return 1
    fi
    piece="$(mktemp)"
    if ! curl -fsSL -r "$MMH55_ZIP_OFFSET-$((MMH55_ZIP_OFFSET + 30 + 1024 + MMH55_ZIP_PACKED))" -o "$piece" "$MMH55_ZIP_URL"; then
        rm -f "$piece"; fetch_why="the download failed"; return 1
    fi
    n="$(u16 "$piece" 26)"
    e="$(u16 "$piece" 28)"
    start=$((30 + n + e))
    if [ "$(u32 "$piece" 0)" != "67324752" ] || [ "$(dd if="$piece" bs=1 skip=30 count="$n" status=none 2>/dev/null)" != "bin/H5_Game.exe" ]; then
        rm -f "$piece"; fetch_why="the downloaded piece is not the 2008 exe (MMH5.5 may have changed its release)"; return 1
    fi
    if ! python3 -I -c 'import sys, zlib
d = open(sys.argv[1], "rb").read()
s, n = int(sys.argv[2]), int(sys.argv[3])
open(sys.argv[4], "wb").write(zlib.decompressobj(-15).decompress(d[s:s + n]))' "$piece" "$start" "$MMH55_ZIP_PACKED" "$out"; then
        rm -f "$piece" "$out"; fetch_why="unpacking it failed"; return 1
    fi
    rm -f "$piece"
    if [ "$(sha256_of "$out")" != "$MMH55_ZIP_SHA256" ]; then
        rm -f "$out"; fetch_why="the downloaded exe is not the expected 2008 exe (its SHA-256 differs)"; return 1
    fi
}

original_state="none"
original_from=""
dst4="$bin/H5_Original_4GB.exe"
if [ -n "$(find_ci "$bin" H5_Original_4GB.exe)" ]; then
    original_state="kept"
else
    # a copy of the game's own 2008 exe without Steam's protection, already in the game folder
    for name in H5_Game.exe H5_GameOLD.exe; do
        f="$(find_ci "$bin" "$name")"
        if [ -n "$f" ] && [ -f "$f" ] && [ "$(exe_2008_length "$f" 0)" = "$(wc -c < "$f" | tr -d ' ')" ] && exe_2008_bytes "$f"; then
            cp -f "$f" "$dst4" && original_from="$name" && break
        fi
    done
    sources=()
    if [ -z "$original_from" ]; then
        if [ -n "$mmh55_installer_arg" ]; then
            sources=("$mmh55_installer_arg")
        else
            for place in "$here" "$(dirname "$here")" "$HOME/Downloads" "$HOME/Desktop" "${XDG_DOWNLOAD_DIR:-}"; do
                [ -n "$place" ] && [ -d "$place" ] || continue
                while IFS= read -r f; do sources+=("$f"); done < <(find "$place" -maxdepth 1 -type f -iname 'MMH55*.exe' -size +100M 2>/dev/null)
            done
        fi
        for src in "${sources[@]}"; do
            if [ ! -f "$src" ]; then original_state="failed"; original_from="$src (the file was not found)"; break; fi
            echo "Looking for the original game's exe in $(basename "$src") ..."
            if copy_exe_2008 "$src" "$dst4"; then original_from="$(basename "$src")"; break; fi
        done
    fi
    # else, with the player's yes: MMH5.5's release on GitHub (about 5.4 MB)
    if [ -z "$original_from" ] && [ "$original_state" != "failed" ] && [ "${#sources[@]}" -eq 0 ] &&
       [ "$no_download" = "0" ] && [ -z "${H5_TOGETHER_NO_DOWNLOAD:-}" ]; then
        echo
        echo "The original 2008 game (without MMH5.5) with 4 GB of memory: I can make bin/H5_Original_4GB.exe."
        echo "Its exe comes from MMH5.5's own release on GitHub (only that file: about 5.4 MB; MMH5.5"
        echo "itself is not installed). Skip it if you only play MMH5.5."
        answer=""
        if read -r -p "Fetch it? Y = yes, N = no: " answer; then
            case "$(printf '%s' "$answer" | tr 'A-Z' 'a-z')" in
                ""|y|yes|o|oui)
                    echo "Fetching the original game's exe from MMH5.5's release on GitHub ..."
                    if fetch_exe_2008 "$dst4"; then
                        original_from="MMH5.5's release on GitHub, checked"
                    else
                        original_state="failed"
                        original_from="MMH5.5's release on GitHub ($fetch_why)"
                    fi
                    ;;
            esac
        fi
    fi
    if [ -n "$original_from" ] && [ -f "$dst4" ] && [ "$original_state" != "failed" ]; then
        set_4gb "$dst4"
        original_state="made"
    elif [ "$original_state" != "failed" ] && [ "${#sources[@]}" -gt 0 ]; then
        original_state="failed"
        original_from="${sources[*]} (the original game's exe is not in it)"
    fi
fi

echo
echo "Done. What I did:"
if [ "$old_moved" != " " ]; then
    what=""
    case "$old_moved" in *" open-homm5.ini "*) what="$what, the settings" ;; esac
    case "$old_moved" in *" open-homm5-skins.ini "*) what="$what, the hero looks" ;; esac
    case "$old_moved" in *" open-homm5.log "*) what="$what, the log" ;; esac
    case "$old_moved" in *" open-homm5 "*) what="$what, the folder" ;; esac
    echo "  - open-homm5 is now called H5 Together: your files got the new name, h5-together (${what#, })"
fi
for name in "${old_left[@]}"; do
    echo "  - kept $name as it is: there is already a h5-together${name#open-homm5}, which is the one used"
done
[ "$old_folder_left" -gt 0 ] && echo "  - left $old_folder_left file(s) in the folder open-homm5: the folder h5-together has them too"
case "$dll_was" in
    new)   echo "  - added dinput8.dll (the add-on)" ;;
    same)  echo "  - dinput8.dll was already installed and up to date" ;;
    older) echo "  - replaced the older H5 Together dinput8.dll with this version" ;;
esac
case "$ini_was" in
    new) echo "  - added h5-together.ini (settings, all at their defaults)" ;;
    refreshed)
        echo "  - settings file refreshed: $ini_settings of your settings kept, $(count_text "$ini_new" "new setting" "new settings") available (see the comments)"
        echo "    (your previous file is saved as h5-together.ini.bak)"
        [ "$ini_unknown" -gt 0 ] && echo "    ($(count_text "$ini_unknown" setting settings) not listed in this version: kept at the end of their section, under \"kept from your previous file\")"
        ;;
    current)
        if [ "$ini_settings" -gt 0 ]; then
            echo "  - settings file already up to date: $ini_settings of your settings kept"
        else
            echo "  - settings file already up to date (every setting at its default)"
        fi
        ;;
    unreadable) echo "  - kept h5-together.ini as it is: it is not a plain text file, so I could not add the new settings" ;;
    *)
        echo "  - could not refresh h5-together.ini (see the message above)"
        echo "    If the game is running, close it and run the installer again."
        ;;
esac
echo "  - installed ${#tpl_files[@]} random map templates in h5-together/templates (they are in the game's random map list)"
[ "$tpl_replaced" -gt 0 ] && echo "    ($tpl_replaced of them replaced our files from an earlier install)"
[ "$tpl_others" -gt 0 ] && echo "    (left the $tpl_others other file(s) in that folder alone)"
echo "    Everyone in a multiplayer game needs the same templates: install the same version."
echo "  - installed ${#txt_files[@]} language files in h5-together/texts (${txt_codes#, }): the words of the add-on's panels"
[ "$txt_replaced" -gt 0 ] && echo "    ($txt_replaced of them replaced our files from an earlier install)"
[ "$txt_others" -gt 0 ] && echo "    (left the $txt_others other file(s) in that folder alone)"
[ "$steam_installed" -eq 1 ] && echo "  - installed Valve's steam_api.dll in h5-together (online play over Steam, off until you turn it on in Options)"
case "$original_state" in
    made)
        echo "  - made H5_Original_4GB.exe: the original 2008 game, with 4 GB of memory (its exe, from $original_from)"
        echo "    To play the original game without MMH5.5, use the launch option for it below."
        ;;
    kept) echo "  - H5_Original_4GB.exe (the original game with 4 GB of memory) is already there" ;;
esac
echo "  - did not change or replace any file of the game itself"
case "$original_state" in
    none)
        echo
        echo "Tip: to play the original game (without MMH5.5) with 4 GB of memory, H5 Together can make"
        echo "bin/H5_Original_4GB.exe from MMH5.5's installer file (MMH55_RC19k.exe, from MMH5.5's ModDB page:"
        echo "download it, you do not need to install it). Put it in ~/Downloads and run install.sh again."
        ;;
    failed)
        echo
        echo "Could not make H5_Original_4GB.exe (the original game with 4 GB) from $original_from."
        ;;
esac

if [ "$gi_mmh55" = "missing" ] && [ "$gi_has2008" != "1" ]; then
    echo
    echo "Reminder: neither MMH5.5 nor the original 2008 game's exe was found in this game folder."
    echo "H5 Together does nothing with Steam's 2025 rebuild: install MMH5.5 first (see docs/linux.md)."
elif [ "$gi_mmh55" = "untested" ]; then
    echo
    echo "Reminder: this MMH5.5 is a version we have not tested. If H5 Together seems to do nothing,"
    echo "h5-together.log (see below) tells why."
fi
echo
echo "All done: $addon is installed."
echo
echo "What to do next:"
cat <<'EOF'
  1. In Steam (I do not touch Steam's settings myself): right-click the game > Properties >
     General > Launch options, and paste this line:

         WINEDLLOVERRIDES="dinput8=n,b" %command%

     It tells Wine (Proton) to load our dinput8.dll instead of its own.

EOF
if [ -f "$bin/H5_Original_4GB.exe" ]; then
cat <<'EOF'
  2. Which exe starts? Steam starts H5_Game.exe. For the original game with 4 GB of
     memory (H5_Original_4GB.exe, which the installer made), paste this line instead:

         WINEDLLOVERRIDES="dinput8=n,b" bash -c 'exec "${@/H5_Game.exe/H5_Original_4GB.exe}"' -- %command%

     For MMH5.5 instead, the same line with its co-op exe:

         WINEDLLOVERRIDES="dinput8=n,b" bash -c 'exec "${@/H5_Game.exe/MMH55_Utility_64.exe}"' -- %command%

EOF
elif [ "$gi_mmh55" != "missing" ]; then
cat <<'EOF'
  2. Which exe starts? Steam starts H5_Game.exe, and with MMH5.5 the add-on works with
     its co-op exe, MMH55_Utility_64.exe. To make Steam start that exe, paste this line
     instead of the one above:

         WINEDLLOVERRIDES="dinput8=n,b" bash -c 'exec "${@/H5_Game.exe/MMH55_Utility_64.exe}"' -- %command%

     If it does not work, docs/linux.md (step 3) has another way: swap the two exe
     files. https://github.com/jozrcr/h5-together/blob/main/docs/linux.md

EOF
else
cat <<'EOF'
  2. Which exe starts? Steam starts H5_Game.exe, the original game: the line above is enough.

EOF
fi
cat <<'EOF'
  3. Start the game from Steam. Then look in the game's bin folder for the file
     h5-together.log:
EOF
echo "         $bin"
echo "     If its first line says \"H5 Together hook DLL $version loaded\", the add-on is running."
echo "  4. Everyone in the same co-op game should install the same version."
echo
echo "To change a setting: in the game, Options > H5 Together (or press F3)."
echo "Or open h5-together.ini in that folder: every line says what it does."
echo "To remove H5 Together, run: bash uninstall.sh. If something goes wrong, run: bash bug-report.sh"
exit 0
