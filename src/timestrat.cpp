#include "timestrat.h"

#include <algorithm>
#include <cmath>

#include "search.h"
#include "tune.h"

// See https://www.chessprogramming.org/Time_Management.

constexpr Ms MIN_TIME_MS = 1;
TUNABLE(DEFAULT_MOVES_TO_GO, 30, 10, 60);
TUNABLE(MAX_MOVES_TO_GO, 50, 20, 100);
TUNABLE(INCREMENT_PERCENT_TO_USE_PER_MOVE, 75, 25, 100);
TUNABLE(HARD_LIMIT_PERCENT_OF_SOFT, 400, 150, 800);
TUNABLE(MIN_HARD_PERCENT_OF_REMAINING, 30, 10, 60);
TUNABLE(MAX_HARD_PERCENT_OF_REMAINING, 90, 50, 98);

TUNABLE(LATEST_START_TIME_PERCENT, 50, 20, 90);

TUNABLE(STABILITY_SCALE_MAX_PERCENT, 150, 100, 250);
TUNABLE(STABILITY_SCALE_MIN_PERCENT, 60, 30, 100);
TUNABLE(STABILITY_DECAY_PERCENT, 67, 30, 95);

TUNABLE(DEFAULT_PER_ITERATION_GROWTH_PERCENT, 400, 150, 1000);
TUNABLE(MIN_PER_ITERATION_GROWTH_PERCENT, 150, 100, 300);
TUNABLE(MAX_PER_ITERATION_GROWTH_PERCENT, 1000, 400, 2000);
constexpr Us MIN_MEASURABLE_ITERATION_US = 200;
TUNABLE(MAX_PREDICTED_OVERSHOOT_PERCENT, 200, 100, 400);

TUNABLE(EVAL_DROP_MARGIN, 25, 5, 100);
TUNABLE(EVAL_DROP_SCALE_PERCENT, 150, 100, 250);
TUNABLE(STABLE_MATE_ITERATIONS, 2, 1, 6);

static double stability_scale(int stable_iterations)
{
    const double decay = std::pow(STABILITY_DECAY_PERCENT / 100.0, stable_iterations);
    return (STABILITY_SCALE_MIN_PERCENT + (STABILITY_SCALE_MAX_PERCENT - STABILITY_SCALE_MIN_PERCENT) * decay) / 100;
}

TimeLimits TimeManager::compute_limits(Colour side, const SearchConditions& conditions, Ms move_overhead)
{
    if (conditions.move_time > 0) {
        const Ms time = std::max(Ms(conditions.move_time) - move_overhead, MIN_TIME_MS);
        return { time, time, true };
    }

    const Ms remaining = side == WHITE ? conditions.wtime : conditions.btime;
    const Ms increment = side == WHITE ? conditions.winc : conditions.binc;
    const double available = std::max(remaining - move_overhead, MIN_TIME_MS);

    const int horizon = conditions.moves_to_go > 0 ? std::min(conditions.moves_to_go, MAX_MOVES_TO_GO) : DEFAULT_MOVES_TO_GO;
    const double max_fraction = std::clamp(HARD_LIMIT_PERCENT_OF_SOFT / 100.0 / horizon, MIN_HARD_PERCENT_OF_REMAINING / 100.0, MAX_HARD_PERCENT_OF_REMAINING / 100.0);

    const double soft = available / horizon + increment * INCREMENT_PERCENT_TO_USE_PER_MOVE / 100.0;
    const double hard = std::min(soft * HARD_LIMIT_PERCENT_OF_SOFT / 100.0, available * max_fraction);

    return { std::max(Ms(std::min(soft, hard)), MIN_TIME_MS), std::max(Ms(hard), MIN_TIME_MS), false };
}

void TimeManager::init(Colour side, const SearchConditions& conditions, Ms move_overhead)
{
    limits = compute_limits(side, conditions, move_overhead);
    previous_best = Move::make_none();
    stable_iterations = 0;
    stable_mate_iterations = 0;
    evals[0] = evals[1] = 0;
    previous_duration = 0;

    start();
}

void TimeManager::start()
{
    start_time = now_us();
}

bool TimeManager::should_stop_at_iteration(const IterationInfo& iteration, Us elapsed)
{
    // Update historical data
    stable_iterations = iteration.best_move == previous_best ? stable_iterations + 1 : 0;
    previous_best = iteration.best_move;
    stable_mate_iterations = is_mate(iteration.eval) && iteration.eval == evals[0] ? stable_mate_iterations + 1 : 0;
    const bool score_dropped = iteration.depth > 2 && iteration.eval < evals[1] - EVAL_DROP_MARGIN;
    evals[1] = evals[0];
    evals[0] = iteration.eval;
    const Us prev_dur = previous_duration;
    previous_duration = iteration.duration;

    // Easy stops
    if (limits.is_fixed_time)
        return false;
    if (iteration.root_moves == 1 || stable_mate_iterations >= STABLE_MATE_ITERATIONS)
        return true;

    // Adjust target time based on stability
    double additional_time_factor = stability_scale(stable_iterations);
    if (score_dropped)
        additional_time_factor *= EVAL_DROP_SCALE_PERCENT / 100.0;

    const double target_us = std::min(limits.soft * additional_time_factor, double(limits.hard)) * 1000;

    if (elapsed >= target_us * LATEST_START_TIME_PERCENT / 100.0)
        return true;

    // Predict next iteration's time
    const double predicted_iteration_growth = prev_dur >= MIN_MEASURABLE_ITERATION_US
                                                  ? std::clamp(double(iteration.duration) / prev_dur, MIN_PER_ITERATION_GROWTH_PERCENT / 100.0, MAX_PER_ITERATION_GROWTH_PERCENT / 100.0)
                                                  : DEFAULT_PER_ITERATION_GROWTH_PERCENT / 100.0;
    const double predicted_finish = elapsed + iteration.duration * predicted_iteration_growth;

    return predicted_finish > std::min(target_us * MAX_PREDICTED_OVERSHOOT_PERCENT / 100.0, limits.hard * 1000.0);
}
