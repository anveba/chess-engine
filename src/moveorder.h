#ifndef MOVEORDER_H_INCLUDED
#define MOVEORDER_H_INCLUDED

#include "board.h"
#include "movegen.h"
#include "tune.h"

using MoveEval = int32_t;

TUNABLE(MAX_HISTORY, 16018, 4096, 32768);
TUNABLE(MAX_HISTORY_BONUS, 1400, 100, 4000);

MoveEval ordering_value(PieceType p);

// Score from captures and promotions, minus a penalty for moving into a pawn attack.
MoveEval estimate(const Board& board, Move move);

// Static exchange evaluation.
// Does the move win at least threshold?
bool see_threshold(const Board& board, Move move, MoveEval threshold);

constexpr size_t KILLER_SLOTS = 4;

class MovePicker
{
  public:
    MovePicker(const Board& board, MoveList& moves, Move first, const Move* killers = nullptr, const MoveEval* side_history = nullptr);

    // Returns a none move once every move has been picked.
    Move next();

  private:
    enum Stage
    {
        FIRST,
        PARTITION,
        GOOD_LOUD,
        KILLERS,
        SCORE_QUIETS,
        QUIETS,
        BAD_LOUD,
        DONE
    };

    void partition_by_loudness();
    void score_quiets();
    int best_in(int begin, int end) const;
    Move consume(int& next, int index);

    const Board& board;
    const Move* killers;
    const MoveEval* side_history;
    Move* moves;
    int size;

    // loud moves in [next_loud, loud_end)
    // quiet moves in [next_quiet, size).
    // bad loud moves in [good_loud_end, loud_end).
    int next_loud;
    int good_loud_end;
    int loud_end;
    int next_quiet;
    Stage stage;
    size_t killer_index;
    Bitboard pawn_threats;
    MoveEval scores[MAX_MOVES];
};

#endif
