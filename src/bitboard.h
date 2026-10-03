#ifndef BITBOARD_H_INCLUDED
#define BITBOARD_H_INCLUDED

#include <cassert>
#include <cstdint>
#include <string>

#include "chess.h"

using Bitboard = uint64_t;

constexpr Bitboard BB_EMPTY = 0;
constexpr Bitboard BB_FULL = -1;

constexpr Bitboard FILE_A = 72340172838076673ULL;
constexpr Bitboard FILE_B = FILE_A << 1;
constexpr Bitboard FILE_C = FILE_A << 2;
constexpr Bitboard FILE_D = FILE_A << 3;
constexpr Bitboard FILE_E = FILE_A << 4;
constexpr Bitboard FILE_F = FILE_A << 5;
constexpr Bitboard FILE_G = FILE_A << 6;
constexpr Bitboard FILE_H = FILE_A << 7;

constexpr Bitboard RANK_1 = 255ULL;
constexpr Bitboard RANK_2 = RANK_1 << 8;
constexpr Bitboard RANK_3 = RANK_1 << 16;
constexpr Bitboard RANK_4 = RANK_1 << 24;
constexpr Bitboard RANK_5 = RANK_1 << 32;
constexpr Bitboard RANK_6 = RANK_1 << 40;
constexpr Bitboard RANK_7 = RANK_1 << 48;
constexpr Bitboard RANK_8 = RANK_1 << 56;

constexpr Bitboard LIGHT_SQUARES = 0x55AA55AA55AA55AAULL;
constexpr Bitboard CENTER_SQUARES = ~(FILE_A | FILE_H | RANK_1 | RANK_8);

extern Bitboard king_move_masks[SQ_MAX];
extern Bitboard knight_move_masks[SQ_MAX];
extern Bitboard rook_move_masks[SQ_MAX];
extern Bitboard bishop_move_masks[SQ_MAX];
extern Bitboard pawn_move_masks[COLOUR_MAX][SQ_MAX];
extern Bitboard pawn_capture_masks[COLOUR_MAX][SQ_MAX];

extern Bitboard between_lines[SQ_MAX][SQ_MAX];
extern Bitboard whole_lines[SQ_MAX][SQ_MAX];

constexpr size_t MAGIC_TABLE_SIZE = 1 << 12;

struct MagicEntry
{
    uint64_t mask;
    uint64_t magic;
    uint8_t shift;
    Bitboard moves[MAGIC_TABLE_SIZE];

    constexpr Bitboard get(Bitboard occ)
    {
        return moves[((mask & occ) * magic) >> shift];
    }
};

extern MagicEntry rook_magic[SQ_MAX];
extern MagicEntry bishop_magic[SQ_MAX];

void precompute_bitboards();

constexpr Bitboard bb_set(Square sq)
{
    assert(is_sq(sq));
    return 1ULL << sq;
}

// Returns a shifted version of the bitboard in some direction.
// Bits do not 'wrap'.
constexpr Bitboard bb_shift(Bitboard bb, Direction dir)
{
    return dir == NORTH        ? bb << 8
           : dir == NORTH_EAST ? (bb << 9) & ~FILE_A
           : dir == EAST       ? (bb << 1) & ~FILE_A
           : dir == SOUTH_EAST ? (bb >> 7) & ~FILE_A
           : dir == SOUTH      ? bb >> 8
           : dir == SOUTH_WEST ? (bb >> 9) & ~FILE_H
           : dir == WEST       ? (bb >> 1) & ~FILE_H
           : dir == NORTH_WEST ? (bb << 7) & ~FILE_H
                               : BB_EMPTY;
}

template<Direction Dir>
constexpr Bitboard bb_shift(Bitboard bb)
{
    return Dir == NORTH        ? bb << 8
           : Dir == NORTH_EAST ? (bb << 9) & ~FILE_A
           : Dir == EAST       ? (bb << 1) & ~FILE_A
           : Dir == SOUTH_EAST ? (bb >> 7) & ~FILE_A
           : Dir == SOUTH      ? bb >> 8
           : Dir == SOUTH_WEST ? (bb >> 9) & ~FILE_H
           : Dir == WEST       ? (bb >> 1) & ~FILE_H
           : Dir == NORTH_WEST ? (bb << 7) & ~FILE_H
                               : BB_EMPTY;
}

constexpr bool two_or_more(Bitboard bb)
{
    return bb & (bb - 1);
}

constexpr bool exactly_one(Bitboard bb)
{
    return bb && !two_or_more(bb);
}

// Returns moves in the form of a bitboard according to the given
// occupancy bitboard. The piece can capture occupied squares but
// sliders cannot slide past them.
template<PieceType P>
constexpr Bitboard piece_moves(Square from, Bitboard occ)
{
    static_assert(is_piece_type(P) && P != PAWN);
    assert(is_sq(from));
    return P == KING     ? king_move_masks[from]
           : P == ROOK   ? rook_magic[from].get(occ)
           : P == KNIGHT ? knight_move_masks[from]
           : P == BISHOP ? bishop_magic[from].get(occ)
           : P == QUEEN  ? bishop_magic[from].get(occ) | rook_magic[from].get(occ)
                         : BB_EMPTY;
}

template<PieceType P>
constexpr Bitboard piece_move_mask(Square from)
{
    static_assert(is_piece_type(P) && P != PAWN);
    assert(is_sq(from));
    return P == KING     ? king_move_masks[from]
           : P == ROOK   ? rook_move_masks[from]
           : P == KNIGHT ? knight_move_masks[from]
           : P == BISHOP ? bishop_move_masks[from]
           : P == QUEEN  ? rook_move_masks[from] | bishop_move_masks[from]
                         : BB_EMPTY;
}

template<Colour C>
constexpr Bitboard pawn_move_mask_bb(Bitboard bb)
{
    return bb_shift<forward(C)>(bb);
}

constexpr Bitboard pawn_move_mask_bb(Colour c, Bitboard bb)
{
    return c == WHITE ? bb_shift<NORTH>(bb)
                      : bb_shift<SOUTH>(bb);
}

template<Colour C>
constexpr Bitboard pawn_move_mask_sq(Square sq)
{
    return pawn_move_mask_bb<C>(bb_set(sq));
}

inline Bitboard pawn_move_mask(Colour c, Square sq)
{
    return pawn_move_masks[c][sq];
}

template<Colour C>
constexpr Bitboard pawn_capture_mask_bb(Bitboard bb)
{
    return C == WHITE ? bb_shift<NORTH_EAST>(bb) | bb_shift<NORTH_WEST>(bb)
                      : bb_shift<SOUTH_EAST>(bb) | bb_shift<SOUTH_WEST>(bb);
}

constexpr Bitboard pawn_capture_mask_bb(Colour c, Bitboard bb)
{
    return c == WHITE ? bb_shift<NORTH_EAST>(bb) | bb_shift<NORTH_WEST>(bb)
                      : bb_shift<SOUTH_EAST>(bb) | bb_shift<SOUTH_WEST>(bb);
}

template<Colour C>
constexpr Bitboard pawn_capture_mask_sq(Square sq)
{
    return pawn_capture_mask_bb<C>(bb_set(sq));
}

inline Bitboard pawn_capture_mask_sq(Colour c, Square sq)
{
    return pawn_capture_masks[c][sq];
}

constexpr Bitboard north_fill(Bitboard bb)
{
    bb |= (bb << 8);
    bb |= (bb << 16);
    bb |= (bb << 32);
    return bb;
}

constexpr Bitboard south_fill(Bitboard bb)
{
    bb |= (bb >> 8);
    bb |= (bb >> 16);
    bb |= (bb >> 32);
    return bb;
}

template<Colour C>
constexpr Bitboard front_fill(Bitboard bb)
{
    return C == WHITE ? north_fill(bb) : south_fill(bb);
}

template<Colour C>
constexpr Bitboard rear_fill(Bitboard bb)
{
    return C == WHITE ? south_fill(bb) : north_fill(bb);
}

constexpr Bitboard bb_file(int file)
{
    return FILE_A << file;
}

constexpr Bitboard bb_adjacent_files(int file)
{
    return bb_shift<EAST>(bb_file(file)) | bb_shift<WEST>(bb_file(file));
}

// Returns a bitboard with the bits corresponding to the squares
// between the two given squares set. The two given squares are
// not in the line. The empty bitboard is returned if there is
// no horizontal, vertical, or diagonal line between the two squares.
inline Bitboard line_between(Square from, Square to)
{
    assert(is_sq(from) && is_sq(to));
    return between_lines[from][to];
}

// Same as line_between() but it returns the entire line from border
// to border.
inline Bitboard whole_line(Square from, Square to)
{
    assert(is_sq(from) && is_sq(to));
    return whole_lines[from][to];
}

std::string bb_as_image_str(Bitboard bb);

constexpr int pop_count(Bitboard bb)
{
    return __builtin_popcountll(bb);
}

constexpr Square lsb_idx(Bitboard bb)
{
    return __builtin_ctzll(bb);
}

constexpr Square msb_idx(Bitboard bb)
{
    return 63 - __builtin_clzll(bb);
}

constexpr Square pop_lsb(Bitboard& bb)
{
    assert(bb != 0);
    Square sq = lsb_idx(bb);
    bb &= bb - 1;
    return sq;
}

#endif