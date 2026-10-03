#include "test.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "timestrat.h"
#include "util.h"

class TestResultReceiver : public ISearchReceiver
{
  public:
    TestResultReceiver()
        : best_move(Move::make_none())
    {
    }

    void receive_search_result(const SearchResult& result) override
    {
        if (result.type() == BEST_RESULT)
            best_move = result.pv().len() > 0 ? result.pv().first() : Move::make_none();
    }

    inline Move move() const { return best_move; }

  private:
    Move best_move;
};

Tester::Tester()
{
}

size_t Tester::start_test(const TestSuite& suite, SearchMaster& searcher, const SearchConditions& conditions)
{
    Board board;

    TestResultReceiver res;

    size_t success_count = 0;

    log_sync("\ntestsuite " + suite.name() + "\n\n");

    for (const TestPosition& p : suite) {

        // Print test info
        log_sync("test    " + p.name() + "\n");
        if (!p.comment().empty())
            log_sync("comment " + p.comment() + "\n");
        log_sync("fen     " + p.fen() + "\n");

        board.set_fen(p.fen());
        searcher.ttable.clear();
        searcher.clear_history();

        // Run the test
        searcher.go(res, board, conditions);
        searcher.wait_for();

        std::ostringstream out;

        const bool success = p.is_solved_by(res.move());

        out << "best    ";
        for (Move m : p.best_moves())
            out << m.uci_notation() << " ";

        out << "\navoid   ";
        for (Move m : p.avoid_moves())
            out << m.uci_notation() << " ";

        out << "\n";

        success_count += success;

        // Print result
        out << "actual  " << res.move().uci_notation() << "\n"
            << "result  " << (success ? "success" : "failure") << "\n\n";

        log_sync(out.str());
    }

    log_sync("total   " + std::to_string(success_count) + "/" + std::to_string(suite.size()) + "\n");
    return success_count;
}

bool is_opcode(const std::string& token)
{
    return token == "bm" ||
           token == "am" ||
           token == "c0" ||
           token == "id";
}

TestSuite TestSuite::from_file(const std::string& path)
{
    TestSuite suite(path);

    std::ifstream fs(path);
    assert(!fs.fail());

    Board board;

    // This parser is not very robust. For example if an opcode is present in quotation
    // (for example in a comment), it is interpreted as an actual opcode. And all commas
    // and semicolons are erased -- even in names and comments -- to make it easier to parse.

    std::string line, token;
    while (getline(fs, line)) {

        if (line.empty())
            continue;

        for (char c : { ';', ',', '\"' })
            line.erase(std::remove(line.begin(), line.end(), c), line.end());

        std::istringstream ss(line);

        // Get the FEN first
        std::string fen;
        while (ss >> token && !is_opcode(token))
            fen += token + " ";

        board.set_fen(fen);

        // Then parse the opcodes
        std::string id, c0;
        std::vector<Move> bests, avoids;

        std::string op;
        do {

            if (is_opcode(token)) {
                op = token;
                continue;
            }

            if (op == "bm")
                bests.push_back(Move::from_alg_notation(board, token));
            else if (op == "am")
                avoids.push_back(Move::from_alg_notation(board, token));
            else if (op == "id")
                id += (id.empty() ? "" : " ") + token;
            else if (op == "c0")
                c0 += (c0.empty() ? "" : " ") + token;
            else
                assert(0);

        } while (ss >> token);

        suite.add(TestPosition(id, board.fen(), bests, avoids, c0));
    }

    return suite;
}
