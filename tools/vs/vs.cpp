#include <chrono>
#include <cmath>
#include <iostream>

#include "fenutil.h"
#include "match.h"

int main(int argc, char** argv)
{
    if (argc != 7) {
        std::cout << "Usage\n    " << argv[0] << " infen threads p1name p1expath p2name p2expath\n\n"
                  << "infen:    path to file with list of FEN positions to play\n"
                  << "threads:  the number of matches to play concurrently (zero for automatic)\n"
                  << "p1name:   name of first player\n"
                  << "p1expath: path to first player's executable\n"
                  << "p2name:   name of second player\n"
                  << "p2expath: path to second player's executable" << std::endl;
        exit(1);
    }

    size_t concurrency;
    try {
        concurrency = std::stoull(argv[2]);
    } catch (const std::exception& e) {
        std::cout << "Thread count was not a non-negative integer" << std::endl;
        exit(1);
    }

    precompute_bitboards();
    precompute_board_constants();

    constexpr size_t max_halfmoves = 400;
    constexpr size_t ms_per_move = 100;

    PlayerParams players[2];
    players[0].name = argv[3];
    players[0].executable_path = argv[4];
    players[1].name = argv[5];
    players[1].executable_path = argv[6];

    std::vector<std::string> fens;
    load_fen_strings(fens, argv[1]);

    // Construct match parameters
    std::vector<Match> matches;
    matches.reserve(fens.size() * 2);

    for (const std::string& fen : fens) {
        for (bool first_is_white : { true, false }) {
            matches.emplace_back();
            MatchParams& params = matches.back().params;
            params.fen = fen;
            params.first_is_white = first_is_white;
            params.max_halfmoves = max_halfmoves;
            params.ms_per_move = ms_per_move;
        }
    }

    auto start_time = std::chrono::steady_clock::now();

    play_matches(&matches[0], matches.size(), players, concurrency);

    float elapsed_seconds = std::chrono::duration_cast<std::chrono::duration<float, std::ratio<1>>>(
                                std::chrono::steady_clock::now() - start_time)
                                .count();

    size_t p1_wins_as_white = 0, p2_wins_as_white = 0, p1_wins_as_black = 0, p2_wins_as_black = 0;
    size_t draws_by_rep = 0, draws_by_stalemate = 0, draws_by_fifty_move_rule = 0, ongoing = 0;

    for (const Match& match : matches) {
        const MatchParams& params = match.params;
        const MatchResult& result = match.result;

        if (result.status == WHITE_WIN) {
            p1_wins_as_white += params.first_is_white ? 1 : 0;
            p2_wins_as_white += params.first_is_white ? 0 : 1;
        } else if (result.status == BLACK_WIN) {
            p1_wins_as_black += params.first_is_white ? 0 : 1;
            p2_wins_as_black += params.first_is_white ? 1 : 0;
        } else if (result.status == ONGOING) {
            ongoing++;
        } else if (result.status == DRAW_BY_REPETITION) {
            draws_by_rep++;
        } else if (result.status == DRAW_BY_STALEMATE) {
            draws_by_stalemate++;
        } else if (result.status == DRAW_BY_FIFTY_MOVE_RULE) {
            draws_by_fifty_move_rule++;
        }
    }
    assert(matches.size() == p1_wins_as_white + p1_wins_as_black + p2_wins_as_white + p2_wins_as_black +
                                 draws_by_stalemate + draws_by_rep + draws_by_fifty_move_rule + ongoing);

    std::cout << "\n"
              << players[0].name << " VS " << players[1].name << " results\n"
              << players[0].name << " wins: " << p1_wins_as_white + p1_wins_as_black
              << "\n  as white: " << p1_wins_as_white
              << "\n  as black: " << p1_wins_as_black
              << "\n"
              << players[1].name << " wins: " << p2_wins_as_white + p2_wins_as_black
              << "\n  as white: " << p2_wins_as_white
              << "\n  as black: " << p2_wins_as_black
              << "\nDraws: " << draws_by_stalemate + draws_by_rep + draws_by_fifty_move_rule
              << "\n  by stalemate:       " << draws_by_stalemate
              << "\n  by repetition:      " << draws_by_rep
              << "\n  by fifty move rule: " << draws_by_fifty_move_rule
              << "\nUndecided: " << ongoing
              << "\nTotal: " << matches.size()
              << "\nTime taken: " << std::floor(elapsed_seconds / 60.0f) << " minutes "
              << elapsed_seconds - std::floor(elapsed_seconds / 60.0f) * 60.0f << " seconds" << std::endl;
}