#ifndef MATCH_H_INCLUDED
#define MATCH_H_INCLUDED

#include "board.h"
#include "cinstance.h"

enum MatchStatus
{
    WHITE_WIN = 0,
    BLACK_WIN = 1,
    DRAW_BY_STALEMATE = 2,
    DRAW_BY_FIFTY_MOVE_RULE = 3,
    DRAW_BY_REPETITION = 4,
    ONGOING = 5
};

struct MatchResult
{
    MatchStatus status;
    size_t halfmoves;
    std::vector<Move> moves;
};

struct MatchParams
{
    std::string fen;
    size_t ms_per_move;
    size_t max_halfmoves;
    bool first_is_white;
};

struct PlayerParams
{
    std::string name;
    std::string executable_path;
    std::vector<ChessOption> options;
};

struct Match
{
    MatchParams params;
    MatchResult result;
};

void play_matches(Match* matches, size_t match_count, PlayerParams players[2], size_t thread_count);

#endif