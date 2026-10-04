#include "nnue.h"

#include "board.h"

#include <cstdlib>
#include <iostream>

#define EMBEDDED_NET "nnue/gen2.nnue"

asm(".section .rodata\n"
    ".balign 64\n"
    ".global embedded_net\n"
    "embedded_net: .incbin \"" EMBEDDED_NET "\"\n"
    ".global embedded_net_end\n"
    "embedded_net_end:\n"
    ".previous");
extern "C" const unsigned char embedded_net[], embedded_net_end[];

constexpr size_t NNUE_FILE_SIZE = (sizeof(NNUE) + 63) / 64 * 64;

static const NNUE& nnue = NNUE::embedded();

const NNUE& get_nnue()
{
    return nnue;
}

template<Colour Perspective>
static constexpr int acc_index(Square sq, Piece piece)
{
    static_assert(WHITE == 0);
    static_assert(BLACK == 1);
    const Colour side = Colour(uint8_t(Perspective) ^ uint8_t(colour_of(piece)));
    sq = square_wrt(Perspective, sq);
    static_assert(NO_PIECE_TYPE == 0);
    return side * SQ_MAX * VALID_PIECE_TYPE_COUNT + (type_of(piece) - 1) * SQ_MAX + sq;
}

static constexpr int32_t screlu(int16_t val)
{
    int32_t c = val > NNUE_QA ? NNUE_QA : (val < 0 ? 0 : val);
    return c * c;
}

template<bool Add>
void NNUEAccumulatorPair::update_feature(const NNUE& nnue, Square sq, Piece piece)
{
    int iw = acc_index<WHITE>(sq, piece);
    int ib = acc_index<BLACK>(sq, piece);
    for (size_t i = 0; i < NNUE_HIDDEN_SIZE; i++) {
        int16_t vw = nnue.feature_weights[iw][i];
        int16_t vb = nnue.feature_weights[ib][i];
        if (Add) {
            acc[i] += vw;
            acc[i + NNUE_HIDDEN_SIZE] += vb;
        } else {
            acc[i] -= vw;
            acc[i + NNUE_HIDDEN_SIZE] -= vb;
        }
    }
}

// https://chessprogramming.org/NNUE
int32_t NNUEAccumulatorPair::evaluate(const NNUE& nnue, Colour perspective) const
{
    const int16_t* our_acc = perspective == WHITE ? acc : acc + NNUE_HIDDEN_SIZE;
    const int16_t* their_acc = perspective == WHITE ? acc + NNUE_HIDDEN_SIZE : acc;

    int32_t eval = 0;
    for (size_t i = 0; i < NNUE_HIDDEN_SIZE; i++) {
        eval += screlu(our_acc[i]) * nnue.output_weights[i];
        eval += screlu(their_acc[i]) * nnue.output_weights[NNUE_HIDDEN_SIZE + i];
    }

    eval /= NNUE_QA;
    eval += nnue.output_bias;

    eval *= NNUE_SCALE;
    eval /= NNUE_QA * NNUE_QB;

    return eval;
}

void NNUEAccumulatorPair::set(const NNUE& nnue, const Board& board)
{
    for (size_t i = 0; i < NNUE_HIDDEN_SIZE; i++) {
        acc[i] = nnue.accumulator_bias[i];
        acc[i + NNUE_HIDDEN_SIZE] = nnue.accumulator_bias[i];
    }

    update_piece<W_PAWN>(nnue, board);
    update_piece<W_ROOK>(nnue, board);
    update_piece<W_KNIGHT>(nnue, board);
    update_piece<W_BISHOP>(nnue, board);
    update_piece<W_QUEEN>(nnue, board);
    update_piece<W_KING>(nnue, board);

    update_piece<B_PAWN>(nnue, board);
    update_piece<B_ROOK>(nnue, board);
    update_piece<B_KNIGHT>(nnue, board);
    update_piece<B_BISHOP>(nnue, board);
    update_piece<B_QUEEN>(nnue, board);
    update_piece<B_KING>(nnue, board);

}

void NNUEAccumulatorPair::make_move(const NNUE& nnue, const Board& board, Move move)
{
    update_move<true>(nnue, board, move);
}

void NNUEAccumulatorPair::unmake_move(const NNUE& nnue, const Board& board, Move move)
{
    update_move<false>(nnue, board, move);
}

template<bool Make>
void NNUEAccumulatorPair::update_move(const NNUE& nnue, const Board& board, Move move)
{
    assert(move.is_proper());

    if (move.is_normal()) {
        Piece from_piece = board.at(move.from_sq());
        update_feature<!Make>(nnue, move.from_sq(), from_piece);

        Piece to_piece = board.at(move.to_sq());
        if (is_piece(to_piece))
            update_feature<!Make>(nnue, move.to_sq(), to_piece);
        update_feature<Make>(nnue, move.to_sq(), from_piece);

    } else if (move.is_castle()) {
        update_feature<!Make>(nnue, move.from_sq(), board.side() * KING);
        update_feature<!Make>(nnue, move.to_sq(), board.side() * ROOK);
        update_feature<Make>(nnue, move.king_castle_to(), board.side() * KING);
        update_feature<Make>(nnue, move.rook_castle_to(), board.side() * ROOK);

    } else if (move.is_promotion()) {
        update_feature<!Make>(nnue, move.from_sq(), board.side() * PAWN);
        update_feature<Make>(nnue, move.to_sq(), board.side() * move.promotion_to());
        Piece to_piece = board.at(move.to_sq());
        if (is_piece(to_piece))
            update_feature<!Make>(nnue, move.to_sq(), to_piece);

    } else { // EP
        assert(move.is_ep());
        update_feature<!Make>(nnue, move.from_sq(), board.side() * PAWN);
        update_feature<!Make>(nnue, move.captured_ep_pawn_sq(), ~board.side() * PAWN);
        update_feature<Make>(nnue, move.to_sq(), board.side() * PAWN);
    }
}

template<Piece P>
void NNUEAccumulatorPair::update_piece(const NNUE& nnue, const Board& board)
{
    Bitboard occ = board.occ(P);
    while (occ) {
        Square sq = pop_lsb(occ);
        update_feature<true>(nnue, sq, P);
    }
}

// Returns the position after the values read.
static const unsigned char* read_little_endian_int16s(const unsigned char* data, int16_t* out, size_t count)
{
    for (size_t i = 0; i < count; i++, data += 2)
        out[i] = int16_t(data[0] | (data[1] << 8));
    return data;
}

bool NNUE::load_from_bytes(const unsigned char* data, size_t size, NNUE& result)
{
    if (size != NNUE_FILE_SIZE)
        return false;

    data = read_little_endian_int16s(data, &result.feature_weights[0][0], NNUE_FEATURE_COUNT * NNUE_HIDDEN_SIZE);
    data = read_little_endian_int16s(data, result.accumulator_bias, NNUE_HIDDEN_SIZE);
    data = read_little_endian_int16s(data, result.output_weights, 2 * NNUE_HIDDEN_SIZE);
    read_little_endian_int16s(data, &result.output_bias, 1);
    return true;
}

const NNUE& NNUE::embedded()
{
    static const NNUE net = [] {
        NNUE n;
        if (!load_from_bytes(embedded_net, embedded_net_end - embedded_net, n)) {
            std::cerr << "The embedded network has the wrong size for this architecture" << std::endl;
            std::abort();
        }
        return n;
    }();
    return net;
}
