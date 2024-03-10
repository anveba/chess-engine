#ifndef PERFT_H_INCLUDED
#define PERFT_H_INCLUDED

#include <cstdint>

#include "board.h"
#include "movegen.h"

struct PerftResults
{
    PerftResults()
        : nodes(0)
        , seconds(0.0f)
    {
    }
    uint64_t nodes;
    double seconds;
};

PerftResults perft(Board& board, int depth);

#endif