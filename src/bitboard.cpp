#include "bitboard.h"

#include <sstream>

#include "rng.h"

Bitboard king_move_masks[SQ_MAX];
Bitboard knight_move_masks[SQ_MAX];
Bitboard rook_move_masks[SQ_MAX];
Bitboard bishop_move_masks[SQ_MAX];
Bitboard pawn_move_masks[COLOUR_MAX][SQ_MAX];
Bitboard pawn_capture_masks[COLOUR_MAX][SQ_MAX];

Bitboard between_lines[SQ_MAX][SQ_MAX];
Bitboard whole_lines[SQ_MAX][SQ_MAX];

MagicEntry rook_magic[SQ_MAX];
MagicEntry bishop_magic[SQ_MAX];

std::string bb_as_image_str(Bitboard bb)
{
    std::ostringstream ss;
    ss << "+-----------------+\n";
    for (int j = BOARD_LEN - 1; j >= 0; j--) {
        ss << "| ";
        for (int i = 0; i < BOARD_LEN; i++)
            ss << (bb & bb_set(i + j * BOARD_LEN) ? "1 " : ". ");

        ss << "| " << j + 1 << "\n";
    }
    ss << "+-----------------+\n";
    ss << "  a b c d e f g h  \n";

    return ss.str();
}

static void precompute_king_tables()
{
    Bitboard bb = 1;
    for (Square sq = SQ_ZERO; sq < SQ_MAX; sq++, bb <<= 1) {
        Bitboard atk = bb;
        atk |= ((atk << 1) & ~FILE_A) | ((atk >> 1) & ~FILE_H);
        atk |= (atk << 8) | (atk >> 8);
        king_move_masks[sq] = atk & ~bb;
    }
}

static void precompute_knight_tables()
{
    Bitboard bb = 1;
    for (Square sq = SQ_ZERO; sq < SQ_MAX; sq++, bb <<= 1) {
        Bitboard atk = BB_EMPTY;

        atk |= (bb << 6) & ~(FILE_G | FILE_H);  // NWW
        atk |= (bb << 15) & ~FILE_H;            // NNW
        atk |= (bb << 17) & ~FILE_A;            // NNE
        atk |= (bb << 10) & ~(FILE_A | FILE_B); // NEE
        atk |= (bb >> 6) & ~(FILE_A | FILE_B);  // SEE
        atk |= (bb >> 15) & ~FILE_A;            // SSE
        atk |= (bb >> 17) & ~FILE_H;            // SSW
        atk |= (bb >> 10) & ~(FILE_G | FILE_H); // SWW

        knight_move_masks[sq] = atk;
    }
}

template<bool MagicMask>
static Bitboard rook_move_mask(Square sq)
{
    constexpr Bitboard RANK_1_MASK = RANK_1 & (MagicMask ? ~(FILE_A | FILE_H) : BB_FULL);
    constexpr Bitboard FILE_A_MASK = FILE_A & (MagicMask ? ~(RANK_1 | RANK_8) : BB_FULL);
    return ((RANK_1_MASK << (sq & 56)) | (FILE_A_MASK << (sq & 7))) & ~bb_set(sq);
}

static void precompute_rook_tables()
{
    for (Square sq = SQ_ZERO; sq < SQ_MAX; sq++)
        rook_move_masks[sq] = rook_move_mask<false>(sq);
}

template<bool MagicMask>
static Bitboard bishop_move_mask(Square sq)
{
    constexpr Bitboard pos_diag = 0x8040201008040201;
    constexpr Bitboard neg_diag = 0x0102040810204080;

    // Reference: https://www.chessprogramming.org/On_an_empty_Board
    Bitboard bb;
    {
        int diag = 8 * (sq & 7) - (sq & 56);
        int north = -diag & (diag >> 31);
        int south = diag & (-diag >> 31);
        bb = (pos_diag >> south) << north;
    }
    {
        int diag = 56 - 8 * (sq & 7) - (sq & 56);
        int north = -diag & (diag >> 31);
        int south = diag & (-diag >> 31);
        bb |= ((neg_diag >> south) << north);
    }
    return bb & ~bb_set(sq) & (MagicMask ? CENTER_SQUARES : BB_FULL);
}

static void precompute_bishop_tables()
{
    for (Square sq = SQ_ZERO; sq < SQ_MAX; sq++)
        bishop_move_masks[sq] = bishop_move_mask<false>(sq);
}

static void precompute_pawn_tables()
{
    for (Square sq = SQ_ZERO; sq < SQ_MAX; sq++) {
        pawn_move_masks[WHITE][sq] = pawn_move_mask_sq<WHITE>(sq);
        pawn_capture_masks[WHITE][sq] = pawn_capture_mask_sq<WHITE>(sq);
        pawn_move_masks[BLACK][sq] = pawn_move_mask_sq<BLACK>(sq);
        pawn_capture_masks[BLACK][sq] = pawn_capture_mask_sq<BLACK>(sq);
    }
}

template<PieceType P>
static void find_magic()
{
    static_assert(P == ROOK || P == BISHOP);

    Xorshift64 rng(1);

    constexpr Direction rook_dirs[4] = { NORTH, EAST, SOUTH, WEST };
    constexpr Direction bishop_dirs[4] = { NORTH_EAST, SOUTH_EAST, SOUTH_WEST, NORTH_WEST };

    uint32_t last_updated[MAGIC_TABLE_SIZE] = {};
    uint64_t current_iter = 0;

    for (Square sq = SQ_ZERO; sq < SQ_MAX; sq++) {

        MagicEntry& entry = P == ROOK ? rook_magic[sq] : bishop_magic[sq];
        entry.mask = P == ROOK ? rook_move_mask<true>(sq)
                               : bishop_move_mask<true>(sq);
        entry.shift = 64 - pop_count(entry.mask);

        // Generate moves and remember them. We use the Carry-Rippler trick to
        // traverse subsets.
        Bitboard moves[MAGIC_TABLE_SIZE];
        Bitboard subsets[MAGIC_TABLE_SIZE];
        int subset_count = 0;
        {
            Bitboard occ = 0;
            do {
                Bitboard slider_move_bb = BB_EMPTY;
                for (Direction dir : P == ROOK ? rook_dirs : bishop_dirs) {
                    Bitboard bb = bb_set(sq);
                    do {
                        bb = bb_shift(bb, dir);
                        slider_move_bb |= bb;
                    } while (bb && (bb & occ) == BB_EMPTY);
                }

                subsets[subset_count] = occ;
                moves[subset_count] = slider_move_bb;
                subset_count++;
                occ = (occ - entry.mask) & entry.mask;
            } while (occ);
        }

        // Brute force magics
        bool is_valid_magic;
        do {
            // Generate sparse random number with more higher bits
            do {
                entry.magic = rng.next() & rng.next() & rng.next();
            } while (pop_count((entry.magic * entry.mask) >> entry.shift) < pop_count(entry.mask) - 2);

            is_valid_magic = true;
            current_iter++;

            // Traverse the subsets and corresponding generated moves to check if the
            // magic is valid.
            for (int i = 0; i < subset_count; i++) {
                Bitboard index = ((entry.mask & subsets[i]) * entry.magic) >> entry.shift;

                if (last_updated[index] < current_iter) {
                    last_updated[index] = current_iter;
                    entry.moves[index] = moves[i];
                } else if (entry.moves[index] != moves[i]) {
                    is_valid_magic = false;
                    break;
                }
            }
        } while (!is_valid_magic);
    }
}

static void precompute_magic()
{
    find_magic<ROOK>();
    find_magic<BISHOP>();
}

static void precompute_between_line_table()
{
    for (Square from = SQ_ZERO; from < SQ_MAX; from++) {
        for (Square to = SQ_ZERO; to < SQ_MAX; to++) {
            Bitboard line = BB_EMPTY;
            Direction dir = direction_between(from, to);

            Bitboard bb = bb_shift(bb_set(from), dir);
            while (bb && (bb & bb_set(to)) == BB_EMPTY) {
                line |= bb;
                bb = bb_shift(bb, dir);
            }

            if (!bb) // Border was reached: there is no line between the squares
                line = BB_EMPTY;

            between_lines[from][to] = line;
        }
    }
}

static void precompute_whole_line_table()
{
    for (Square from = SQ_ZERO; from < SQ_MAX; from++) {
        for (Square to = SQ_ZERO; to < SQ_MAX; to++) {

            if (rank_of(from) == rank_of(to) || file_of(from) == file_of(to)) {
                whole_lines[from][to] = (rook_move_mask<false>(from) & rook_move_mask<false>(to)) |
                                        bb_set(from) | bb_set(to);
            } else if (std::abs(rank_of(from) - rank_of(to)) == std::abs(file_of(from) - file_of(to))) {
                whole_lines[from][to] = (bishop_move_mask<false>(from) & bishop_move_mask<false>(to)) |
                                        bb_set(from) | bb_set(to);
            } else {
                whole_lines[from][to] = BB_EMPTY;
            }
        }
    }
}

void precompute_bitboards()
{
    precompute_king_tables();
    precompute_knight_tables();
    precompute_rook_tables();
    precompute_bishop_tables();
    precompute_pawn_tables();

    precompute_magic();

    precompute_between_line_table();
    precompute_whole_line_table();
}
