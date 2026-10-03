#!/usr/bin/env python3
"""Tests whether a change is stronger than a reference version via SPRT.

Usage:
    sprt.py --openings FILE --output-dir DIR [--base REV] [--fastchess PATH] [--tc TC] [--concurrency N]
            [--elo0 E0] [--elo1 E1]
"""

import argparse
import collections
import datetime
import os
import re
import shutil
import subprocess
import tempfile

MAX_GAMES = 40000
HASH_MB = 16
RESULT_LINE = re.compile(r"^(Results of|Elo:|LOS:|Games:|Ptnml|LLR:|SPRT)")


def time_forfeits(pgn_paths):
    losses = collections.Counter()
    for path in pgn_paths:
        with open(path) as f:
            for game in f.read().split("[Event ")[1:]:
                tags = dict(re.findall(r'\[(\w+) "([^"]*)"\]', game))
                if tags.get("Termination") == "time forfeit":
                    losses[tags["Black"] if tags["Result"] == "1-0" else tags["White"]] += 1
    return losses


def git(repo, *args):
    return subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True, check=True).stdout.strip()


def build(source_dir, target):
    subprocess.run(["make", "-C", source_dir, "release"], check=True, stdout=subprocess.DEVNULL)
    shutil.copy2(os.path.join(source_dir, "bin", "chess"), target)


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

    repo = git(os.getcwd(), "rev-parse", "--show-toplevel")
    base = git(repo, "rev-parse", "--short", args.base)
    dirty = "-dirty" if git(repo, "status", "--porcelain", "--untracked-files=no") else ""
    new_name, base_name = f"new-{git(repo, 'rev-parse', '--short', 'HEAD')}{dirty}", f"base-{base}"

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = os.path.join(os.path.abspath(args.output_dir), f"{stamp}-{new_name}-vs-{base_name}")
    os.makedirs(run_dir)
    new_engine, base_engine = os.path.join(run_dir, "new"), os.path.join(run_dir, "base")

    print(f"Building {base_name} and {new_name}...", flush=True)
    # The base is built clean
    with tempfile.TemporaryDirectory() as tmp:
        archive = subprocess.run(["git", "-C", repo, "archive", base], capture_output=True, check=True).stdout
        subprocess.run(["tar", "-x", "-C", tmp], input=archive, check=True)
        build(tmp, base_engine)
    build(repo, new_engine)

    fastchess = os.path.abspath(args.fastchess) if os.sep in args.fastchess else args.fastchess
    cmd = [fastchess,
           "-engine", f"cmd={new_engine}", f"name={new_name}", f"dir={run_dir}",
           "-engine", f"cmd={base_engine}", f"name={base_name}", f"dir={run_dir}",
           "-each", f"tc={args.tc}", f"option.Hash={HASH_MB}",
           "-openings", f"file={os.path.abspath(args.openings)}", "format=epd", "order=random",
           "-games", "2", "-rounds", str(MAX_GAMES // 2), "-repeat", "-recover", "-concurrency", str(args.concurrency),
           "-sprt", f"elo0={args.elo0}", f"elo1={args.elo1}", "alpha=0.05", "beta=0.05",
           "-ratinginterval", "100", "-pgnout", f"file={os.path.join(run_dir, 'games.pgn')}",
           "-draw", "movenumber=40", "movecount=8", "score=10",
           "-resign", "movecount=4", "score=1000", "twosided=true"]

    print(f"SPRT [{args.elo0}, {args.elo1}] at {args.tc} with concurrency {args.concurrency}. Output in {run_dir}",
          flush=True)
    # fastchess runs in the output directory since it saves its config.json in its working directory.
    with open(os.path.join(run_dir, "fastchess.log"), "w") as log, \
            subprocess.Popen(cmd, cwd=run_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as proc:
        for line in proc.stdout:
            log.write(line)
            if RESULT_LINE.match(line):
                print(line.rstrip(), flush=True)
    print("Time forfeits: " + (", ".join(f"{name} {n}" for name, n in time_forfeits([os.path.join(run_dir, "games.pgn")]).most_common()) or "none"))


if __name__ == "__main__":
    main()
