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

TTable::TTable(size_t size_in_mib)
    : table(nullptr)
    , size(0)
    , current_age(0)
{
    if (!resize(size_in_mib)) {
        std::cerr << "Failed to allocate memory for transposition table." << std::endl;
        exit(1);
    }
}

TTable::~TTable()
{
    std::free(table);
}

bool TTable::resize(size_t size_in_mib)
{
    const size_t new_size = (size_in_mib << 20) / sizeof(TableCluster);
    assert(new_size > 0);

    // aligned_alloc requires the size to be a multiple of the alignment.
    constexpr size_t page_size = 4096;
    const size_t bytes = (new_size * sizeof(TableCluster) + page_size - 1) / page_size * page_size;
    TableCluster* new_table = static_cast<TableCluster*>(std::aligned_alloc(page_size, bytes));
    if (!new_table)
        return false;

    std::free(table);
    table = new_table;
    size = new_size;
    clear();
    return true;
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

TTProbe TTable::get(BoardHash hash)
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