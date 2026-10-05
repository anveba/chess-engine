import collections
import os
import re
import subprocess
from dataclasses import dataclass, field

DEFAULT_DRAW_ADJUDICATION = {"movenumber": 40, "movecount": 8, "score": 10}
DEFAULT_RESIGN_ADJUDICATION = {"movecount": 4, "score": 1000, "twosided": True}


@dataclass
class Engine:
    name: str
    cmd: str
    options: dict = field(default_factory=dict)
    directory: str = None  # Defaults to the directory of cmd.


def _value(v):
    return str(v).lower() if isinstance(v, bool) else str(v)


def _settings(settings):
    return [f"{key}={_value(v)}" for key, v in settings.items()]


def command(engines, *, tc, openings, games, concurrency, pgn_out=None, hash_mb=16, time_margin=None,
            tournament=None, seed=None, sprt_elo=None, rating_interval=None,
            draw=DEFAULT_DRAW_ADJUDICATION, resign=DEFAULT_RESIGN_ADJUDICATION, fastchess="fastchess"):
    """Games are played in pairs: each opening twice, with colours swapped. sprt_elo is (elo0, elo1)."""
    cmd = [os.path.abspath(fastchess) if os.sep in fastchess else fastchess]
    if tournament:
        cmd += ["-tournament", tournament]
    for e in engines:
        cmd += ["-engine", f"name={e.name}", f"cmd={e.cmd}", f"dir={e.directory or os.path.dirname(e.cmd)}",
                *[f"option.{key}={_value(v)}" for key, v in e.options.items()]]

    cmd += ["-each", f"tc={tc}", *([f"timemargin={time_margin}"] if time_margin is not None else []), f"option.Hash={hash_mb}"]
    cmd += ["-games", "2", "-rounds", str(games // 2), "-repeat", "-recover"]
    if seed is not None:
        cmd += ["-srand", str(seed)]
    cmd += ["-openings", f"file={os.path.abspath(openings)}", "format=epd", "order=random"]
    cmd += ["-concurrency", str(concurrency)]
    if sprt_elo:
        cmd += ["-sprt", f"elo0={sprt_elo[0]}", f"elo1={sprt_elo[1]}", "alpha=0.05", "beta=0.05"]
    if rating_interval:
        cmd += ["-ratinginterval", str(rating_interval)]
    if pgn_out:
        cmd += ["-pgnout", f"file={pgn_out}"]
    return cmd + ["-draw", *_settings(draw), "-resign", *_settings(resign)]


def run_in_dir_with_log(cmd, run_dir, on_output_line=None):
    # Run in run_dir since fastchess writes its config.json to its working directory.
    with open(os.path.join(run_dir, "fastchess.log"), "w") as log, \
            subprocess.Popen(cmd, cwd=run_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as proc:
        for line in proc.stdout:
            log.write(line)
            if on_output_line:
                on_output_line(line)


def time_forfeits_by_engine(pgn_paths):
    losses = collections.Counter()
    for path in pgn_paths:
        with open(path) as f:
            for game in f.read().split("[Event ")[1:]:
                tags = dict(re.findall(r'\[(\w+) "([^"]*)"\]', game))
                if tags.get("Termination") == "time forfeit":
                    losses[tags["Black"] if tags["Result"] == "1-0" else tags["White"]] += 1
    return losses


def time_forfeits_summary(pgn_paths):
    losses = time_forfeits_by_engine(pgn_paths)
    return "Time forfeits: " + (", ".join(f"{name} {n}" for name, n in losses.most_common()) or "none")
