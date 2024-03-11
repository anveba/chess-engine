#include "uci.h"

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
                    << "    test <test-file> <ms-per-test>\n"
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

    SearchConditions conditions;

    std::string token;
    while (in >> token) {

        if (token == "depth" || token == "mate") {
            in >> conditions.depth;

            if (conditions.depth >= MAX_DEPTH)
                conditions.depth = MAX_DEPTH;

        } else if (token == "infinite") {

        } else if (token == "ponder") {
            conditions.ponder = true;

        } else if (token == "movetime") {
            in >> conditions.move_time;

        } else if (token == "movestogo") {
            in >> conditions.moves_to_go;

        } else if (token == "wtime") {
            in >> conditions.wtime;

        } else if (token == "btime") {
            in >> conditions.btime;

        } else if (token == "winc") {
            in >> conditions.winc;

        } else if (token == "binc") {
            in >> conditions.binc;

        } else {
            log_sync("Unknown arguments.\n");
            return;
        }
    }

    searcher.go(search_receiver, board, conditions);
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
    uint64_t search_time = 0;
    in >> token >> search_time;

    TestSuite suite = TestSuite::from_file(token);

    Tester tester;
    tester.start_test(suite, search_time);
}

void UCISearchReceiver::receive_search_result(const SearchResult& result)
{
    std::ostringstream out;

    if (result.type() == BEST_RESULT) {

        out << "bestmove " << result.pv().first().uci_notation();

        if (result.pv().len() > 1)
            out << " ponder " << result.pv().second().uci_notation();

        out << std::endl;

    } else if (result.type() == INFO_RESULT) {

        out << "info currmove " << result.pv().first().uci_notation()
            << " depth " << result.depth()
            << " seldepth " << result.pv().len();

        if (is_mate(result.evaluation()))
            out << " score mate " << sign(result.evaluation()) * (result.pv().len() + 1) / 2;
        else
            out << " score cp " << result.evaluation();

        out << " nodes " << result.nodes()
            << " time " << result.time_in_ms();

        if (result.time_in_ms() != 0)
            out << " nps " << ((result.nodes() * 1000) / result.time_in_ms());

        out << " pv " << result.pv().to_string()
            << std::endl;

    } else {
        out << "no result" << std::endl;
    }

    log_sync(out.str());
}