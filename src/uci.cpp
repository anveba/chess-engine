#include "uci.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

#include "bench.h"
#include "moveorder.h"
#include "perft.h"
#include "test.h"
#include "util.h"

constexpr int64_t DEFAULT_THREAD_COUNT = 1;
constexpr int64_t DEFAULT_TABLE_SIZE = 64;
constexpr int64_t MAX_TABLE_SIZE = 33554432;

static const std::string EMPTY_STRING_VALUE = "<empty>";

UCI::UCI()
    : searcher(DEFAULT_THREAD_COUNT, DEFAULT_TABLE_SIZE)
{
    board.set_fen(START_FEN);

    UCIOption thread_option("Threads", SPIN_OPTION, std::to_string(DEFAULT_THREAD_COUNT));
    thread_option.set_int_bounds(0, MAX_WORKERS - 1);
    thread_option.set_on_change_callback([&](UCIOption* opt) { searcher.set_worker_count(opt->get_int()); });
    options.push_back(thread_option);

    UCIOption memory_option("Hash", SPIN_OPTION, std::to_string(DEFAULT_TABLE_SIZE));
    memory_option.set_int_bounds(1, MAX_TABLE_SIZE);
    memory_option.set_on_change_callback([&](UCIOption* opt) {
        if (!searcher.ttable.resize(opt->get_int()))
            log_sync("info string Could not allocate the hash table, so the previous one is kept\n");
    });
    options.push_back(memory_option);

    UCIOption overhead_option("Move Overhead", SPIN_OPTION, std::to_string(DEFAULT_MOVE_OVERHEAD_MS));
    overhead_option.set_int_bounds(0, 10000);
    overhead_option.set_on_change_callback([&](UCIOption* opt) { searcher.set_move_overhead(opt->get_int()); });
    options.push_back(overhead_option);

    UCIOption log_option("Debug Log File", STRING_OPTION, "");
    log_option.set_on_change_callback([&](UCIOption* opt) { set_debug_log(opt->get_string()); });
    options.push_back(log_option);
}

void UCI::start()
{
    std::ostringstream out;

    while (true) {
        out.str(std::string());

        std::string line, token;
        if (getline(std::cin, line).eof())
            break;
        debug_log_input(line);

        std::istringstream ss(line);
        ss >> token;

        if (!searcher.is_searching()) {

            if (token == "uci") {
                out << "id name Chrunch\n"
                    << "id author anveba\n\n";

                for (const UCIOption& option : options)
                    out << option.uci_info() << "\n";

                out << "\nuciok" << std::endl;

            } else if (token == "position") {
                position(ss);

            } else if (token == "setoption") {
                set_option(ss);

            } else if (token == "ucinewgame") {
                new_game();

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

            } else if (token == "bench") {
                start_bench(ss);

            } else if (token == "eval") {
                out << eval_trace(board);

            } else if (token == "flip") {
                board.set_fen(board.mirrored_fen());
                board_memories.clear();

            } else if (token == "moves") {
                out << legal_moves_str();

            } else if (token == "key") {
                out << "Key: " << std::hex << std::setw(16) << std::setfill('0') << board.hash()
                    << " (recomputed " << std::setw(16) << board.make_full_hash() << ")" << std::dec << std::endl;

            } else if (token == "order") {
                out << move_order_str();

            } else if (token == "help") {
                out << "This chess engine uses the Universal Chess Interface (UCI).\n"
                    << "Please refer to it for further information. Nonstandard commands\n"
                    << "include:\n\n"
                    << "    d                               show the board\n"
                    << "    perft <depth>                   count leaf nodes\n"
                    << "    test <test-file> <ms-per-test>  run a test suite with a time limit\n"
                    << "    test <test-file> depth <depth>  run a test suite with a depth limit\n"
                    << "    bench [depth]                   search a fixed set of positions\n"
                    << "    eval                            show the evaluation broken down by term\n"
                    << "    flip                            mirror the position and swap colours\n"
                    << "    moves                           list legal moves\n"
                    << "    key                             show the position's hash\n"
                    << "    order                           show move ordering scores\n"
                    << "\nThe go command also accepts 'nodes <n>'.\n"
                    << std::endl;

            } else if (token == "quit" || token == "exit") {
                break;
            } else if (token.size() > 0 && token != "stop" && token != "ponderhit") {
                out << "Unknown command. Type \'help\' for more information." << std::endl;
            }

        } else {

            if (token == "stop") {
                searcher.stop();
                searcher.wait_for();
            } else if (token == "ponderhit") {
                searcher.realise_ponder();
            } else if (token == "isready") {
                out << "readyok" << std::endl;
            } else if (token == "quit" || token == "exit") {
                break;
            }
        }

        log_sync(out.str());
    }

    searcher.stop();
    searcher.wait_for();
}

static Move find_legal_move(const Board& board, const std::string& uci)
{
    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);
    for (Move move : moves)
        if (move.uci_notation() == uci)
            return move;
    return Move::make_none();
}

void UCI::position(std::istringstream& in)
{
    std::string token, fen;
    in >> token;

    if (token == "startpos") {
        fen = START_FEN;
        in >> token;
    } else if (token == "fen") {
        while (in >> token && token != "moves")
            fen += token + " ";
    } else {
        log_sync("info string Unknown position arguments\n");
        return;
    }

    // Checked on another board first to avoid corruption.
    Board check;
    if (!check.set_fen(fen)) {
        log_sync("info string Invalid FEN " + fen + "\n");
        return;
    }

    board.set_fen(fen);
    board_memories.clear();

    if (token == "moves") {
        while (in >> token) {
            const Move move = find_legal_move(board, token);
            if (move.is_none()) {
                log_sync("info string Illegal move " + token + ", so it and the moves after it are ignored\n");
                return;
            }
            board_memories.emplace_back();
            board.make_move(move, board_memories.back());
        }
    }
}

static bool looks_like_uci_move(const std::string& token)
{
    return (token.size() == 4 || token.size() == 5) && token[0] >= 'a' && token[0] <= 'h' && token[1] >= '1' &&
           token[1] <= '8' && token[2] >= 'a' && token[2] <= 'h' && token[3] >= '1' && token[3] <= '8';
}

void UCI::go(std::istringstream& in)
{
    assert(!searcher.is_searching());

    SearchConditions conditions;

    std::string token;
    bool skipping_moves = false;
    while (in >> token) {

        if (token == "depth") {
            in >> conditions.depth;
            conditions.depth = std::clamp(conditions.depth, 1, MAX_DEPTH);

        } else if (token == "mate") {
            // Searched without a depth limit, since reductions can hide a mate
            in >> conditions.mate_in;
            conditions.mate_in = std::max(conditions.mate_in, 0);

        } else if (token == "infinite") {
            conditions.infinite = true;

        } else if (token == "ponder") {
            conditions.ponder = true;

        } else if (token == "nodes") {
            in >> conditions.nodes;

        } else if (token == "movetime") {
            in >> conditions.move_time;

        } else if (token == "movestogo") {
            in >> conditions.moves_to_go;

        } else if (token == "wtime") {
            in >> conditions.wtime;
            conditions.clock_given = true;

        } else if (token == "btime") {
            in >> conditions.btime;
            conditions.clock_given = true;

        } else if (token == "winc") {
            in >> conditions.winc;

        } else if (token == "binc") {
            in >> conditions.binc;

        } else if (token == "searchmoves") {
            // Not supported
            log_sync("info string searchmoves is not supported\n");
            skipping_moves = true;

        } else if (skipping_moves && looks_like_uci_move(token)) {

        } else {
            log_sync("info string Unknown go argument " + token + "\n");
        }
    }

    searcher.go(search_receiver, board, conditions);
}

static std::string to_lower(const std::string& str)
{
    std::string result = str;
    for (char& c : result)
        c = tolower(c);
    return result;
}

void UCI::set_option(std::istringstream& in)
{
    std::string token, op, name, value;
    while (in >> token) {
        if (to_lower(token) == "name" || to_lower(token) == "value") {
            op = to_lower(token);
            continue;
        }

        if (op == "name")
            name += (name.empty() ? "" : " ") + token;
        else if (op == "value")
            value += (value.empty() ? "" : " ") + token;
        else
            assert(0);
    };

    bool found = false;
    for (UCIOption& opt : options) {
        if (to_lower(opt.get_name()) == to_lower(name)) {
            found = true;
            if (!opt.set(value))
                log_error_sync("Invalid value.\n");
            break;
        }
    }
    if (!found)
        log_error_sync("Unknown option.\n");
}

void UCI::new_game()
{
    searcher.ttable.clear();
    searcher.clear_history();
}

void UCI::start_perft(std::istringstream& in)
{
    std::string token;
    in >> token;

    int depth = 0;
    try {
        depth = std::stoi(token);
    } catch (const std::exception&) {
    }
    if (depth < 1) {
        log_sync("info string perft needs a depth of at least 1\n");
        return;
    }

    std::ostringstream out;

    PerftResults results = perft(board, depth);

    out << "\nNumber of nodes: " << results.nodes << "\n\n"
        << "Time taken: " << results.seconds << " s\n"
        << "Nodes per second: " << uint64_t(double(results.nodes) / results.seconds)
        << std::endl;

    log_sync(out.str());
}

void UCI::start_test(std::istringstream& in)
{
    std::string path, limit;
    in >> path >> limit;

    SearchConditions conditions;
    try {
        if (limit == "depth") {
            in >> limit;
            conditions.depth = std::min(std::stoi(limit), MAX_DEPTH);
        } else {
            conditions.move_time = std::stoull(limit);
        }
    } catch (const std::exception&) {
        log_sync("Unknown arguments.\n");
        return;
    }

    TestSuite suite = TestSuite::from_file(path);

    Tester tester;
    tester.start_test(suite, searcher, conditions);
}

void UCI::start_bench(std::istringstream& in)
{
    int depth = DEFAULT_BENCH_DEPTH;
    std::string token;
    if (in >> token) {
        try {
            depth = std::min(std::stoi(token), MAX_DEPTH);
        } catch (const std::exception&) {
            log_sync("Unknown arguments.\n");
            return;
        }
    }

    BenchResult result = run_bench(searcher, depth, true);

    std::ostringstream out;
    out << "\nDepth: " << depth
        << "\nNodes searched: " << result.nodes
        << "\nTime: " << result.time_ms << " ms"
        << "\nNodes per second: " << (result.time_ms ? result.nodes * 1000 / result.time_ms : 0) << std::endl;
    log_sync(out.str());
}

std::string UCI::legal_moves_str()
{
    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);

    std::string uci = "moves", san = "san";
    for (Move move : moves) {
        uci += " " + move.uci_notation();
        san += " " + move.san_notation(board);
    }
    return uci + "\n" + san + "\n";
}

std::string UCI::move_order_str()
{
    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);
    MovePicker picker(board, moves, Move::make_none());

    std::ostringstream out;
    for (Move move = picker.next(); !move.is_none(); move = picker.next())
        out << std::setw(6) << move.uci_notation() << " " << estimate(board, move) << "\n";
    return out.str();
}

UCIOption::UCIOption(const std::string& name, OptionType type, const std::string& default_value)
    : name(name)
    , type(type)
{
    assert(!name.empty());

    if (type == SPIN_OPTION) {
        spin_min = std::numeric_limits<int64_t>().min();
        spin_max = std::numeric_limits<int64_t>().max();
    }

    bool success = set(default_value);
    if (!success)
        log_error_sync("Failed to initialise value for option " + name + ".\n");
}

UCIOption::~UCIOption()
{
}

bool UCIOption::set(const std::string& value)
{
    bool success = false;
    switch (type) {
        case STRING_OPTION:
            string_value = value == EMPTY_STRING_VALUE ? "" : value;
            success = true;
            break;
        case SPIN_OPTION:
            int64_t int_value;
            try {
                int_value = std::stoi(value);
            } catch (const std::exception&) {
                break;
            }
            if (int_value < spin_min || int_value > spin_max)
                break;
            spin_value = int_value;
            success = true;
            break;
        case CHECK_OPTION:
            std::string lower_case = to_lower(value);
            if (lower_case == "true")
                check_value = true;
            else if (lower_case == "false")
                check_value = false;
            else
                break;
            success = true;
            break;
    }
    if (success && on_change)
        on_change(this);
    return success;
}

bool UCIOption::set_int_bounds(int64_t min, int64_t max)
{
    if (type != SPIN_OPTION)
        return false;
    spin_min = min;
    spin_max = max;
    return true;
}

const std::string& UCIOption::get_string() const
{
    assert(type == STRING_OPTION);
    return string_value;
}

uint64_t UCIOption::get_int() const
{
    assert(type == SPIN_OPTION);
    return spin_value;
}

bool UCIOption::get_bool() const
{
    assert(type == CHECK_OPTION);
    return check_value;
}

std::string UCIOption::uci_info() const
{
    std::string type_string, value_str, more_str;
    if (type == STRING_OPTION) {
        type_string += "string";
        value_str = string_value.empty() ? EMPTY_STRING_VALUE : string_value;
    } else if (type == SPIN_OPTION) {
        type_string += "spin";
        value_str = std::to_string(spin_value);
        more_str = "min " + std::to_string(spin_min) + " max " + std::to_string(spin_max);
    } else if (type == CHECK_OPTION) {
        type_string += "check";
        value_str = check_value ? "true" : "false";
    } else {
        assert(0);
        type_string = "unknown";
        value_str = "none";
    }

    return "option name " + name + " type " + type_string + " default " + value_str + (more_str.empty() ? "" : " " + more_str);
}

static std::string info_line(const SearchResult& result)
{
    std::ostringstream out;
    out << "info depth " << result.depth()
        << " seldepth " << result.sel_depth();

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
    return out.str();
}

void UCISearchReceiver::receive_search_result(const SearchResult& result)
{
    std::ostringstream out;

    if (result.type() == BEST_RESULT) {

        if (result.depth() > 0)
            out << info_line(result);

        out << "bestmove " << (result.pv().len() > 0 ? result.pv().first().uci_notation() : "(none)");

        if (result.pv().len() > 1)
            out << " ponder " << result.pv().second().uci_notation();

        out << std::endl;

    } else if (result.type() == INFO_RESULT) {
        out << info_line(result);

    } else {
        out << "no result" << std::endl;
    }

    log_sync(out.str());
}
