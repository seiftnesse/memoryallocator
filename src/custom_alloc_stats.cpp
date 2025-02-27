//
// Created by seiftnesse on 2/27/2025.
//
#include "customalloc/custom_alloc_internal.h"

// Allocation statistics
AllocationStats allocation_stats = {0};

// Get allocation statistics
void HeapGetStats(size_t *allocated, size_t *freed, size_t *count, size_t *peak) {
    if (allocated) *allocated = allocation_stats.total_allocated;
    if (freed) *freed = allocation_stats.total_freed;
    if (count) *count = allocation_stats.allocation_count;
    if (peak) *peak = allocation_stats.peak_allocation;

    HEAP_LOG("Stats queried: allocated=%zu, freed=%zu, count=%zu, peak=%zu\n",
             allocation_stats.total_allocated, allocation_stats.total_freed,
             allocation_stats.allocation_count, allocation_stats.peak_allocation);
}

// Update allocation statistics
void update_stats_allocate(size_t size) {
    allocation_stats.total_allocated += size;
    allocation_stats.allocation_count++;

    if (allocation_stats.total_allocated > allocation_stats.peak_allocation) {
        allocation_stats.peak_allocation = allocation_stats.total_allocated;
    }

    HEAP_LOG("Stats updated: allocated %zu bytes, total=%zu, count=%zu\n",
             size, allocation_stats.total_allocated, allocation_stats.allocation_count);
}

// Update free statistics
void update_stats_free(size_t size) {
    // Prevent underflow - don't subtract more than what we have
    if (size > allocation_stats.total_allocated) {
        HEAP_LOG("Warning: Freeing more memory than allocated: %zu > %zu\n",
                 size, allocation_stats.total_allocated);
        allocation_stats.total_freed += allocation_stats.total_allocated;
        allocation_stats.total_allocated = 0;
    } else {
        allocation_stats.total_allocated -= size;
        allocation_stats.total_freed += size;
    }

    // Don't let allocation count go negative
    if (allocation_stats.allocation_count > 0) {
        allocation_stats.allocation_count--;
    }

    HEAP_LOG("Stats updated: freed %zu bytes, remaining=%zu, count=%zu\n",
             size, allocation_stats.total_allocated, allocation_stats.allocation_count);
}

// Get fragmentation estimate
float HeapGetFragmentation() {
    if (!heap_initialized) {
        HEAP_LOG("Heap not initialized, fragmentation=0\n");
        return 0.0f;
    }

    // Count number of free segments and total free memory
    int free_segments = 0;
    size_t free_memory = 0;
    segment_t *s = segments;

    while (s) {
        if (s->is_free) {
            free_segments++;
            free_memory += s->size * BLOCK_SIZE;
        }
        s = s->next;
    }

    // If no free memory, fragmentation is 0
    if (free_memory == 0) {
        HEAP_LOG("No free memory, fragmentation=0\n");
        return 0.0f;
    }

    // Calculate average free segment size
    float avg_segment_size = (float) free_memory / free_segments;

    // Calculate fragmentation as 1 - (avg segment size / total free memory)
    // This gives a value between 0 (no fragmentation) and close to 1 (high fragmentation)
    float frag = 1.0f - (avg_segment_size / free_memory);

    HEAP_LOG("Fragmentation calculation: free_segments=%d, free_memory=%zu, result=%.4f\n",
             free_segments, free_memory, frag);

    return frag;
}
