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

struct TableEntry
{
    BoardHash hash;
    BoardEval eval;
    Move best_move;
    uint8_t depth;

    // Bits 0 and 1 indicate the bound. Bits 2..7 indicate the age.
    int8_t flags;

    uint8_t age() const { return flags & 0b11111100; }
    TableBound bound() const { return TableBound(flags & 0b00000011); }
    void set_flags(uint8_t age, TableBound bound)
    {
        assert(bound < 4);
        assert(!(age & 0b00000011));
        flags = age | bound;
    }
};

constexpr size_t CLUSTER_SIZE = 4;

struct TableCluster
{
    TableEntry entries[CLUSTER_SIZE];
};

static_assert(sizeof(TableCluster) % 32 == 0);

class TTable
{
  public:
    TTable(size_t size_in_mb);
    ~TTable();

    void resize(size_t size_in_mb);
    void clear();

    TableCluster* get_cluster(BoardHash hash);
    TableEntry* insert(TableCluster* cluster, BoardHash hash, uint8_t depth);
    TableEntry* get(TableCluster* cluster, BoardHash hash);
    void set_age(uint8_t age) { current_age = age << 2; }
    void next_age() { current_age = (current_age + (1 << 2)); }

    size_t cluster_count() const { return size; }
    uint8_t get_age() const { return current_age; }

    TTable(const TTable&) = delete;
    TTable& operator=(const TTable&) = delete;

    float occupancy();

  private:
    TableCluster* table;
    size_t size;
    uint8_t current_age;
};

#endif