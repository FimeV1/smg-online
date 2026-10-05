#!/usr/bin/env python3
"""
SMG Online - relay server (protocol 1.x)

Runs anywhere Python 3.8+ runs (Windows, Linux, macOS). No dependencies.

    python smg_server.py                 # listen on UDP 5029
    python smg_server.py --port 5029 --max-players 64 --state progress.json

What it does
  * Hands out player ids, frees them when a client goes quiet (no more
    "server full" after a few restarts).
  * Relays player positions only between players that are in the same
    galaxy + scenario, bundled into one datagram per tick per client.
  * Keeps the shared save progress (stars, unlocks, story flags, Luma
    feeding) in an ordered log, delivers it reliably (ack + resend) to every
    client, replays it to late joiners, and persists it to disk.

Wire format: a datagram is one or more records, each `u32 tag` + fixed-size
payload (big endian, sizes below). See SMGNetworkMultiplayer/source/packets.cpp.
"""
import argparse
import json
import os
import random
import select
import socket
import struct
import sys
import time

MAJOR, MINOR = 1, 0

TAG_CONNECT, TAG_ACK, TAG_SIR, TAG_POS, TAG_TQ, TAG_TR, TAG_STAR, TAG_PROGRESS = range(8)
PAYLOAD_SIZE = {
    TAG_CONNECT: 16,
    TAG_ACK: 4,
    TAG_SIR: 16,
    TAG_POS: 60,
    TAG_TQ: 8,
    TAG_TR: 8,
    TAG_STAR: 32,
    TAG_PROGRESS: 64,
}
CONNECT_MAGIC = b"Connect\0"

# PlayerPosition payload offsets
POS_OFF_ID = 0
POS_OFF_SCENARIO = 2
POS_OFF_STAGE = 56

# GameProgress payload: u8 id, u8 type, u8 pad[2], u32 seq, s32 arg, s32 value, char name[48]
PROGRESS_FMT = ">BBxxIii48s"
PE_TICO_SEED = 4  # value is an absolute total; only ever grows

MAX_DATAGRAM = 1280           # stays under the client's receive buffer and typical MTU
TICK_HZ = 60
FULL_RATE_GROUP = 8           # stages with more players than this relay at half rate
POSITION_MAX_AGE = 1.0        # stop relaying a position that has not been refreshed
SESSION_TIMEOUT = 60.0        # free a slot after this much silence
KEEPALIVE_INTERVAL = 1.0
PROGRESS_WINDOW = 16          # log entries per progress datagram
PROGRESS_INTERVAL = 0.1       # seconds between progress datagrams to one client


def record(tag, payload):
    return struct.pack(">I", tag) + payload


class Session:
    __slots__ = (
        "addr", "id", "last_rx", "stage", "pos", "pos_version", "pos_time",
        "sent_versions", "delivered", "last_progress_send", "last_event_seq",
        "last_keepalive", "connected_at",
    )

    def __init__(self, addr, pid, now):
        self.addr = addr
        self.id = pid
        self.last_rx = now
        self.stage = None            # (stageHash, scenario) or None when unknown
        self.pos = None              # latest position record (tag + payload)
        self.pos_version = 0
        self.pos_time = 0.0
        self.sent_versions = {}      # source id -> pos_version last relayed to us
        self.delivered = 0           # next progress-log index this client expects
        self.last_progress_send = 0.0
        self.last_event_seq = 0xFFFFFFFF
        self.last_keepalive = now
        self.connected_at = now


class ProgressLog:
    """Ordered, de-duplicated log of save-progress events."""

    def __init__(self, path):
        self.path = path
        self.epoch = random.randrange(1, 0xFFFFFFFF)
        self.entries = []            # [type, name(bytes), arg, value]
        self.latest = {}             # (type, name, arg) -> value
        self.dirty = False
        self.last_save = 0.0
        self.load()

    def load(self):
        if not self.path or not os.path.exists(self.path):
            return
        try:
            with open(self.path, "r", encoding="utf-8") as f:
                data = json.load(f)
            self.epoch = int(data["epoch"])
            for e in data["entries"]:
                # latin-1 round-trips the raw (Shift-JIS) name bytes losslessly
                self._append(int(e["t"]), e["n"].encode("latin-1"), int(e["a"]), int(e["v"]))
            self.compact()
            self.dirty = False
        except (OSError, ValueError, KeyError, TypeError) as err:
            backup = self.path + ".corrupt"
            print(f"[warn] could not read {self.path} ({err}); starting fresh, old file kept as {backup}")
            try:
                os.replace(self.path, backup)
            except OSError:
                pass
            self.entries, self.latest = [], {}
            self.epoch = random.randrange(1, 0xFFFFFFFF)

    def _append(self, etype, name, arg, value):
        self.entries.append([etype, name, arg, value])
        self.latest[(etype, name, arg)] = value

    def compact(self):
        """Keep only the last entry per key (order of last occurrence)."""
        seen, out = set(), []
        for e in reversed(self.entries):
            key = (e[0], e[1], e[2])
            if key not in seen:
                seen.add(key)
                out.append(e)
        out.reverse()
        self.entries = out

    def add(self, etype, name, arg, value):
        """Returns True if this event changed the shared state."""
        key = (etype, name, arg)
        old = self.latest.get(key)
        if old == value:
            return False
        if etype == PE_TICO_SEED and old is not None and value <= old:
            return False
        self._append(etype, name, arg, value)
        self.dirty = True
        return True

    def save(self, now, force=False):
        if not self.path or not self.dirty:
            return
        if not force and now - self.last_save < 1.0:
            return
        data = {
            "epoch": self.epoch,
            "entries": [
                {"t": e[0], "n": e[1].decode("latin-1"), "a": e[2], "v": e[3]} for e in self.entries
            ],
        }
        tmp = self.path + ".tmp"
        try:
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(data, f)
            os.replace(tmp, self.path)
            self.dirty = False
            self.last_save = now
        except OSError as err:
            print(f"[warn] could not save progress to {self.path}: {err}")
            self.last_save = now


class Server:
    def __init__(self, host, port, max_players, state_path, verbose=False,
                 session_timeout=SESSION_TIMEOUT, share_progress=True, share_star_bits=True,
                 update_rate=TICK_HZ):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1 << 20)
        except OSError:
            pass
        self.sock.bind((host, port))
        self.sock.setblocking(False)
        self.max_players = max_players
        self.session_timeout = session_timeout
        self.share_progress = share_progress
        self.share_star_bits = share_star_bits
        self.update_rate = update_rate
        self.verbose = verbose
        self.by_addr = {}
        self.by_id = {}
        self.log = ProgressLog(state_path if share_progress else "")
        self.start = time.monotonic()
        self.tick_no = 0
        self.full_warned = 0.0
        self.running = True

    # ---- helpers -------------------------------------------------------
    def now_ms(self):
        return int((time.monotonic() - self.start) * 1000) & 0xFFFFFFFF

    def send(self, addr, data):
        try:
            self.sock.sendto(data, addr)
        except (BlockingIOError, ConnectionResetError):
            pass  # dropped datagram; every stream here tolerates loss
        except OSError as err:
            if self.verbose:
                print(f"[warn] send to {addr} failed: {err}")

    def sir(self, s):
        return record(TAG_SIR, struct.pack(
            ">IIIBBxx", MAJOR, MINOR, self.log.epoch, s.id, min(self.max_players, 255)))

    def drop(self, s, why):
        self.by_addr.pop(s.addr, None)
        self.by_id.pop(s.id, None)
        print(f"[-] player {s.id} {s.addr[0]}:{s.addr[1]} left ({why}); {len(self.by_id)} online")

    # ---- inbound -------------------------------------------------------
    def on_datagram(self, data, addr, now):
        s = self.by_addr.get(addr)
        off, n = 0, len(data)
        while n - off >= 4:
            (tag,) = struct.unpack_from(">I", data, off)
            size = PAYLOAD_SIZE.get(tag)
            if size is None or off + 4 + size > n:
                break
            payload = data[off + 4: off + 4 + size]
            off += 4 + size
            if tag == TAG_CONNECT:
                s = self.on_connect(payload, addr, now, s)
            elif s is not None:
                s.last_rx = now
                self.on_record(s, tag, payload, now)
            # records from unknown addresses are ignored: the client notices the
            # silence and re-sends CONNECT on its own

    def on_connect(self, payload, addr, now, s):
        magic, major, _minor = struct.unpack(">8sII", payload)
        if magic != CONNECT_MAGIC:
            return s
        if major != MAJOR:
            if now - self.full_warned > 5.0:
                self.full_warned = now
                print(f"[warn] {addr[0]} runs protocol {major}.x, server is {MAJOR}.x - update the mod")
            return s
        if s is None:
            free = next((i for i in range(self.max_players) if i not in self.by_id), None)
            if free is None:
                if now - self.full_warned > 5.0:
                    self.full_warned = now
                    print(f"[warn] server full ({self.max_players}), rejected {addr[0]}:{addr[1]}")
                return None
            s = Session(addr, free, now)
            self.by_addr[addr] = s
            self.by_id[free] = s
            print(f"[+] player {s.id} joined from {addr[0]}:{addr[1]}; {len(self.by_id)} online")
        s.last_rx = now
        self.send(addr, self.sir(s))
        return s

    def on_record(self, s, tag, payload, now):
        if tag == TAG_POS:
            stage_hash, = struct.unpack_from(">I", payload, POS_OFF_STAGE)
            stage = (stage_hash, payload[POS_OFF_SCENARIO]) if stage_hash else None
            if stage != s.stage:
                s.stage = stage
                if self.verbose:
                    print(f"[i] player {s.id} now in stage {stage}")
            # never trust the id the client wrote
            s.pos = record(TAG_POS, bytes([s.id]) + payload[1:])
            s.pos_version += 1
            s.pos_time = now
        elif tag == TAG_TQ:
            _t, check = struct.unpack(">II", payload)
            self.send(s.addr, record(TAG_TR, struct.pack(">II", self.now_ms(), check)))
        elif tag == TAG_STAR:
            if s.stage is None or not self.share_star_bits:
                return
            out = record(TAG_STAR, bytes([s.id]) + payload[1:])
            for o in self.by_id.values():
                if o is not s and o.stage == s.stage:
                    self.send(o.addr, out)
        elif tag == TAG_PROGRESS:
            _pid, etype, seq, arg, value, name = struct.unpack(PROGRESS_FMT, payload)
            name = name.split(b"\0", 1)[0]
            # With sharing off the event is still acked (so the game stops
            # re-sending it) but goes nowhere.
            if self.share_progress and self.log.add(etype, name, arg, value):
                print(f"[*] progress from player {s.id}: type={etype} "
                      f"name={name.decode('shift_jis', 'replace')} arg={arg} value={value}")
            s.last_event_seq = seq
            self.send(s.addr, record(TAG_ACK, struct.pack(">I", seq)))
        elif tag == TAG_ACK:
            # cumulative: "the next log index I expect". May move backwards when
            # the client reloads a save and wants the whole log again.
            expected, = struct.unpack(">I", payload)
            if expected <= len(self.log.entries):
                if expected < s.delivered:
                    s.last_progress_send = 0.0
                if self.verbose and expected == len(self.log.entries) != s.delivered:
                    print(f"[i] player {s.id} is up to date with all {expected} progress events")
                s.delivered = expected

    # ---- outbound ------------------------------------------------------
    def tick(self, now):
        self.tick_no += 1

        for s in [s for s in self.by_id.values() if now - s.last_rx > self.session_timeout]:
            self.drop(s, "timed out")

        groups = {}
        for s in self.by_id.values():
            if s.stage is not None and s.pos is not None and now - s.pos_time <= POSITION_MAX_AGE:
                groups.setdefault(s.stage, []).append(s)

        for s in self.by_id.values():
            members = groups.get(s.stage) if s.stage is not None else None
            if members and len(members) > 1:
                if len(members) <= FULL_RATE_GROUP or (self.tick_no + s.id) & 1:
                    self.relay_positions(s, members)
            self.deliver_progress(s, now)
            if now - s.last_keepalive >= KEEPALIVE_INTERVAL:
                s.last_keepalive = now
                self.send(s.addr, record(TAG_ACK, struct.pack(">I", s.last_event_seq)))

        self.log.save(now)

    def relay_positions(self, s, members):
        out, size = [], 0
        sent = s.sent_versions
        for o in members:
            if o is s or sent.get(o.id) == (o.connected_at, o.pos_version):
                continue
            sent[o.id] = (o.connected_at, o.pos_version)
            if size + len(o.pos) > MAX_DATAGRAM:
                self.send(s.addr, b"".join(out))
                out, size = [], 0
            out.append(o.pos)
            size += len(o.pos)
        if out:
            self.send(s.addr, b"".join(out))

    def deliver_progress(self, s, now):
        entries = self.log.entries
        if s.delivered >= len(entries) or now - s.last_progress_send < PROGRESS_INTERVAL:
            return
        s.last_progress_send = now
        out = []
        for idx in range(s.delivered, min(s.delivered + PROGRESS_WINDOW, len(entries))):
            etype, name, arg, value = entries[idx]
            out.append(record(TAG_PROGRESS, struct.pack(PROGRESS_FMT, 0xFF, etype, idx, arg, value, name)))
        self.send(s.addr, b"".join(out))

    # ---- main loop -----------------------------------------------------
    def run(self):
        interval = 1.0 / self.update_rate
        next_tick = time.monotonic() + interval
        while self.running:
            now = time.monotonic()
            timeout = max(0.0, next_tick - now)
            try:
                ready, _, _ = select.select([self.sock], [], [], timeout)
            except InterruptedError:
                continue
            if ready:
                # drain everything that is queued before ticking
                for _ in range(512):
                    try:
                        data, addr = self.sock.recvfrom(2048)
                    except BlockingIOError:
                        break
                    except ConnectionResetError:
                        continue  # Windows reports ICMP "port unreachable" here
                    except OSError:
                        break
                    try:
                        self.on_datagram(data, addr, time.monotonic())
                    except struct.error:
                        pass  # malformed record; ignore
            now = time.monotonic()
            if now >= next_tick:
                self.tick(now)
                next_tick += interval
                if next_tick < now:  # fell behind; do not spiral
                    next_tick = now + interval

    def close(self):
        self.log.save(time.monotonic(), force=True)
        self.sock.close()


SETTINGS_FILE = "server-settings.ini"
SETTINGS_TEMPLATE = """\
# SMG Online server settings. Edit, save, then restart the server.
# Lines starting with # are comments.

[server]
# UDP port the server listens on. Forward this port (UDP) on your router.
# Players join with  your-ip:port  (the :port part can be left out for 5029).
port = 5029

# How many players may be connected at once (1-250).
max_players = 64

# Name of the shared world. Each name has its own saved progress, so you can
# keep several runs side by side (e.g. default, 100percent, friday).
world = default

# yes = Power Stars, unlocks and story progress are shared by everyone.
# no  = players only see each other; everyone keeps their own progress.
share_progress = yes

# yes = star bits a player shoots appear for (and hit) other players.
share_star_bits = yes

# Seconds of silence before a player counts as gone and their slot is freed.
player_timeout = 60

# Position updates per second sent to each player (10-60). Lower it to save
# bandwidth on a slow connection; movement gets a little less smooth.
update_rate = 60

# yes = also print stage changes and sync details in the server window.
verbose = no
"""


def load_settings(path):
    """Read the settings file (creating it with defaults on first run)."""
    import configparser
    if not os.path.exists(path):
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(SETTINGS_TEMPLATE)
            print(f"[i] created {path} - edit it to change the port and other settings")
        except OSError as err:
            print(f"[warn] could not create {path}: {err}")
    cp = configparser.ConfigParser()
    cp.read_string(SETTINGS_TEMPLATE)  # defaults
    try:
        cp.read(path, encoding="utf-8-sig")
    except configparser.Error as err:
        print(f"[warn] {path} has a mistake ({err}); using defaults for the rest")
    sec = cp["server"]
    out = {}
    for key, getter, fallback in (
        ("port", sec.getint, 5029), ("max_players", sec.getint, 64),
        ("share_progress", sec.getboolean, True), ("share_star_bits", sec.getboolean, True),
        ("player_timeout", sec.getfloat, SESSION_TIMEOUT), ("update_rate", sec.getint, TICK_HZ),
        ("verbose", sec.getboolean, False),
    ):
        try:
            out[key] = getter(key)
        except ValueError:
            print(f"[warn] {path}: '{key} = {sec.get(key)}' is not valid, using {fallback}")
            out[key] = fallback
    out["world"] = sec.get("world", "default").strip() or "default"
    return out


def world_file(folder, world):
    """The default world keeps the original file name."""
    if world == "default":
        return os.path.join(folder, "progress.json")
    safe = "".join(c if c.isalnum() or c in "-_" else "_" for c in world)
    return os.path.join(folder, f"progress-{safe}.json")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(
        description="SMG Online relay server. Settings come from server-settings.ini next to this "
                    "file; anything given on the command line wins.")
    ap.add_argument("--settings", default=os.path.join(here, SETTINGS_FILE), help="settings file to use")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int)
    ap.add_argument("--max-players", type=int)
    ap.add_argument("--world", help="name of the shared world (its own saved progress)")
    ap.add_argument("--state", help="exact file holding the shared progress (overrides --world)")
    ap.add_argument("--fresh", action="store_true", help="discard this world's saved progress and start over")
    ap.add_argument("--session-timeout", type=float, help="seconds of silence before a player's slot is freed")
    ap.add_argument("--update-rate", type=int, help="position updates per second (10-60)")
    ap.add_argument("--no-progress", action="store_true", help="do not share save progress")
    ap.add_argument("--no-star-bits", action="store_true", help="do not share star bits")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    cfg = load_settings(args.settings)
    port = args.port if args.port is not None else cfg["port"]
    max_players = args.max_players if args.max_players is not None else cfg["max_players"]
    world = args.world or cfg["world"]
    state = args.state if args.state is not None else world_file(here, world)
    timeout = args.session_timeout if args.session_timeout is not None else cfg["player_timeout"]
    update_rate = args.update_rate if args.update_rate is not None else cfg["update_rate"]
    share_progress = cfg["share_progress"] and not args.no_progress
    share_star_bits = cfg["share_star_bits"] and not args.no_star_bits
    verbose = args.verbose or cfg["verbose"]

    if not 1 <= port <= 65535:
        ap.error("port must be between 1 and 65535")
    if not 1 <= max_players <= 250:
        ap.error("max_players must be between 1 and 250")
    update_rate = max(10, min(60, update_rate))
    timeout = max(0.5, timeout)

    if args.fresh and state and os.path.exists(state):
        os.replace(state, state + ".old")
        print(f"[i] previous progress moved to {state}.old")

    try:
        server = Server(args.host, port, max_players, state, verbose, timeout,
                        share_progress, share_star_bits, update_rate)
    except OSError as err:
        print(f"[error] cannot listen on {args.host}:{port}: {err}")
        print("        Is another server already running, or the port in use? "
              "Change 'port' in the settings file.")
        return 1

    print(f"SMG Online server {MAJOR}.{MINOR} listening on UDP port {port}")
    print(f"  max players    : {max_players}")
    if share_progress:
        print(f"  shared progress: on, world '{world}' ({len(server.log.entries)} saved events)")
    else:
        print("  shared progress: off")
    print(f"  star bits      : {'shared' if share_star_bits else 'not shared'}")
    print(f"  settings file  : {args.settings}")
    print("Press Ctrl+C (or close this window) to stop.")
    sys.stdout.flush()
    try:
        server.run()
    except KeyboardInterrupt:
        print("\nstopping")
    finally:
        server.close()
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(line_buffering=True, errors="replace")
    except AttributeError:
        pass
    sys.exit(main())
