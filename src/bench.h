#ifndef BENCH_H_INCLUDED
#define BENCH_H_INCLUDED

#include <cstdint>

#include "search.h"

constexpr int DEFAULT_BENCH_DEPTH = 6;

struct BenchResult
{
    uint64_t nodes = 0;
    uint64_t time_ms = 0;
};

BenchResult run_bench(SearchMaster& searcher, int depth, bool verbose);

#endif
