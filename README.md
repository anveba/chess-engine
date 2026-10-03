# Chrunch
A UCI chess engine written in C++.

Estimated strength: **2673 +/- 12 Elo** on the CCRL Blitz scale after 3200 games (see [Estimating Elo](#estimating-elo)).

**Features**
- Alpha-beta search with principal variation search and iterative deepening
- Null-move pruning, late move reductions, and check extensions
- Move ordering by the transposition table move, static exchange evaluation (SEE) and MVV-LVA for captures, killer moves, and the history heuristic
- Quiescence search, and SEE and delta pruning of captures
- Transposition table
- Move generation with magic bitboards
- Tapered evaluation with piece-square tables, pawn structure, mobility and king safety
- Time management that adapts to the position with pondering
- Tools for testing: unit and UCI protocol tests, SPRT, Elo estimation, a perft speed comparison, a FEN filter and a
  simple match runner

**To-do**
- Reverse futility pruning, futility pruning and late move pruning
- Tuning the evaluation (or NNUE?)
- Multithreading
- ...

# Move generation speed
Perft speed is at **328 Mnps** (bulk counting, single thread, median of 25 runs) on an Intel i7-1260P compared to Stockfish 19's 178 Mnps. As a disclaimer, Stockfish also does work that is useful for search and not raw move generation, so this number only serves as a point of reference. The test positions are from the Chess Programming Wiki's [Perft Results](https://www.chessprogramming.org/Perft_Results) page. Reproduce with

    python3 tools/perft_compare/perft_compare.py --engine name=Stockfish cmd=path/to/stockfish \
        --engine name=Chrunch cmd=bin/chess "perft=perft {depth}" "nodes=Number of nodes" --cpu 2 --runs 25

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

The unit tests are in `tests/unit`, and `./bin/tests <filter>` runs the tests whose names contain the filter. Test positions are in `tests/data`.

## Measuring changes in strength
`make sprt` plays the working tree against the last commit with [fastchess](https://github.com/Disservin/fastchess) and stops once a sequential probability ratio test (SPRT) can tell whether the change gains strength:

    make sprt FASTCHESS=path/to/fastchess                      # Working tree vs HEAD at 10+0.1.
    make sprt FASTCHESS=path/to/fastchess BASE=a3f6262         # Against another commit.
    make sprt FASTCHESS=path/to/fastchess SPRT_ARGS="--elo0 -5 --elo1 0"  # Check that a change loses nothing.

By default it tests whether the change gains at least 5 normalized Elo.

# Tools
In the tools directory, the source code of programs used for testing can be found. This includes programs that:
 - pit two UCI-compliant programs against each other to determine which is stronger (`vs`),
 - collect FEN strings of positions where both sides are roughly equal,
 - estimate Elo,
 - compare move generation speed with other engines or builds, and
 - run a sequential probability ratio test (SPRT).

Furthermore, the engine can run tests of specific positions with a known best move.

## Estimating Elo
`tools/elo/gauntlet.py` plays the engine against opponents with known ratings using [fastchess](https://github.com/Disservin/fastchess) and estimates its rating with [Ordo](https://github.com/michiguel/Ordo) using the opponents' ratings as anchors. The opponents, the engine's name and options, and the match settings are in a config file (e.g. `tools/elo/configs/gauntlet.toml`) and the engine to rate is given on the command line along with the directory that the opponents' paths in the config are relative to:

    python3 tools/elo/gauntlet.py run CONFIG ENGINE --engines-dir DIR --output-dir DIR [--history FILE] [--fastchess PATH] [--ordo PATH]
    python3 tools/elo/gauntlet.py rate CONFIG [--ordo PATH] games.pgn ... # Estimate from existing games.
