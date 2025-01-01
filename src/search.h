#ifndef SEARCH_H_INCLUDED
#define SEARCH_H_INCLUDED

#include <atomic>
#include <thread>

#include "board.h"
#include "eval.h"
#include "timestrat.h"
#include "ttable.h"

constexpr size_t MAX_WORKERS = 256;
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
        ponder = false;
        wtime = btime = winc = binc = 0;
        moves_to_go = 0;
        move_time = 0;
        depth = MAX_DEPTH;
    }

    uint64_t move_time;
    uint64_t wtime, btime, winc, binc;

    int depth, moves_to_go;

    bool ponder;
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
    constexpr uint64_t time_in_ms() const { return time_ms; }

  private:
    friend SearchWorker;
    friend SearchMaster;

    SearchResult(SearchResultType type);

    SearchResultType result_type;
    PVLine pv_line;
    int depth_searched;
    BoardEval eval;
    uint64_t nodes_searched;
    uint64_t time_ms;
};

class ISearchReceiver
{
  public:
    virtual void receive_search_result(const SearchResult& result) = 0;
};

struct StackFrame
{
    PVLine pv;
    bool is_leftmost;
    int root_dist;
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

    SearchMaster* master;
    PVLine prev_pv;
    std::atomic<uint64_t> nodes_searched;
};

class SearchMaster
{
  public:
    SearchMaster(size_t worker_count, size_t ttable_size);
    ~SearchMaster();

    void set_worker_count(size_t count);

    void go(ISearchReceiver& receiver, Board& board, const SearchConditions& conditions);
    void realise_ponder();

    inline void stop() { abort_search = true; }
    void check_time();
    void wait_for();

    inline bool is_aborted() { return abort_search.load(std::memory_order_relaxed); }
    inline bool is_searching() const { return in_search; }

    inline Ms elapsed() const { return now() - start_time; }

    inline bool is_main_worker(const SearchWorker* worker) const { return worker == &workers[0]; }
    uint64_t nodes_searched() const;

    TTable ttable;

  private:
    void start_search(ISearchReceiver& receiver, Board& board, const SearchConditions& conditions);

    int worker_count;
    SearchWorker workers[MAX_WORKERS];

    TimeManager time_manager;
    Ms start_time;

    std::atomic<bool> in_search, abort_search, is_ponder, is_infinite;
    std::thread search_thread;

    ISearchReceiver* search_receiver;
};

constexpr bool is_mate(BoardEval eval)
{
    return std::abs(eval) >= MATE_EVAL - MAX_PV_LENGTH && std::abs(eval) < INF_EVAL;
}

#endif