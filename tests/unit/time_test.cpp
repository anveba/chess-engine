#include "search.h"
#include "testing.h"
#include "timestrat.h"

constexpr Ms OVERHEAD = 30;

static SearchConditions create_search_conditions(uint64_t time, uint64_t inc = 0, int moves_to_go = 0)
{
    SearchConditions c;
    c.wtime = c.btime = time;
    c.winc = c.binc = inc;
    c.moves_to_go = moves_to_go;
    return c;
}

static TimeLimits compute_time_limits(Colour side, const SearchConditions& conditions, Ms overhead)
{
    TimeManager tm;
    tm.init(side, conditions, overhead);
    return tm.get_limits();
}

TEST(time_limits_movetime_uses_all_of_it)
{
    SearchConditions c;
    c.move_time = 500;
    TimeLimits limits = compute_time_limits(WHITE, c, OVERHEAD);
    CHECK(limits.is_fixed_time);
    CHECK_EQ(limits.soft, Ms(470));
    CHECK_EQ(limits.hard, Ms(470));
}

TEST(time_limits_never_exceed_remaining_time)
{
    for (uint64_t time : { 1, 10, 31, 50, 100, 1000, 10000, 100000, 10000000 }) {
        for (uint64_t inc : { 0, 10, 100, 1000, 100000 }) {
            for (int moves_to_go : { 0, 1, 2, 5, 40 }) {
                TimeLimits limits = compute_time_limits(WHITE, create_search_conditions(time, inc, moves_to_go), OVERHEAD);
                const Ms available = std::max(Ms(time) - OVERHEAD, Ms(1));
                CHECK(limits.soft >= 1 && limits.soft <= limits.hard);
                CHECK_CTX(limits.hard <= available, time << "+" << inc << " mtg " << moves_to_go);
            }
        }
    }
}

TEST(time_limits_use_side_to_move_clock)
{
    SearchConditions c = create_search_conditions(60000);
    c.btime = 100;
    CHECK(compute_time_limits(BLACK, c, OVERHEAD).hard <= 100 - OVERHEAD);
    CHECK_EQ(compute_time_limits(WHITE, c, OVERHEAD).soft, compute_time_limits(WHITE, create_search_conditions(60000), OVERHEAD).soft);
}

static IterationInfo iteration(int depth, BoardEval eval, int root_moves = 30)
{
    return { depth, Move::make_none(), eval, root_moves };
}

TEST(time_manager_stops_early_when_nothing_to_think_about)
{
    TimeManager tm;
    tm.init(WHITE, create_search_conditions(60000), OVERHEAD);
    CHECK(tm.should_stop_at_iteration(iteration(1, 0, 1), 100));

    tm.init(WHITE, create_search_conditions(60000), OVERHEAD);
    const BoardEval mate = MATE_EVAL - 3;
    CHECK(!tm.should_stop_at_iteration(iteration(3, mate), 100));
    CHECK(!tm.should_stop_at_iteration(iteration(4, mate), 200));
    CHECK(tm.should_stop_at_iteration(iteration(5, mate), 300));

    // Fixed movetime never stops early.
    SearchConditions c;
    c.move_time = 1000;
    tm.init(WHITE, c, OVERHEAD);
    CHECK(!tm.should_stop_at_iteration(iteration(1, 0, 1), 100));
}
