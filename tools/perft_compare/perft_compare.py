#!/usr/bin/env python3
"""Compares the move generation speed of engines using perft, and prints a Markdown table.

Usage:
    perft_compare.py --engine name=NAME cmd=PATH [perft=COMMAND] [nodes=PREFIX] --engine ... [--runs N] [--cpu N]

Each engine is given with --engine and key=value pairs:
    name   label in the table
    cmd    path to the engine
    perft  perft command, with {depth} for the depth (default: "go perft {depth}")
    nodes  start of the line with the node count (default: "Nodes searched")

--cpu pins to a CPU with taskset.

Example:
    perft_compare.py --engine name=Chrunch cmd=bin/chess "perft=perft {depth}" "nodes=Number of nodes" \\
                     --engine name=Stockfish cmd=path/to/stockfish --cpu 2
"""

import argparse
import re
import statistics
import subprocess
import sys
import time

# Standard perft positions from https://www.chessprogramming.org/Perft_Results
POSITIONS = [
    ("Start position", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 6),
    ("Kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 5),
    ("Position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 7),
    ("Position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 6),
    ("Position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 5),
]


class Engine:
    def __init__(self, pairs, cpu):
        spec = {"perft": "go perft {depth}", "nodes": "Nodes searched", **dict(p.split("=", 1) for p in pairs)}
        self.name, self.perft_command, self.nodes_prefix = spec.get("name", spec["cmd"]), spec["perft"], spec["nodes"]
        cmd = ([] if cpu is None else ["taskset", "-c", str(cpu)]) + [spec["cmd"]]
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.send("isready")
        self.read_until("readyok")

    def send(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def read_until(self, prefix):
        for line in self.proc.stdout:
            if line.startswith(prefix):
                return line
        sys.exit(f"{self.name} exited while waiting for '{prefix}'")

    def perft(self, fen, depth):
        """Returns (nodes, seconds)."""
        self.send(f"position fen {fen}")
        self.send("isready")
        self.read_until("readyok")
        start = time.perf_counter()
        self.send(self.perft_command.format(depth=depth))
        line = self.read_until(self.nodes_prefix)
        seconds = time.perf_counter() - start
        return int(re.search(r"\d[\d,]*", line[len(self.nodes_prefix):]).group().replace(",", "")), seconds

    def close(self):
        self.send("quit")
        self.proc.wait()


def markdown_table(header, rows):
    widths = [max(len(row[i]) for row in [header] + rows) for i in range(len(header))]

    def line(cells):
        return "| " + " | ".join(cell.ljust(w) if i == 0 else cell.rjust(w)
                                 for i, (cell, w) in enumerate(zip(cells, widths))) + " |"

    separator = "|" + "|".join("-" * (w + 2) if i == 0 else "-" * (w + 1) + ":" for i, w in enumerate(widths)) + "|"
    return "\n".join([line(header), separator] + [line(row) for row in rows])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine", nargs="+", action="append", required=True, metavar="KEY=VALUE",
                        help="an engine; the first is the baseline")
    parser.add_argument("--runs", type=int, default=5, help="runs per position; the median is reported")
    parser.add_argument("--cpu", type=int, help="CPU to pin the engines to")
    args = parser.parse_args()

    engines = [Engine(pairs, args.cpu) for pairs in args.engine]
    baseline, others = engines[0], engines[1:]

    rows, total_nodes, total_seconds = [], 0, {engine.name: 0.0 for engine in engines}
    for label, fen, depth in POSITIONS:
        nps, nodes_by_engine = {}, {}
        for engine in engines:
            runs = [engine.perft(fen, depth) for _ in range(args.runs)]
            seconds = statistics.median(seconds for _, seconds in runs)
            nodes_by_engine[engine.name] = runs[0][0]
            nps[engine.name] = runs[0][0] / seconds
            total_seconds[engine.name] += seconds
        if len(set(nodes_by_engine.values())) != 1:
            sys.exit(f"Perft node counts differ for {label} (FEN: {fen}): {nodes_by_engine}")
        total_nodes += nodes_by_engine[baseline.name]
        rows.append((label, depth, nodes_by_engine[baseline.name], nps))

    for engine in engines:
        engine.close()

    def factor(speed, baseline_speed):
        return f"{speed / baseline_speed:.2f}x"

    header = (["Position", "Depth", "Nodes"] + [f"{engine.name} (Mnps)" for engine in engines] +
              [f"{engine.name} vs {baseline.name}" for engine in others])
    table = [[label, str(depth), f"{nodes:,}"] + [f"{nps[engine.name] / 1e6:.0f}" for engine in engines] +
             [factor(nps[engine.name], nps[baseline.name]) for engine in others]
             for label, depth, nodes, nps in rows]
    total_nps = {engine.name: total_nodes / total_seconds[engine.name] for engine in engines}
    table.append(["**Total**", "", f"{total_nodes:,}"] + [f"**{total_nps[engine.name] / 1e6:.0f}**" for engine in engines] +
                 [f"**{factor(total_nps[engine.name], total_nps[baseline.name])}**" for engine in others])
    print(markdown_table(header, table))


if __name__ == "__main__":
    main()
