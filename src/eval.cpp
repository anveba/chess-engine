#include "eval.h"

#include <algorithm>
#include <iomanip>
#include <iterator>
#include <sstream>

BoardEval evaluate(const Board& board)
{
    constexpr int32_t MAX_NNUE_EVAL = MATE_EVAL - 512;
    int32_t eval = board.get_nnue_accumulator().evaluate(get_nnue(), board);
    return eval > MAX_NNUE_EVAL ? MAX_NNUE_EVAL : (eval < -MAX_NNUE_EVAL ? -MAX_NNUE_EVAL : eval);
}

std::string eval_trace(const Board& board)
{
    std::ostringstream out;
    out << "NNUE evaluation (side to move): " << evaluate(board) << "\n";
    return out.str();
}
