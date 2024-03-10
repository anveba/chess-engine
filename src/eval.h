#ifndef EVAL_H_INCLUDED
#define EVAL_H_INCLUDED

#include "board.h"
#include "movegen.h"

using BoardEval = int16_t;

constexpr BoardEval MATE_EVAL = 30000;
constexpr BoardEval INF_EVAL = 32000;

BoardEval evaluate(const Board& board);
BoardEval estimate(const Board& board, Move move);

void sort_moves(const Board& board, Move* start, Move* end);

#endif