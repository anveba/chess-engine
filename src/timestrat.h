#ifndef TIMESTRAT_H_INCLUDED
#define TIMESTRAT_H_INCLUDED

#include "chess.h"
#include "eval.h"

#include <atomic>
#include <chrono>

using Ms = std::chrono::milliseconds::rep;
using Us = std::chrono::microseconds::rep;

inline Us now_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline Ms now()
{
    return now_us() / 1000;
}

struct SearchConditions;

// Soft and hard bounds: https://www.chessprogramming.org/Time_Management
struct TimeLimits
{
    Ms soft; // Target time.
    Ms hard; // Never exceeded.
    bool is_fixed_time;
};

struct IterationInfo
{
    int depth;
    Move best_move;
    BoardEval eval;
    int root_moves;
    Us duration = 0;
};

class TimeManager
{
  public:
    void init(Colour side, const SearchConditions& conditions, Ms move_overhead);
    void start();

    Us elapsed_us() const { return now_us() - start_time; }
    bool hard_limit_reached() const { return elapsed_us() >= limits.hard * 1000; }

    bool should_stop_at_iteration(const IterationInfo& iteration, Us elapsed);

    const TimeLimits& get_limits() const { return limits; }

  private:
    static TimeLimits compute_limits(Colour side, const SearchConditions& conditions, Ms move_overhead);

    std::atomic<Us> start_time; // Written by the UCI thread on ponderhit.

    TimeLimits limits;

    Move previous_best;
    int stable_iterations;
    int stable_mate_iterations;
    BoardEval evals[2]; // The previous two iterations. Newest first.
    Us previous_duration;
};

#endif
