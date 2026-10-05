#ifndef VIRIFORMAT_H_INCLUDED
#define VIRIFORMAT_H_INCLUDED

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "chess.h"

enum GameResult : uint8_t
{
    BLACK_WIN,
    DRAW,
    WHITE_WIN
};

struct GameRecord
{
    std::string start_fen;
    std::vector<Move> moves;
    std::vector<int> white_scores;
    GameResult result = DRAW;

    // https://github.com/cosmobobak/viriformat
    void serialize_viri(std::ostream& out) const;
};

struct ViriFileContents
{
    int64_t games = 0;
    int64_t positions = 0;
};

ViriFileContents keep_complete_viri_games(const std::string& path);

#endif
