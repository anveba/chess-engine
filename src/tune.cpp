#include "tune.h"

#include "uci.h"

static std::vector<Tunable>& tunables()
{
    static std::vector<Tunable> list;
    return list;
}

void register_tunable(const Tunable& tunable)
{
    tunables().push_back(tunable);
}

std::vector<UCIOption> tunable_options()
{
    std::vector<UCIOption> result;
    for (const Tunable& t : tunables()) {
        result.push_back(UCIOption("TUNABLE_" + t.name, SPIN_OPTION, std::to_string(*t.value)));
        result.back().set_int_bounds(t.min, t.max);
        result.back().set_on_change_callback([value = t.value](UCIOption* opt) { *value = int(opt->get_int()); });
    }
    return result;
}