"""Assembles the Windows, Mac and Wii / Wii U packs as zips.

    python tools/pack_all.py [output folder]      (default: ./packs)

Run from the repository root. If the game-side code has been built
(see docs/BUILDING.md), the fresh builds are copied into riivolution/ and
wii/ first; otherwise the ready-built files already there are used."""
import os
import shutil
import sys
import time
import zipfile

BASE = os.getcwd()
OUT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(BASE, "packs")

# ---- use fresh builds when there are any ----------------------------------
for built, target in (
    (os.path.join("SMGNetworkMultiplayer", "bin", "Debug", "Dolphin", "CustomCode_USA.bin"),
     os.path.join("riivolution", "CustomCode_USA.bin")),
    (os.path.join("SMGNetworkMultiplayer", "bin", "Debug", "CustomCode_USA.bin"),
     os.path.join("wii", "SD-card", "smgonline", "CustomCode_USA.bin")),
):
    if os.path.exists(built):
        shutil.copyfile(built, target)

# The default server settings file, exactly as the server would create it
sys.path.insert(0, os.path.join(BASE, "server"))
import smg_server  # noqa: E402

SETTINGS = smg_server.SETTINGS_TEMPLATE.encode()


# ---- zips ----------------------------------------------------------------
def make_zip(path, root, items):
    """items: (source path or bytes, name in zip, executable)"""
    if os.path.exists(path):
        os.remove(path)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for src, name, exe in items:
            data = src if isinstance(src, bytes) else open(src, "rb").read()
            if name.endswith((".sh", ".command")):
                data = data.replace(b"\r\n", b"\n")     # CRLF breaks scripts on a Mac
            zi = zipfile.ZipInfo(root + "/" + name, time.localtime()[:6])
            zi.create_system = 3                        # unix, so the mode below is kept
            zi.external_attr = (0o755 if exe else 0o644) << 16
            zi.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(zi, data)
    return len(items)


mod = [(os.path.join("riivolution", f), "riivolution/" + f, False) for f in ("CustomCode_USA.bin", "riivo_USA.xml", "debugIP.txt")]
mod.append((b"127.0.0.1:5029", "riivolution/serverIP.txt", False))
server = [(os.path.join("server", "smg_server.py"), "server/smg_server.py", True),
          (SETTINGS, "server/server-settings.ini", False)]
legal = [(f, f, False) for f in ("LICENSE", "CREDITS.md") if os.path.exists(f)]

os.makedirs(OUT, exist_ok=True)

win = mod + server + legal + [(f, f, False) for f in (
    "SMG ONLINE.bat", "launcher.ps1", "start-galaxy.ps1", "start-server.ps1", "HOST A GAME.bat", "JOIN A FRIEND.bat",
    "SERVER SETTINGS.bat", "SET DOLPHIN AND GAME PATH.bat", "START GALAXY ONLINE.bat", "FRIENDS - READ ME.txt", "README.md")]
win += [(os.path.join("title", f), "title/" + f, False) for f in ("build-title.ps1", "online-strip.bin")]
n1 = make_zip(os.path.join(OUT, "SMG-Online-Windows.zip"), "SMG-Online", win)

mac = mod + server + legal + [(os.path.join("mac", f), f, True) for f in (
    "start-galaxy.sh", "JOIN A FRIEND.command", "HOST A GAME.command", "CHOOSE COLOUR.command",
    "SET DOLPHIN AND GAME PATH.command", "SERVER SETTINGS.command")]
mac.append((os.path.join("mac", "READ ME - MAC.txt"), "READ ME - MAC.txt", False))
n2 = make_zip(os.path.join(OUT, "SMG-Online-Mac.zip"), "SMG-Online-Mac", mac)

wii = list(legal)
for root, _, files in os.walk("wii"):
    for f in files:
        p = os.path.join(root, f)
        wii.append((p, os.path.relpath(p, "wii").replace(os.sep, "/"), False))
n3 = make_zip(os.path.join(OUT, "SMG-Online-Wii-and-WiiU.zip"), "SMG-Online-Wii", wii)

print("zips:", n1, n2, n3, "files")
for f in sorted(os.listdir(OUT)):
    if f.endswith(".zip"):
        print("  %8d  %s" % (os.path.getsize(os.path.join(OUT, f)), f))
