#include "moveorder.h"

#include <algorithm>
#include <cassert>

constexpr MoveEval PAWN_ORDERING_VALUE = 100;
TUNABLE(KNIGHT_ORDERING_VALUE, 311, 200, 500);
TUNABLE(BISHOP_ORDERING_VALUE, 328, 200, 500);
TUNABLE(ROOK_ORDERING_VALUE, 506, 350, 800);
TUNABLE(QUEEN_ORDERING_VALUE, 884, 700, 1400);
constexpr MoveEval KING_ORDERING_VALUE = 10000;

TUNABLE(ATTACKER_VALUE_DIVISOR, 10, 2, 50);

constexpr PieceType PIECE_BY_VALUE[] = { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };

static MoveEval ordering_value(PieceType p)
{
    switch (p) {
        case PAWN:
            return PAWN_ORDERING_VALUE;
        case KNIGHT:
            return KNIGHT_ORDERING_VALUE;
        case BISHOP:
            return BISHOP_ORDERING_VALUE;
        case ROOK:
            return ROOK_ORDERING_VALUE;
        case QUEEN:
            return QUEEN_ORDERING_VALUE;
        case KING:
            return KING_ORDERING_VALUE;
        default:
            return 0;
    }
}

static Bitboard enemy_pawn_attacks(const Board& board)
{
    return pawn_capture_mask_bb(~board.side(), board.occ(PAWN * ~board.side()));
}

static MoveEval pawn_threat_penalty(const Board& board, Move move, Bitboard pawn_threats)
{
    return (bb_set(move.to_sq()) & pawn_threats) ? ordering_value(type_of(board.at(move.from_sq()))) : 0;
}

static MoveEval material_gain(const Board& board, Move move)
{
    // TODO check if checkers are being captured

    const Piece to_piece = board.at(move.to_sq());

    // The king's value is capped so that its captures are not considered bad.
    MoveEval gain = 0;
    const MoveEval attacker_value = std::min(ordering_value(type_of(board.at(move.from_sq()))), ordering_value(QUEEN));
    if (is_piece(to_piece))
        gain += ordering_value(type_of(to_piece)) - attacker_value / ATTACKER_VALUE_DIVISOR;
    else if (move.is_ep())
        gain += ordering_value(PAWN) - ordering_value(PAWN) / ATTACKER_VALUE_DIVISOR;

    if (move.is_promotion())
        gain += ordering_value(move.promotion_to()) - ordering_value(PAWN);

    return gain;
}

MoveEval estimate(const Board& board, Move move)
{
    assert(move.is_proper());

    if (move.is_castle())
        return 0;
    return material_gain(board, move) - pawn_threat_penalty(board, move, enemy_pawn_attacks(board));
}

static Bitboard attackers_to(const Board& board, Square sq, Bitboard occ)
{
    const Bitboard queens = board.occ(W_QUEEN) | board.occ(B_QUEEN);
    return ((piece_moves<ROOK>(sq, occ) & (board.occ(W_ROOK) | board.occ(B_ROOK) | queens)) |
            (piece_moves<BISHOP>(sq, occ) & (board.occ(W_BISHOP) | board.occ(B_BISHOP) | queens)) |
            (piece_moves<KNIGHT>(sq, occ) & (board.occ(W_KNIGHT) | board.occ(B_KNIGHT))) |
            (piece_moves<KING>(sq, occ) & (board.occ(W_KING) | board.occ(B_KING))) |
            (pawn_capture_mask_sq(WHITE, sq) & board.occ(B_PAWN)) |
            (pawn_capture_mask_sq(BLACK, sq) & board.occ(W_PAWN))) &
           occ;
}

static Bitboard xray_attackers(const Board& board, Square sq, Bitboard occ, PieceType p)
{
    const Bitboard queens = board.occ(W_QUEEN) | board.occ(B_QUEEN);
    Bitboard revealed = BB_EMPTY;
    if (p == PAWN || p == BISHOP || p == QUEEN)
        revealed |= piece_moves<BISHOP>(sq, occ) & (board.occ(W_BISHOP) | board.occ(B_BISHOP) | queens);
    if (p == ROOK || p == QUEEN)
        revealed |= piece_moves<ROOK>(sq, occ) & (board.occ(W_ROOK) | board.occ(B_ROOK) | queens);
    return revealed & occ;
}

static Square least_valuable_attacker(const Board& board, Bitboard attackers, Colour side, PieceType& type)
{
    for (PieceType p : PIECE_BY_VALUE) {
        if (const Bitboard bb = attackers & board.occ(side * p)) {
            type = p;
            return lsb_idx(bb);
        }
    }
    assert(false);
    return SQ_NONE;
}

static MoveEval first_capture(const Board& board, Move move, Bitboard& occ, PieceType& moved)
{
    occ = board.occ() ^ bb_set(move.from_sq());
    moved = type_of(board.at(move.from_sq()));

    MoveEval gain = 0;
    if (move.is_ep()) {
        gain = ordering_value(PAWN);
        occ ^= bb_set(sq_move(move.to_sq(), board.side() == WHITE ? SOUTH : NORTH));
    } else if (!move.is_castle()) {
        gain = ordering_value(type_of(board.at(move.to_sq())));
    }

    if (move.is_promotion()) {
        gain += ordering_value(move.promotion_to()) - ordering_value(PAWN);
        moved = move.promotion_to();
    }
    return gain;
}

bool see_threshold(const Board& board, Move move, MoveEval threshold)
{
    assert(move.is_proper());

    if (move.is_castle())
        return 0 >= threshold;

    const Square to = move.to_sq();
    Bitboard occ;
    PieceType attacking_piece;

    MoveEval diff = first_capture(board, move, occ, attacking_piece) - threshold;
    if (diff < 0)
        return false;

    diff = ordering_value(attacking_piece) - diff;
    if (diff <= 0)
        return true;

    Bitboard attackers = attackers_to(board, to, occ);
    Colour side = board.side();

    while (true) {
        side = ~side;
        attackers &= occ;

        const Bitboard own_attackers = attackers & board.occ(side);
        if (!own_attackers)
            return side != board.side();

        // Now we check if this side can capture without losing material.
        const Square from = least_valuable_attacker(board, own_attackers, side, attacking_piece);

        // The king will be the last attacker.
        if (attacking_piece == KING) // If the king has attackers at this point, all is lost
            return (attackers & board.occ(~side)) != (side == board.side());

        diff = -diff + ordering_value(attacking_piece);
        if (diff < MoveEval(side == board.side())) // trick because ties go to the original side to move
            break;

        occ ^= bb_set(from);
        attackers |= xray_attackers(board, to, occ, attacking_piece);
    }

    return side == board.side();
}

MovePicker::MovePicker(const Board& board, MoveList& list, Move first, const Move* killers, const MoveEval* side_history)
    : board(board)
    , killers(killers)
    , side_history(side_history)
    , moves(list.begin())
    , size(list.size())
    , next_loud(0)
    , stage(PARTITION)
    , killer_index(0)
{
    Move* found = first.is_proper() ? std::find(moves, moves + size, first) : moves + size;
    if (found != moves + size) {
        std::swap(moves[0], *found);
        next_loud = 1;
        stage = FIRST;
    }
}

// ordering from https://chessprogramming.org/Move_Ordering
Move MovePicker::next()
{
    switch (stage) {
        case FIRST:
            stage = PARTITION;
            return moves[0];

        case PARTITION:
            partition_by_loudness();
            stage = GOOD_LOUD;

        case GOOD_LOUD:
            while (next_loud < good_loud_end) {
                const int best = best_in(next_loud, good_loud_end);
                // Is it actually a good capture?
                if (see_threshold(board, moves[best], 0))
                    return consume(next_loud, best);
                // A bad capture, move it to the bad loud moves.
                good_loud_end--;
                std::swap(moves[best], moves[good_loud_end]);
                std::swap(scores[best], scores[good_loud_end]);
            }
            stage = KILLERS;

        case KILLERS:
            // Only quiet moves are searched, since a killer may be a capture here.
            while (killers && killer_index < KILLER_SLOTS) {
                const Move* found = std::find(moves + next_quiet, moves + size, killers[killer_index++]);
                if (found != moves + size)
                    return consume(next_quiet, found - moves);
            }
            stage = SCORE_QUIETS;

        case SCORE_QUIETS:
            score_quiets();
            stage = QUIETS;

        case QUIETS:
            if (next_quiet < size)
                return consume(next_quiet, best_in(next_quiet, size));
            stage = BAD_LOUD;

        case BAD_LOUD:
            if (next_loud < loud_end)
                return consume(next_loud, best_in(next_loud, loud_end));
            stage = DONE;

        case DONE:
            return Move::make_none();
    }
    return Move::make_none();
}

// Moves the loud moves in front of the quiet ones and scores them.
void MovePicker::partition_by_loudness()
{
    pawn_threats = enemy_pawn_attacks(board);

    loud_end = next_loud;
    for (int i = next_loud; i < size; i++) {
        if (board.is_loud(moves[i])) {
            std::swap(moves[i], moves[loud_end]);
            scores[loud_end] = material_gain(board, moves[loud_end]);
            loud_end++;
        }
    }
    good_loud_end = loud_end;
    next_quiet = loud_end;
}

void MovePicker::score_quiets()
{
    for (int i = next_quiet; i < size; i++) {
        scores[i] = side_history ? side_history[moves[i].from_to_index()] : 0;
        if (!moves[i].is_castle())
            scores[i] -= pawn_threat_penalty(board, moves[i], pawn_threats);
    }
}

int MovePicker::best_in(int begin, int end) const
{
    int best = begin;
    for (int i = begin + 1; i < end; i++) {
        if (scores[i] > scores[best])
            best = i;
    }
    return best;
}

// Swaps the move at index to the front of its region and picks it.
Move MovePicker::consume(int& next, int index)
{
    std::swap(moves[next], moves[index]);
    std::swap(scores[next], scores[index]);
    return moves[next++];
}
