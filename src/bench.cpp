#include "bench.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <string>
#include <vector>

#include "eval.h"
#include "timestrat.h"
#include "util.h"

constexpr int EVALS_PER_RUN = 1000;

// clang-format off
static const char* const BENCH_FENS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bqkb1r/pppnpp1p/5np1/3N4/2B5/5Q2/PPPP1PPP/R1B1K1NR w KQkq - 4 7",
    "rnbqkbnr/pppp1ppp/8/8/3pP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 3",
    "2kr2nr/1ppb4/p2p1p1b/4qNp1/4P3/5Q2/PPPBN1PP/R4RK1 w - - 4 17",
    "r1bq1rk1/ppp2ppp/5n2/4Q3/8/2PBB2P/P1P2PP1/R3K2R b KQ - 0 12",
    "r2qkbnr/ppp2ppp/4p3/3p1b2/3P4/3PPN1P/PP3PP1/RNBQK2R w KQkq - 0 7",
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R b KQkq - 5 4",
    "r1bq1rk1/pppnbppp/3p1n2/4p3/2BPP3/2P2N2/PP3PPP/RNBQR1K1 b - - 0 7",
    "r1b1k2r/p1q1bppp/2pppn2/6B1/4P3/2NQ4/PPP2PPP/R3KB1R w KQkq - 2 10",
    "r1bqk1r1/1p1p1n2/p1n2pN1/2p1b2Q/2P1Pp2/1PN5/PB4PP/R4RK1 w q - 0 1",
    "r1n2N1k/2n2K1p/3pp3/5Pp1/b5R1/8/1PPP4/8 w - - 0 1",
    "r1b1r1k1/1pqn1pbp/p2pp1p1/P7/1n1NPP1Q/2NBBR2/1PP3PP/R6K w - - 0 1",
    "2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1",
    "6k1/5p2/8/3P2P1/1p6/8/8/6K1 w - - 0 1",
    "8/8/3k4/8/8/8/2R5/4K3 w - - 0 1",
    "8/8/8/4k3/8/8/8/R3K2R w - - 0 1",
    "8/5k2/8/3P4/8/8/5K2/8 w - - 0 1",
    "8/8/3kr3/8/8/8/2B5/2R1K3 w - - 0 1",
    "4k3/pp6/8/8/8/8/6PP/4K3 w - - 0 1",
    "2r3k1/1p3ppp/p2P4/8/1P6/P4N2/5PPP/3R2K1 b - - 0 1",
    "r1b2rk1/pp3ppp/2n5/q2p4/3P4/2PBB3/P1Q2PPP/R4RK1 w - - 0 1",
    "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 1",
    "8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - - 0 1",
    "4r1k1/5pp1/7p/8/2Q5/6P1/5P1P/6K1 b - - 0 1",
    "3r2k1/pp3ppp/8/8/8/8/PP3PPP/3R2K1 w - - 0 1",
};
// clang-format on

class BenchReceiver : public ISearchReceiver
{
  public:
    void receive_search_result(const SearchResult& result) override
    {
        if (result.type() == BEST_RESULT)
            nodes = result.nodes();
    }

    uint64_t nodes = 0;
};

BenchResult run_bench(SearchMaster& searcher, int depth, bool verbose)
{
    BenchResult total;
    Board board;
    BenchReceiver receiver;

    SearchConditions conditions;
    conditions.depth = depth;

    const Ms start = now();
    int index = 0;
    for (const char* fen : BENCH_FENS) {
        board.set_fen(fen);
        searcher.ttable.clear();
        searcher.clear_history();
        searcher.go(receiver, board, conditions);
        searcher.wait_for();

        total.nodes += receiver.nodes;
        if (verbose)
            log_sync("Position " + std::to_string(++index) + ": " + std::to_string(receiver.nodes) + " nodes\n");
    }
    total.time_ms = now() - start;
    return total;
}

uint64_t time_evaluation_ns()
{
    std::deque<Board> boards(std::size(BENCH_FENS));
    for (size_t i = 0; i < boards.size(); i++)
        boards[i].set_fen(BENCH_FENS[i]);

    volatile int sink = 0;
    double total_ns = 0;
    for (Board& board : boards) {
        std::vector<double> run_ns;
        for (int run = 0; run < EVAL_TIMING_RUNS; run++) {
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < EVALS_PER_RUN; i++)
                sink = sink + evaluate(board);
            const std::chrono::duration<double, std::nano> elapsed = std::chrono::steady_clock::now() - start;
            run_ns.push_back(elapsed.count() / EVALS_PER_RUN);
        }
        std::nth_element(run_ns.begin(), run_ns.begin() + run_ns.size() / 2, run_ns.end());
        total_ns += run_ns[run_ns.size() / 2];
    }
    return uint64_t(total_ns);
}
