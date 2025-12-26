#include "ttable.h"

#include <iostream>

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
};

constexpr size_t CLUSTER_SIZE = 4;

struct TableCluster
{
    TableEntry entries[CLUSTER_SIZE];
};

static_assert(sizeof(TableCluster) % 32 == 0);

int8_t make_flags(uint8_t age, TableBound bound)
{
    assert(bound < 4);
    assert(!(age & 0b00000011));
    return age | bound;
}

TTable::TTable(size_t size_in_mb)
    : table(nullptr)
    , current_age(0)
{
    resize(size_in_mb);
}

TTable::~TTable()
{
    std::free(table);
}

void TTable::resize(size_t size_in_mb)
{
    std::free(table);

    size = (size_in_mb * 1000000) / sizeof(TableCluster);
    assert(size > 0);

    constexpr size_t page_size = 4096;
    table = static_cast<TableCluster*>(std::aligned_alloc(page_size, size * sizeof(TableCluster)));
    if (!table) {
        std::cerr << "Failed to allocate memory for transposition table." << std::endl;
        exit(1);
    }

    clear();
}

void TTable::clear()
{
    uint8_t flags = make_flags(current_age - (1 << 2), INVALID);
    for (size_t i = 0; i < size; i++)
        for (size_t j = 0; j < CLUSTER_SIZE; j++)
            table[i].entries[j].flags = flags;
}

TableCluster* TTable::get_cluster(BoardHash hash)
{
    size_t index = hash % size;
    return table + index;
}

std::tuple<bool, TEntryHandle> TTable::get(BoardHash hash)
{
    TableCluster* cluster = get_cluster(hash);

    for (size_t i = 0; i < CLUSTER_SIZE; i++) {
        TableEntry* entry = cluster->entries + i;

        if (entry->bound() != INVALID && hash == entry->hash) {
            return { true, TEntryHandle(entry->eval, entry->best_move, entry->depth, entry->bound(), cluster, hash) };
        }
    }
    return { false, TEntryHandle(0, Move::make_none(), 0, INVALID, cluster, hash) };
}

void TTable::insert(TEntryHandle& handle, BoardEval eval, Move best_move, uint8_t depth, TableBound bound)
{
    assert(get_cluster(handle.hash) == handle.cluster);

    TableEntry new_entry = { handle.hash, eval, best_move, depth, make_flags(get_age(), bound) };

    for (size_t i = 0; i < CLUSTER_SIZE; i++) {
        TableEntry* entry = handle.cluster->entries + i;

        // If the same position is already in the table and we are trying to insert it again,
        // it means the old entry was likely not good enough to skip computation, so replacing
        // it might be good. It also prevents several entries containing the same hash.
        if (entry->bound() == INVALID || entry->hash == handle.hash) {
            *entry = new_entry;
            return;
        }
    }

    TableEntry* replaced = nullptr;
    for (size_t i = 0; i < CLUSTER_SIZE; i++) {
        TableEntry* entry = handle.cluster->entries + i;

        // Prefer replacing stale entries over shallow ones.
        if (entry->age() != current_age) {
            *entry = new_entry;
            return;
        }

        // Replace shallowest entry
        if (replaced == nullptr || entry->depth < replaced->depth)
            replaced = entry;
    }

    if (replaced)
        *replaced = new_entry;
}

float TTable::occupancy()
{
    size_t occupied = 0;
    for (size_t i = 0; i < size; i++)
        for (size_t j = 0; j < CLUSTER_SIZE; j++)
            if (table[i].entries[j].bound() != INVALID)
                occupied++;
    return (float)occupied / (size * CLUSTER_SIZE);
}