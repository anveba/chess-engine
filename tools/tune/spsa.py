#!/usr/bin/env python3
"""Tunes the engine's TUNABLE parameters with SPSA. The tuned values are written to --output for apply.py.

Usage:
    spsa.py --openings FILE --output FILE [--iterations N] [--pairs N] [--tc TC] [--params NAME,...] [--fastchess PATH]
"""

import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

from params import find_params

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import fastchess, util

# https://chessprogramming.org/SPSA

REPO = Path(__file__).resolve().parents[2]
RESULTS_LINE = re.compile(r"Wins: (\d+), Losses: (\d+)")


def match(plus, minus, book, tune_binary, run_dir, tc, pairs, fastchess_path):
    cmd = fastchess.command([fastchess.Engine("plus", tune_binary, options=plus), fastchess.Engine("minus", tune_binary, options=minus)],
                            tc=tc, openings=book, games=2 * pairs, concurrency=pairs, seed=random.randrange(2**31),
                            fastchess=fastchess_path)
    lines = []
    fastchess.run_in_dir_with_log(cmd, run_dir, lines.append)
    wins, losses = map(int, [m for m in map(RESULTS_LINE.search, lines) if m][-1].groups())
    return wins - losses


def make_schedule(low, high, n, alpha, gamma, r_end):
    c_end = np.maximum((high - low) / 20, 1.0)
    A = 0.1 * n
    c = c_end * n ** gamma
    a = r_end * c_end ** 2 * (A + n) ** alpha

    def step_sizes(k):
        return a / (k + 1 + A) ** alpha, c / (k + 1) ** gamma

    return step_sizes


def engine_options(names, theta, low, high):
    return dict(zip(names, np.clip(np.round(theta), low, high).astype(int)))


def load_state(output, names):
    """Returns (iteration, parameters) from an interrupted run with the same output file, or None."""
    if not os.path.exists(output + ".state"):
        return None
    with open(output + ".state") as f:
        state = json.load(f)
    print(f"Resuming from iteration {state['k']} with the state in {output}.state")
    return state["k"], np.array([state["theta"][name] for name in names])


def save_state(output, names, params, iterations_done):
    with open(output, "w") as f:
        f.writelines(f"{name}, {round(value)}\n" for name, value in zip(names, params))
    with open(output + ".state.tmp", "w") as f:
        json.dump({"k": iterations_done, "theta": dict(zip(names, params.tolist()))}, f)
    os.replace(output + ".state.tmp", output + ".state")


def spsa(n, params, names, low, high, play, output, alpha=0.602, gamma=0.101, r_end=0.002):
    step_sizes = make_schedule(low, high, n, alpha, gamma, r_end)
    start, params = load_state(output, names) or (0, params)

    for k in range(start, n):
        ak, ck = step_sizes(k)
        delta_p = np.random.choice([-1, 1], size=len(params))

        result = play(engine_options(names, params + ck * delta_p, low, high),
                      engine_options(names, params - ck * delta_p, low, high))
        params = np.clip(params + ak * result / (ck * delta_p), low, high)

        save_state(output, names, params, k + 1)
        print(f"Iteration {k + 1}/{n}: {result:+d}", flush=True)

    return params


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--openings", required=True, help="EPD file with opening positions")
    parser.add_argument("--output", required=True, help="file for the tuned values, rewritten after every iteration")
    parser.add_argument("--iterations", type=int, default=2000)
    parser.add_argument("--pairs", type=int, default=8, help="game pairs per iteration, played in parallel (default 8)")
    parser.add_argument("--tc", default="8+0.08")
    parser.add_argument("--params", help="comma-separated names to tune (default all)")
    parser.add_argument("--fastchess", default="fastchess", help="path to fastchess (default: fastchess from PATH)")
    args = parser.parse_args()

    if args.params and (unknown := set(args.params.split(",")) - set(find_params())):
        sys.exit(f"Unknown parameters: {', '.join(sorted(unknown))}")
    params = [p for p in find_params().values() if not args.params or p.name in args.params.split(",")]

    run_dir = util.make_timestamped_dir(REPO / "tools" / "tune" / "results", "spsa")
    subprocess.run(["make", "-C", REPO, "tune"], check=True, stdout=subprocess.DEVNULL)
    tune_binary = shutil.copy2(REPO / "bin" / "chess-tune", run_dir)  # copy so we can rebuild while running

    def play(plus, minus):
        return match(plus, minus, args.openings, tune_binary, run_dir, args.tc, args.pairs, args.fastchess)

    print(f"Tuning {len(params)} parameters for {args.iterations} iterations of {args.pairs} pairs. Log in {run_dir}")
    spsa(args.iterations, np.array([p.value for p in params], dtype=float), [p.uci_name() for p in params],
         np.array([p.min for p in params]), np.array([p.max for p in params]), play, args.output)


if __name__ == "__main__":
    main()
