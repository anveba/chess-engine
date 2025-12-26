#ifndef CHESS_H_INCLUDED
#define CHESS_H_INCLUDED

#include <cassert>
#include <cstdint>
#include <string>

enum Colour : uint8_t
{
    WHITE = 0,
    BLACK = 1
};

constexpr int COLOUR_MAX = 2;

constexpr Colour operator~(Colour c)
{
    return Colour(c ^ BLACK);
}

enum Direction : int8_t
{
    NORTH = 8,
    EAST = 1,
    SOUTH = -NORTH,
    WEST = -EAST,
    NORTH_EAST = NORTH + EAST,
    SOUTH_EAST = SOUTH + EAST,
    SOUTH_WEST = SOUTH + WEST,
    NORTH_WEST = NORTH + WEST
};

constexpr Direction operator~(Direction dir)
{
    return Direction(-dir);
}

constexpr Direction forward(Colour c)
{
    return c == WHITE ? NORTH : SOUTH;
}

constexpr uint8_t BOARD_LEN = 8;

using Square = uint8_t;
constexpr Square SQ_ZERO = 0;
constexpr Square SQ_MAX = 64;
constexpr Square SQ_NONE = 255;

// clang-format off
constexpr Square SQ_A1 = 0,  SQ_B1 = 1,  SQ_C1 = 2,  SQ_D1 = 3,  SQ_E1 = 4,  SQ_F1 = 5,  SQ_G1 = 6,  SQ_H1 = 7;
constexpr Square SQ_A2 = 8,  SQ_B2 = 9,  SQ_C2 = 10, SQ_D2 = 11, SQ_E2 = 12, SQ_F2 = 13, SQ_G2 = 14, SQ_H2 = 15;
constexpr Square SQ_A3 = 16, SQ_B3 = 17, SQ_C3 = 18, SQ_D3 = 19, SQ_E3 = 20, SQ_F3 = 21, SQ_G3 = 22, SQ_H3 = 23;
constexpr Square SQ_A4 = 24, SQ_B4 = 25, SQ_C4 = 26, SQ_D4 = 27, SQ_E4 = 28, SQ_F4 = 29, SQ_G4 = 30, SQ_H4 = 31;
constexpr Square SQ_A5 = 32, SQ_B5 = 33, SQ_C5 = 34, SQ_D5 = 35, SQ_E5 = 36, SQ_F5 = 37, SQ_G5 = 38, SQ_H5 = 39;
constexpr Square SQ_A6 = 40, SQ_B6 = 41, SQ_C6 = 42, SQ_D6 = 43, SQ_E6 = 44, SQ_F6 = 45, SQ_G6 = 46, SQ_H6 = 47;
constexpr Square SQ_A7 = 48, SQ_B7 = 49, SQ_C7 = 50, SQ_D7 = 51, SQ_E7 = 52, SQ_F7 = 53, SQ_G7 = 54, SQ_H7 = 55;
constexpr Square SQ_A8 = 56, SQ_B8 = 57, SQ_C8 = 58, SQ_D8 = 59, SQ_E8 = 60, SQ_F8 = 61, SQ_G8 = 62, SQ_H8 = 63;
// clang-format on

constexpr Square sq_move(Square sq, Direction dir)
{
    return sq + dir;
}

constexpr Square sq_move(Square sq, Direction dir, int amount)
{
    return sq + dir * amount;
}

constexpr bool is_sq(Square sq)
{
    return sq < 64;
}

constexpr int rank_of(Square sq)
{
    assert(is_sq(sq));
    return sq >> 3;
}

constexpr int file_of(Square sq)
{
    assert(is_sq(sq));
    return sq & 7;
}

constexpr bool is_file(int file)
{
    return file >= 0 && file < BOARD_LEN;
}

constexpr bool is_rank(int rank)
{
    return rank >= 0 && rank < BOARD_LEN;
}

constexpr Square square_of(int rank, int file)
{
    assert(is_rank(rank) && is_file(file));
    return rank * BOARD_LEN + file;
}

// Returns the relative square. The same square is returned for white, but
// corresponding square on the other side of the board is returned for black.
constexpr Square square_wrt(Colour c, Square sq)
{
    return Square(sq ^ (c * 56));
}

// Returns the relative rank. The same rank is returned for white and the
// 'inverse' rank is returned for black.
constexpr int rank_wrt(Colour c, int rank)
{
    return c == WHITE ? c : (BOARD_LEN - rank - 1);
}

constexpr Direction direction_between(Square from, Square to)
{
    Direction dir = Direction(0);
    if (rank_of(to) > rank_of(from))
        dir = Direction(dir + NORTH);
    else if (rank_of(to) < rank_of(from))
        dir = Direction(dir + SOUTH);
    if (file_of(to) > file_of(from))
        dir = Direction(dir + EAST);
    else if (file_of(to) < file_of(from))
        dir = Direction(dir + WEST);
    return dir;
}

enum PieceType : uint8_t
{
    NO_PIECE_TYPE = 0,
    PAWN = 1,
    ROOK = 2,
    KNIGHT = 3,
    BISHOP = 4,
    QUEEN = 5,
    KING = 6
};

constexpr uint8_t MAX_PIECE_TYPE = KING + 1;

enum Piece : uint8_t
{
    NO_PIECE = 0,

    W_PAWN = PAWN,
    W_ROOK = ROOK,
    W_KNIGHT = KNIGHT,
    W_BISHOP = BISHOP,
    W_QUEEN = QUEEN,
    W_KING = KING,

    B_PAWN = PAWN + 8,
    B_ROOK = ROOK + 8,
    B_KNIGHT = KNIGHT + 8,
    B_BISHOP = BISHOP + 8,
    B_QUEEN = QUEEN + 8,
    B_KING = KING + 8
};

constexpr uint8_t MAX_PIECE = B_KING + 1;

constexpr PieceType type_of(Piece p)
{
    return PieceType(p & 7);
}

constexpr Colour colour_of(Piece p)
{
    return Colour(p >> 3);
}

constexpr bool is_piece_type(PieceType p)
{
    return p != NO_PIECE_TYPE && p < MAX_PIECE_TYPE;
}

constexpr bool is_promotable_to(PieceType p)
{
    return p - ROOK <= QUEEN;
}

constexpr bool is_piece(Piece p)
{
    return is_piece_type(type_of(p));
}

constexpr bool is_slider(PieceType p)
{
    return p == ROOK || p == BISHOP || p == QUEEN;
}

constexpr Piece operator*(Colour c, PieceType p)
{
    return Piece((c << 3) | p);
}

constexpr Piece operator*(PieceType p, Colour c)
{
    return c * p;
}

Piece fen_code_to_piece(char code);
char to_fen_code(Piece p);

enum MoveType
{
    NORMAL_MOVE = 0,
    CASTLING_MOVE = 1 << 12,
    PROMOTION_MOVE = 2 << 12,
    EP_MOVE = 3 << 12
};

class Board;

class Move
{
  public:
    inline Move() {}

    constexpr static Move make_normal(Square from, Square to)
    {
        assert(is_sq(from) && is_sq(to));
        return Move(from | (to << 6));
    }

    constexpr static Move make_null() { return Move(63 | (63 << 6)); }
    constexpr static Move make_none() { return Move(1 | (1 << 6)); }

    constexpr static Move make_castle(Square from, Square to)
    {
        assert(is_sq(from) && is_sq(to));
        return Move(from | (to << 6) | CASTLING_MOVE);
    }

    constexpr static Move make_promotion(Square from, Square to, PieceType promotion_to)
    {
        assert(is_sq(from) && is_sq(to) && is_promotable_to(promotion_to));
        return Move(from | (to << 6) | PROMOTION_MOVE | ((promotion_to - ROOK) << 14));
    }

    constexpr static Move make_ep(Square from, Square to)
    {
        assert(is_sq(from) && is_sq(to));
        return Move(from | (to << 6) | EP_MOVE);
    }

    static Move from_alg_notation(const Board& board, const std::string& notation);
    static Move from_uci_notation(const Board& board, const std::string& notation);

    constexpr Square from_sq() const { return value & 63; }
    constexpr Square to_sq() const { return (value & (63 << 6)) >> 6; }

    constexpr MoveType type() const { return MoveType(value & EP_MOVE); }
    constexpr bool is_normal() const { return type() == NORMAL_MOVE; }
    constexpr bool is_ep() const { return type() == EP_MOVE; }
    constexpr bool is_castle() const { return type() == CASTLING_MOVE; }
    constexpr bool is_promotion() const { return type() == PROMOTION_MOVE; }
    constexpr PieceType promotion_to() const
    {
        assert(is_promotion());
        return PieceType((value >> 14) + ROOK);
    }

    constexpr bool is_proper() const { return from_sq() != to_sq(); }
    constexpr bool is_null() const { return value == (63 | (63 << 6)); }
    constexpr bool is_none() const { return value == (1 | (1 << 6)); }
    bool is_legal(const Board& board) const;

    constexpr uint16_t encoding() const { return value; }

    std::string uci_notation() const;

  private:
    constexpr Move(uint16_t value)
        : value(value)
    {
    }

    // Bits 0..5 indicate the square the piece is moved from.
    // Bits 6..11 indicate the square the piece is moved to.
    // Bits 11..13 indicate the type of move (normal, en passant, castle, promotion).
    // Bits 14..15 indicate which piece is promoted to.
    // A castling move has the king moving to the rook.
    uint16_t value;
};

constexpr bool operator==(Move m1, Move m2)
{
    return m1.encoding() == m2.encoding();
}

// clang-format off
enum CastlingRights : uint8_t
{
    NO_CASTLING_RIGHTS = 0,

    CASTLING_W_KINGSIDE = 1 << 0, CASTLING_W_QUEENSIDE = 1 << 1,
    CASTLING_B_KINGSIDE = 1 << 2, CASTLING_B_QUEENSIDE = 1 << 3,

    CASTLING_W = CASTLING_W_KINGSIDE | CASTLING_W_QUEENSIDE,
    CASTLING_B = CASTLING_B_KINGSIDE | CASTLING_B_QUEENSIDE,
    CASTLING_KINGSIDE = CASTLING_W_KINGSIDE | CASTLING_B_KINGSIDE,
    CASTLING_QUEENSIDE = CASTLING_W_QUEENSIDE | CASTLING_B_QUEENSIDE,

    CASTLING_ALL = CASTLING_B | CASTLING_W
};
// clang-format on

constexpr uint8_t MAX_CASTLE = CASTLING_ALL + 1;

constexpr CastlingRights operator+(CastlingRights r1, CastlingRights r2)
{
    return CastlingRights(r1 | r2);
}

constexpr CastlingRights& operator+=(CastlingRights& r1, CastlingRights r2)
{
    return r1 = r1 + r2;
}

constexpr CastlingRights operator-(CastlingRights r1, CastlingRights r2)
{
    return CastlingRights(r1 & ~r2);
}

constexpr CastlingRights& operator-=(CastlingRights& r1, CastlingRights r2)
{
    return r1 = r1 - r2;
}

constexpr CastlingRights operator&(Colour s, CastlingRights cr)
{
    return CastlingRights(cr & (s == WHITE ? CASTLING_W : CASTLING_B));
}

constexpr CastlingRights operator&(CastlingRights cr, Colour s)
{
    return s & cr;
}

constexpr CastlingRights operator&(CastlingRights cr1, CastlingRights cr2)
{
    return CastlingRights(uint8_t(cr1) & uint8_t(cr2));
}

constexpr bool is_single(CastlingRights cr)
{
    return cr && !(cr & (cr - 1));
}

constexpr Square rook_castle_from(CastlingRights cr)
{
    assert(is_single(cr));
    return cr == CASTLING_W_KINGSIDE    ? SQ_H1
           : cr == CASTLING_W_QUEENSIDE ? SQ_A1
           : cr == CASTLING_B_KINGSIDE  ? SQ_H8
           : cr == CASTLING_B_QUEENSIDE ? SQ_A8
                                        : SQ_NONE;
}

// clang-format off
enum EPRights : uint8_t {
    NO_EP_RIGHTS = 8,

    EP_FILE_A = 0, EP_FILE_B = 1, EP_FILE_C = 2, EP_FILE_D = 3,
    EP_FILE_E = 4, EP_FILE_F = 5, EP_FILE_G = 6, EP_FILE_H = 7
};
// clang-format on

constexpr uint8_t MAX_EP = NO_EP_RIGHTS + 1;

constexpr EPRights file_to_ep(int file)
{
    assert(file < BOARD_LEN);
    return EPRights(file);
}

constexpr int file_of(EPRights ep)
{
    assert(ep < 8);
    return ep;
}

#endif