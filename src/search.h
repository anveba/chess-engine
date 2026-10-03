#ifndef SEARCH_H_INCLUDED
#define SEARCH_H_INCLUDED

#include <atomic>
#include <memory>
#include <thread>

#include "board.h"
#include "eval.h"
#include "moveorder.h"
#include "timestrat.h"
#include "ttable.h"

constexpr size_t MAX_WORKERS = 256;
constexpr Ms DEFAULT_MOVE_OVERHEAD_MS = 30;
constexpr int MAX_DEPTH = 255;
constexpr int MAX_PV_LENGTH = MAX_DEPTH + 32;

enum SearchNode
{
    PV_NODE,
    NON_PV_NODE
};

struct SearchConditions
{
    SearchConditions()
    {
        ponder = infinite = clock_given = false;
        wtime = btime = winc = binc = 0;
        moves_to_go = 0;
        move_time = 0;
        depth = MAX_DEPTH;
        nodes = 0;
        mate_in = 0;
    }

    uint64_t move_time;
    uint64_t wtime, btime, winc, binc;

    int depth, moves_to_go;
    uint64_t nodes; // Zero for no limit.
    int mate_in;    // Zero for none.

    bool ponder;
    bool infinite;    // Search until stopped.
    bool clock_given; // wtime or btime was given
};

class SearchWorker;
class SearchMaster;

class PVLine
{
  public:
    void copy(const PVLine& pv);

    constexpr int len() const { return length; }
    constexpr Move first() const
    {
        assert(length > 0);
        return moves[0];
    }

    constexpr Move second() const
    {
        assert(length > 1);
        return moves[1];
    }

    constexpr Move at(int i) const
    {
        assert(i < length);
        return moves[i];
    }

    inline void set_empty() { length = 0; }

    std::string to_string() const;

  private:
    friend SearchWorker;

    Move moves[MAX_PV_LENGTH];
    int length;
};

enum SearchResultType
{
    BEST_RESULT,
    INFO_RESULT
};

class SearchResult
{
  public:
    constexpr SearchResultType type() const { return result_type; }
    constexpr const PVLine& pv() const { return pv_line; }
    constexpr BoardEval evaluation() const { return eval; }
    constexpr uint64_t nodes() const { return nodes_searched; }
    constexpr uint64_t depth() const { return depth_searched; }
    constexpr int sel_depth() const { return sel_depth_reached; }
    constexpr uint64_t time_in_ms() const { return time_ms; }

  private:
    friend SearchWorker;
    friend SearchMaster;

    SearchResult(SearchResultType type);

    SearchResultType result_type;
    PVLine pv_line;
    int depth_searched;
    int sel_depth_reached;
    BoardEval eval;
    uint64_t nodes_searched;
    uint64_t time_ms;
};

class ISearchReceiver
{
  public:
    virtual void receive_search_result(const SearchResult& result) = 0;
};

struct SearchStats
{
    uint64_t tt_probes = 0, tt_hits = 0, tt_cutoffs = 0;
    uint64_t null_tries = 0, null_cutoffs = 0;
    uint64_t qnodes = 0;
    uint64_t beta_cutoffs = 0, first_move_cutoffs = 0, killer_cutoffs = 0;

    std::string to_string() const;
};

struct StackFrame
{
    PVLine pv;
    bool is_leftmost;
    int root_dist;
    int extensions;
};

class SearchMaster;

class SearchWorker
{
  private:
    friend SearchMaster;

    SearchWorker();

    template<SearchNode Node>
    BoardEval alpha_beta(Board& board, StackFrame& f, BoardEval alpha, BoardEval beta, int depth);

    template<SearchNode Node>
    BoardEval quiescence(Board& board, StackFrame& f, BoardEval alpha, BoardEval beta);

    BoardEval iterative_deepening(ISearchReceiver& receiver, Board& board, int depth);

    bool check_for_stop();

    void tt_store(TEntryHandle& handle, BoardEval eval, Move best_move, int depth, TableBound bound, int root_dist);
    void store_killer(int dist, Move move);
    void update_history(Colour side, Move move, MoveEval bonus);
    void clear_history();

    Move killers[MAX_PV_LENGTH][KILLER_SLOTS];
    MoveEval history[COLOUR_MAX][SQ_MAX * SQ_MAX];

    SearchMaster* master;
    PVLine prev_pv;
    int iteration_depth;
    int completed_depth;
    int sel_depth;           // Deepest depth reached
    int completed_sel_depth; // Deepest depth last iteration
    SearchStats stats;
    std::atomic<uint64_t> nodes_searched;
};

class SearchMaster
{
  public:
    SearchMaster(size_t worker_count, size_t ttable_size);
    ~SearchMaster();

    void set_worker_count(size_t count);
    void clear_history(); // For a new game.

    void go(ISearchReceiver& receiver, Board& board, const SearchConditions& conditions);
    void realise_ponder();
    void set_move_overhead(Ms overhead) { move_overhead = overhead; }

    inline void stop() { abort_search = true; }
    void check_time();

    bool should_stop_at_iteration(const IterationInfo& iteration);
    void wait_for();

    inline bool is_aborted() { return abort_search.load(std::memory_order_relaxed); }
    inline bool is_searching() const { return in_search; }

    inline Ms elapsed() const { return now() - start_time; }

    friend SearchWorker;

    inline bool is_main_worker(const SearchWorker* worker) const { return worker == &workers[0]; }
    uint64_t nodes_searched() const;

    TTable ttable;

  private:
    void start_search(ISearchReceiver& receiver, Board& board, const SearchConditions& conditions);

    int worker_count;
    std::unique_ptr<SearchWorker[]> workers;

    TimeManager time_manager;
    Ms start_time;
    uint64_t node_limit;
    int mate_limit;
    Ms move_overhead;

    std::atomic<bool> in_search, abort_search, is_ponder, is_untimed;
    // Set while pondering when the last iteration would have ended a normal search.
    std::atomic<bool> stop_on_ponderhit;
    std::thread search_thread;

    ISearchReceiver* search_receiver;
};

constexpr bool is_mate(BoardEval eval)
{
    return std::abs(eval) >= MATE_EVAL - MAX_PV_LENGTH && std::abs(eval) < INF_EVAL;
}

// Mate scores are stored in the TT relative to the node rather than the root.
constexpr BoardEval eval_to_tt(BoardEval eval, int root_dist)
{
    return is_mate(eval) ? (eval > 0 ? eval + root_dist : eval - root_dist) : eval;
}

constexpr BoardEval eval_from_tt(BoardEval eval, int root_dist)
{
    return is_mate(eval) ? (eval > 0 ? eval - root_dist : eval + root_dist) : eval;
}

#endif