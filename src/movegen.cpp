#include "movegen.h"

#include "bitboard.h"

template<Colour Side>
constexpr bool can_move_wrt_pin(const Board& board, Square from, Square to)
{
    // We check if the piece is pinned and if it is, we check if it moves along the threat direction.
    return !(board.pinned() & bb_set(from)) ||
           (whole_line(lsb_idx(board.occ(Side * KING)), from) & bb_set(to));
}

template<MovegenMode Mode, Colour Side, PieceType P>
static Move* generate_piecewise(const Board& board, Move* moves, Bitboard check_mask)
{
    static_assert(P != KING && P != PAWN);

    Bitboard pieces = board.occ(Side * P);

    while (pieces) {
        Square from = pop_lsb(pieces);

        Bitboard moves_bb = piece_moves<P>(from, board.occ());
        moves_bb &= ~board.occ(Side);
        moves_bb &= check_mask;

        if (Mode == LOUD_MOVES)
            moves_bb &= board.occ();

        while (moves_bb) {
            Square to = pop_lsb(moves_bb);
            if (Mode == PSEUDO_MOVES || can_move_wrt_pin<Side>(board, from, to))
                *moves++ = Move::make_normal(from, to);
        }
    }
    return moves;
}

template<MovegenMode Mode, Colour Side, CastlingRights Castle>
static Move* generate_castle(const Board& board, Move* moves, Square king_sq)
{
    static_assert(Castle == CASTLING_QUEENSIDE || Castle == CASTLING_KINGSIDE);
    static_assert(is_single(Side & Castle));
    assert(!board.checkers());

    constexpr Square king_pass_through[2] = { square_wrt(Side, Castle == CASTLING_QUEENSIDE ? SQ_C1 : SQ_F1),
                                              square_wrt(Side, Castle == CASTLING_QUEENSIDE ? SQ_D1 : SQ_G1) };

    Square castle_to = rook_castle_from(Side & Castle);
    if (board.has_castling_right(Side & Castle) &&
        !(line_between(king_sq, castle_to) & board.occ()) &&
        (Mode == PSEUDO_MOVES ||
         (!board.threats_to<Side>(king_pass_through[0], board.occ()) &&
          !board.threats_to<Side>(king_pass_through[1], board.occ())))) {

        *moves++ = Move::make_castle(king_sq, castle_to);
    }

    return moves;
}

template<MovegenMode Mode, Colour Side>
static Move* generate_king(const Board& board, Move* moves)
{
    Bitboard king_sq = lsb_idx(board.occ(Side * KING));

    Bitboard moves_bb = piece_moves<KING>(king_sq, board.occ());
    moves_bb &= ~board.occ(Side);

    if (Mode == LOUD_MOVES)
        moves_bb &= board.occ();

    while (moves_bb) {
        Square to = pop_lsb(moves_bb);

        // We check if the square moved to has a threat.
        if (Mode == PSEUDO_MOVES || !board.threats_to<Side>(to, board.occ() ^ board.occ(KING * Side)))
            *moves++ = Move::make_normal(king_sq, to);
    }

    if (Mode != LOUD_MOVES && !board.checkers()) {
        moves = generate_castle<Mode, Side, CASTLING_KINGSIDE>(board, moves, king_sq);
        moves = generate_castle<Mode, Side, CASTLING_QUEENSIDE>(board, moves, king_sq);
    }

    return moves;
}

template<MovegenMode Mode, Colour Side>
static Move* generate_pawn(const Board& board, Move* moves, Bitboard check_mask)
{
    Bitboard pieces = board.occ(Side * PAWN);

    while (pieces) {
        Square from = pop_lsb(pieces);
        Bitboard from_bb = bb_set(from);

        Bitboard capture_mask = pawn_capture_mask_bb<Side>(from_bb);

        if (board.ep_rights() != NO_EP_RIGHTS) {
            Bitboard ep_file = FILE_A << file_of(board.ep_rights());
            Bitboard to_bb = (Side == WHITE ? RANK_6 : RANK_3) & ep_file;

            if (Mode == PSEUDO_MOVES) {

                if (capture_mask & to_bb & check_mask)
                    *moves++ = Move::make_ep(from, lsb_idx(to_bb));

            } else {
                constexpr Bitboard pawn_rank = Side == WHITE ? RANK_5 : RANK_4;
                Bitboard captured_pawn = pawn_rank & ep_file;
                constexpr Direction push_dir = Side == WHITE ? NORTH : SOUTH;

                // We have to account for the edge case where the EP move will remove
                // the checker. In this case we allow it, even if it is not part of
                // the allowed check mask.
                assert(!two_or_more(board.checkers()));
                if (capture_mask & to_bb & check_mask ||
                    (bb_shift<push_dir>(captured_pawn & board.checkers()) & capture_mask)) {
                    Square to = lsb_idx(to_bb);

                    if (can_move_wrt_pin<Side>(board, from, to)) {

                        // We check the case where doing an EP move will expose the king to a
                        // slider attack. Note that we in this case can ignore vertical attacks.
                        Square king_sq = lsb_idx(board.occ(KING * Side));
                        Bitboard potential_threat =
                            ((piece_move_mask<ROOK>(king_sq) & (board.occ(ROOK * ~Side) | board.occ(QUEEN * ~Side))) |
                             (piece_move_mask<BISHOP>(king_sq) & (board.occ(BISHOP * ~Side) | board.occ(QUEEN * ~Side)))) &
                            ~(FILE_A << file_of(king_sq));

                        bool ep_discovered_check = false;
                        while (potential_threat) {
                            Bitboard threat_line = line_between(king_sq, pop_lsb(potential_threat));

                            // We check if the king is exposed when the EP move is made.
                            if (!(threat_line & (board.occ() | to_bb) & ~from_bb & ~captured_pawn)) {
                                ep_discovered_check = true;
                                break;
                            }
                        }

                        if (!ep_discovered_check)
                            *moves++ = Move::make_ep(from, to);
                    }
                }
            }
        }

        // Single push
        Bitboard moves_bb = pawn_move_mask_bb<Side>(from_bb) & ~board.occ();

        // Double push
        if (from_bb & (Side == WHITE ? RANK_2 : RANK_7))
            moves_bb |= pawn_move_mask_bb<Side>(moves_bb) & ~board.occ();

        moves_bb |= capture_mask & board.occ(~Side);
        moves_bb &= check_mask;

        Bitboard promotions = moves_bb & (Side == WHITE ? RANK_8 : RANK_1);
        Bitboard normal_moves = moves_bb ^ promotions;

        if (Mode == LOUD_MOVES)
            normal_moves &= board.occ();

        while (normal_moves) {
            Square to = pop_lsb(normal_moves);
            if (Mode == PSEUDO_MOVES || can_move_wrt_pin<Side>(board, from, to))
                *moves++ = Move::make_normal(from, to);
        }

        while (promotions) {
            Square to = pop_lsb(promotions);
            if (Mode == PSEUDO_MOVES || can_move_wrt_pin<Side>(board, from, to)) {
                *moves++ = Move::make_promotion(from, to, ROOK);
                *moves++ = Move::make_promotion(from, to, KNIGHT);
                *moves++ = Move::make_promotion(from, to, BISHOP);
                *moves++ = Move::make_promotion(from, to, QUEEN);
            }
        }
    }
    return moves;
}

template<MovegenMode Mode, Colour Side>
static Move* generate_by_side(const Board& board, Move* moves)
{
    moves = generate_king<Mode, Side>(board, moves);

    Bitboard check_mask = BB_FULL;

    // When we are in check, the moves we can make are limited.
    if (Mode != PSEUDO_MOVES && exactly_one(board.checkers())) {
        check_mask = line_between(lsb_idx(board.occ(Side * KING)), lsb_idx(board.checkers())) |
                     board.checkers();
    }

    // Only king moves are valid in double check
    if (Mode == PSEUDO_MOVES || !two_or_more(board.checkers())) {
        moves = generate_piecewise<Mode, Side, ROOK>(board, moves, check_mask);
        moves = generate_piecewise<Mode, Side, KNIGHT>(board, moves, check_mask);
        moves = generate_piecewise<Mode, Side, BISHOP>(board, moves, check_mask);
        moves = generate_piecewise<Mode, Side, QUEEN>(board, moves, check_mask);
        moves = generate_pawn<Mode, Side>(board, moves, check_mask);
    }

    return moves;
}

template<MovegenMode Mode>
static Move* generate_moves(const Board& board, Move* moves)
{
    return board.side() == WHITE ? generate_by_side<Mode, WHITE>(board, moves)
                                 : generate_by_side<Mode, BLACK>(board, moves);
}

template<MovegenMode Mode>
void MoveList::generate(const Board& board)
{
    top = generate_moves<Mode>(board, moves);
}

template void MoveList::generate<ALL_LEGAL_MOVES>(const Board& board);
template void MoveList::generate<LOUD_MOVES>(const Board& board);
template void MoveList::generate<PSEUDO_MOVES>(const Board& board);

void MoveList::filter_quiet(const Board& board)
{
    for (Move* move = begin(); move != end();) {
        if (!board.is_loud(*move))
            *move = *--top;
        else
            move++;
    }
}
