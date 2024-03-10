#ifndef SEARCH_H_INCLUDED
#define SEARCH_H_INCLUDED

#include <atomic>
#include <chrono>
#include <thread>

#include "board.h"
#include "eval.h"

constexpr size_t MAX_WORKERS = 256;
constexpr int MAX_DEPTH = 255;

enum SearchNode
{
    PV_NODE,
    NON_PV_NODE
};

enum SearchResultType
{
    FINAL_BEST,
    CURRENT_BEST
};

class SearchWorker;

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

    std::string to_string() const;

  private:
    friend SearchWorker;

    Move moves[MAX_DEPTH + 16];
    int length;
};

class SearchResult
{
  public:
    SearchResult(SearchResultType type, const PVLine& pv, BoardEval eval)
        : result_type(type)
        , eval(eval)
    {
        pv_line.copy(pv);
    }

    constexpr SearchResultType type() const { return result_type; }
    constexpr const PVLine& pv() const { return pv_line; }
    constexpr BoardEval evaluation() const { return eval; }

  private:
    SearchResultType result_type;
    PVLine pv_line;
    BoardEval eval;
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
    SearchWorker() {}

    friend SearchMaster;

    template<SearchNode Node>
    BoardEval alpha_beta(Board& board, StackFrame& f, BoardEval alpha, BoardEval beta, int depth);

    template<SearchNode Node>
    BoardEval quiescence(Board& board, BoardEval alpha, BoardEval beta);

    BoardEval iterative_deepening(ISearchReceiver& receiver, Board& board, int depth);

    SearchMaster* master;
    PVLine prev_pv;
};

class SearchMaster
{
  public:
    SearchMaster(int worker_count);
    ~SearchMaster();

    void go(ISearchReceiver& receiver, Board& board, int depth, float time, bool ponder);
    void realise_ponder();

    inline void stop() { abort_search = true; }
    void wait_for();

    inline void check_time();
    inline bool should_stop() { return abort_search.load(std::memory_order_relaxed); }
    inline bool is_searching() const { return in_search; }
    inline bool is_main_worker(const SearchWorker* worker) const { return worker == &workers[0]; }

  private:
    void start_search(ISearchReceiver& receiver, Board& board, int depth);
    void start_timer(float time);

    void set_workers(int count);

    int worker_count;
    SearchWorker workers[MAX_WORKERS];

    std::atomic<bool> in_search, abort_search, is_ponder;
    std::thread search_thread, timer_thread;

    ISearchReceiver* search_receiver;
};

#endif