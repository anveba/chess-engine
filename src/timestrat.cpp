#include "timestrat.h"

#include "search.h"

#include <chrono>
#include <thread>

bool TimeManager::check()
{
    return now() - start_time >= search_time - 9;
}

void TimeManager::set(Colour side, const SearchConditions& conditions)
{
    assert(conditions.move_time > 0 || (conditions.wtime > 0 && conditions.btime > 0));

    uint64_t time_to_spend;

    if (conditions.move_time > 0)
        time_to_spend = conditions.move_time;
    else
        time_to_spend = (side == WHITE ? conditions.wtime : conditions.btime) / 20; // TODO: follow a better strategy

    search_time = Ms(time_to_spend);
}

void TimeManager::start()
{
    start_time = now();
}