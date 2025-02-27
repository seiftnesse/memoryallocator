//
// Created by seiftnesse on 2/27/2025.
//

#ifndef CUSTOMALLOCINTERNAL_H
#define CUSTOMALLOCINTERNAL_H

#include "custom_alloc.h"

#include <cstddef>   // for size_t
#include <climits>   // for INT_MAX
#include <cstdint>   // for uintptr_t, uint64_t, and uint8_t

// Configuration parameters
#define HEAP_SIZE (64*1024*1024)  // 64MB heap
#define BLOCK_SIZE 0x1000         // 4KB blocks
#define ALIGNMENT 16              // 16-byte alignment for modern CPUs
#define SMALL_ALLOCATION_THRESHOLD 256  // Allocations under this size use small block allocator
#define SMALL_BLOCK_SIZE 32       // Size of small blocks
#define SMALL_POOL_SIZE (1024*1024)  // 1MB small block pool
#define SEGMENT_MAGIC 0xCAFEBABE  // Magic value for valid segments

// Zero-on-free depth options
#define ZERO_DEPTH_NONE 0        // Don't zero memory on free (fastest)
#define ZERO_DEPTH_SHALLOW 1     // Zero the first few bytes (headers/pointers)
#define ZERO_DEPTH_MEDIUM 2      // Zero 50% of the memory
#define ZERO_DEPTH_DEEP 3        // Zero all memory (most secure, slowest)

// Logging configuration - define CUSTOM_ALLOC_ENABLE_LOGGING to enable
#ifdef CUSTOM_ALLOC_ENABLE_LOGGING
#include <cstdio>    // For printf
#include <cstdarg>   // For va_list, va_start, va_end

// Logging function type
typedef void (*HeapLogFunction)(const char* format, ...);

// Declaration - defined in CustomAllocDebug.cpp
extern HeapLogFunction heap_log_function;

// Logging macro that checks debug_mode
#define HEAP_LOG(...)                          \
    do {                                       \
        if (debug_mode && heap_log_function) { \
            heap_log_function(__VA_ARGS__);    \
        }                                      \
    } while (0)

#else  // Logging disabled
#define HEAP_LOG(...) {}
#endif  // CUSTOM_ALLOC_ENABLE_LOGGING

// Main heap segment structure
typedef struct segment {
    int is_free;
    int size; // in blocks
    struct segment *next;
    struct segment *prev;

    // Metadata for debugging/tracking
    const char *allocation_file; // Source file of allocation (debug only)
    int allocation_line; // Source line of allocation (debug only)
    uint32_t allocation_id; // Unique allocation ID
    uint32_t magic; // Magic number to detect corruption
} segment_t;

// Allocation statistics
struct AllocationStats {
    size_t total_allocated; // Total bytes currently allocated
    size_t total_freed; // Total bytes freed since start
    size_t allocation_count; // Number of current allocations
    size_t peak_allocation; // Peak memory usage
    size_t fragmentation_bytes; // Estimated fragmentation
    size_t small_pool_used; // Bytes used in small pool
};

// External declarations of global variables
extern uint8_t memory[HEAP_SIZE];
extern uint8_t small_pool[SMALL_POOL_SIZE];
extern AllocationStats allocation_stats;
extern int debug_mode;
extern int track_allocations;
extern segment_t *segments;
extern segment_t *last_free_segment;
extern int heap_initialized;
extern uint32_t next_allocation_id;
extern uint32_t small_block_bitmap[SMALL_POOL_SIZE / SMALL_BLOCK_SIZE / 32];
extern int zero_on_free_depth;
extern size_t shallow_zero_size;

// Memory utility functions
void *_memset(void *dest, int value, size_t count);
void *_memcpy(void *dest, const void *src, size_t bytes);

// Small allocation functions
void *allocate_small(size_t size);
void free_small(void *ptr);
int is_small_allocation(void *ptr);

// Segment management functions
segment_t *SearchFree(segment_t *s, int size);
int GetNumBlock(size_t size);
segment_t *CutSegment(segment_t *s, int size_to_cut);
segment_t *MergeSegment(segment_t *first_segment, segment_t *second_segment);
void *SegmentToPtr(segment_t *s);
segment_t *PtrToSegment(void *ptr);

// Statistics functions
void update_stats_allocate(size_t size);
void update_stats_free(size_t size);

// Debug functions
void check_memory_corruption(segment_t *s);
void EnsureHeapInitialized();

#endif //CUSTOMALLOCINTERNAL_H