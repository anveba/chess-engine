#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <unistd.h>

#include "cinstance.h"

static void save_fen_strings(const std::vector<std::string>& strings, const std::string& path)
{
    std::ofstream file(path);
    for (const std::string& fen : strings)
        file << fen << "\n";
}

// Expects a text file with a list of fens with an empty line indicating the start of a new game.
static void load_random_fen_for_each_game(std::vector<std::string>& strings, const std::string& path, size_t max_games)
{
    std::ifstream file(path);
    std::string line;

    std::random_device rd;
    std::mt19937 rng(rd());

    std::vector<std::string> game_fens;

    while (std::getline(file, line)) {
        if (line.empty()) {
            if (game_fens.size() > 1) {
                // Final position might have no possible moves, so we skip it.
                std::uniform_int_distribution<int> dist(0, game_fens.size() - 2);
                strings.push_back(game_fens.at(dist(rng)));
                if (strings.size() >= max_games)
                    break;
            }
            game_fens.clear();
            continue;
        }
        game_fens.push_back(line);
    }
}

static void filter_fen_strings(const std::vector<std::string>& source,
                               std::vector<std::string>& dest,
                               ChessInstance& evaluator,
                               size_t eval_time_us,
                               size_t max_abs_score_cp)
{
    Board board;
    const std::vector<Move> no_moves;

    for (const std::string& fen : source) {
        // This is a slow duplicate check, but it does not seem to be a performance bottleneck.
        if (std::find(dest.begin(), dest.end(), fen) != dest.end()) {
            std::cout << "duplicate: " << fen << std::endl;
            continue;
        }
        std::cout << "processing: " << fen << std::endl;
        board.set_fen(fen);
        evaluator.set_board(fen, no_moves);
        evaluator.wait_for_ready();
        evaluator.start_search();
        usleep(eval_time_us);
        InstanceMoveResponse result;
        evaluator.get_search_result(board, result);
        if (abs(result.evaluation) <= max_abs_score_cp && !result.mate_in) {
            std::cout << "included, cp score " << result.evaluation << ", depth "
                      << result.depth << ": " << fen << std::endl;
            dest.push_back(fen);
        } else {
            std::cout << "excluded, cp score " << result.evaluation << ", depth "
                      << result.depth << ": " << fen << std::endl;
        }
    }
}

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cout << "Expected path to evaluator, input fen, and output fen paths.\n"
                  << "Usage:\n    " << argv[0] << " eval infen outfen" << std::endl;
        exit(1);
    }
    precompute_bitboards();
    precompute_board_constants();

    // TODO: remove magic numbers

    std::vector<std::string> fen_raw, fen_filtered;
    load_random_fen_for_each_game(fen_raw, argv[2], 10000);

    ChessInstance evaluator(argv[1], argv[1]);

    filter_fen_strings(fen_raw, fen_filtered, evaluator, 300000, 20);

    save_fen_strings(fen_filtered, argv[3]);
}