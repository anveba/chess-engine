# General
A chess engine compliant with UCI. 

**Features**:
- Alpha-beta pruning, quiescience search, move ordering, iterative deepening, etc.
- Transposition table
- Fast move generation (via magic bitboards)
- Tools for testing
- And many search heuristics

**To-do**:
- Multithreading
- Currently has a weak static board evaluation

# Compilation
The chess engine can be compiled using the Makefile in the root directory. Makefiles to compile the included tools are present in their respective directories. 

# Tools
In the tools directory, the source code of programs used for testing can be found. This includes
 - a program to pit two UCI-compliant programs against each other to determine which is stronger,
 - and a program to collect FEN strings of positions where both sides are roughly equal.

Furthermore, the engine can run tests of specific positions with a known best move.
