#include "nnue.h"

#include "board.h"

#include <cstring>
#include <fstream>
#include <vector>

static NNUE nnue;

void set_nnue(const NNUE& new_nnue)
{
    nnue = new_nnue;
}

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
    if (eval_is_cached[perspective])
        return cached_eval[perspective];

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

    eval_is_cached[perspective] = true;
    cached_eval[perspective] = eval;

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

    eval_is_cached[0] = eval_is_cached[1] = false;
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
    eval_is_cached[0] = eval_is_cached[1] = false;
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

bool NNUE::load(std::string path, NNUE& result)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file)
        return false;

    const std::streamsize size = file.tellg();
    if (size < std::streamsize(sizeof(NNUE)))
        return false;

    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> raw(static_cast<std::size_t>(size));

    if (!file.read((char*)raw.data(), size))
        return false;

    std::vector<int16_t> values;
    values.reserve(raw.size() / 2);

    // Convert from little-endian
    for (std::size_t i = 0; i + 1 < raw.size(); i += 2) {
        uint16_t bits = static_cast<uint16_t>(raw[i]) | (static_cast<uint16_t>(raw[i + 1]) << 8);

        int16_t value;
        std::memcpy(&value, &bits, sizeof(value));

        values.push_back(value);
    }

    size_t next = 0;
    memcpy(result.feature_weights, values.data() + next, sizeof(result.feature_weights));
    next += sizeof(result.feature_weights) / sizeof(int16_t);
    memcpy(result.accumulator_bias, values.data() + next, sizeof(result.accumulator_bias));
    next += sizeof(result.accumulator_bias) / sizeof(int16_t);
    memcpy(result.output_weights, values.data() + next, sizeof(result.output_weights));
    next += sizeof(result.output_weights) / sizeof(int16_t);
    memcpy(&result.output_bias, values.data() + next, sizeof(result.output_bias));

    return true;
}
