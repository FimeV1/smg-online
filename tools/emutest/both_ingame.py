"""Launch instances 1 and 2 against an isolated mod folder and get both into save file 2.
   usage: both_ingame.py <riivolution folder> <server log>"""
import os, subprocess, sys, time, drive
riivos, log = sys.argv[1:-1], sys.argv[-1]
OBS = "(1224986660"

def run(n, steps, extra=0.0):
    drive.send(n, {"op": "seq", "steps": steps}); time.sleep(sum(s["frames"] for s in steps) / 60 + extra)

def in_observatory():
    return open(log, errors="replace").read().count("now in stage " + OBS)

for n in ("1", "2"):
    subprocess.check_call([sys.executable, "launch_test.py", n, riivos[(int(n) - 1) % len(riivos)]]); time.sleep(4)
for n in ("1", "2"):
    end = time.time() + 150
    while time.time() < end and "error" in drive.send(n, {"op": "ping"}, timeout=3): pass
time.sleep(10)
want = 0
for n in ("1", "2"):
    want += 1
    for attempt in range(4):
        run(n, [{"wm": {"A": True}, "frames": 10}], 7)                    # strap screen (harmless later)
        run(n, [{"wm": {"A": True, "B": True}, "frames": 10}], 12)        # title
        f2 = [0.176, -0.26]
        run(n, [{"ptr": f2, "frames": 90}, {"ptr": f2, "wm": {"A": True}, "frames": 8}, {"ptr": f2, "frames": 60}], 2)
        steps = []
        for y in (-0.5, -0.6, -0.7, -0.8):
            steps += [{"ptr": [0.43, y], "frames": 90}, {"ptr": [0.43, y], "wm": {"A": True}, "frames": 6}, {"ptr": [0.43, y], "frames": 20}]
        run(n, steps, 14)
        if in_observatory() >= want: break
    print("instance", n, "in observatory:", in_observatory() >= want)
