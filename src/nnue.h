#ifndef NNUE_H_INCLUDED
#define NNUE_H_INCLUDED

#include <algorithm>
#include <iterator>

#include "chess.h"

class Board;

constexpr size_t NNUE_FEATURE_COUNT = 2 * SQ_MAX * VALID_PIECE_TYPE_COUNT;
constexpr size_t NNUE_HIDDEN_SIZE = 128;
constexpr int32_t NNUE_QA = 255;
constexpr int32_t NNUE_QB = 64;
constexpr int32_t NNUE_SCALE = 400;

struct NNUEAccumulatorPair;

struct NNUE
{
  public:
    static bool load_from_bytes(const unsigned char* data, size_t size, NNUE& result);
    static const NNUE& embedded();

  private:
    friend NNUEAccumulatorPair;

    int16_t feature_weights[NNUE_FEATURE_COUNT][NNUE_HIDDEN_SIZE];
    int16_t accumulator_bias[NNUE_HIDDEN_SIZE];
    int16_t output_weights[NNUE_HIDDEN_SIZE * 2];
    int16_t output_bias;
};

struct NNUEAccumulatorPair
{
  public:
    int32_t evaluate(const NNUE& nnue, Colour perspective) const;
    void set(const NNUE& nnue, const Board& board);
    void make_move(const NNUE& nnue, const Board& board, Move move);
    void unmake_move(const NNUE& nnue, const Board& board, Move move);

    bool operator==(const NNUEAccumulatorPair& other) const
    {
        return std::equal(std::begin(acc), std::end(acc), std::begin(other.acc));
    }

  private:
    template<Piece P>
    void update_piece(const NNUE& nnue, const Board& board);
    template<bool Add>
    void update_feature(const NNUE& nnue, Square sq, Piece piece);
    template<bool Make>
    void update_move(const NNUE& nnue, const Board& board, Move move);

    mutable bool eval_is_cached[COLOUR_MAX];
    mutable int32_t cached_eval[COLOUR_MAX];
    alignas(64) int16_t acc[NNUE_HIDDEN_SIZE * 2]; // First white then black values
};

const NNUE& get_nnue();

#endif