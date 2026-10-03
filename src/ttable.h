#ifndef TTABLE_H_INCLUDED
#define TTABLE_H_INCLUDED

#include "eval.h"
#include <atomic>

enum TableBound : uint8_t
{
    INVALID = 0,
    LOWER_BOUND = 1,
    UPPER_BOUND = 2,
    EXACT = 3,
};

struct TableCluster;

class TEntryHandle
{
  public:
    BoardEval eval;
    Move best_move;
    uint8_t depth;
    TableBound bound;

  private:
    TableCluster* cluster;
    BoardHash hash;
    TEntryHandle(BoardEval eval, Move best_move, uint8_t depth, TableBound bound, TableCluster* cluster, BoardHash hash)
        : eval(eval)
        , best_move(best_move)
        , depth(depth)
        , bound(bound)
        , cluster(cluster)
        , hash(hash)
    {
    }
    friend class TTable;
};

struct TTProbe
{
    bool hit;
    TEntryHandle handle;
};

class TTable
{
  public:
    TTable(size_t size_in_mb);
    ~TTable();

    bool resize(size_t size_in_mib); // False on failure
    void clear();

    void insert(TEntryHandle& handle, BoardEval eval, Move best_move, uint8_t depth, TableBound bound);
    TTProbe get(BoardHash hash);
    void set_age(uint8_t age) { current_age = age << 2; }
    void next_age() { current_age = (current_age + (1 << 2)); }

    size_t cluster_count() const { return size; }
    uint8_t get_age() const { return current_age; }

    TTable(const TTable&) = delete;
    TTable& operator=(const TTable&) = delete;

    float occupancy();

  private:
    TableCluster* get_cluster(BoardHash hash);

    TableCluster* table;
    size_t size;
    uint8_t current_age;
};

#endif