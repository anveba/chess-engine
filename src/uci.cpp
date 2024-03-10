#include "uci.h"

#include <cmath>
#include <iostream>

#include "perft.h"
#include "test.h"
#include "util.h"

UCI::UCI()
    : searcher(1)
{
    board.set_fen(START_FEN);
}

void UCI::start()
{
    std::ostringstream out;

    while (true) {
        out.str(std::string());

        std::string line, token;
        if (getline(std::cin, line).eof())
            break;

        std::istringstream ss(line);
        ss >> token;

        if (!searcher.is_searching()) {

            if (token == "uci") {
                out << "id name Chrunch\n"
                    << "id author anveba\n"
                    << "uciok" << std::endl;

            } else if (token == "position") {
                position(ss);

            } else if (token == "setoption") {
                // TODO

            } else if (token == "ucinewgame") {
                // TODO

            } else if (token == "isready") {
                out << "readyok" << std::endl;

            } else if (token == "go") {
                go(ss);

            } else if (token == "d") {
                out << board.as_image_str() << std::endl;

            } else if (token == "perft") {
                start_perft(ss);

            } else if (token == "test") {
                start_test(ss);

            } else if (token == "help") {
                out << "This chess engine uses the Universal Chess Interface (UCI).\n"
                    << "Please refer to it for further information. Nonstandard commands\n"
                    << "include:\n\n"
                    << "    perft <depth>\n"
                    << "    test <test-file> <secs-per-test>\n"
                    << std::endl;

            } else if (token == "quit" || token == "exit") {
                break;
            } else if (token.size() > 0 && token != "stop" && token != "ponderhit") {
                out << "Unknown command. Type \'help\' for more information." << std::endl;
            }

        } else {

            if (token == "stop") {
                searcher.stop();
            } else if (token == "ponderhit") {
                searcher.realise_ponder();
            }
        }

        log_sync(out.str());
    }

    searcher.stop();
    searcher.wait_for();
}

void UCI::position(std::istringstream& in)
{
    std::string token;
    in >> token;
    std::string fen;
    std::vector<Move> moves;

    if (token == "startpos") {
        fen = START_FEN;
        in >> token;

    } else if (token == "fen") {
        fen.clear();
        while (in >> token && token != "moves")
            fen += token + " ";
    } else {
        log_sync("Unknown arguments.\n");
        return;
    }

    board.set_fen(fen);

    if (token == "moves") {

        BoardMemory memory;

        while (in >> token)
            board.make_move(Move::from_uci_notation(board, token), memory);

        assert(board.is_valid());

        board.set_fen(board.fen());
    }
}

void UCI::go(std::istringstream& in)
{
    assert(!searcher.is_searching());

    constexpr float inf = std::numeric_limits<float>().infinity();

    int search_depth = MAX_DEPTH;
    float search_time = inf;

    bool is_ponder = false;
    float wtime = 0.0f, btime = 0.0f, winc = 0.0f, binc = 0.0f;
    int moves_to_go = 0;

    std::string token;
    while (in >> token) {

        if (token == "depth" || token == "mate") {
            in >> search_depth;

            if (search_depth >= MAX_DEPTH)
                search_depth = MAX_DEPTH;

        } else if (token == "infinite") {
            search_depth = MAX_DEPTH;

        } else if (token == "ponder") {
            is_ponder = true;

        } else if (token == "movetime") {
            in >> search_time;
            search_time /= 1000.0f;

        } else if (token == "movestogo") {
            in >> moves_to_go;

        } else if (token == "wtime") {
            in >> wtime;
            wtime /= 1000.0f;

        } else if (token == "btime") {
            in >> btime;
            btime /= 1000.0f;

        } else if (token == "winc") {
            in >> winc;
            winc /= 1000.0f;

        } else if (token == "binc") {
            in >> binc;
            binc /= 1000.0f;

        } else {
            log_sync("Unknown arguments.\n");
            return;
        }
    }

    if (board.side() == BLACK) {
        std::swap(wtime, btime);
        std::swap(winc, binc);
    }

    if (wtime > 0.0f) {
        search_time = std::min(search_time, TimeStrategy::one_twentieth(wtime, btime, winc, binc, moves_to_go));
    }

    assert(search_time > 0.0f);

    searcher.go(search_receiver, board, search_depth, search_time, is_ponder);
}

void UCI::start_perft(std::istringstream& in)
{
    std::string token;
    in >> token;

    std::ostringstream out;

    PerftResults results = perft(board, std::stoi(token));

    out << "\nNumber of nodes: " << results.nodes << "\n\n"
        << "Time taken: " << results.seconds << " s\n"
        << "Nodes per second: " << uint64_t(double(results.nodes) / results.seconds)
        << std::endl;

    log_sync(out.str());
}

void UCI::start_test(std::istringstream& in)
{
    std::string token;
    float search_time;
    in >> token >> search_time;

    TestSuite suite = TestSuite::from_file(token);

    Tester tester;
    tester.start_test(suite, search_time);
}

void UCISearchReceiver::receive_search_result(const SearchResult& result)
{
    std::ostringstream out;

    if (result.type() == FINAL_BEST) {

        out << "bestmove " << result.pv().first().uci_notation();

        if (result.pv().len() > 1)
            out << " ponder " << result.pv().second().uci_notation();

        out << std::endl;

    } else if (result.type() == CURRENT_BEST) {

        out << "info currmove " << result.pv().first().uci_notation()
            << " depth " << result.pv().len()
            << " score cp " << result.evaluation()
            << " pv " << result.pv().to_string()
            << std::endl;

    } else {
        out << "no result" << std::endl;
    }

    log_sync(out.str());
}