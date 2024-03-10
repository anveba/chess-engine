#ifndef MOVEGEN_H_INCLUDED
#define MOVEGEN_H_INCLUDED

#include "board.h"

constexpr int MAX_MOVES = 218;

enum MovegenMode
{
    ALL_LEGAL_MOVES,
    LOUD_MOVES,
    PSEUDO_MOVES
};

class MoveList
{
  public:
    inline MoveList() {}

    template<MovegenMode Mode>
    void generate(const Board& board);

    MoveList(const MoveList&) = delete;
    MoveList& operator=(const MoveList&) = delete;

    constexpr Move* begin() { return moves; }
    constexpr const Move* begin() const { return moves; }
    constexpr Move* end() { return top; }
    constexpr const Move* end() const { return top; }

    constexpr Move at(int i) { return moves[i]; }

    constexpr int size() const { return top - &moves[0]; }

    void filter_quiet(const Board& board);

  private:
    Move moves[MAX_MOVES];
    Move* top;
};

#endif