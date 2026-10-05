import sys, os, json, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import drive
BASE = 0x806C25A0 + 0x8B74   # info__11Multiplayer
def rd(n, specs): return drive.send(n, {"op":"read","specs":specs})["values"]
for n in sys.argv[1:]:
    raw = bytes.fromhex(rd(n, [{"type":"bytes","addr":BASE + 0x8EC,"len":0x60}])[0])
    owners = list(raw[0:15]); rx = struct.unpack(">15I", raw[16:76]); srv, sess, epoch, stage = struct.unpack(">4I", raw[76:92]); scen = raw[92]
    conn = rd(n, [{"type":"u8","addr":0x806C25A0 + 0x8B6F}])[0]
    print(f"instance {n}: connected={conn} stage={stage} scen={scen} serverRx={srv} sessions={sess}")
    for i in range(15):
        if owners[i] != 0xFF or rx[i]:
            st = rd(n, [{"type":"u32","addr":BASE}])[0]
            b = (st >> i) & 1
            p = BASE + 4 + i*152 + 8 + b*72
            pos = [rd(n, [{"type":"f32","addr":p+12+4*k}])[0] for k in range(3)]
            gid = rd(n, [{"type":"u8","addr":p}])[0]
            print(f"   slot {i}: owner={owners[i]} rx={rx[i]} bufGid={gid} pos=({pos[0]:.0f},{pos[1]:.0f},{pos[2]:.0f})")
