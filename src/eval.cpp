#include "eval.h"

#include <algorithm>
#include <iomanip>
#include <iterator>
#include <sstream>

#include "piecetables.h"

#if TRADITIONAL_EVAL

struct BoardScores
{
    int mg, eg;
};

static_assert(sizeof(int) >= 4);

static constexpr BoardScores operator+(BoardScores a, BoardScores b)
{
    return { a.mg + b.mg, a.eg + b.eg };
}
static constexpr BoardScores operator-(BoardScores a, BoardScores b)
{
    return { a.mg - b.mg, a.eg - b.eg };
}
static constexpr BoardScores operator*(int n, BoardScores s)
{
    return { n * s.mg, n * s.eg };
}
static constexpr BoardScores& operator+=(BoardScores& a, BoardScores b)
{
    return a = a + b;
}

// Tapered eval: https://www.chessprogramming.org/Tapered_Eval
static constexpr int PHASE_WEIGHT[] = { 0, 0, 2, 1, 1, 4, 0 };
static constexpr int MAX_PHASE = 24;

// The values from here on without a source are untuned guesses of a typical size.

// https://www.chessprogramming.org/Tempo
static constexpr int TEMPO = 10;

// https://www.chessprogramming.org/Bishop_Pair
static constexpr BoardScores BISHOP_PAIR = { 25, 50 };
// https://www.chessprogramming.org/Rook_on_Open_File
static constexpr BoardScores ROOK_OPEN_FILE = { 25, 10 };
static constexpr BoardScores ROOK_SEMI_OPEN_FILE = { 12, 6 };

// https://www.chessprogramming.org/Isolated_Pawn
static constexpr BoardScores ISOLATED_PAWN = { -8, -12 };

// https://www.chessprogramming.org/Doubled_Pawn
static constexpr BoardScores DOUBLED_PAWN = { -8, -20 };

// https://www.chessprogramming.org/Passed_Pawn
static constexpr BoardScores PASSED_PAWN[] = { { 0, 0 }, { 2, 8 }, { 4, 12 }, { 8, 20 }, { 15, 35 }, { 25, 60 }, { 40, 90 }, { 0, 0 } };
static constexpr BoardScores PASSED_PAWN_FREE = { 5, 15 };

// King safety: https://www.chessprogramming.org/King_Safety
static constexpr BoardScores KING_SHIELD_NEAR = { 12, 0 };
static constexpr BoardScores KING_SHIELD_FAR = { 6, 0 };
static constexpr BoardScores KING_OPEN_FILE = { -25, 0 };
static constexpr BoardScores KING_SEMI_OPEN_FILE = { -12, 0 };
static constexpr int KING_ATTACK_WEIGHT_BY_PIECE[] = { 0, 0, 24, 16, 16, 40, 0 };
static constexpr int KING_ATTACK_PERCENT_BY_ATTACKER_COUNT[] = { 0, 0, 50, 75, 88, 94, 97, 99 };

// Mobility bonus: https://www.chessprogramming.org/Mobility
// Values from: https://github.com/official-stockfish/Stockfish/blob/sf_12/src/evaluate.cpp
// clang-format off
static constexpr BoardScores KNIGHT_MOBILITY[] = {
    { -31, -40 }, { -26, -28 }, { -6, -15 }, { -2, -8 }, { 2, 2 }, { 6, 5 }, { 11, 8 }, { 14, 10 }, { 16, 12 },
};
static constexpr BoardScores BISHOP_MOBILITY[] = {
    { -24, -30 }, { -10, -12 }, { 8, -2 }, { 13, 6 }, { 19, 12 }, { 25, 21 }, { 27, 27 },
    { 31, 28 }, { 31, 32 }, { 34, 36 }, { 40, 39 }, { 40, 43 }, { 45, 44 }, { 49, 48 },
};
static constexpr BoardScores ROOK_MOBILITY[] = {
    { -30, -39 }, { -10, -8 }, { 1, 11 }, { 2, 20 }, { 2, 35 }, { 5, 49 }, { 11, 51 }, { 15, 60 },
    { 20, 67 }, { 20, 70 }, { 20, 79 }, { 24, 82 }, { 28, 84 }, { 28, 85 }, { 31, 86 },
};
static constexpr BoardScores QUEEN_MOBILITY[] = {
    { -15, -24 }, { -6, -15 }, { -4, -4 }, { -4, 10 }, { 10, 20 }, { 11, 27 }, { 11, 30 },
    { 17, 37 }, { 19, 39 }, { 26, 48 }, { 32, 48 }, { 32, 50 }, { 32, 60 }, { 33, 63 },
    { 33, 65 }, { 33, 66 }, { 36, 68 }, { 36, 70 }, { 38, 73 }, { 39, 75 }, { 46, 75 },
    { 54, 84 }, { 54, 84 }, { 54, 85 }, { 55, 91 }, { 57, 91 }, { 57, 96 }, { 58, 109 },
};
// clang-format on

static constexpr const BoardScores* MOBILITY_BY_PIECE[MAX_PIECE_TYPE] = {
    nullptr,
    nullptr,
    ROOK_MOBILITY,
    KNIGHT_MOBILITY,
    BISHOP_MOBILITY,
    QUEEN_MOBILITY,
    nullptr
};

static_assert(std::size(PHASE_WEIGHT) == MAX_PIECE_TYPE);
static_assert(std::size(KING_ATTACK_WEIGHT_BY_PIECE) == MAX_PIECE_TYPE);
static_assert(std::size(KING_ATTACK_PERCENT_BY_ATTACKER_COUNT) == 8);
static_assert(std::size(PASSED_PAWN) == 8);
static_assert(std::size(KNIGHT_MOBILITY) == 9);
static_assert(std::size(BISHOP_MOBILITY) == 14);
static_assert(std::size(ROOK_MOBILITY) == 15);
static_assert(std::size(QUEEN_MOBILITY) == 28);

// Mop-up: https://www.chessprogramming.org/Mop-up_Evaluation
static constexpr int MOPUP_MIN_ADVANTAGE = 400;
static constexpr int MOPUP_EDGE = 10;
static constexpr int MOPUP_KING_PROXIMITY = 4;

// Draw Evaluation: https://www.chessprogramming.org/Draw_Evaluation
static constexpr int DRAW_MARGIN = 400;
static constexpr int DRAW_SCALE_DIVISOR = 8;

enum Term
{
    MATERIAL,
    PAWN_STRUCTURE,
    MOBILITY,
    KING_SAFETY,
    PIECES,
    TERM_COUNT
};

static constexpr const char* TERM_NAMES[] = { "Material", "Pawns", "Mobility", "King safety", "Pieces" };
static_assert(std::size(TERM_NAMES) == TERM_COUNT);

struct Terms
{
    BoardScores term[TERM_COUNT] = {};

    BoardScores total() const
    {
        BoardScores sum = { 0, 0 };
        for (BoardScores s : term)
            sum += s;
        return sum;
    }
};

template<Colour Side>
static constexpr BoardScores piece_square(PieceType p, Square sq)
{
    const Square idx = square_wrt(Side, sq) ^ 56;
    return { PIECE_VALUE_MG[p] + PST_MG[p][idx], PIECE_VALUE_EG[p] + PST_EG[p][idx] };
}

template<Colour Side, PieceType P>
static void evaluate_pieces(const Board& board, Bitboard movable_to, Bitboard enemy_king_zone, int& king_attackers, int& king_attack_weight, Terms& t)
{
    const Bitboard own_pawns = board.occ(Side * PAWN);
    const Bitboard enemy_pawns = board.occ(~Side * PAWN);

    Bitboard pieces = board.occ(Side * P);
    while (pieces) {
        const Square sq = pop_lsb(pieces);
        const Bitboard attacks = piece_moves<P>(sq, board.occ());

        t.term[MATERIAL] += piece_square<Side>(P, sq);
        t.term[MOBILITY] += MOBILITY_BY_PIECE[P][pop_count(attacks & movable_to)];

        // Counted here and scored later.
        if (attacks & enemy_king_zone) {
            king_attackers++;
            king_attack_weight += KING_ATTACK_WEIGHT_BY_PIECE[P];
        }

        if (P == ROOK && !(own_pawns & bb_file(file_of(sq))))
            t.term[PIECES] += (enemy_pawns & bb_file(file_of(sq))) ? ROOK_SEMI_OPEN_FILE : ROOK_OPEN_FILE;
    }
}

template<Colour Side>
static void evaluate_pawns(const Board& board, Terms& t)
{
    const Bitboard own_pawns = board.occ(Side * PAWN);
    const Bitboard enemy_pawns = board.occ(~Side * PAWN);

    Bitboard pawns = own_pawns;
    while (pawns) {
        const Square sq = pop_lsb(pawns);
        const int file = file_of(sq);
        const Bitboard front = front_fill<Side>(bb_shift<forward(Side)>(bb_set(sq)));

        t.term[MATERIAL] += piece_square<Side>(PAWN, sq);

        // Check isolated pawns
        if (!(own_pawns & bb_adjacent_files(file)))
            t.term[PAWN_STRUCTURE] += ISOLATED_PAWN;

        // Only the rearmost of doubled pawns is penalised. It cannot be a passed pawn.
        if (own_pawns & front)
            t.term[PAWN_STRUCTURE] += DOUBLED_PAWN;
        else if (!(enemy_pawns & (front | bb_shift<EAST>(front) | bb_shift<WEST>(front)))) {
            t.term[PAWN_STRUCTURE] += PASSED_PAWN[rank_wrt(Side, rank_of(sq))];
            if (!(board.occ() & bb_shift<forward(Side)>(bb_set(sq))))
                t.term[PAWN_STRUCTURE] += PASSED_PAWN_FREE;
        }
    }
}

template<Colour Side>
static void evaluate_king(const Board& board, Terms& t)
{
    const Bitboard own_pawns = board.occ(Side * PAWN);
    const Bitboard enemy_pawns = board.occ(~Side * PAWN);
    const Square king_sq = lsb_idx(board.occ(Side * KING));
    const Bitboard king_bb = bb_set(king_sq);

    t.term[MATERIAL] += piece_square<Side>(KING, king_sq);

    // Count shielding pawns
    const Bitboard shield_near = bb_shift<forward(Side)>(king_bb | bb_shift<EAST>(king_bb) | bb_shift<WEST>(king_bb));
    const Bitboard shield_far = bb_shift<forward(Side)>(shield_near);
    t.term[KING_SAFETY] += pop_count(own_pawns & shield_near) * KING_SHIELD_NEAR;
    t.term[KING_SAFETY] += pop_count(own_pawns & shield_far) * KING_SHIELD_FAR;

    // Check open files
    // semi-open if an enemy pawn is in a close-by file and open if no pawns.
    for (int file = std::max(file_of(king_sq) - 1, 0); file <= std::min(file_of(king_sq) + 1, 7); file++) {
        if (!(own_pawns & bb_file(file)))
            t.term[KING_SAFETY] += (enemy_pawns & bb_file(file)) ? KING_SEMI_OPEN_FILE : KING_OPEN_FILE;
    }
}

template<Colour Side>
static Terms evaluate_side(const Board& board)
{
    const Square enemy_king_sq = lsb_idx(board.occ(~Side * KING));
    // 3x3 around king
    const Bitboard enemy_king_zone = piece_moves<KING>(enemy_king_sq, board.occ()) | bb_set(enemy_king_sq);
    // squares not occupied by own pieces and not attacked by enemy pawns.
    const Bitboard movable_to = ~board.occ(Side) & ~pawn_capture_mask_bb<~Side>(board.occ(~Side * PAWN));

    Terms t;
    int king_attackers = 0, king_attack_weight = 0;
    evaluate_pawns<Side>(board, t);
    evaluate_king<Side>(board, t);
    evaluate_pieces<Side, KNIGHT>(board, movable_to, enemy_king_zone, king_attackers, king_attack_weight, t);
    evaluate_pieces<Side, BISHOP>(board, movable_to, enemy_king_zone, king_attackers, king_attack_weight, t);
    evaluate_pieces<Side, ROOK>(board, movable_to, enemy_king_zone, king_attackers, king_attack_weight, t);
    evaluate_pieces<Side, QUEEN>(board, movable_to, enemy_king_zone, king_attackers, king_attack_weight, t);

    // Attacks on the enemy king count as safety
    t.term[KING_SAFETY].mg += (KING_ATTACK_PERCENT_BY_ATTACKER_COUNT[std::min(king_attackers, 7)] * king_attack_weight) / 100;

    if (two_or_more(board.occ(Side * BISHOP)))
        t.term[PIECES] += BISHOP_PAIR;

    return t;
}

static int game_phase(const Board& board)
{
    int phase = 0;
    for (PieceType p : { ROOK, KNIGHT, BISHOP, QUEEN })
        phase += PHASE_WEIGHT[p] * pop_count(board.occ(WHITE * p) | board.occ(BLACK * p));
    return std::min(phase, MAX_PHASE);
}

static int blend_by_game_phase(BoardScores s, int phase)
{
    return (s.mg * phase + s.eg * (MAX_PHASE - phase)) / MAX_PHASE;
}

static int endgame_material(const Board& board, Colour side)
{
    int material = 0;
    for (PieceType p : { PAWN, ROOK, KNIGHT, BISHOP, QUEEN })
        material += PIECE_VALUE_EG[p] * pop_count(board.occ(side * p));
    return material;
}

static int mop_up(const Board& board, int eval)
{
    const Colour strong_side = eval >= 0 ? WHITE : BLACK;
    const Colour weak_side = ~strong_side;
    const int advantage = endgame_material(board, strong_side) - endgame_material(board, weak_side);
    if (board.occ(weak_side) != board.occ(weak_side * KING) || advantage < MOPUP_MIN_ADVANTAGE)
        return eval;

    const Square strong_king = lsb_idx(board.occ(strong_side * KING));
    const Square weak_king = lsb_idx(board.occ(weak_side * KING));
    const int bonus = MOPUP_EDGE * center_distance(weak_king) +
                      MOPUP_KING_PROXIMITY * (14 - manhattan_distance(strong_king, weak_king));
    return eval + (strong_side == WHITE ? bonus : -bonus);
}

static int scale_if_likely_draw(const Board& board, int eval)
{
    const Colour strong_side = eval >= 0 ? WHITE : BLACK;
    const Colour weak_side = ~strong_side;
    const int advantage = endgame_material(board, strong_side) - endgame_material(board, weak_side);

    // Only knights are a draw.
    const Bitboard strong_pieces = board.occ(strong_side) & ~board.occ(strong_side * KING);
    if (strong_pieces == board.occ(strong_side * KNIGHT) && pop_count(strong_pieces) <= 2 &&
        board.occ(weak_side) == board.occ(weak_side * KING))
        return 0;

    // No pawns and little material are likely draws
    if (!board.occ(strong_side * PAWN) && advantage < DRAW_MARGIN)
        return eval / DRAW_SCALE_DIVISOR;

    return eval;
}

#endif

BoardEval evaluate(const Board& board)
{
#if TRADITIONAL_EVAL
    const BoardScores score = evaluate_side<WHITE>(board).total() - evaluate_side<BLACK>(board).total();
    int blended_score = blend_by_game_phase(score, game_phase(board));
    int mopped_up_score = mop_up(board, blended_score);
    const int eval = scale_if_likely_draw(board, mopped_up_score);

    return BoardEval((board.side() == WHITE ? eval : -eval) + TEMPO);
#else
    constexpr int32_t MAX_NNUE_EVAL = MATE_EVAL - 512;
    int32_t eval = board.get_nnue_accumulator().evaluate(get_nnue(), board.side());
    return eval > MAX_NNUE_EVAL ? MAX_NNUE_EVAL : (eval < -MAX_NNUE_EVAL ? -MAX_NNUE_EVAL : eval);
#endif
}

std::string eval_trace(const Board& board)
{
#if TRADITIONAL_EVAL

    const Terms white = evaluate_side<WHITE>(board), black = evaluate_side<BLACK>(board);
    const int phase = game_phase(board);

    std::ostringstream out;
    auto cell = [&](BoardScores s) { out << std::setw(6) << s.mg << std::setw(6) << s.eg; };

    out << std::setw(12) << "Term"
        << " |     White MG/EG |     Black MG/EG |     Total MG/EG | Blended\n";
    for (int i = 0; i < TERM_COUNT; i++) {
        const BoardScores diff = white.term[i] - black.term[i];
        out << std::setw(12) << TERM_NAMES[i] << " | ";
        cell(white.term[i]);
        out << "    | ";
        cell(black.term[i]);
        out << "    | ";
        cell(diff);
        out << "    | " << std::setw(7) << blend_by_game_phase(diff, phase) << "\n";
    }

    const BoardScores total = white.total() - black.total();
    const int blended_score = blend_by_game_phase(total, phase);
    const int mopped_up_score = mop_up(board, blended_score);
    const int adjusted = scale_if_likely_draw(board, mopped_up_score);
    out << "\nPhase: " << phase << "/" << MAX_PHASE << " (" << MAX_PHASE << " is the opening)\n"
        << "Blended by phase (white): " << blended_score << "\n"
        << "Adjusting for mopping up (white): " << mopped_up_score - blended_score << "\n"
        << "Adjusting for likely draws (white): " << adjusted - mopped_up_score << "\n"
        << "Tempo: " << TEMPO << "\n"
        << "Final (side to move): " << evaluate(board) << "\n";
    return out.str();
#else
    std::ostringstream out;
    out << "NNUE evaluation (side to move): " << evaluate(board) << "\n";
    return out.str();
#endif
}
