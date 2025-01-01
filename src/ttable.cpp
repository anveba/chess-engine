#include "ttable.h"

#include <iostream>

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
    for (size_t i = 0; i < size; i++)
        for (size_t j = 0; j < CLUSTER_SIZE; j++)
            table[i].entries[j].set_flags(current_age - (1 << 2), INVALID);
}

TableCluster* TTable::get_cluster(BoardHash hash)
{
    size_t index = hash % size;
    return table + index;
}

TableEntry* TTable::insert(TableCluster* cluster, BoardHash hash, uint8_t depth)
{
    assert(get_cluster(hash) == cluster);

    for (size_t i = 0; i < CLUSTER_SIZE; i++) {
        TableEntry* entry = cluster->entries + i;

        // If the same position is already in the table and we are trying to insert it again,
        // it means the old entry was likely not good enough to skip computation, so replacing
        // it might be good. It also prevents several entries containing the same hash.
        if (entry->bound() == INVALID || entry->hash == hash) {
            return entry;
        }
    }

    TableEntry* replaced = nullptr;
    for (size_t i = 0; i < CLUSTER_SIZE; i++) {
        TableEntry* entry = cluster->entries + i;

        // Prefer replacing stale entries over shallow ones.
        if (entry->age() != current_age)
            return entry;

        // Replace shallowest entry
        if (replaced == nullptr || entry->depth < replaced->depth)
            replaced = entry;
    }
    return replaced;
}

TableEntry* TTable::get(TableCluster* cluster, BoardHash hash)
{
    assert(get_cluster(hash) == cluster);

    for (size_t i = 0; i < CLUSTER_SIZE; i++) {
        TableEntry* entry = cluster->entries + i;

        if (entry->bound() != INVALID && hash == entry->hash)
            return entry;
    }

    return nullptr;
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