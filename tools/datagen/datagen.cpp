#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "board.h"
#include "movegen.h"
#include "search.h"
#include "viriformat.h"

constexpr size_t HASH_MB = 16;
constexpr uint64_t DEFAULT_NODES_PER_MOVE = 5000;

constexpr int MIN_RANDOM_OPENING_PLIES = 8;
constexpr int MAX_BALANCED_OPENING_SCORE = 1000;

constexpr int WIN_ADJUDICATION_SCORE = 2500, WIN_ADJUDICATION_PLIES = 4;
constexpr int DRAW_ADJUDICATION_SCORE = 10, DRAW_ADJUDICATION_PLIES = 12, DRAW_ADJUDICATION_MIN_PLY = 80;

constexpr int GAMES_PER_PROGRESS_REPORT = 100;

struct Settings
{
    std::string path;
    int threads;
    int64_t total_games;
    uint64_t nodes_per_move;
    uint64_t seed;
};

class BestMoveReceiver : public ISearchReceiver
{
  public:
    void receive_search_result(const SearchResult& result) override
    {
        if (result.type() == BEST_RESULT) {
            move = result.pv().len() > 0 ? result.pv().first() : Move::make_none();
            score = result.evaluation();
        }
    }

    Move move = Move::make_none();
    BoardEval score = 0;
};

class Adjudicator
{
  public:
    std::optional<GameResult> result_after(int ply, int white_score)
    {
        const bool decisive = std::abs(white_score) >= WIN_ADJUDICATION_SCORE;
        const bool same_winner_as_before = (white_score > 0) == (previous_white_score > 0);
        decisive_plies = decisive ? (same_winner_as_before ? decisive_plies + 1 : 1) : 0;
        previous_white_score = white_score;

        const bool drawish = ply >= DRAW_ADJUDICATION_MIN_PLY && std::abs(white_score) <= DRAW_ADJUDICATION_SCORE;
        drawish_plies = drawish ? drawish_plies + 1 : 0;

        if (decisive_plies >= WIN_ADJUDICATION_PLIES)
            return white_score > 0 ? WHITE_WIN : BLACK_WIN;
        if (drawish_plies >= DRAW_ADJUDICATION_PLIES)
            return DRAW;
        return std::nullopt;
    }

  private:
    int decisive_plies = 0, drawish_plies = 0, previous_white_score = 0;
};

static std::optional<GameResult> result_by_rules(Board& board)
{
    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);
    if (moves.size() == 0 && board.checkers())
        return board.side() == WHITE ? BLACK_WIN : WHITE_WIN;
    if (moves.size() == 0 || board.is_draw_by_fifty_move() || board.is_insufficient_material() ||
        board.is_draw_by_repetition())
        return DRAW;
    return std::nullopt;
}

static bool play_random_opening(Board& board, std::deque<BoardMemory>& stable_memories, std::mt19937_64& rng)
{
    board.set_fen(START_FEN);
    const int plies = MIN_RANDOM_OPENING_PLIES + rng() % 2;
    for (int i = 0; i < plies; i++) {
        if (result_by_rules(board))
            return false;
        MoveList moves;
        moves.generate<ALL_LEGAL_MOVES>(board);
        stable_memories.emplace_back();
        board.make_move(*(moves.begin() + rng() % moves.size()), stable_memories.back());
    }
    return !result_by_rules(board);
}

static std::optional<GameRecord> run_match(SearchMaster& searcher, std::mt19937_64& rng, uint64_t nodes_per_move)
{
    searcher.ttable.clear();
    searcher.clear_history();

    Board board;
    std::deque<BoardMemory> stable_memories;
    if (!play_random_opening(board, stable_memories, rng))
        return std::nullopt;

    GameRecord record;
    record.start_fen = board.fen();
    SearchConditions conditions;
    conditions.nodes = nodes_per_move;
    Adjudicator adjudicator;

    for (int ply = 0;; ply++) {
        if (std::optional<GameResult> result = result_by_rules(board)) {
            record.result = *result;
            return record;
        }

        BestMoveReceiver best;
        searcher.go(best, board, conditions);
        searcher.wait_for();
        if (best.move.is_none())
            return std::nullopt;

        const int white_score = board.side() == WHITE ? best.score : -best.score;
        const bool unbalanced_opening = ply == 0 && std::abs(white_score) > MAX_BALANCED_OPENING_SCORE;
        if (unbalanced_opening)
            return std::nullopt;

        record.moves.push_back(best.move);
        record.white_scores.push_back(white_score);
        stable_memories.emplace_back();
        board.make_move(best.move, stable_memories.back());

        if (std::optional<GameResult> result = adjudicator.result_after(ply, white_score)) {
            record.result = *result;
            return record;
        }
    }
}

static std::mt19937_64 rng_unique_to(uint64_t seed, int64_t games_already_written, int thread)
{
    std::seed_seq seeds{ uint32_t(seed), uint32_t(seed >> 32), uint32_t(games_already_written), uint32_t(thread) };
    return std::mt19937_64(seeds);
}

static void generate_games(const Settings& settings, const ViriFileContents& already_written)
{
    std::ofstream output(settings.path, std::ios::binary | std::ios::app);
    std::mutex output_mutex;
    std::atomic<int64_t> games_started = already_written.games;
    int64_t games_written = already_written.games, positions_written = already_written.positions, new_positions = 0;
    const auto start = std::chrono::steady_clock::now();

    auto play_games = [&](int thread) {
        SearchMaster searcher(1, HASH_MB);
        std::mt19937_64 rng = rng_unique_to(settings.seed, already_written.games, thread);

        while (games_started++ < settings.total_games) {
            std::optional<GameRecord> record;
            while (!record)
                record = run_match(searcher, rng, settings.nodes_per_move);

            std::lock_guard<std::mutex> lock(output_mutex);
            record->serialize_viri(output);
            output.flush();
            positions_written += record->moves.size();
            new_positions += record->moves.size();
            if (++games_written % GAMES_PER_PROGRESS_REPORT == 0) {
                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                std::cout << games_written << " games, " << positions_written << " positions, "
                          << int(new_positions / seconds) << " positions/s" << std::endl;
            }
        }
    };

    std::vector<std::thread> workers;
    for (int i = 0; i < settings.threads; i++)
        workers.emplace_back(play_games, i);
    for (std::thread& worker : workers)
        worker.join();

    std::cout << "Done: " << games_written << " games, " << positions_written << " positions" << std::endl;
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cerr << "Usage: datagen OUTPUT THREADS TOTAL_GAMES [NODES_PER_MOVE] [SEED]\n"
                  << "An existing OUTPUT is continued until it holds TOTAL_GAMES games.\n";
        return 1;
    }
    const Settings settings = {
        argv[1],
        std::stoi(argv[2]),
        std::stoll(argv[3]),
        argc > 4 ? std::stoull(argv[4]) : DEFAULT_NODES_PER_MOVE,
        argc > 5 ? std::stoull(argv[5]) : std::random_device()(),
    };

    precompute_bitboards();
    precompute_board_constants();

    const ViriFileContents already_written = keep_complete_viri_games(settings.path);
    std::cout << "Playing " << std::max<int64_t>(settings.total_games - already_written.games, 0) << " more games on "
              << settings.threads << " threads at " << settings.nodes_per_move << " nodes per move, seed "
              << settings.seed << std::endl;

    generate_games(settings, already_written);
}
