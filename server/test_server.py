#!/usr/bin/env python3
"""
End-to-end test for smg_server.py using simulated game clients.

    python test_server.py            # all tests
    python test_server.py --load 64  # also run a 64-player load test

The fake client below speaks the same wire format as the mod
(SMGNetworkMultiplayer/source/packets.cpp) and follows the same rules as
progressSync.cpp / packetProcessor.cpp, including packet loss.
"""
import argparse
import os
import random
import socket
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import smg_server as proto  # noqa: E402  (constants only)

POS_FMT = ">BBBxi9fhhf4BI"
assert struct.calcsize(POS_FMT) == proto.PAYLOAD_SIZE[proto.TAG_POS]
assert struct.calcsize(proto.PROGRESS_FMT) == proto.PAYLOAD_SIZE[proto.TAG_PROGRESS]


class FakeClient:
    def __init__(self, port, loss=0.0, seed=0):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.setblocking(False)
        self.server = ("127.0.0.1", port)
        self.loss = loss
        self.rng = random.Random(seed)
        self.id = None
        self.epoch = None
        self.max_players = None
        self.stage = (0, 0)
        self.positions = {}        # player id -> (x, datagram count)
        self.pos_records = 0
        self.max_bundle = 0
        self.star_pieces = 0
        self.rx_expected = 0
        self.applied = []          # (type, name, arg, value) in apply order
        self.pending = []          # our events not yet acked: (seq, payload)
        self.next_seq = 1
        self.need_ack = False
        self.time_responses = []
        self.server_acks = 0

    # -- sending ---------------------------------------------------------
    def _send(self, tag, payload):
        if self.rng.random() < self.loss:
            return
        self.sock.sendto(proto.record(tag, payload), self.server)

    def connect(self):
        self._send(proto.TAG_CONNECT, struct.pack(">8sII", proto.CONNECT_MAGIC, proto.MAJOR, proto.MINOR))

    def send_position(self, x, ts=0):
        self._send(proto.TAG_POS, struct.pack(
            POS_FMT, self.id if self.id is not None else 0, 0, self.stage[1], ts,
            x, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5, 0, 1.0, 0, 0, 0, 255, self.stage[0]))

    def send_star_piece(self):
        self._send(proto.TAG_STAR, struct.pack(">Bxxxi6f", self.id, 0, *([0.0] * 6)))

    def send_time_query(self, check):
        self._send(proto.TAG_TQ, struct.pack(">II", 123, check))

    def queue_event(self, etype, name, arg, value):
        payload = struct.pack(proto.PROGRESS_FMT, self.id or 0, etype, self.next_seq, arg, value, name)
        self.pending.append((self.next_seq, payload))
        self.next_seq += 1

    def pump_events(self):
        """What ProgressSync::update does once per resend interval."""
        if self.pending:
            self._send(proto.TAG_PROGRESS, self.pending[0][1])
        # like the game: ack when something arrived, and every so often anyway
        # (a lost ack must not stall a replay request)
        self.pumps = getattr(self, "pumps", 0) + 1
        if self.need_ack or self.pumps % 20 == 0:
            self.need_ack = False
            self._send(proto.TAG_ACK, struct.pack(">I", self.rx_expected))

    def request_full_replay(self):
        self.rx_expected = 0
        self.applied = []
        self.need_ack = True

    # -- receiving -------------------------------------------------------
    def poll(self):
        while True:
            try:
                data, _ = self.sock.recvfrom(4096)
            except (BlockingIOError, ConnectionResetError):
                return
            if self.rng.random() < self.loss:
                continue
            assert len(data) <= 1408, f"datagram of {len(data)} bytes exceeds the mod's receive buffer"
            off, bundle = 0, 0
            while len(data) - off >= 4:
                tag, = struct.unpack_from(">I", data, off)
                size = proto.PAYLOAD_SIZE[tag]
                payload = data[off + 4: off + 4 + size]
                assert len(payload) == size, "truncated record"
                off += 4 + size
                self.on_record(tag, payload)
                bundle += tag == proto.TAG_POS
            assert off == len(data), "trailing bytes in datagram"
            self.max_bundle = max(self.max_bundle, bundle)

    def on_record(self, tag, payload):
        if tag == proto.TAG_SIR:
            major, _minor, epoch, pid, max_players = struct.unpack(">IIIBBxx", payload)
            assert major == proto.MAJOR
            self.id, self.epoch, self.max_players = pid, epoch, max_players
        elif tag == proto.TAG_POS:
            f = struct.unpack(POS_FMT, payload)
            pid, scenario, x, stage = f[0], f[2], f[4], f[-1]
            assert pid != self.id, "server echoed our own position"
            assert (stage, scenario) == self.stage, "got a position from another stage"
            self.positions[pid] = x
            self.pos_records += 1
        elif tag == proto.TAG_STAR:
            self.star_pieces += 1
        elif tag == proto.TAG_TR:
            self.time_responses.append(struct.unpack(">II", payload))
        elif tag == proto.TAG_ACK:
            seq, = struct.unpack(">I", payload)
            self.server_acks += 1
            if self.pending and self.pending[0][0] == seq:
                self.pending.pop(0)
        elif tag == proto.TAG_PROGRESS:
            _pid, etype, seq, arg, value, name = struct.unpack(proto.PROGRESS_FMT, payload)
            if seq == self.rx_expected:
                self.rx_expected += 1
                self.applied.append((etype, name.split(b"\0", 1)[0], arg, value))
            self.need_ack = True

    def state(self):
        """Final save state after applying the log in order."""
        out = {}
        for etype, name, arg, value in self.applied:
            out[(etype, name, arg)] = value
        return out

    def close(self):
        self.sock.close()


class ServerProcess:
    def __init__(self, state, extra=(), settings=None):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.bind(("127.0.0.1", 0))
            self.port = s.getsockname()[1]
        # Never read the real server-settings.ini: tests must not depend on it.
        # `settings` (ini text, {port} is filled in) replaces --port on the
        # command line so the file itself is what gets tested.
        self.settings_path = state + ".%d.ini" % self.port
        port_args = ["--port", str(self.port)]
        if settings is not None:
            with open(self.settings_path, "w") as f:
                f.write(settings.format(port=self.port))
            port_args = []
        # Log to a file: a pipe nobody reads fills up and blocks the server.
        self.log_path = state + ".%d.log" % self.port
        self.log_file = open(self.log_path, "w")
        self.proc = subprocess.Popen(
            [sys.executable, os.path.join(HERE, "smg_server.py"), "--host", "127.0.0.1",
             "--settings", self.settings_path, *port_args, "--state", state, *extra],
            stdout=self.log_file, stderr=subprocess.STDOUT)
        time.sleep(0.4)
        assert self.proc.poll() is None, self.output()

    def output(self):
        self.log_file.flush()
        with open(self.log_path, "r", errors="replace") as f:
            return f.read()

    def stop(self):
        self.proc.terminate()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        out = self.output()
        self.log_file.close()
        return out


def run_for(clients, seconds, each=None, step=1 / 60):
    end = time.monotonic() + seconds
    n = 0
    while time.monotonic() < end:
        if each:
            each(n)
        for c in clients:
            c.poll()
        n += 1
        time.sleep(step)


def wait_until(clients, cond, timeout, each=None):
    end = time.monotonic() + timeout
    n = 0
    while time.monotonic() < end:
        if each:
            each(n)
        for c in clients:
            c.poll()
        if cond():
            return True
        n += 1
        time.sleep(1 / 60)
    return False


def handshake(clients):
    def each(n):
        if n % 10 == 0:
            for c in clients:
                if c.id is None:
                    c.connect()
    assert wait_until(clients, lambda: all(c.id is not None for c in clients), 5, each), "handshake failed"


# ---------------------------------------------------------------------------

def test_handshake_and_ids(tmp):
    srv = ServerProcess(os.path.join(tmp, "a.json"), ["--max-players", "3"])
    try:
        cs = [FakeClient(srv.port) for _ in range(3)]
        handshake(cs)
        assert sorted(c.id for c in cs) == [0, 1, 2], [c.id for c in cs]
        assert len({c.epoch for c in cs}) == 1
        assert cs[0].max_players == 3

        # a repeated CONNECT keeps the same id
        old = cs[0].id
        cs[0].id = None
        handshake(cs)
        assert cs[0].id == old

        # a 4th player is refused while full
        extra = FakeClient(srv.port)
        run_for([extra], 0.5, lambda n: extra.connect() if n % 10 == 0 else None)
        assert extra.id is None, "server accepted a player beyond --max-players"

        # time query is answered with the same check code
        cs[1].send_time_query(0xABCD)
        assert wait_until(cs, lambda: cs[1].time_responses, 2)
        assert cs[1].time_responses[0][1] == 0xABCD
        for c in cs + [extra]:
            c.close()
    finally:
        srv.stop()


def test_stage_filtering(tmp):
    srv = ServerProcess(os.path.join(tmp, "b.json"))
    try:
        a, b, c, d = cs = [FakeClient(srv.port) for _ in range(4)]
        handshake(cs)
        a.stage = b.stage = (0x1111, 1)
        c.stage = (0x1111, 2)   # same galaxy, different star
        d.stage = (0, 0)        # not in a stage (loading / title)

        def each(n):
            for i, cl in enumerate(cs):
                cl.send_position(float(i * 100 + n))
        run_for(cs, 1.0, each)

        assert set(a.positions) == {b.id}, a.positions
        assert set(b.positions) == {a.id}, b.positions
        assert not c.positions and not d.positions
        assert a.pos_records > 30, f"only {a.pos_records} positions relayed in 1s"

        # star pieces only reach players in the same stage
        a.send_star_piece()
        run_for(cs, 0.3)
        assert (a.star_pieces, b.star_pieces, c.star_pieces, d.star_pieces) == (0, 1, 0, 0)

        # c moves into the same scenario and becomes visible both ways
        c.stage = (0x1111, 1)
        run_for(cs, 0.5, each)
        assert set(a.positions) == {b.id, c.id}
        assert set(c.positions) == {a.id, b.id}

        # a stops sending (loading screen): others stop hearing about it
        run_for(cs, 0.3, lambda n: [cl.send_position(1.0) for cl in (b, c)])  # flush what is in flight
        before = b.pos_records
        b.positions.clear()
        run_for(cs, 0.5, lambda n: [cl.send_position(1.0) for cl in (b, c)])
        assert a.id not in b.positions
        assert b.pos_records > before
        for cl in cs:
            cl.close()
    finally:
        srv.stop()


def test_progress_reliable_and_persistent(tmp):
    state = os.path.join(tmp, "c.json")
    srv = ServerProcess(state)
    events = [(0, b"EggStarGalaxy", i, 1) for i in range(1, 7)]
    events += [(1, b"EggStarGalaxy", i, 0) for i in range(1, 7)]
    events += [(5, ("Flag%d" % i).encode(), 0, 0) for i in range(60)]
    events += [(3, "ピーチ城".encode("shift_jis"), 0, 0)]          # non-ASCII name
    events += [(4, b"", 2, 100), (4, b"", 2, 250), (4, b"", 2, 250), (4, b"", 2, 90)]  # Luma feeding total
    events += [(2, b"Bit", 3, 1), (2, b"Bit", 3, 0), (2, b"Bit", 3, 1)]                 # toggling value
    expected = {}
    for t, nm, a, v in events:
        if t == 4 and expected.get((t, nm, a), -1) >= v:
            continue
        expected[(t, nm, a)] = v
    try:
        # 30% loss in both directions
        a = FakeClient(srv.port, loss=0.3, seed=1)
        b = FakeClient(srv.port, loss=0.3, seed=2)
        cs = [a, b]
        handshake(cs)
        # Events whose result depends on order (same key, different values) all
        # come from one player; across players the arrival order is not defined.
        for i, ev in enumerate(events):
            key = ev[:3]
            ordered = sum(1 for e in events if e[:3] == key) > 1
            (a if ordered or i % 2 == 0 else b).queue_event(*ev)

        def each(n):
            if n % 3 == 0:
                for cl in cs:
                    cl.pump_events()
        done = lambda: not a.pending and not b.pending and a.state() == expected and b.state() == expected
        assert wait_until(cs, done, 60, each), (
            f"did not converge: pending={len(a.pending)},{len(b.pending)} "
            f"applied={len(a.state())},{len(b.state())} of {len(expected)}")

        # a late joiner gets everything
        c = FakeClient(srv.port, loss=0.3, seed=3)
        handshake([c])
        c.need_ack = True
        assert wait_until([c], lambda: c.state() == expected, 30,
                          lambda n: c.pump_events() if n % 3 == 0 else None), "late joiner did not catch up"

        # a client that loads a save asks for the whole log again
        a.request_full_replay()
        assert wait_until(cs, lambda: a.state() == expected, 30, each), "full replay failed"
        epoch = a.epoch
        for cl in cs + [c]:
            cl.close()
    finally:
        out = srv.stop()

    # the log survives a server restart, with the same epoch
    assert os.path.exists(state), "progress was not saved:\n" + out
    srv = ServerProcess(state)
    try:
        d = FakeClient(srv.port)
        handshake([d])
        assert d.epoch == epoch, "epoch changed across restart"
        d.need_ack = True
        assert wait_until([d], lambda: d.state() == expected, 10,
                          lambda n: d.pump_events() if n % 3 == 0 else None), "log lost across restart"
        d.close()
    finally:
        srv.stop()


def test_timeout_and_reconnect(tmp):
    state = os.path.join(tmp, "d.json")
    srv = ServerProcess(state, ["--max-players", "2", "--session-timeout", "1"])
    try:
        a, b = FakeClient(srv.port), FakeClient(srv.port)
        handshake([a, b])
        a.stage = b.stage = (7, 1)
        # the server keeps talking to an idle but connected client (liveness)
        run_for([a, b], 1.5, lambda n: [a.send_position(0.0), b.send_position(0.0)])
        assert a.server_acks >= 1, "no keepalive from server"

        # b vanishes; after the timeout its slot is reusable
        run_for([a], 1.6, lambda n: a.send_position(0.0))
        c = FakeClient(srv.port)
        handshake([c])
        assert c.id == b.id, "slot of a timed-out player was not reused"
        a.close(), b.close(), c.close()
    finally:
        srv.stop()

    # server restart: packets from a client the new server has never seen are
    # ignored (the mod then notices the silence and reconnects)
    srv = ServerProcess(state)
    try:
        a = FakeClient(srv.port)
        a.id, a.stage = 0, (7, 1)
        run_for([a], 0.5, lambda n: a.send_position(0.0))
        assert a.server_acks == 0 and not a.positions
        a.id = None
        handshake([a])
        a.close()
    finally:
        out = srv.stop()
    assert "Traceback" not in out, out


def test_garbage(tmp):
    srv = ServerProcess(os.path.join(tmp, "e.json"))
    try:
        rng = random.Random(4)
        junk = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        a = FakeClient(srv.port)
        handshake([a])
        for _ in range(500):
            n = rng.randrange(0, 200)
            junk.sendto(bytes(rng.randrange(256) for _ in range(n)), ("127.0.0.1", srv.port))
            # also junk from a connected client's own socket, with valid tags
            tag = rng.randrange(0, 10)
            a.sock.sendto(struct.pack(">I", tag) + bytes(rng.randrange(256) for _ in range(rng.randrange(0, 80))),
                          a.server)
        junk.close()
        # still alive and answering
        a.send_time_query(77)
        assert wait_until([a], lambda: any(t[1] == 77 for t in a.time_responses), 3), "server died on garbage"
        a.close()
    finally:
        out = srv.stop()
    assert "Traceback" not in out, out


def test_settings_file(tmp):
    # first run without a settings file creates one with the defaults
    srv = ServerProcess(os.path.join(tmp, "s0.json"))
    try:
        assert os.path.exists(srv.settings_path), "settings file was not created"
        text = open(srv.settings_path).read()
        assert "port = 5029" in text and "share_progress = yes" in text
    finally:
        srv.stop()

    # port, player limit and sharing switches come from the file
    ini = "[server]\nport = {port}\nmax_players = 2\nshare_progress = no\nshare_star_bits = no\n"
    srv = ServerProcess(os.path.join(tmp, "s1.json"), settings=ini)
    try:
        a, b, c = FakeClient(srv.port), FakeClient(srv.port), FakeClient(srv.port)
        handshake([a, b])
        run_for([c], 0.5, lambda n: c.connect() if n % 10 == 0 else None)
        assert c.id is None, "max_players from the settings file was ignored"

        a.queue_event(0, b"EggStarGalaxy", 1, 1)
        both = [a, b]
        pump = lambda n: [cl.pump_events() for cl in both] if n % 3 == 0 else None
        assert wait_until(both, lambda: not a.pending, 5, pump), "event was not acked with sharing off"
        b.need_ack = True
        run_for(both, 1.0, pump)
        assert not b.applied and not a.applied, "progress was shared although share_progress = no"

        a.stage = b.stage = (5, 1)
        run_for(both, 0.3, lambda n: [a.send_position(0.0, n + 1), b.send_position(0.0, n + 1)])
        a.send_star_piece()
        run_for(both, 0.4, lambda n: [a.send_position(0.0, n + 100), b.send_position(0.0, n + 100)])
        assert b.star_pieces == 0, "star bits were shared although share_star_bits = no"
        assert b.positions, "players should still see each other"
        for cl in (a, b, c):
            cl.close()
    finally:
        out = srv.stop()
    assert "Traceback" not in out, out
    assert not os.path.exists(os.path.join(tmp, "s1.json")), "progress file written with sharing off"

    # a broken value falls back to the default instead of crashing
    srv = ServerProcess(os.path.join(tmp, "s2.json"), settings="[server]\nport = {port}\nmax_players = lots\n")
    try:
        a = FakeClient(srv.port)
        handshake([a])
        assert a.max_players == 64
        a.close()
    finally:
        out = srv.stop()
    assert "not valid" in out, out


def load_test(tmp, players):
    srv = ServerProcess(os.path.join(tmp, "load.json"), ["--max-players", str(players)])
    try:
        cs = [FakeClient(srv.port) for _ in range(players)]
        handshake(cs)
        assert len({c.id for c in cs}) == players
        for c in cs:
            c.stage = (0xABCDEF, 1)   # worst case: everyone in the same stage

        seconds = 5.0
        frames = [0]

        def each(n):
            frames[0] += 1
            for i, c in enumerate(cs):
                c.send_position(float(i))
        t0 = time.monotonic()
        run_for(cs, seconds, each)
        elapsed = time.monotonic() - t0
        for c in cs:
            assert len(c.positions) == players - 1, f"player {c.id} sees {len(c.positions)} of {players - 1}"
        rate = sum(c.pos_records for c in cs) / players / (players - 1) / elapsed
        print(f"    {players} players in one stage: each sees {players - 1} others, "
              f"{rate:.1f} updates/s per remote player, biggest bundle {max(c.max_bundle for c in cs)} "
              f"positions, client loop ran at {frames[0] / elapsed:.0f} fps")
        assert rate > 12, "relay rate collapsed under load"
        for c in cs:
            c.close()
    finally:
        out = srv.stop()
    assert "Traceback" not in out, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--load", type=int, default=0, help="also run a load test with this many players")
    args = ap.parse_args()
    tests = [test_handshake_and_ids, test_stage_filtering, test_progress_reliable_and_persistent,
             test_timeout_and_reconnect, test_garbage, test_settings_file]
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for t in tests:
            try:
                t(tmp)
                print(f"PASS {t.__name__}")
            except AssertionError as err:
                failed += 1
                print(f"FAIL {t.__name__}: {err}")
        if args.load:
            try:
                load_test(tmp, args.load)
                print(f"PASS load_test({args.load})")
            except AssertionError as err:
                failed += 1
                print(f"FAIL load_test({args.load}): {err}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
