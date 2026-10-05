"""Boot instance N (already launched) from the strap screen into save file 2."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import drive  # noqa: E402

n = sys.argv[1]
SP = drive.SP


def run(steps, extra=0.0):
    drive.send(n, {"op": "seq", "steps": steps})
    time.sleep(sum(s["frames"] for s in steps) / 60 + extra)


# wait until the script answers (boot)
end = time.time() + 120
while time.time() < end and "error" in drive.send(n, {"op": "ping"}, timeout=3):
    pass
time.sleep(8)
run([{"wm": {"A": True}, "frames": 10}], 7)        # strap screen
run([{"wm": {"A": True, "B": True}, "frames": 10}], 11)  # title
f2 = [0.176, -0.26]
run([{"ptr": f2, "frames": 90}, {"ptr": f2, "wm": {"A": True}, "frames": 8}, {"ptr": f2, "frames": 60}], 2)
steps = []
for y in (-0.5, -0.6, -0.7, -0.8):
    steps += [{"ptr": [0.43, y], "frames": 90}, {"ptr": [0.43, y], "wm": {"A": True}, "frames": 6},
              {"ptr": [0.43, y], "frames": 20}]
run(steps, 12)
print(drive.send(n, {"op": "shot", "path": os.path.join(SP, "g-%s.png" % n), "scale": 1}))
