#include "eval.h"

#include "piecetables.h"

constexpr BoardEval PIECE_VALUES[MAX_PIECE_TYPE] = { 0, 100, 500, 320, 330, 900, 10000 };
constexpr BoardEval MOBILITY = 10;

constexpr BoardEval PAWN_DEFEND = 4;
constexpr BoardEval ISOLATED_PAWN = -2;
constexpr BoardEval DOUBLED_PAWN = -7;
constexpr BoardEval BLOCKED_PAWN_ON_RANK[8] = { 0, -2, -3, -5, -7, -10, -13, 0 };
constexpr BoardEval PASSED_PAWN_ON_RANK[8] = { 0, 0, 3, 9, 16, 25, 36, 0 };

constexpr BoardEval BISHOP_PAIR = 20;
constexpr BoardEval BISHOP_PAWN_BLOCKER = -8;

constexpr BoardEval KNIGHT_KING_CLOSENESS[16] = { 28, 26, 22, 19, 16, 13, 10, 8, 6, 5, 4, 3, 2, 1, 0, 0 };

constexpr BoardEval ROOK_KING_CLOSENESS[8] = { 12, 10, 8, 6, 4, 2, 0, 0 };

constexpr BoardEval QUEEN_KING_CLOSENESS[16] = { 28, 26, 22, 19, 16, 13, 10, 8, 6, 5, 4, 3, 2, 1, 0, 0 };

constexpr BoardEval value_of(PieceType p)
{
    return PIECE_VALUES[p];
}

template<Colour Side>
constexpr BoardEval evaluate_rooks(const Board& board)
{
    Bitboard rook_bb = board.occ(Side * ROOK);
    BoardEval eval = pop_count(rook_bb) * value_of(ROOK);
    const Square enemy_king_sq = lsb_idx(board.occ(~Side * KING));

    while (rook_bb) {
        Square sq = square_wrt(Side, pop_lsb(rook_bb));
        eval += ROOK_EVAL_TABLE[sq];

        // Reward closeness to enemy king
        int dist = std::min(std::abs(file_of(enemy_king_sq) - file_of(sq)),
                            std::abs(rank_of(enemy_king_sq) - rank_of(sq)));
        eval += ROOK_KING_CLOSENESS[dist];
    }
    return eval;
}

template<Colour Side>
constexpr BoardEval evaluate_knights(const Board& board)
{
    Bitboard knight_bb = board.occ(Side * KNIGHT);
    const Square enemy_king_sq = lsb_idx(board.occ(~Side * KING));
    BoardEval eval = pop_count(knight_bb) * value_of(KNIGHT);

    while (knight_bb) {

        Square sq = square_wrt(Side, pop_lsb(knight_bb));
        eval += KNIGHT_EVAL_TABLE[sq];

        // Reward closeness to enemy king
        int dist = std::abs(file_of(enemy_king_sq) - file_of(sq)) +
                   std::abs(rank_of(enemy_king_sq) - rank_of(sq));
        eval += KNIGHT_KING_CLOSENESS[dist];
    }
    return eval;
}

template<Colour Side>
constexpr BoardEval evaluate_bishops(const Board& board)
{
    Bitboard bishop_bb = board.occ(Side * BISHOP);
    BoardEval eval = pop_count(bishop_bb) * value_of(BISHOP);

    // Reward having both bishops
    if (pop_count(bishop_bb) >= 2)
        eval += BISHOP_PAIR;

    // Punish having blockers directly next by
    Bitboard neighbour_bb = bb_shift<NORTH_WEST>(bishop_bb) |
                            bb_shift<NORTH_EAST>(bishop_bb) |
                            bb_shift<SOUTH_WEST>(bishop_bb) |
                            bb_shift<SOUTH_EAST>(bishop_bb);
    int pawn_blockers = pop_count(neighbour_bb & (board.occ(Side * PAWN) | board.occ(~Side * PAWN)));
    eval += BISHOP_PAWN_BLOCKER * pawn_blockers;

    while (bishop_bb) {
        Square sq = square_wrt(Side, pop_lsb(bishop_bb));
        eval += BISHOP_EVAL_TABLE[sq];
    }
    return eval;
}

template<Colour Side>
constexpr BoardEval evaluate_queens(const Board& board)
{
    Bitboard queen_bb = board.occ(Side * QUEEN);
    const Square enemy_king_sq = lsb_idx(board.occ(~Side * KING));
    BoardEval eval = pop_count(queen_bb) * value_of(QUEEN);
    while (queen_bb) {
        Square sq = square_wrt(Side, pop_lsb(queen_bb));
        eval += QUEEN_EVAL_TABLE[sq];

        // Reward closeness to enemy king
        int dist = std::min(std::abs(file_of(enemy_king_sq) - file_of(sq)),
                            std::abs(rank_of(enemy_king_sq) - rank_of(sq)));
        eval += QUEEN_KING_CLOSENESS[dist];
    }
    return eval;
}

template<Colour Side>
constexpr BoardEval evaluate_king(const Board& board)
{
    Bitboard king_bb = board.occ(Side * KING);
    assert(pop_count(king_bb) == 1);
    BoardEval eval = KING_EVAL_TABLE[square_wrt(Side, lsb_idx(king_bb))];
    return eval;
}

template<Colour Side>
constexpr BoardEval evaluate_pawns(const Board& board)
{
    const Bitboard pawn_bb = board.occ(Side * PAWN);
    const Bitboard all_opponent_bb = board.occ(~Side);
    const Bitboard opponent_pawn_bb = board.occ(~Side * PAWN);
    BoardEval eval = pop_count(pawn_bb) * value_of(PAWN);

    for (int i = 0; i < 8; i++) {
        Bitboard file_bb = FILE_A << i;
        Bitboard pawns_in_file_bb = pawn_bb & file_bb;
        int num_pawns = pop_count(pawns_in_file_bb);

        while (pawns_in_file_bb) {
            Square sq = square_wrt(Side, pop_lsb(pawns_in_file_bb));
            eval += PAWN_EVAL_TABLE[sq];
        }

        // Reward passed pawns and punish isolated, doubled, and pawns blocked by an opponent
        // piece directly in front.
        if (num_pawns == 0) {
            eval += ISOLATED_PAWN;
        } else if (num_pawns == 1) {
            const int relative_rank = rank_wrt(Side, rank_of(lsb_idx(pawn_bb)));

            Bitboard front_bb = front_fill<Side>(bb_shift<forward(Side)>(pawn_bb));
            front_bb = front_bb | bb_shift<EAST>(front_bb) | bb_shift<WEST>(front_bb);
            const Bitboard pawn_blockers_bb = opponent_pawn_bb & front_bb;
            if (!pawn_blockers_bb)
                eval += PASSED_PAWN_ON_RANK[relative_rank];

            const Bitboard direct_blockers_bb = all_opponent_bb & bb_shift<forward(Side)>(pawn_bb);
            if (direct_blockers_bb)
                eval += BLOCKED_PAWN_ON_RANK[relative_rank];

        } else {
            eval += DOUBLED_PAWN;
        }
    }

    // Encourage pawns defending pieces.
    int defended = pop_count(pawn_capture_mask_bb<Side>(pawn_bb) & board.occ(Side));
    eval += PAWN_DEFEND * defended;

    return eval;
}

template<Colour Side>
constexpr BoardEval mobility(const Board& board)
{
    MoveList moves;
    moves.generate<PSEUDO_MOVES>(board);
    return MOBILITY * moves.size();
}

template<Colour Side>
BoardEval evaluate_side(const Board& board)
{
    return evaluate_pawns<Side>(board) +
           evaluate_rooks<Side>(board) +
           evaluate_knights<Side>(board) +
           evaluate_bishops<Side>(board) +
           evaluate_queens<Side>(board) +
           evaluate_king<Side>(board) +
           mobility<Side>(board);
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
    assert(start <= end);

    if (start == end)
        return;

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
