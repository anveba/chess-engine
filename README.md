# Chrunch
A UCI chess engine written in C++.

Estimated strength: **3322 +/- 10 Elo** on the CCRL Blitz scale after 3200 games (see [Estimating Elo](#estimating-elo)).

**Features**
- Alpha-beta search with principal variation search
- Iterative deepening with aspiration windows
- Null-move pruning, late move reductions, reverse futility pruning, and check extensions
- NNUE evaluation trained via self-play with 512 hidden nodes and 8 output buckets, optimised with SIMD
- Tuned search parameters
- Transposition table
- Move ordering by the transposition table move, MVV-LVA for captures, killer moves, and the history heuristic
- Quiescence search, delta pruning of captures
- Static exchange evaluation (SEE)
- Move generation with magic bitboards
- Time management that adapts based on intermediate search results

**To-do**
- NNUE training with more and better data on bigger networks + king buckets
- Reverse futility pruning, futility pruning and late move pruning
- Multithreading
- ...

# Compilation
The chess engine can be compiled using the Makefile in the root directory. Makefiles to compile the included tools are present in their respective directories. 

| Target          | Output               | Purpose                                                  |
|-----------------|----------------------|----------------------------------------------------------|
| `make`          | `bin/chess`          | Optimised engine                                         |
| `make debug`    | `bin/chess-debug`    | debug info + asserts                                     |
| `make profile`  | `bin/chess-profile`  | For gprof                                                |
| `make stats`    | `bin/chess-stats`    | Prints search statistics                                 |
| `make sanitize` | `bin/chess-asan`     | AddressSanitizer and UndefinedBehaviorSanitizer          |
| `make tsan`     | `bin/chess-tsan`     | ThreadSanitizer                                          |

# Testing
- `make test` runs the unit tests.
- `make test-sanitize` and `make test-tsan` run them under the sanitizers.
- `make test-all` runs everything.

The unit tests are in `tests/unit`.

## Measuring changes in strength
`make sprt` plays the working tree against the last commit with [fastchess](https://github.com/Disservin/fastchess) and stops once a sequential probability ratio test (SPRT) can tell whether the change gains strength:

    make sprt FASTCHESS=path/to/fastchess                      # Working tree vs HEAD at 10+0.1.
    make sprt FASTCHESS=path/to/fastchess BASE=a3f6262         # Against another commit.
    make sprt FASTCHESS=path/to/fastchess SPRT_ARGS="--elo0 -5 --elo1 0"

By default it tests whether the change gains at least 5 normalized Elo.

# Tools
In the tools directory, the source code of programs used for testing can be found. This includes programs that:
 - estimate Elo,
 - tune the engine's parameters (SPSA),
 - run a sequential probability ratio test (SPRT),
 - generate data for NNUE training,
 - train a neural network (NNUE) using [Bullet](https://github.com/jw1912/bullet),
 - pit two UCI-compliant programs against each other to determine which is stronger (`vs`), and
 - collect FEN strings of positions where both sides are roughly equal.

Furthermore, the engine can run tests of specific positions with a known best move.

## Estimating Elo
`tools/elo/gauntlet.py` plays the engine against opponents with known ratings using [fastchess](https://github.com/Disservin/fastchess) and estimates its rating with [Ordo](https://github.com/michiguel/Ordo) using the opponents' ratings as anchors. The opponents, the engine's name and options, and the match settings are in a config file (e.g. `tools/elo/configs/gauntlet.toml`). Run with:

    python3 tools/elo/gauntlet.py run CONFIG ENGINE --engines-dir DIR --output-dir DIR [--history FILE] [--fastchess PATH] [--ordo PATH]
    python3 tools/elo/gauntlet.py rate CONFIG [--ordo PATH] games.pgn ... # Estimate from existing games.

## Generating training data
`tools/datagen` generates data for NNUE training via self-play from randomised openings and writes the positions with their scores and the game results in viriformat. Build it with `make` and run with:

    tools/datagen/bin/datagen OUTPUT THREADS TOTAL_GAMES [NODES_PER_MOVE] [SEED]
    python3 tools/datagen/fit_scale.py FILE ... # Fit the scale to use in training and inference
