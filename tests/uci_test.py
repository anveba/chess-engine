#!/usr/bin/env python3
import queue
import re
import subprocess
import sys
import threading
import time
import traceback

ENGINE = sys.argv[1] if len(sys.argv) > 1 else "bin/chess"

# Sanitizer builds are much slower.
SLOW = 10 if ("asan" in ENGINE or "tsan" in ENGINE) else 1

MATE_IN_2 = "r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 1"
MIDDLEGAME = "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 1"

DRAWN_ROOT = "8/8/8/4k3/8/8/3Q4/K7 w - - 100 80"


class Engine:
    def __init__(self):
        self.proc = subprocess.Popen([ENGINE], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.lines = queue.Queue()
        threading.Thread(target=self.read_output, daemon=True).start()

        self.send("isready")
        self.read_until(r"^readyok", 10)

    def read_output(self):
        for line in self.proc.stdout:
            self.lines.put(line.rstrip("\r\n"))

    def send(self, text):
        self.proc.stdin.write(text + "\n")
        self.proc.stdin.flush()

    def read_line(self, timeout):
        try:
            return self.lines.get(timeout=max(0, timeout) * SLOW)
        except queue.Empty:
            return None

    def read_until(self, pattern, timeout=5):
        lines = []
        deadline = time.time() + timeout * SLOW
        while True:
            line = self.read_line((deadline - time.time()) / SLOW)
            if line is None:
                raise AssertionError(f"timed out waiting for {pattern!r}; got {lines[-5:]}")
            lines.append(line)
            if re.search(pattern, line):
                return lines

    def expect_no_bestmove(self, seconds):
        deadline = time.time() + seconds
        while (remaining := deadline - time.time()) > 0:
            line = self.read_line(remaining / SLOW)
            assert line is None or not line.startswith("bestmove"), f"unexpected {line!r}"

    def start_infinite_search(self, position="position startpos"):
        self.send(position)
        self.send("go infinite")
        self.read_until(r"^info")

    def legal_moves(self, position):
        self.send(position)
        self.send("moves")
        return self.read_until(r"^moves")[-1].split()[1:]

    def search(self, position, go, timeout=10):
        legal = self.legal_moves(position)
        self.send(position)
        self.send(go)
        lines = self.read_until(r"^bestmove", timeout)
        best = lines[-1].split()[1]
        assert best in legal, f"illegal bestmove {best} for {position}"
        return best, lines

    def fen(self):
        self.send("d")
        return self.read_until(r"^FEN:")[-1].split(" ", 1)[1]

    def wait_exit(self, timeout):
        try:
            self.proc.wait(timeout * SLOW)
            return True
        except subprocess.TimeoutExpired:
            return False

    def close(self):
        if self.proc.poll() is None:
            self.send("quit")
            if not self.wait_exit(5):
                self.proc.kill()


def test_handshake_and_options(e):
    e.send("uci")
    lines = e.read_until(r"^uciok")
    assert any(line.startswith("id name") for line in lines)
    options = [line for line in lines if line.startswith("option")]
    names = [re.search(r"option name (.+?) type", line).group(1) for line in options]
    for name in ["Hash", "Threads", "Move Overhead"]:
        assert name in names, names

    for line in options:
        match = re.fullmatch(r"option name .+ type (spin default (-?\d+) min (-?\d+) max (-?\d+)|"
                             r"check default (true|false)|string default \S.*)", line)
        assert match, line
        if match.group(2):
            default, low, high = int(match.group(2)), int(match.group(3)), int(match.group(4))
            assert low <= default <= high and -2**31 <= low and high < 2**31, line


def test_go_depth_reports_completed_depth(e):
    _, lines = e.search("position startpos", "go depth 4")
    assert lines[-2].startswith("info depth 4 "), lines[-2]


def test_go_mate(e):
    best, lines = e.search("position fen " + MATE_IN_2, "go mate 2")
    assert best == "d5f6", best
    assert "score mate 2" in lines[-2], lines[-2]


def test_stop_right_after_go(e):
    for _ in range(50):
        e.send("position fen " + MIDDLEGAME)
        e.send("go")
        e.send("stop")
        best = e.read_until(r"^bestmove", 2)[-1].split()[1]
        assert best != "(none)"


def test_isready_during_search(e):
    e.start_infinite_search()
    e.send("isready")
    lines = e.read_until(r"^readyok", 2)
    assert not any(line.startswith("bestmove") for line in lines)
    e.send("stop")
    e.read_until(r"^bestmove")


def test_quit_during_search(e):
    e.start_infinite_search()
    e.send("quit")
    assert e.wait_exit(3), "engine did not exit"


def test_infinite_waits_for_stop_even_when_done(e):
    e.send("position fen " + DRAWN_ROOT)
    e.send("go infinite")
    e.expect_no_bestmove(1)
    e.send("stop")
    e.read_until(r"^bestmove", 2)


def test_ponder_waits_for_ponderhit_even_when_done(e):
    e.send("position fen " + DRAWN_ROOT)
    e.send("go ponder wtime 10000 btime 10000")
    e.expect_no_bestmove(1)
    e.send("ponderhit")
    e.read_until(r"^bestmove", 3)


def test_time_limits_are_respected(e):
    def elapsed(position, go):
        start = time.time()
        e.search(position, go, 30)
        return time.time() - start

    middlegame = "position fen " + MIDDLEGAME
    assert 0.2 < elapsed(middlegame, "go movetime 300") < 0.3 + 0.5 * SLOW
    assert elapsed(middlegame, "go wtime 50 btime 50") < 0.05 * SLOW
    assert elapsed("position startpos", "go wtime 0 btime 5000 winc 100 binc 100") < 0.5 * SLOW

    # Only one legal move needs no thought.
    assert elapsed("position fen 7k/8/8/8/8/8/6q1/7K w - - 0 1", "go wtime 60000 btime 60000") < 0.1 * SLOW


def test_illegal_move_keeps_earlier_moves(e):
    e.send("position startpos moves e2e4 e7e5 e2e5 d7d5")
    assert e.fen() == "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"


def test_invalid_fen_keeps_previous_position(e):
    e.send("position startpos moves e2e4")
    before = e.fen()
    for fen in ["8/8/8/8/8/8/8/8 w - - 0 1", "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "garbage"]:
        e.send("position fen " + fen)
        assert e.fen() == before, fen


def test_setoption_and_new_game(e):
    e.send("setoption name Hash value 8")
    e.send("ucinewgame")
    e.send("isready")
    e.read_until(r"^readyok")
    e.search("position startpos", "go depth 3")


def main():
    tests = [(name, fn) for name, fn in globals().items() if name.startswith("test_")]
    failed = 0
    for name, fn in tests:
        start = time.time()
        engine = None
        try:
            engine = Engine()
            fn(engine)
            print(f"[  OK  ] {name} ({int((time.time() - start) * 1000)} ms)", flush=True)
        except Exception:
            failed += 1
            print(f"[ FAIL ] {name}", flush=True)
            traceback.print_exc()
        finally:
            if engine:
                engine.close()
    print(f"\n{len(tests) - failed}/{len(tests)} UCI tests passed.")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
