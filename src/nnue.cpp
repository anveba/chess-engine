#include "nnue.h"

#include <cstring>
#include <fstream>
#include <vector>

template<Colour Perspective>
static constexpr int acc_index(Square sq, Piece piece)
{
    sq = square_wrt(BLACK, sq);
    static_assert(WHITE == 0);
    static_assert(BLACK == 1);
    const Colour side = Colour(uint8_t(Perspective) ^ uint8_t(colour_of(piece)));
    static_assert(NO_PIECE_TYPE == 0);
    return side * SQ_MAX * VALID_PIECE_TYPE_COUNT + (type_of(piece) - 1) * SQ_MAX + sq;
}

static constexpr int32_t screlu(int16_t val)
{
    int32_t c = val > NNUE_QA ? NNUE_QA : (val < 0 ? 0 : val);
    return c * c;
}

template<Colour Perspective>
void NNUEAccumulatorPair::add_feature(const NNUE& nnue, Square sq, Piece piece)
{
    size_t offset = Perspective == WHITE ? 0 : NNUE_HIDDEN_SIZE;
    for (size_t i = offset; i < NNUE_HIDDEN_SIZE + offset; i++) {
        acc[i] += nnue.feature_weights[acc_index<Perspective>(sq, piece)];
    }
}

template<Colour Perspective>
void NNUEAccumulatorPair::sub_feature(const NNUE& nnue, Square sq, Piece piece)
{
    size_t offset = Perspective == WHITE ? 0 : NNUE_HIDDEN_SIZE;
    for (size_t i = offset; i < NNUE_HIDDEN_SIZE + offset; i++) {
        acc[i] -= nnue.feature_weights[acc_index<Perspective>(sq, piece)];
    }
}

// https://chessprogramming.org/NNUE
int16_t NNUEAccumulatorPair::evaluate(const NNUE& nnue, Colour perspective)
{
    int16_t* our_acc = perspective == WHITE ? acc : acc + NNUE_HIDDEN_SIZE;
    int16_t* their_acc = perspective == WHITE ? acc + NNUE_HIDDEN_SIZE : acc;

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
    std::memset(acc, 0, sizeof(acc));

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

template<Colour Perspective>
void NNUEAccumulatorPair::update(const NNUE& nnue, const Board& board, Move move)
{
    assert(Perspective == board.side());
    assert(move.is_proper());

    if (move.is_normal()) {
        Piece from_piece = board.at(move.from_sq());
        sub_feature<Perspective>(nnue, move.from_sq(), from_piece);

        Piece to_piece = board.at(move.to_sq());
        if (is_piece(to_piece))
            sub_feature<Perspective>(nnue, move.to_sq(), to_piece);
        add_feature<Perspective>(nnue, move.to_sq(), from_piece);

    } else if (move.is_castle()) {
        sub_feature<Perspective>(nnue, move.from_sq(), Perspective * KING);
        sub_feature<Perspective>(nnue, move.to_sq(), Perspective * ROOK);
        add_feature<Perspective>(nnue, move.king_castle_to(), Perspective * KING);
        add_feature<Perspective>(nnue, move.rook_castle_to(), Perspective * ROOK);

    } else if (move.is_promotion()) {
        sub_feature<Perspective>(nnue, move.from_sq(), Perspective * PAWN);
        add_feature<Perspective>(nnue, move.to_sq(), Perspective * move.promotion_to());

    } else if (move.is_ep()) {
        sub_feature<Perspective>(nnue, move.from_sq(), Perspective * PAWN);
        sub_feature<Perspective>(nnue, move.captured_ep_pawn_sq(), ~Perspective * PAWN);
        add_feature<Perspective>(nnue, move.to_sq(), Perspective * PAWN);
    }
}

template<Piece P>
void NNUEAccumulatorPair::update_piece(const NNUE& nnue, const Board& board)
{
    constexpr Colour Perspective = colour_of(P);
    Bitboard occ = board.occ(P);
    while (occ) {
        Square sq = pop_lsb(occ);
        add_feature<Perspective>(nnue, sq, P);
    }
}

constexpr

    NNUE
    NNUE::load(std::string path, bool& success)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    NNUE result;

    if (!file) {
        success = false;
        return result;
    }

    const std::streamsize size = file.tellg();
    if (size < sizeof(NNUE)) {
        success = false;
        return result;
    }

    file.seekg(0, std::ios::beg);

    std::vector<char> raw(static_cast<std::size_t>(size));

    if (!file.read(raw.data(), size)) {
        success = false;
        return result;
    }

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
    next += sizeof(result.feature_weights);
    memcpy(result.accumulator_bias, values.data() + next, sizeof(result.accumulator_bias));
    next += sizeof(result.accumulator_bias);
    memcpy(result.output_weights, values.data() + next, sizeof(result.output_weights));
    next += sizeof(result.output_weights);
    memcpy(&result.output_bias, values.data() + next, sizeof(result.output_bias));

    success = true;
    return result;
}
