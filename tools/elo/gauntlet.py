#!/usr/bin/env python3
"""Estimates the engine's Elo by playing a gauntlet against rated opponents with fastchess.

Usage:
    gauntlet.py run CONFIG ENGINE --engines-dir DIR --output-dir DIR [--history FILE] [--fastchess PATH]
                    [--ordo PATH] [--games N] [--concurrency N] [--tc TC]
    gauntlet.py rate CONFIG [--ordo PATH] PGN...

'run' plays ENGINE against every opponent in the config and estimates its rating. 'rate' estimates the rating
from existing games. Opponents' paths in the config are relative to --engines-dir, and other relative paths to
the config file's directory.

The rating is calculated by Ordo (https://github.com/michiguel/Ordo).
"""

import argparse
import collections
import csv
import datetime
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import tomllib

Z_95 = 1.96
PROGRESS_INTERVAL = 10
ORDO_SIMULATIONS = 2000

HISTORY_COLUMNS = ["date", "revision", "tc", "games", "rating", "low", "high", "opponents", "pgn"]


def time_forfeits(pgn_paths):
    losses = collections.Counter()
    for path in pgn_paths:
        with open(path) as f:
            for game in f.read().split("[Event ")[1:]:
                tags = dict(re.findall(r'\[(\w+) "([^"]*)"\]', game))
                if tags.get("Termination") == "time forfeit":
                    losses[tags["Black"] if tags["Result"] == "1-0" else tags["White"]] += 1
    return losses

def rate(ordo, pgn_paths, config):
    """Returns (rating, 95% error, games)."""
    name = config["engine"]["name"]
    opponents = config["opponent"]
    fixed = [[opp["name"], opp["rating"]] for opp in opponents if not opp.get("rating_error")]
    loose = [[opp["name"], opp["rating"], opp["rating_error"] / Z_95] for opp in opponents if opp.get("rating_error")]

    with tempfile.TemporaryDirectory() as tmp:
        pgn_list, csv_path = os.path.join(tmp, "pgns.txt"), os.path.join(tmp, "ratings.csv")
        with open(pgn_list, "w") as f:
            f.write("".join(os.path.abspath(path) + "\n" for path in pgn_paths))
        cmd = [ordo, "-Q", "-P", pgn_list, "-c", csv_path, "-s", str(ORDO_SIMULATIONS),
               "-n", str(os.cpu_count()), "-W", "-D"]
        for option, rows in [("-m", fixed), ("-y", loose)]:
            if rows:
                path = os.path.join(tmp, option[1] + ".csv")
                with open(path, "w", newline="") as f:
                    csv.writer(f, quoting=csv.QUOTE_NONNUMERIC).writerows(rows)
                cmd += [option, path]

        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0 or not os.path.isfile(csv_path):
            sys.exit(f"Ordo failed:\n{proc.stdout}{proc.stderr}")

        with open(csv_path, newline="") as f:
            row = next(row for row in csv.DictReader(f) if row["PLAYER"] == name)

    rating, error, games = float(row["RATING"]), float(row["ERROR"]), int(row["PLAYED"])
    print(f"\nEstimated rating: {rating:.0f} (95% interval {rating - error:.0f} to {rating + error:.0f}) from {games} games")
    print("Time forfeits: " + (", ".join(f"{name} {n}" for name, n in time_forfeits(pgn_paths).most_common()) or "none"))
    return rating, error, games


def engine_args(name, cmd, options):
    def value(v):
        return str(v).lower() if isinstance(v, bool) else str(v)
    return ["-engine", f"name={name}", f"cmd={cmd}", f"dir={os.path.dirname(cmd)}",
            *[f"option.{key}={value(v)}" for key, v in options.items()]]


def git_revision(directory):
    def git(*args):
        return subprocess.run(["git", *args], cwd=directory, capture_output=True, text=True).stdout.strip()
    commit = git("rev-parse", "--short", "HEAD") or "unknown"
    return commit + ("-dirty" if git("status", "--porcelain", "--untracked-files=no") else "")


def play(cmd, out_dir, total_games):
    finished = 0
    with open(os.path.join(out_dir, "fastchess.log"), "w") as log, \
            subprocess.Popen(cmd, cwd=out_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as proc:
        for line in proc.stdout:
            log.write(line)
            if line.startswith("Finished game"):
                finished += 1
                if finished % PROGRESS_INTERVAL == 0:
                    print(f"  {finished}/{total_games} games", flush=True)


def append_history(path, row):
    new = not os.path.exists(path)
    with open(path, "a", newline="") as f:
        writer = csv.DictWriter(f, HISTORY_COLUMNS)
        if new:
            writer.writeheader()
        writer.writerow(row)
    print(f"Recorded in {path}")


def run(args, config):
    match, engine = config["match"], config["engine"]
    games = args.games or match["games_per_opponent"]
    concurrency = args.concurrency or match["concurrency"]
    tc = args.tc or match["tc"]
    engines_dir = os.path.abspath(args.engines_dir)
    fastchess = os.path.abspath(args.fastchess) if os.sep in args.fastchess else args.fastchess

    # The engine is copied so changes don't affect it.
    source = os.path.abspath(args.engine)
    revision = git_revision(os.path.dirname(source))
    out_dir = os.path.join(os.path.abspath(args.output_dir), datetime.datetime.now().strftime("%Y%m%d-%H%M%S-") + revision)
    os.makedirs(out_dir)
    engine_cmd = os.path.join(out_dir, os.path.basename(source))
    shutil.copy2(source, engine_cmd)
    pgn_path = os.path.join(out_dir, "games.pgn")

    draw, resign = match["draw"], match["resign"]
    cmd = [fastchess, "-tournament", "gauntlet",
           *engine_args(engine["name"], engine_cmd, engine.get("options", {}))]
    for opp in config["opponent"]:
        cmd += engine_args(opp["name"], os.path.join(engines_dir, opp["cmd"]), opp.get("options", {}))
    cmd += ["-each", f"tc={tc}", f"timemargin={match['timemargin']}", f"option.Hash={match['hash']}",
            "-games", "2", "-rounds", str(games // 2), "-repeat", "-recover", "-srand", str(int(time.time())),
            "-openings", f"file={os.path.join(config['dir'], match['openings'])}", "format=epd", "order=random",
            "-concurrency", str(concurrency), "-pgnout", f"file={pgn_path}",
            "-draw", f"movenumber={draw['movenumber']}", f"movecount={draw['movecount']}", f"score={draw['score']}",
            "-resign", f"movecount={resign['movecount']}", f"score={resign['score']}"]

    total = games * len(config["opponent"])
    print(f"Playing {total} games at {tc} with concurrency {concurrency}. Output in {out_dir}")
    play(cmd, out_dir, total)

    rating, error, played = rate(args.ordo, [pgn_path], config)
    if args.history:
        append_history(args.history, {
            "date": datetime.date.today().isoformat(), "revision": revision, "tc": tc, "games": played,
            "rating": round(rating), "low": round(rating - error), "high": round(rating + error),
            "opponents": ";".join(sorted(opp["name"] for opp in config["opponent"])), "pgn": pgn_path})


def main():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("config", help="TOML file with the engine's name and options, the opponents and match settings")
    common.add_argument("--ordo", default="ordo", help="path to ordo (default: ordo from PATH)")

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)

    run_parser = sub.add_parser("run", parents=[common], help="play a gauntlet and estimate the rating")
    run_parser.add_argument("engine", help="path to the engine to rate")
    run_parser.add_argument("--engines-dir", required=True, help="directory that the opponents' paths are relative to")
    run_parser.add_argument("--output-dir", required=True, help="directory for each run's games, log and engine copy")
    run_parser.add_argument("--history", help="CSV file to append the estimate to")
    run_parser.add_argument("--fastchess", default="fastchess", help="path to fastchess (default: fastchess from PATH)")
    run_parser.add_argument("--games", type=int, help="games per opponent")
    run_parser.add_argument("--concurrency", type=int)
    run_parser.add_argument("--tc", help="time control, e.g. 10+0.1")

    rate_parser = sub.add_parser("rate", parents=[common], help="estimate the rating from existing PGN files")
    rate_parser.add_argument("pgn", nargs="+")

    args = parser.parse_args()
    with open(args.config, "rb") as f:
        config = tomllib.load(f)
    config["dir"] = os.path.dirname(os.path.abspath(args.config))
    run(args, config) if args.mode == "run" else rate(args.ordo, args.pgn, config)


if __name__ == "__main__":
    main()
