#!/bin/bash
# =====================================================================
#  SMG Online - macOS and Linux launcher (same job as start-galaxy.ps1 on Windows)
#
#    ./start-galaxy.sh join            ask for a server (Enter = last one) and join
#    ./start-galaxy.sh join 203.0.113.7:5029
#    ./start-galaxy.sh host            start the server here + one game
#    ./start-galaxy.sh paths           set/change where Dolphin and the game are
#    ./start-galaxy.sh colour          choose your player colour
#
#  Written for the bash 3.2 that ships with macOS: no bash 4 features.
# =====================================================================
set -u

MODE="${1:-join}"
SERVER="${2:-}"
PLAYERS="${PLAYERS:-1}"
DRY_RUN="${DRY_RUN:-}"

BASE="$(cd "$(dirname "$0")" && pwd)"
# Darwin = macOS; anything else is treated as Linux
OS="${SMG_OS:-$(uname -s)}"
if [ "$OS" = Darwin ]; then
    APPDIR="$HOME/Library/Application Support/SMG-Online"
else
    APPDIR="${XDG_DATA_HOME:-$HOME/.local/share}/SMG-Online"
fi
SETTINGS="$APPDIR/settings.txt"
mkdir -p "$APPDIR"

say()  { printf '%s\n' "$*"; }
warn() { printf '      %s\n' "$*"; }
die()  { printf '\nERROR: %s\n' "$*"; exit 1; }

# ---- remembered settings (dolphin, game, last server) -----------------
get_setting() { [ -f "$SETTINGS" ] && sed -n "s/^$1=//p" "$SETTINGS" | head -n 1; }
SAVED_DOLPHIN="$(get_setting dolphin)"
SAVED_GAME="$(get_setting game)"
SAVED_SERVER="$(get_setting server)"
COLOR="$(get_setting color)"
case "$COLOR" in [0-7]) ;; *) COLOR=0 ;; esac
save_settings() {
    {
        [ -n "$DOLPHIN" ] && printf 'dolphin=%s\n' "$DOLPHIN"
        [ -n "$GAME" ] && printf 'game=%s\n' "$GAME"
        [ -n "$SAVED_SERVER" ] && printf 'server=%s\n' "$SAVED_SERVER"
        printf 'color=%s\n' "$COLOR"
    } > "$SETTINGS"
}

# A path typed, pasted or dragged into Terminal: drop quotes, the backslashes
# Terminal adds before spaces, and stray spaces at the ends.
clean_path() {
    printf '%s' "$1" | sed -e "s/^[[:space:]]*//" -e "s/[[:space:]]*$//" -e "s/^'\(.*\)'$/\1/" -e 's/^"\(.*\)"$/\1/' -e 's/\\\(.\)/\1/g'
}

# Dolphin.app is a folder; the program inside is what we start.
resolve_dolphin() {
    case "$1" in
        *.app|*.app/) printf '%s' "${1%/}/Contents/MacOS/Dolphin" ;;
        *) printf '%s' "$1" ;;
    esac
}

ask_file() {  # ask_file "question" "current value" kind
    local answer path
    while true; do
        if [ -n "$2" ]; then
            printf '%s\n  now: %s\n  New path (drag the file here), or just press Enter to keep it: ' "$1" "$2" >&2
        else
            printf '%s\n  Drag the file into this window (or paste its path) and press Enter: ' "$1" >&2
        fi
        IFS= read -r answer || exit 1
        path="$(clean_path "$answer")"
        if [ -z "$path" ] && [ -n "$2" ]; then printf '%s' "$2"; return; fi
        [ "$3" = dolphin ] && path="$(resolve_dolphin "$path")"
        if [ -f "$path" ]; then printf '%s' "$path"; return; fi
        printf '      Not found, try again.\n' >&2
    done
}

find_first() { local c; for c in "$@"; do [ -n "$c" ] && [ -f "$c" ] && { printf '%s' "$c"; return; }; done; }

if [ "$OS" = Darwin ]; then
    DOLPHIN="$(find_first "$SAVED_DOLPHIN" \
        "/Applications/Dolphin.app/Contents/MacOS/Dolphin" \
        "$HOME/Applications/Dolphin.app/Contents/MacOS/Dolphin" \
        "$HOME/Downloads/Dolphin.app/Contents/MacOS/Dolphin")"
else
    DOLPHIN="$(find_first "$SAVED_DOLPHIN" \
        "$(command -v dolphin-emu 2>/dev/null)" \
        "/usr/bin/dolphin-emu" "/usr/games/dolphin-emu" "/usr/local/bin/dolphin-emu" \
        "/var/lib/flatpak/exports/bin/org.DolphinEmu.dolphin-emu" \
        "$HOME/.local/share/flatpak/exports/bin/org.DolphinEmu.dolphin-emu")"
fi
GAME="$(find_first "$SAVED_GAME")"

if [ "$MODE" = colour ] || [ "$MODE" = color ]; then
    say "Player colours (shirt and cap / overalls):"
    say "  0  Mario (normal)      4  Blue and red"
    say "  1  Green               5  White and red"
    say "  2  Yellow and purple   6  Orange and teal"
    say "  3  Purple and black    7  Pink and white"
    while true; do
        printf 'Your colour (0-7, now %s): ' "$COLOR"
        IFS= read -r answer || exit 1
        case "$answer" in
            "") break ;;
            [0-7]) COLOR="$answer"; break ;;
            *) say "      Type a number from 0 to 7." ;;
        esac
    done
    save_settings
    say "Saved. Other players see you in that colour the next time you start the game."
    exit 0
fi

if [ "$MODE" = paths ]; then
    DOLPHIN="$(ask_file "Dolphin (the Dolphin app)" "$DOLPHIN" dolphin)"
    GAME="$(ask_file "Your Super Mario Galaxy (USA) game file (.wbfs/.iso/.rvz)" "$GAME" game)"
    save_settings
    say "Saved. You will not be asked again."
    exit 0
fi

[ -n "$DOLPHIN" ] || DOLPHIN="$(ask_file "Where is Dolphin (the Dolphin app)?" "" dolphin)"
[ -n "$GAME" ] || GAME="$(ask_file "Where is your Super Mario Galaxy (USA) game file (.wbfs/.iso/.rvz)?" "" game)"

# ---- 1: server ---------------------------------------------------------
port_in_use() {
    if command -v lsof >/dev/null 2>&1; then lsof -nP -iUDP:"$1" >/dev/null 2>&1
    elif command -v ss >/dev/null 2>&1; then ss -lun 2>/dev/null | grep -q ":$1 "
    else return 1; fi
}

# Linux: the first terminal program found gets the server; with none, it runs
# in the background and writes to server/server.log.
start_server_linux() {
    local run="cd '$BASE/server' && python3 smg_server.py; echo; echo 'Server stopped. Press Enter to close.'; read _"
    if command -v x-terminal-emulator >/dev/null 2>&1; then x-terminal-emulator -e bash -c "$run" >/dev/null 2>&1 &
    elif command -v gnome-terminal >/dev/null 2>&1; then gnome-terminal -- bash -c "$run" >/dev/null 2>&1 &
    elif command -v konsole >/dev/null 2>&1; then konsole -e bash -c "$run" >/dev/null 2>&1 &
    elif command -v xfce4-terminal >/dev/null 2>&1; then xfce4-terminal -x bash -c "$run" >/dev/null 2>&1 &
    elif command -v xterm >/dev/null 2>&1; then xterm -e bash -c "$run" >/dev/null 2>&1 &
    else
        ( cd "$BASE/server" && nohup python3 smg_server.py > server.log 2>&1 & )
        warn "no terminal program found: the server runs in the background (log: server/server.log)."
        warn "stop it with:  pkill -f smg_server.py"
    fi
}

PORT=5029
if [ "$MODE" = host ]; then
    INI="$BASE/server/server-settings.ini"
    if [ -f "$INI" ]; then
        p="$(sed -n 's/^[[:space:]]*port[[:space:]]*=[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$INI" | head -n 1)"
        [ -n "$p" ] && PORT="$p"
    fi
    ADDRESS="127.0.0.1:$PORT"
    say "[1/3] Hosting on this computer (UDP $PORT)..."
    if [ -n "$DRY_RUN" ]; then
        warn "(dry run: not starting the server)"
    elif port_in_use "$PORT"; then
        warn "server already up."
    else
        command -v python3 >/dev/null 2>&1 || die "Python 3 is needed to host (https://www.python.org/downloads/)."
        # Its own Terminal window: shows who joins; closing it stops the server.
        if [ "$OS" = Darwin ]; then
            osascript -e "tell application \"Terminal\" to do script \"cd '$BASE/server' && python3 smg_server.py\"" >/dev/null \
                || die "could not open a Terminal window for the server."
        else
            start_server_linux
        fi
        sleep 2
    fi
else
    while [ -z "$SERVER" ]; do
        if [ -n "$SAVED_SERVER" ]; then
            printf 'Server address (press Enter for %s): ' "$SAVED_SERVER"
            IFS= read -r SERVER || exit 1
            [ -z "$SERVER" ] && SERVER="$SAVED_SERVER"
        else
            printf 'Server address (e.g. 203.0.113.7 or 203.0.113.7:5029): '
            IFS= read -r SERVER || exit 1
        fi
        SERVER="$(printf '%s' "$SERVER" | tr -d '[:space:]')"
    done
    case "$SERVER" in *:*) ADDRESS="$SERVER" ;; *) ADDRESS="$SERVER:$PORT" ;; esac
    SAVED_SERVER="$SERVER"
    say "[1/3] Joining server $ADDRESS"
fi
save_settings

# The game reads this file (through the mod loader) when it boots
# "address:port c=N": N is this player's colour
LINE="$ADDRESS"
[ "$COLOR" != 0 ] && LINE="$ADDRESS c=$COLOR"
printf '%s' "$LINE" > "$BASE/riivolution/serverIP.txt"
warn "serverIP.txt -> $LINE"

# ---- 2: Dolphin game preset (absolute paths, rewritten every launch) ----
json_escape() { printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'; }
PRESET="$APPDIR/galaxy-online.json"
cat > "$PRESET" <<EOF
{
  "type": "dolphin-game-mod-descriptor",
  "version": 1,
  "base-file": "$(json_escape "$GAME")",
  "display-name": "SMG Online (USA)",
  "riivolution": {
    "patches": [
      {
        "xml": "$(json_escape "$BASE/riivolution/riivo_USA.xml")",
        "root": "$(json_escape "$BASE/riivolution")",
        "options": [ { "section-name": "Syati Loader", "option-id": "smgmp", "choice": 1 } ]
      }
    ]
  }
}
EOF

# ---- 3: one Dolphin per player ------------------------------------------
if [ "$OS" = Darwin ]; then
    GLOBAL="$HOME/Library/Application Support/Dolphin"
else
    # Native package, Flatpak, or the old location; the first one that exists
    GLOBAL="${XDG_DATA_HOME:-$HOME/.local/share}/dolphin-emu"
    for d in "$GLOBAL" "$HOME/.var/app/org.DolphinEmu.dolphin-emu/data/dolphin-emu" "$HOME/.dolphin-emu"; do
        [ -d "$d" ] && { GLOBAL="$d"; break; }
    done
fi
SAVE_REL="Wii/title/00010000/524d4745"   # RMGE = Super Mario Galaxy (USA)

# Make sure "[Core] SerialPort1 = 255" (nothing in GameCube slot SP1): arcade
# hardware there makes Dolphin refuse to boot any normal game.
fix_sp1() {
    local ini="$1" tmp="$1.tmp"
    [ -f "$ini" ] || : > "$ini"
    awk '
        BEGIN { insec = 0; done = 0; seen = 0 }
        /^\[/ {
            if (insec && !done) { print "SerialPort1 = 255"; done = 1 }
            insec = ($0 ~ /^\[Core\][[:space:]]*$/)
            if (insec) seen = 1
        }
        insec && /^[[:space:]]*SerialPort1[[:space:]]*=/ { print "SerialPort1 = 255"; done = 1; next }
        { print }
        END {
            if (insec && !done) print "SerialPort1 = 255"
            if (!seen) { print "[Core]"; print "SerialPort1 = 255" }
        }' "$ini" > "$tmp" && mv "$tmp" "$ini"
}

i=1
while [ "$i" -le "$PLAYERS" ]; do
    USERDIR="$APPDIR/profiles/p$i"
    if [ ! -d "$USERDIR" ]; then
        # First run: start from your normal Dolphin settings (controller,
        # graphics) and your existing SMG save, as copies.
        mkdir -p "$USERDIR/Config"
        for f in Dolphin.ini GFX.ini WiimoteNew.ini GCPadNew.ini Hotkeys.ini; do
            [ -f "$GLOBAL/Config/$f" ] && cp "$GLOBAL/Config/$f" "$USERDIR/Config/$f"
        done
        if [ -d "$GLOBAL/$SAVE_REL" ]; then
            mkdir -p "$USERDIR/$(dirname "$SAVE_REL")"
            cp -R "$GLOBAL/$SAVE_REL" "$USERDIR/$SAVE_REL"
        fi
        warn "created profile p$i ($USERDIR)"
    fi
    mkdir -p "$USERDIR/Config"
    fix_sp1 "$USERDIR/Config/Dolphin.ini"

    if [ ! -f "$USERDIR/Config/WiimoteNew.ini" ] || ! grep -q '^Source[[:space:]]*=[[:space:]]*[12]' "$USERDIR/Config/WiimoteNew.ini"; then
        warn "No Wii Remote is set up for player $i yet. In the Dolphin window that opens:"
        warn "Controllers > Wii Remote 1 > Emulated Wii Remote > Configure (Extension: Nunchuk),"
        warn "then close Dolphin and start again."
    fi

    say "[2/3] Launching player $i..."
    if [ -z "$DRY_RUN" ]; then
        nohup "$DOLPHIN" -u "$USERDIR" -e "$PRESET" >/dev/null 2>&1 &
        [ "$i" -lt "$PLAYERS" ] && sleep 3
    fi
    i=$((i + 1))
done

say ""
say "[3/3] Done. Players see each other when they are in the same galaxy and star."
if [ "$MODE" = host ]; then
    say "      Keep the server window open while playing."
    say "      Friends join with your public IP and port $PORT (forward UDP $PORT on your router)."
fi
