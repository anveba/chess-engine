#include "perft.h"

#include <chrono>
#include <iostream>

static uint64_t perft_recursive(Board& board, int depth)
{
    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);

    if (depth == 1)
        return moves.size();

    uint64_t count = 0;

    for (Move& move : moves) {

        BoardMemory memory;
        board.make_move(move, memory);

        count += perft_recursive(board, depth - 1);

        board.unmake_move();
    }

    return count;
}

PerftResults perft(Board& board, int depth)
{
    assert(depth > 0);

    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

    PerftResults res;
    res.nodes = perft_recursive(board, depth);

    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

    res.seconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - begin).count();

    return res;
}