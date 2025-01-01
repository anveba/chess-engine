#include "match.h"

#include <algorithm>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

#include "movegen.h"
#include "util.h"

static MatchStatus get_match_status(const Board& board)
{
    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);
    if (board.checkers() && moves.size() == 0) {
        return board.side() == WHITE ? BLACK_WIN : WHITE_WIN;
    } else if (!board.checkers() && moves.size() == 0) {
        return DRAW_BY_STALEMATE;
    } else if (board.is_draw_by_fifty_move()) {
        return DRAW_BY_FIFTY_MOVE_RULE;
    } else if (board.is_draw_by_repetition()) {
        return DRAW_BY_REPETITION;
    } else {
        return ONGOING;
    }
}

static void play_match(Match& match, ChessInstance players[2])
{
    Board board;
    board.set_fen(match.params.fen);
    match.result.status = get_match_status(board);

    players[0].indicate_new_game();
    players[1].indicate_new_game();

    std::deque<BoardMemory> memories;
    std::vector<Move>& moves = match.result.moves;
    moves.clear();

    ChessInstance* order[2];
    if ((board.side() == WHITE && match.params.first_is_white) ||
        (board.side() == BLACK && !match.params.first_is_white)) {

        order[0] = players + 0;
        order[1] = players + 1;
    } else {
        order[0] = players + 1;
        order[1] = players + 0;
    }

    match.result.halfmoves = 0;
    while (1) {

        for (ChessInstance* inst : order) {
            inst->set_board(match.params.fen, moves);
            inst->wait_for_ready();
            inst->start_search();
            usleep(match.params.ms_per_move * 1000);
            InstanceMoveResponse search_result;
            inst->get_search_result(board, search_result);
            if (!search_result.move.is_legal(board)) {
                log_sync(inst->get_name() + " made an illegal move " +
                         search_result.move.uci_notation() + ".\n" + board.as_image_str());
                match.result.status = board.side() == WHITE ? BLACK_WIN : WHITE_WIN;
                break;
            }

            moves.push_back(search_result.move);
            memories.emplace_back();
            board.make_move(search_result.move, memories.back());
            match.result.halfmoves++;
            match.result.status = get_match_status(board);

            // log_sync("player " + inst->get_name() + " move " + search_result.move.uci_notation() +
            //          " depth " + std::to_string(search_result.depth) +
            //          " score " + (search_result.mate_in ? "mate " : "cp ") +
            //          std::to_string(search_result.evaluation) + "\n");

            if (match.result.status != ONGOING || match.result.halfmoves > match.params.max_halfmoves)
                break;
        }
        if (match.result.status != ONGOING || match.result.halfmoves > match.params.max_halfmoves)
            break;
    }

    std::string log = "\nMatch FEN:\n  " + match.params.fen + "\nwith " +
                      players[match.params.first_is_white ? 0 : 1].get_name() + " as white. Result:\n  ";
    if (match.result.status == WHITE_WIN) {
        log += (players[match.params.first_is_white ? 0 : 1].get_name() + " won as white.\n");
    } else if (match.result.status == BLACK_WIN) {
        log += (players[match.params.first_is_white ? 1 : 0].get_name() + " won as black.\n");
    } else if (match.result.status == ONGOING) {
        log += "Undecided outcome.\n";
    } else if (match.result.status == DRAW_BY_REPETITION) {
        log += "Draw by repetition.\n";
    } else if (match.result.status == DRAW_BY_FIFTY_MOVE_RULE) {
        log += "Draw by fifty move rule.\n";
    } else if (match.result.status == DRAW_BY_STALEMATE) {
        log += "Draw by stalemate.\n";
    } else {
        log += "Unknown outcome.\n";
        assert(0);
    }
    log += "Moves made:\n  " + board.move_history_str() + "\n";
    log_sync(log);
}

// Setting the concurrent match count to zero means that the concurrency is found automatically
// based on the number of cores available.
void play_matches(Match* matches, size_t match_count, PlayerParams players[2], size_t concurrent_matches)
{
    assert(match_count > 0);

    constexpr size_t max_threads = 128;
    std::thread threads[max_threads];

    if (concurrent_matches == 0)
        concurrent_matches = std::thread::hardware_concurrency();
    concurrent_matches = std::min(std::min(concurrent_matches, max_threads), match_count);

    std::cout << "The total match count is " << match_count << ". Running "
              << concurrent_matches << " matches concurrently." << std::endl;

    size_t next_match_index = 0;
    std::mutex lock;

    for (size_t i = 0; i < concurrent_matches; i++) {

        threads[i] = std::thread([&] {
            lock.lock();
            ChessInstance instances[2] = { ChessInstance(players[0].name, players[0].executable_path),
                                           ChessInstance(players[1].name, players[1].executable_path) };

            assert(instances[0].is_running());
            assert(instances[1].is_running());
            lock.unlock();

            for (int i = 0; i < 2; i++)
                for (const ChessOption& opt : players[i].options)
                    instances[i].set_option(opt.name, opt.value);

            while (1) {
                lock.lock();
                if (next_match_index < match_count) {

                    Match& match = matches[next_match_index++];
                    lock.unlock();

                    play_match(match, instances);

                } else {
                    lock.unlock();
                    break;
                }
            }
        });
    }

    for (size_t i = 0; i < concurrent_matches; i++)
        threads[i].join();
}