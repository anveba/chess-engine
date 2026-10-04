#ifndef NNUE_H_INCLUDED
#define NNUE_H_INCLUDED

#include "board.h"

constexpr size_t NNUE_FEATURE_COUNT = 2 * SQ_MAX * VALID_PIECE_TYPE_COUNT;
constexpr size_t NNUE_HIDDEN_SIZE = 128;
constexpr int32_t NNUE_QA = 255;
constexpr int32_t NNUE_QB = 64;
constexpr int32_t NNUE_SCALE = 400;

struct NNUEAccumulatorPair;

struct NNUE
{
  public:
    static NNUE load(std::string path, bool& success);

  private:
    friend NNUEAccumulatorPair;

    int16_t feature_weights[NNUE_FEATURE_COUNT];
    int16_t accumulator_bias[NNUE_HIDDEN_SIZE];
    int16_t output_weights[NNUE_HIDDEN_SIZE * 2];
    int16_t output_bias;
};

struct NNUEAccumulatorPair
{
  public:
    int16_t evaluate(const NNUE& nnue, Colour perspective);
    void set(const NNUE& nnue, const Board& board);
    template<Colour Perspective>
    void update(const NNUE& nnue, const Board& board, Move move);

  private:
    template<Piece P>
    void update_piece(const NNUE& nnue, const Board& board);
    template<Colour Perspective>
    void add_feature(const NNUE& nnue, Square sq, Piece piece);
    template<Colour Perspective>
    void sub_feature(const NNUE& nnue, Square sq, Piece piece);

    bool dirty;
    int16_t acc[NNUE_HIDDEN_SIZE * 2]; // First white then black values
};

#endif