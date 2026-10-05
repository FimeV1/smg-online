"""Deploys the current build and assembles the Windows, Mac and Wii packs.

    python tools/pack_all.py [output folder]      (default: ./packs)

Run from the repository root after building the client (see docs/BUILDING.md)."""
import os
import shutil
import sys
import time
import zipfile

BASE = os.getcwd()
OUT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(BASE, "packs")


def sub(path, pairs):
    s = open(path, encoding="utf-8").read()
    for a, b in pairs:
        if b in s:
            continue                      # already applied
        assert s.count(a) == 1, (path, a[:60], s.count(a))
        s = s.replace(a, b)
    open(path, "w", encoding="utf-8").write(s)


# ---- readmes ------------------------------------------------------------
sub("FRIENDS - READ ME.txt", [(
    """First: right-click the zip > Extract All.""",
    """EASIEST WAY: double-click "SMG ONLINE.bat". A window opens where you pick
your colour, type the host's address and press Join (or Host). The other
.bat files do the same things without the window.

Player colours: everyone can pick one of 8 outfits (shirt/cap and overalls),
so you can tell each other apart. Players on an older version of the mod
simply show as normal Mario.

Title screen: the game's logo gets ONLINE under it. This is made from your
own game file the first time you start; untick it in the window if you do
not want it.

First: right-click the zip > Extract All."""),
])
sub(os.path.join("mac", "READ ME - MAC.txt"), [(
    """Host a game
-----------""",
    """Player colour
-------------
Double-click "CHOOSE COLOUR.command" and type a number from 0 to 7. Other
players then see your Mario in that outfit (0 is normal Mario). Players on
Windows pick theirs in the launcher window; everyone sees everyone's colour.
(The ONLINE title screen is Windows-only for now.)

Host a game
-----------"""),
])
sub(os.path.join("wii", "READ ME - WII AND WII U.txt"), [(
    """Play
----""",
    """Player colour (optional)
------------------------
Add  c=N  after the address in serverIP.txt, with N from 0 to 7, e.g.
    203.0.113.7:1027 c=4
0 normal Mario, 1 green, 2 yellow and purple, 3 purple and black,
4 blue and red, 5 white and red, 6 orange and teal, 7 pink and white.
Other players see you in that outfit.

Play
----"""),
])
sub("README.md", [(
    """## What is synced
""",
    """## Launcher window, colours, title screen

`SMG ONLINE.bat` opens a small window: pick a player colour (8 outfits that
recolour only the shirt, cap and overalls), then Join or Host. Settings are
remembered per Windows user. The colour travels in the position packet (a
byte that used to be padding), so old and new versions can play together.

The title screen gets `ONLINE` under the logo. It is built on each player's
PC from their own game file (`title\\build-title.ps1` + our own artwork in
`title\\online-strip.bin`), so no game files are shipped.

## What is synced
"""),
])

# ---- deploy the current build -------------------------------------------
shutil.copyfile(os.path.join("SMGNetworkMultiplayer", "bin", "Debug", "Dolphin", "CustomCode_USA.bin"),
                os.path.join("riivolution", "CustomCode_USA.bin"))
shutil.copyfile(os.path.join("SMGNetworkMultiplayer", "bin", "Debug", "CustomCode_USA.bin"),
                os.path.join("wii", "SD-card", "smgonline", "CustomCode_USA.bin"))


# ---- zips ----------------------------------------------------------------
def make_zip(path, root, items):
    """items: (source path or bytes, name in zip, executable)"""
    if os.path.exists(path):
        os.remove(path)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for src, name, exe in items:
            data = src if isinstance(src, bytes) else open(src, "rb").read()
            if name.endswith((".sh", ".command")):
                data = data.replace(b"\r\n", b"\n")
            zi = zipfile.ZipInfo(root + "/" + name, time.localtime()[:6])
            zi.create_system = 3
            zi.external_attr = (0o755 if exe else 0o644) << 16
            zi.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(zi, data)
    return len(items)


mod = [(os.path.join("riivolution", f), "riivolution/" + f, False) for f in ("CustomCode_USA.bin", "riivo_USA.xml", "debugIP.txt")]
mod.append((b"127.0.0.1:5029", "riivolution/serverIP.txt", False))
server = [(os.path.join("server", "smg_server.py"), "server/smg_server.py", True),
          (os.path.join("server", "server-settings.ini"), "server/server-settings.ini", False)]

os.makedirs(OUT, exist_ok=True)   # the three zips sit directly in this folder

win = mod + server + [(f, f, False) for f in (
    "SMG ONLINE.bat", "launcher.ps1", "start-galaxy.ps1", "start-server.ps1", "HOST A GAME.bat", "JOIN A FRIEND.bat",
    "SERVER SETTINGS.bat", "SET DOLPHIN AND GAME PATH.bat", "START GALAXY ONLINE.bat", "FRIENDS - READ ME.txt", "README.md")]
win += [(os.path.join("title", f), "title/" + f, False) for f in ("build-title.ps1", "online-strip.bin")]
n1 = make_zip(os.path.join(OUT, "SMG-Online-Windows.zip"), "SMG-Online", win)

mac = mod + server + [(os.path.join("mac", f), f, True) for f in (
    "start-galaxy.sh", "JOIN A FRIEND.command", "HOST A GAME.command", "CHOOSE COLOUR.command",
    "SET DOLPHIN AND GAME PATH.command", "SERVER SETTINGS.command")]
mac.append((os.path.join("mac", "READ ME - MAC.txt"), "READ ME - MAC.txt", False))
n2 = make_zip(os.path.join(OUT, "SMG-Online-Mac.zip"), "SMG-Online-Mac", mac)

wii = []
for root, _, files in os.walk("wii"):
    for f in files:
        p = os.path.join(root, f)
        wii.append((p, os.path.relpath(p, "wii").replace(os.sep, "/"), False))
n3 = make_zip(os.path.join(OUT, "SMG-Online-Wii-and-WiiU.zip"), "SMG-Online-Wii", wii)

print("zips:", n1, n2, n3, "files")
for root, _, files in os.walk(OUT):
    for f in files:
        p = os.path.join(root, f)
        print("  %8d  %s" % (os.path.getsize(p), os.path.relpath(p, OUT)))
