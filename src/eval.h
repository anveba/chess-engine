#ifndef EVAL_H_INCLUDED
#define EVAL_H_INCLUDED

#include "board.h"
#include "movegen.h"
#include "nnue.h"

#define TRADITIONAL_EVAL 0

using BoardEval = int16_t;

constexpr BoardEval MATE_EVAL = 30000;
constexpr BoardEval INF_EVAL = 32000;

BoardEval evaluate(const Board& board);
std::string eval_trace(const Board& board); // Per-term breakdown of evaluate().

#endif