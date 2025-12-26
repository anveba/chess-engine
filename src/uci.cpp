#include "uci.h"

#include <iostream>

#include "perft.h"
#include "test.h"
#include "util.h"

constexpr int64_t DEFAULT_THREAD_COUNT = 1;
constexpr int64_t DEFAULT_TABLE_SIZE = 64;

UCI::UCI()
    : searcher(DEFAULT_THREAD_COUNT, DEFAULT_TABLE_SIZE)
{
    board.set_fen(START_FEN);

    UCIOption thread_option("threads", SPIN_OPTION, std::to_string(DEFAULT_THREAD_COUNT));
    thread_option.set_int_bounds(0, MAX_WORKERS - 1);
    thread_option.set_on_change_callback([&](UCIOption* opt) { searcher.set_worker_count(opt->get_int()); });
    options.push_back(thread_option);

    UCIOption memory_option("table size mb", SPIN_OPTION, std::to_string(DEFAULT_TABLE_SIZE));
    memory_option.set_int_bounds(1, 1LL << 48);
    memory_option.set_on_change_callback([&](UCIOption* opt) { searcher.ttable.resize(opt->get_int()); });
    options.push_back(memory_option);
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

    board_memories.clear();

    if (token == "moves") {
        while (in >> token) {
            board_memories.emplace_back();
            board.make_move(Move::from_uci_notation(board, token), board_memories.back());
        }
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

static std::string to_lower(const std::string& str)
{
    std::string result = str;
    for (auto& c : result)
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
        if (opt.get_name() == name) {
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
    tester.start_test(suite, searcher, search_time);
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
            if (value.empty())
                break;
            string_value = value;
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
        value_str = string_value;
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

    return "option name " + name + " type " + type_string + " value " + value_str + " " + more_str;
}

void UCISearchReceiver::receive_search_result(const SearchResult& result)
{
    std::ostringstream out;

    if (result.type() == BEST_RESULT) {

        out << "bestmove " << (result.pv().len() > 0 ? result.pv().first().uci_notation() : "(none)");

        if (result.pv().len() > 1)
            out << " ponder " << result.pv().second().uci_notation();

        out << std::endl;

    } else if (result.type() == INFO_RESULT) {
        out << "info";
        if (result.pv().len() > 0)
            out << " currmove " << result.pv().first().uci_notation();
        out << " depth " << result.depth()
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