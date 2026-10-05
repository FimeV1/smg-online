"""Launch instance N against an isolated copy of the mod (argv[2] = riivolution folder)."""
import json, os, sys, drive
n, riivo = sys.argv[1], os.path.abspath(sys.argv[2])
preset = os.path.join(drive.SP, "isolated-preset.json")
json.dump({"type": "dolphin-game-mod-descriptor", "version": 1, "base-file": drive.GAME.replace(os.sep, "/"),
           "riivolution": {"patches": [{"xml": (riivo + "/riivo_USA.xml").replace(os.sep, "/"), "root": riivo.replace(os.sep, "/"),
                                        "options": [{"section-name": "Syati Loader", "option-id": "smgmp", "choice": 1}]}]}},
          open(preset, "w"))
os.environ["SMG_TEST_ID"] = n
os.environ["SMG_TEST_DIR"] = drive.SP
pid = drive.launch_on_hidden_desktop([drive.DOLPHIN, "-u", os.path.join(drive.SP, "profiles", "test%s" % n),
                                      "--no-python-subinterpreters", "--script", os.path.join(drive.SP, "ingame.py"),
                                      "-b", "-e", preset], os.path.dirname(drive.DOLPHIN))
pids = drive.load_pids(); pids[n] = pid; json.dump(pids, open(drive.PIDS, "w")); print("launched", n, pid)
