#!/usr/bin/env python3
"""Tests whether a change is stronger than a reference version via SPRT.

Usage:
    sprt.py --openings FILE --output-dir DIR [--base REV] [--fastchess PATH] [--tc TC] [--concurrency N]
            [--elo0 E0] [--elo1 E1]
"""

import argparse
import os
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import fastchess, git, util

MAX_GAMES = 40000
HASH_MB = 16
RESULT_LINE = re.compile(r"^(Results of|Elo:|LOS:|Games:|Ptnml|LLR:|SPRT)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--openings", required=True, help="EPD file with opening positions")
    parser.add_argument("--output-dir", required=True, help="directory for each test's engines, games and log")
    parser.add_argument("--base", default="HEAD", help="revision to compare the working tree with (default HEAD)")
    parser.add_argument("--fastchess", default="fastchess", help="path to fastchess (default: fastchess from PATH)")
    parser.add_argument("--tc", default="10+0.1", help="time control (default 10+0.1)")
    parser.add_argument("--concurrency", type=int, default=os.cpu_count() - 1)
    parser.add_argument("--elo0", type=float, default=0.0)
    parser.add_argument("--elo1", type=float, default=5.0)
    args = parser.parse_args()

    repo = git.Repo(os.getcwd())
    base = repo.short_revision(args.base)
    new_name, base_name = f"new-{repo.working_tree_label()}", f"base-{base}"

    run_dir = util.make_timestamped_dir(args.output_dir, f"{new_name}-vs-{base_name}")
    new_engine, base_engine = os.path.join(run_dir, "new"), os.path.join(run_dir, "base")

    print(f"Building {base_name} and {new_name}...", flush=True)
    # The base is built clean
    with tempfile.TemporaryDirectory() as tmp:
        repo.export_revision(base, tmp)
        util.build_engine(tmp, base_engine)
    util.build_engine(repo.root, new_engine)

    pgn_path = os.path.join(run_dir, "games.pgn")
    cmd = fastchess.command(
        [fastchess.Engine(new_name, new_engine, directory=run_dir), fastchess.Engine(base_name, base_engine, directory=run_dir)],
        tc=args.tc, openings=args.openings, games=MAX_GAMES, concurrency=args.concurrency, pgn_out=pgn_path,
        hash_mb=HASH_MB, sprt_elo=(args.elo0, args.elo1), rating_interval=100, fastchess=args.fastchess)

    print(f"SPRT [{args.elo0}, {args.elo1}] at {args.tc} with concurrency {args.concurrency}. Output in {run_dir}",
          flush=True)

    def print_results(line):
        if RESULT_LINE.match(line):
            print(line.rstrip(), flush=True)

    fastchess.run_in_dir_with_log(cmd, run_dir, print_results)
    print(fastchess.time_forfeits_summary([pgn_path]))


if __name__ == "__main__":
    main()
