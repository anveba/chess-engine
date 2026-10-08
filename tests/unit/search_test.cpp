#include <algorithm>

#include "bench.h"
#include "search.h"
#include "test.h"
#include "testing.h"

constexpr size_t TEST_TABLE_SIZE_MB = 16;

struct Found
{
    Move move = Move::make_none();
    uint64_t nodes = 0;
};

class CaptureReceiver : public ISearchReceiver
{
  public:
    void receive_search_result(const SearchResult& result) override
    {
        if (result.type() != BEST_RESULT)
            return;
        found.move = result.pv().len() > 0 ? result.pv().first() : Move::make_none();
        found.nodes = result.nodes();
    }

    Found found;
};

static Found search_nodes(SearchMaster& searcher, Board& board, uint64_t nodes)
{
    SearchConditions conditions;
    conditions.nodes = nodes;
    CaptureReceiver receiver;
    searcher.go(receiver, board, conditions);
    searcher.wait_for();
    return receiver.found;
}

static void reset(SearchMaster& searcher)
{
    searcher.ttable.clear();
    searcher.clear_history();
}

TEST(node_limited_search_is_deterministic)
{
    SearchMaster searcher(1, TEST_TABLE_SIZE_MB);
    Board board;
    board.set_fen("r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 1");

    constexpr uint64_t NODES = 50000;

    reset(searcher);
    Found first = search_nodes(searcher, board, NODES);
    reset(searcher);
    Found second = search_nodes(searcher, board, NODES);

    CHECK(first.nodes >= NODES);
    CHECK(first.nodes < NODES + 1000);
    CHECK_EQ(first.nodes, second.nodes);
    CHECK_EQ(first.move.uci_notation(), second.move.uci_notation());
}

TEST(bench_is_deterministic)
{
    SearchMaster searcher(1, TEST_TABLE_SIZE_MB);
    const uint64_t first = run_bench(searcher, false).nodes;
    const uint64_t second = run_bench(searcher, false).nodes;
    CHECK(first > 0);
    CHECK_EQ(first, second);
}

// Skill regression tests. A fixed node budget rather than depth.
static size_t solve_suite(const std::string& file, uint64_t nodes, size_t positions)
{
    TestSuite suite = TestSuite::from_file(testing::data_path(file));
    CHECK_CTX(suite.size() >= positions, file);

    SearchMaster searcher(1, TEST_TABLE_SIZE_MB);
    Board board;
    size_t solved = 0;
    for (size_t i = 0; i < std::min(positions, suite.size()); i++) {
        const TestPosition& p = *(suite.begin() + i);
        board.set_fen(p.fen());
        reset(searcher);
        solved += p.is_solved_by(search_nodes(searcher, board, nodes).move);
    }
    std::cout << "  solved " << solved << "/" << positions << " of " << file << " at " << nodes << " nodes" << std::endl;
    return solved;
}

TEST(win_at_chess_subset)
{
    CHECK(solve_suite("wac.txt", 50000, 100) >= 82);
}

TEST(bratko_kopec)
{
    CHECK(solve_suite("bk.txt", 200000, 24) >= 10);
}

TEST(eigenmann_rapid_engine_test)
{
    CHECK(solve_suite("eret.txt", 100000, 111) >= 7);
}
