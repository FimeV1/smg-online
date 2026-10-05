#!/usr/bin/env python3
"""Checks zips for personal information before they are published.

    python tools/scan_zips.py <zip> [<zip> ...]

Each zip is unpacked to a temporary folder and put through the same scan as
the repository export (personal paths, real IP addresses, e-mail addresses,
and the names in private-words.txt next to the working folder, if present).
Exits with an error if anything is found."""
import os
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_repo  # noqa: E402

FORBIDDEN_FILES = (".wbfs", ".iso", ".rvz", ".wia", ".gcz", ".arc", ".exe", ".sav", ".ttf", ".7z")

bad = 0
for path in sys.argv[1:]:
    with tempfile.TemporaryDirectory() as tmp, zipfile.ZipFile(path) as z:
        names = z.namelist()
        z.extractall(tmp)
        export_repo.DST = tmp
        problems = export_repo.scan()
        problems += ["%s: a file type that must not be shipped" % n for n in names if n.lower().endswith(FORBIDDEN_FILES)]
        problems += ["%s: looks like save data or a user profile" % n for n in names
                     if any(w in n.lower() for w in ("gamedata", "dolphin-users", "profiles/", "launcher-paths", "progress"))]
    print("%-34s %3d files  %s" % (os.path.basename(path), len(names), "CLEAN" if not problems else "PROBLEMS"))
    for p in problems:
        print("    " + p)
    bad += len(problems)
sys.exit(1 if bad else 0)
