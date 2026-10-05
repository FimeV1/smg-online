#!/usr/bin/env python3
"""
Copies the publishable part of the working folder into a clean folder for git.

    python tools/export_repo.py <destination folder>

Only what is listed here is copied (an allowlist), so the game, extracted game
data, the CodeWarrior compiler, save copies, build leftovers and personal
settings can never end up in the repository by accident. The copy is then
scanned for personal paths and addresses; the script fails if it finds any.
"""
import os
import re
import shutil
import subprocess
import sys

SRC = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

# (path in the working folder, path in the repository). Folders are copied
# whole unless a filter is given.
FILES = [
    "README.md", "FRIENDS - READ ME.txt",
    "SMG ONLINE.bat", "launcher.ps1", "start-galaxy.ps1", "start-server.ps1",
    "HOST A GAME.bat", "JOIN A FRIEND.bat", "SERVER SETTINGS.bat", "SET DOLPHIN AND GAME PATH.bat",
    "START GALAXY ONLINE.bat",
    "server/smg_server.py", "server/test_server.py",
    "riivolution/riivo_USA.xml", "riivolution/debugIP.txt", "riivolution/CustomCode_USA.bin",
    "title/build-title.ps1",
    "mac/start-galaxy.sh", "mac/test-launcher.sh", "mac/READ ME - MAC.txt",
    "mac/JOIN A FRIEND.command", "mac/HOST A GAME.command", "mac/CHOOSE COLOUR.command",
    "mac/SET DOLPHIN AND GAME PATH.command", "mac/SERVER SETTINGS.command",
    "wii/READ ME - WII AND WII U.txt", "wii/SD-card/riivolution/SMGOnline_USA.xml",
    "wii/SD-card/smgonline/CustomCode_USA.bin", "wii/SD-card/smgonline/debugIP.txt",
    "wii/SD-card/smgonline/serverIP.txt",
    "tools/export_repo.py", "tools/pack_all.py", "tools/scan_zips.py",
    "tools/title/build_title.py", "tools/title/make_strip.py",
    "tools/emutest/README.md", "tools/emutest/drive.py", "tools/emutest/ingame.py", "tools/emutest/goto_game.py",
    "tools/emutest/state.py", "tools/emutest/launch_test.py", "tools/emutest/both_ingame.py",
    "tools/emutest/one_ingame.py",
    "SMGNetworkMultiplayer/Makefile", "SMGNetworkMultiplayer/config.mk.template",
    "SMGNetworkMultiplayer/README.md", "SMGNetworkMultiplayer/credits.txt", "SMGNetworkMultiplayer/.gitignore",
]
FOLDERS = [
    ("SMGNetworkMultiplayer/source", (".c", ".cpp")),
    ("SMGNetworkMultiplayer/include", (".h", ".hpp")),
]
# extra files written by the caller into the destination (LICENSE, docs, ...)
# are left alone.

# Anything matching these in a text file stops the export.
FORBIDDEN = [
    (re.compile(r"[A-Za-z]:[\\/]+Users[\\/]+(?!Name\b)\w+", re.I), "a personal Windows path"),
    (re.compile(r"(?<![\w$}\"])/home/(?!user\b)\w+"), "a personal Linux path"),
    (re.compile(r"\b(?!127\.0\.0\.1|0\.0\.0\.0|192\.168\.0\.(?:10|25)\b|203\.0\.113\.\d+|192\.0\.2\.\d+|255\.255\.255\.255)"
                r"(?:\d{1,3}\.){3}\d{1,3}\b"), "a real IP address"),
    (re.compile(r"[\w.+-]+@(?!users\.noreply\.github\.com)[\w-]+\.(?:me|com|net|org)\b", re.I), "an e-mail address"),
]
TEXT_EXT = (".md", ".txt", ".bat", ".ps1", ".py", ".sh", ".command", ".xml", ".c", ".cpp", ".h", ".hpp",
            ".template", ".ini", ".patch", "Makefile", ".gitignore", ".gitattributes")


def copy(rel):
    src = os.path.join(SRC, rel)
    dst = os.path.join(DST, rel)
    if not os.path.exists(src):
        raise SystemExit("missing: " + rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)


def git_patch(repo, out_name):
    """Our changes to someone else's repository, as a patch against its HEAD."""
    cwd = os.path.join(SRC, repo)
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=cwd, text=True).strip()
    url = subprocess.check_output(["git", "remote", "get-url", "origin"], cwd=cwd, text=True).strip()
    # untracked files are included by marking them "intent to add" for the diff
    untracked = subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard"], cwd=cwd, text=True).split("\n")
    untracked = [u for u in untracked if u and not u.startswith("deps/") and not u.endswith((".o", ".exe"))]
    if untracked:
        subprocess.check_call(["git", "add", "--intent-to-add", "--"] + untracked, cwd=cwd)
    diff = subprocess.check_output(["git", "diff", "--no-color", "HEAD", "--", ".", ":(exclude)deps"], cwd=cwd)
    if untracked:
        subprocess.check_call(["git", "reset", "-q", "--"] + untracked, cwd=cwd)
    os.makedirs(os.path.join(DST, "patches"), exist_ok=True)
    with open(os.path.join(DST, "patches", out_name), "wb") as f:
        f.write(diff.replace(b"\r\n", b"\n"))
    return url, head


# Names of people, accounts and machines that must not appear anywhere. They
# are read from a file kept OUTSIDE the repository (one per line), so the list
# itself is never published.
PRIVATE_WORDS_FILE = os.path.join(SRC, "private-words.txt")


def scan():
    problems = []
    words = []
    if os.path.exists(PRIVATE_WORDS_FILE):
        words = [w.strip().lower() for w in open(PRIVATE_WORDS_FILE, encoding="utf-8") if w.strip()]
    for root, dirs, files in os.walk(DST):
        dirs[:] = [d for d in dirs if d != ".git"]
        for name in files:
            path = os.path.join(root, name)
            rel = os.path.relpath(path, DST)
            raw = open(path, "rb").read()
            low = raw.lower()
            # private names: every file, compiled ones included, and file names
            for w in words:
                if w.encode() in low or w in rel.lower():
                    problems.append("%s: contains the private word '%s'" % (rel, w))
            if not name.endswith(TEXT_EXT):
                # compiled files: no embedded user folders either
                for marker in (b":\\users\\", b":/users/", b"/home/"):
                    if marker in low:
                        problems.append("%s: binary contains a path (%s)" % (rel, marker.decode()))
                continue
            text = raw.decode("utf-8", errors="replace")
            for pattern, what in FORBIDDEN:
                for m in pattern.finditer(text):
                    line = text.count("\n", 0, m.start()) + 1
                    problems.append("%s:%d: %s (%s)" % (rel, line, what, m.group(0)))
    return problems


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    DST = os.path.abspath(sys.argv[1])
    if os.path.commonpath([DST, SRC]) == SRC:
        raise SystemExit("the destination must be outside the working folder")
    os.makedirs(DST, exist_ok=True)
    # start from an empty tree (keep .git and the hand-written top-level extras)
    keep = {".git", "LICENSE", "CREDITS.md", ".gitignore", ".gitattributes", "docs"}
    for entry in os.listdir(DST):
        if entry in keep:
            continue
        p = os.path.join(DST, entry)
        shutil.rmtree(p) if os.path.isdir(p) else os.remove(p)

    for rel in FILES:
        copy(rel)
    for folder, exts in FOLDERS:
        for root, _, files in os.walk(os.path.join(SRC, folder)):
            for name in files:
                if name.endswith(exts):
                    copy(os.path.relpath(os.path.join(root, name), SRC).replace(os.sep, "/"))
    with open(os.path.join(DST, "riivolution", "serverIP.txt"), "w", newline="") as f:
        f.write("127.0.0.1:5029")
    # The published ONLINE artwork is the standard-font rendering
    # (tools/title/make_strip.py with no SMG_TITLE_FONT), whatever the working
    # folder uses for private packs.
    public_strip = os.path.join(SRC, "title", "online-strip.public.bin")
    if not os.path.exists(public_strip):
        raise SystemExit("missing title/online-strip.public.bin (run tools/title/make_strip.py title/online-strip.public.bin)")
    shutil.copyfile(public_strip, os.path.join(DST, "title", "online-strip.bin"))

    bases = {
        "Petari-mp": git_patch("Petari-mp", "petari-mp.patch"),
        "BussunChanges": git_patch("BussunChanges", "bussun.patch"),
    }
    client = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=os.path.join(SRC, "SMGNetworkMultiplayer"), text=True).strip()
    with open(os.path.join(DST, "patches", "BASES.txt"), "w", newline="\n") as f:
        f.write("Patches in this folder apply to these upstream commits:\n\n")
        f.write("petari-mp.patch   %s @ %s\n" % bases["Petari-mp"])
        f.write("bussun.patch      %s @ %s\n" % bases["BussunChanges"])
        f.write("\nSMGNetworkMultiplayer/ in this repository started from\n")
        f.write("https://github.com/Headpenguin/SMGNetworkMultiplayer @ %s\n" % client)

    problems = scan()
    count = sum(len(files) for _, _, files in os.walk(DST) if ".git" not in _)
    print("exported to", DST)
    if problems:
        print("\nPERSONAL DATA FOUND - fix these before publishing:")
        for p in problems:
            print("  " + p)
        sys.exit(1)
    print("scan clean: no personal paths, real IP addresses, e-mail addresses or private names"
          + ("" if os.path.exists(PRIVATE_WORDS_FILE) else " (no private-words.txt, names not checked)"))
