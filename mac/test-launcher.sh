#!/bin/bash
# Exercises start-galaxy.sh against a fake Mac home folder (runs on Linux too).
#   bash test-launcher.sh <folder containing riivolution/riivo_USA.xml and server/server-settings.ini>
set -e
SRC="$(cd "$1" && pwd)"
HERE="$(cd "$(dirname "$0")" && pwd)"
T="$(mktemp -d)"
G="$T/home/Library/Application Support/Dolphin"
mkdir -p "$G/Config" "$G/Wii/title/00010000/524d4745/data" "$T/My Apps/Dolphin.app/Contents/MacOS" \
         "$T/my games" "$T/pack/riivolution" "$T/pack/server"
cp "$HERE/start-galaxy.sh" "$T/pack/"
cp "$SRC/riivolution/riivo_USA.xml" "$T/pack/riivolution/"
sed 's/^port = 5029/port = 1027/' "$SRC/server/server-settings.ini" > "$T/pack/server/server-settings.ini"
printf '#!/bin/bash\necho "$@" > "%s/dolphin-args.txt"\n' "$T" > "$T/My Apps/Dolphin.app/Contents/MacOS/Dolphin"
chmod +x "$T/My Apps/Dolphin.app/Contents/MacOS/Dolphin"
echo x > "$T/my games/Super Mario Galaxy.wbfs"
echo save > "$G/Wii/title/00010000/524d4745/data/GameData.bin"
printf '[General]\nfoo = 1\n[Core]\nSerialPort1 = 6\nGFXBackend = OGL\n[DSP]\nx = 1\n' > "$G/Config/Dolphin.ini"
printf '[Wiimote1]\nSource = 1\n' > "$G/Config/WiimoteNew.ini"
export HOME="$T/home"
A="$HOME/Library/Application Support/SMG-Online"
cd "$T/pack"

echo "=== first join: dragged .app path with escaped spaces, quoted game path, typed server"
printf '%s\n%s\n%s\n' "$(printf '%s' "$T/My Apps/Dolphin.app" | sed 's/ /\\ /g') " "'$T/my games/Super Mario Galaxy.wbfs'" "203.0.113.7:1027" \
    | bash ./start-galaxy.sh join
sleep 1
echo "dolphin got: $(sed "s|$T|<T>|g" "$T/dolphin-args.txt")"
echo "--- settings:"; sed "s|$T|<T>|g" "$A/settings.txt"
echo "--- serverIP: $(cat riivolution/serverIP.txt)"
echo "--- profile Dolphin.ini:"; cat "$A/profiles/p1/Config/Dolphin.ini"
echo "--- save copied: $(cat "$A/profiles/p1/Wii/title/00010000/524d4745/data/GameData.bin")"
python3 -c "import json,sys;d=json.load(open(sys.argv[1]));print('preset parses, option:',d['riivolution']['patches'][0]['options'][0]['option-id'])" "$A/galaxy-online.json"

echo "=== second join: just Enter"
printf '\n' | DRY_RUN=1 bash ./start-galaxy.sh join | head -3

echo "=== host (dry run) reads the port from the settings file"
DRY_RUN=1 bash ./start-galaxy.sh host | head -3
echo "serverIP: $(cat riivolution/serverIP.txt)"

echo "=== SP1 fix on an ini with no [Core] section"
printf '[General]\na = 1\n' > "$T/x.ini"
eval "$(sed -n '/^fix_sp1()/,/^}/p' start-galaxy.sh)"
fix_sp1 "$T/x.ini"; cat "$T/x.ini"
echo "=== done (test files left in $T)"
