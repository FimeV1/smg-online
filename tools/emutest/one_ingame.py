"""Launch instance 1 against an isolated mod folder, enter save file 2, print the
colour debug words and save a screenshot.  usage: one_ingame.py <riivolution folder> <debug offset hex>"""
import os, struct, subprocess, sys, time, drive
n = "1"; BASE = 0x806C25A0; DBG = int(sys.argv[2], 16)
subprocess.check_call([sys.executable, "launch_test.py", n, sys.argv[1]])
def run(steps, extra=0.0):
    drive.send(n, {"op": "seq", "steps": steps}); time.sleep(sum(s["frames"] for s in steps) / 60 + extra)
def rd(addr, ln): return bytes.fromhex(drive.send(n, {"op": "read", "specs": [{"type": "bytes", "addr": addr, "len": ln}]})["values"][0])
end = time.time() + 120
while time.time() < end and "error" in drive.send(n, {"op": "ping"}, timeout=3): pass
time.sleep(9)
run([{"wm": {"A": True}, "frames": 10}], 8)
run([{"wm": {"A": True, "B": True}, "frames": 10}], 12)
f2 = [0.176, -0.26]
run([{"ptr": f2, "frames": 90}, {"ptr": f2, "wm": {"A": True}, "frames": 8}, {"ptr": f2, "frames": 60}], 2)
steps = []
for y in (-0.5, -0.6, -0.7, -0.8):
    steps += [{"ptr": [0.43, y], "frames": 90}, {"ptr": [0.43, y], "wm": {"A": True}, "frames": 6}, {"ptr": [0.43, y], "frames": 20}]
run(steps, 15)
print("debug", ["%x" % v for v in struct.unpack(">8I", rd(BASE + DBG, 32))])
for k in range(2):
    drive.send(n, {"op": "shot", "path": os.path.join(drive.SP, "c.png"), "scale": 1}); time.sleep(0.6)
