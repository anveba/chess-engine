#include "eval.h"

constexpr BoardEval value_of(PieceType p)
{
    constexpr BoardEval PIECE_VALUES[MAX_PIECE_TYPE] = { 0, 100, 500, 320, 330, 900, 10000 };
    return PIECE_VALUES[p];
}

// Returns +1 for white and -1 for black.
constexpr BoardEval colour_sign(Colour c)
{
    return c * -2 + 1;
}

template<Colour Side>
constexpr BoardEval material_count(const Board& board)
{
    return pop_count(board.occ(Side * PAWN)) * value_of(PAWN) +
           pop_count(board.occ(Side * ROOK)) * value_of(ROOK) +
           pop_count(board.occ(Side * KNIGHT)) * value_of(KNIGHT) +
           pop_count(board.occ(Side * BISHOP)) * value_of(BISHOP) +
           pop_count(board.occ(Side * QUEEN)) * value_of(QUEEN);
}

template<Colour Side>
constexpr BoardEval mobility(const Board& board)
{
    MoveList moves;
    moves.generate<PSEUDO_MOVES>(board);
    return moves.size() * 10;
}

template<Colour Side>
constexpr BoardEval pawn_structure(const Board& board)
{
    // Encourage pawns defending pieces.
    Bitboard pawn_bb = board.occ(PAWN * Side);
    int defended = pop_count(pawn_capture_mask_bb<Side>(pawn_bb) & board.occ(Side));
    return defended * 12;
}

template<Colour Side>
BoardEval evaluate_side(const Board& board)
{
    return material_count<Side>(board) +
           mobility<Side>(board) +
           pawn_structure<Side>(board);
}

BoardEval evaluate(const Board& board)
{
    return (board.side() == WHITE ? 1 : -1) *
           (evaluate_side<WHITE>(board) - evaluate_side<BLACK>(board));
}

BoardEval estimate(const Board& board, Move move)
{
    assert(move.is_proper());

    // TODO check if checkers are being captured

    const Piece from_piece = board.at(move.from_sq());
    const Piece to_piece = board.at(move.to_sq());

    BoardEval eval = 0;

    // MVV-LVA
    if (is_piece(to_piece)) {
        eval += value_of(type_of(to_piece)) - value_of(type_of(from_piece)) / 10;
    } else if (move.is_ep()) {
        eval += value_of(PAWN) - value_of(PAWN) / 10;
    }

    // We check if the piece moves to a square with a pawn threat.
    if (bb_set(move.to_sq()) & pawn_capture_mask_bb(~board.side(), board.occ(PAWN * ~board.side())))
        eval -= value_of(type_of(from_piece));

    if (move.is_promotion())
        eval += value_of(move.promotion_to()) - value_of(PAWN);

    return eval;
}

void sort_moves(const Board& board, Move* start, Move* end)
{
    assert(start != end);

    BoardEval move_values[MAX_MOVES];

    // The assumption is that the move list is not very long, so insertion
    // sort will be the fastest sorting algorithm.

    move_values[0] = estimate(board, *start);
    for (int i = 1; i < end - start; i++) {

        move_values[i] = estimate(board, start[i]);

        BoardEval eval = move_values[i];
        Move move = start[i];

        int j;
        for (j = i - 1; j >= 0 && move_values[j] < eval; j--) {
            move_values[j + 1] = move_values[j];
            start[j + 1] = start[j];
        }

        move_values[j + 1] = eval;
        start[j + 1] = move;
    }
}
