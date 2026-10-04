#include "nnue.h"

#include "board.h"

#include <cstdlib>
#include <cstring>
#include <immintrin.h>
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

// https://chessprogramming.org/NNUE, https://asteri.sm/files/2024-06-01-nnue
int32_t NNUEAccumulatorPair::evaluate(const NNUE& nnue, Colour perspective) const
{
    const int16_t* our_acc = perspective == WHITE ? acc : acc + NNUE_HIDDEN_SIZE;
    const int16_t* their_acc = perspective == WHITE ? acc + NNUE_HIDDEN_SIZE : acc;

#ifdef __AVX2__
    // Lizard SIMD for SCReLU
    const __m256i vec_zero = _mm256_setzero_si256();
    const __m256i vec_qa = _mm256_set1_epi16(NNUE_QA);
    __m256i sum = vec_zero;

    for (size_t i = 0; i < NNUE_HIDDEN_SIZE; i += 16) {
        const __m256i us = _mm256_load_si256((const __m256i*)(our_acc + i));
        const __m256i them = _mm256_load_si256((const __m256i*)(their_acc + i));
        const __m256i us_weights = _mm256_load_si256((const __m256i*)(nnue.output_weights + i));
        const __m256i them_weights = _mm256_load_si256((const __m256i*)(nnue.output_weights + i + NNUE_HIDDEN_SIZE));

        const __m256i us_clamped = _mm256_min_epi16(_mm256_max_epi16(us, vec_zero), vec_qa);
        const __m256i them_clamped = _mm256_min_epi16(_mm256_max_epi16(them, vec_zero), vec_qa);

        const __m256i us_results = _mm256_madd_epi16(_mm256_mullo_epi16(us_weights, us_clamped), us_clamped);
        const __m256i them_results = _mm256_madd_epi16(_mm256_mullo_epi16(them_weights, them_clamped), them_clamped);

        sum = _mm256_add_epi32(sum, us_results);
        sum = _mm256_add_epi32(sum, them_results);
    }

    sum = _mm256_hadd_epi32(sum, sum);
    sum = _mm256_hadd_epi32(sum, sum);

    alignas(32) int32_t stored_sum[8];
    _mm256_store_si256((__m256i*)stored_sum, sum);
    int32_t eval = stored_sum[0] + stored_sum[4];

#else
    int32_t eval_our = 0, eval_their = 0; // accumulators, ideally more
    for (size_t i = 0; i < NNUE_HIDDEN_SIZE; i++) {
        eval_our += screlu(our_acc[i]) * nnue.output_weights[i];
        eval_their += screlu(their_acc[i]) * nnue.output_weights[NNUE_HIDDEN_SIZE + i];
    }
    int32_t eval = eval_our + eval_their;
#endif

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
            std::cerr << "The embedded NNUE network has the wrong size" << std::endl;
            std::abort();
        }
        return n;
    }();
    return net;
}

template<int Adds, int Subs>
static void fused_update(const int16_t* __restrict__ parent, int16_t* __restrict__ child, const int16_t* __restrict__ const (&add_rows)[2], const int16_t* __restrict__ const (&sub_rows)[2])
{
#ifdef __AVX2__
    for (size_t i = 0; i < NNUE_HIDDEN_SIZE; i += 16) {
        __m256i value = _mm256_load_si256((const __m256i*)(parent + i));
        for (int a = 0; a < Adds; a++) {
            const __m256i add = _mm256_load_si256((const __m256i*)(add_rows[a] + i));
            value = _mm256_add_epi16(value, add);
        }

        for (int s = 0; s < Subs; s++) {
            const __m256i sub = _mm256_load_si256((const __m256i*)(sub_rows[s] + i));
            value = _mm256_sub_epi16(value, sub);
        }

        _mm256_store_si256((__m256i*)(child + i), value);
    }

#else

    for (size_t i = 0; i < NNUE_HIDDEN_SIZE; i++) {
        int16_t value = parent[i];
        for (int a = 0; a < Adds; a++)
            value += add_rows[a][i];
        for (int s = 0; s < Subs; s++)
            value -= sub_rows[s][i];
        child[i] = value;
    }
#endif
}

template<Colour Side>
void NNUEAccumulatorPair::update_by_side(const NNUE& nnue, const NNUEAccumulatorPair& parent, const UpdatedPiece (&updated)[NNUE_MAX_UPDATED_PIECE])
{
    const int16_t* __restrict__ add_rows[2] = {};
    const int16_t* __restrict__ sub_rows[2] = {};
    int adds = 0, subs = 0;
    for (int p = 0; p < NNUE_MAX_UPDATED_PIECE && is_piece(updated[p].piece); p++) {
        if (updated[p].from != SQ_NONE)
            sub_rows[subs++] = nnue.feature_weights[acc_index<Side>(updated[p].from, updated[p].piece)];
        if (updated[p].to != SQ_NONE)
            add_rows[adds++] = nnue.feature_weights[acc_index<Side>(updated[p].to, updated[p].piece)];
    }

    constexpr size_t Offset = Side == WHITE ? 0 : NNUE_HIDDEN_SIZE;
    const int16_t* __restrict__ from = parent.acc + Offset;
    int16_t* __restrict__ to = acc + Offset;
    if (adds == 1 && subs == 1)
        fused_update<1, 1>(from, to, add_rows, sub_rows);
    else if (adds == 1 && subs == 2)
        fused_update<1, 2>(from, to, add_rows, sub_rows);
    else if (adds == 2 && subs == 2)
        fused_update<2, 2>(from, to, add_rows, sub_rows);
    else { // null move
        assert(adds == 0 && subs == 0);
        std::memcpy((char*)to, (char*)from, NNUE_HIDDEN_SIZE * sizeof(int16_t));
    }
}

void NNUEAccumulatorPair::update(const NNUE& nnue, const NNUEAccumulatorPair& parent, const UpdatedPiece (&updated)[NNUE_MAX_UPDATED_PIECE])
{
    update_by_side<WHITE>(nnue, parent, updated);
    update_by_side<BLACK>(nnue, parent, updated);
}
