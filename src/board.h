#ifndef BOARD_H_INCLUDED
#define BOARD_H_INCLUDED

#include <string>

#include "bitboard.h"
#include "chess.h"
#include "nnue.h"

#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

using BoardHash = uint64_t;

void precompute_board_constants();

class BoardMemory
{
  public:
    BoardMemory() {}

  private:
    friend class Board;

    void operator>>(BoardMemory& to);
    std::string move_history_str();
    void make_accumulator_valid() const;

    Piece captured;
    Move move;

    CastlingRights castling_rights;
    EPRights ep_rights;
    uint32_t fifty_move_counter;
    BoardHash hash;

    // These variables are with respect to the current side to move
    Bitboard pinned;
    Bitboard checkers;

    BoardMemory* previous;

    UpdatedPiece updated_pieces[NNUE_MAX_UPDATED_PIECE];

    mutable bool acc_is_valid;
    mutable NNUEAccumulatorPair nnue_acc;
};

class Board
{
  public:
    Board();
    Board(const Board&) = delete;
    Board& operator=(const Board&) = delete;

    constexpr Piece at(Square sq) const { return board[sq]; }
    constexpr Colour side() const { return side_to_move; }
    constexpr uint32_t fifty_move_counter() const { return head->fifty_move_counter; }
    constexpr bool is_draw_by_fifty_move() const { return head->fifty_move_counter >= 100; }
    constexpr uint32_t current_fullmove() const { return fullmove_counter; }
    constexpr uint32_t current_halfmove() const { return (fullmove_counter - 1) * 2 + (side() == WHITE ? 0 : 1); }
    constexpr BoardHash hash() const { return head->hash; }
    BoardHash make_full_hash() const;

    constexpr CastlingRights castling_rights() const { return head->castling_rights; }
    constexpr EPRights ep_rights() const { return head->ep_rights; }
    constexpr bool has_castling_right(CastlingRights cr) const { return head->castling_rights & cr; }

    void make_move(Move move, BoardMemory& memory);
    void unmake_move();

    void make_null_move(BoardMemory& memory);
    void unmake_null_move();

    inline Bitboard occ() const { return occupancy; }
    inline Bitboard occ(Colour c) const { return colour_occupancy[c]; }
    inline Bitboard occ(Piece p) const { return piece_occupancy[p]; }

    constexpr Bitboard pinned() const { return head->pinned; }
    constexpr Bitboard checkers() const { return head->checkers; }

    bool is_insufficient_material() const;
    bool treat_as_draw_by_repetition(int root_dist) const;
    bool is_draw_by_repetition() const;
    uint32_t repetitions() const;

    inline bool is_loud(Move move) const;
    constexpr bool previous_was_null_move() const { return head->move.is_null(); }
    bool is_valid() const;

    template<Colour Side>
    constexpr Bitboard threats_to(Square sq, Bitboard occup) const;
    template<bool IncludeKingThreats>
    inline Bitboard threats_to(Colour side, Square sq, Bitboard occup) const;

    std::string fen() const;
    std::string mirrored_fen() const;
    bool set_fen(const std::string& fen); // False for an invalid FEN
    std::string as_image_str() const;
    std::string move_history_str() const { return head->move_history_str(); }

    const NNUEAccumulatorPair& get_nnue_accumulator() const;

  private:
    void clear();
    inline Piece& at(Square sq) { return board[sq]; }
    Piece move_piece(Square from, Square to);
    void unmove_piece(Square from, Square to, Piece captured);

    void place_piece(Square sq, Piece piece);
    Piece remove_piece(Square sq);

    void recalculate_transients() const;
    bool has_legal_ep_capture() const;

    Piece board[SQ_MAX];

    Colour side_to_move;
    uint32_t fullmove_counter;

    Bitboard occupancy;
    Bitboard colour_occupancy[COLOUR_MAX];
    Bitboard piece_occupancy[MAX_PIECE];

    BoardMemory root, *head;
};

inline bool Board::is_loud(Move move) const
{
    return move.is_promotion() || move.is_ep() || (is_piece(at(move.to_sq())) && !move.is_castle());
}

template<Colour Side>
constexpr Bitboard Board::threats_to(Square sq, Bitboard occup) const
{
    return (piece_moves<ROOK>(sq, occup) & (occ(ROOK * ~Side) | occ(QUEEN * ~Side))) |
           (piece_moves<BISHOP>(sq, occup) & (occ(BISHOP * ~Side) | occ(QUEEN * ~Side))) |
           (piece_moves<KNIGHT>(sq, occup) & occ(KNIGHT * ~Side)) |
           (pawn_capture_mask_sq<Side>(sq) & occ(PAWN * ~Side)) |
           (piece_moves<KING>(sq, occup) & occ(KING * ~Side));
}

template<bool IncludeKingThreats>
inline Bitboard Board::threats_to(Colour side, Square sq, Bitboard occup) const
{
    return (piece_moves<ROOK>(sq, occup) & (occ(ROOK * ~side) | occ(QUEEN * ~side))) |
           (piece_moves<BISHOP>(sq, occup) & (occ(BISHOP * ~side) | occ(QUEEN * ~side))) |
           (piece_moves<KNIGHT>(sq, occup) & occ(KNIGHT * ~side)) |
           (pawn_capture_mask_sq(side, sq) & occ(PAWN * ~side)) |
           (IncludeKingThreats ? piece_moves<KING>(sq, occup) & occ(KING * ~side) : BB_EMPTY);
}

#endif