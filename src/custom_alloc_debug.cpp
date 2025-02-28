//
// Created by seiftnesse on 2/27/2025.
//
#include "customalloc/custom_alloc_internal.h"

// Debugging flags
int debug_mode = 0;
int track_allocations = 0;
int zero_on_free_depth = ZERO_DEPTH_NONE; // Default to no zeroing (best performance)
size_t shallow_zero_size = 64; // Default 64 bytes for shallow zeroing (headers/pointers)

#ifdef CUSTOM_ALLOC_ENABLE_LOGGING
// Default to printf
HeapLogFunction heap_log_function = printf;
#endif

// Check for memory corruption in a segment
void check_memory_corruption(segment_t *s) {
    if (!debug_mode || !s) return;

    // Use our new verify_segment_integrity function with repair=1
    int errors = verify_segment_integrity(s, 1);

    if (errors > 0) {
        HEAP_LOG("CORRUPTION: Found and repaired %d errors in segment %p\n", errors, s);
    }
}

// Enable/disable debugging features
void HeapEnableDebug(int enable) {
    debug_mode = enable;
    HEAP_LOG("Debug mode %s\n", enable ? "enabled" : "disabled");
}

void HeapSetZeroOnFree(int depth, size_t shallow_size) {
    if (depth >= ZERO_DEPTH_NONE && depth <= ZERO_DEPTH_DEEP) {
        zero_on_free_depth = depth;
    } else {
        HEAP_LOG("Invalid zero-on-free depth: %d, using default\n", depth);
        zero_on_free_depth = ZERO_DEPTH_NONE;
    }

    if (shallow_size > 0) {
        shallow_zero_size = shallow_size;
    }

    const char *depth_str;
    switch (zero_on_free_depth) {
        case ZERO_DEPTH_NONE:
            depth_str = "none (best performance)";
            break;
        case ZERO_DEPTH_SHALLOW:
            depth_str = "shallow (headers/pointers only)";
            break;
        case ZERO_DEPTH_MEDIUM:
            depth_str = "medium (50% of memory)";
            break;
        case ZERO_DEPTH_DEEP:
            depth_str = "deep (entire memory block)";
            break;
        default:
            depth_str = "unknown";
    }

    HEAP_LOG("Zero-on-free configured: depth=%s, shallow_size=%zu bytes\n",
             depth_str, shallow_zero_size);
}

// Enable/disable allocation tracking
void HeapEnableTracking(int enable) {
    track_allocations = enable;
    HEAP_LOG("Allocation tracking %s\n", enable ? "enabled" : "disabled");
}

// Set allocation tracking info (for debugging)
void *_malloc_debug(size_t size, const char *file, int line) {
    HEAP_LOG("Debug malloc: size=%zu, file=%s, line=%d\n", size, file ? file : "unknown", line);

    void *ptr = _malloc(size);

    if (ptr && !is_small_allocation(ptr) && track_allocations) {
        segment_t *s = PtrToSegment(ptr);
        s->allocation_file = file;
        s->allocation_line = line;
        HEAP_LOG("Debug info recorded for allocation: %p, id=%u\n", ptr, s->allocation_id);
    }

    return ptr;
}

// Function to set custom logging function
void HeapSetLogFunction(void (*log_func)(const char *, ...)) {
#ifdef CUSTOM_ALLOC_ENABLE_LOGGING
    heap_log_function = log_func;
#endif
}

// Memory usage report (for debugging)
void HeapPrintStatus() {
    if (!heap_initialized) {
        HEAP_LOG("Heap not initialized\n");
        return;
    }

#ifdef CUSTOM_ALLOC_ENABLE_LOGGING
    if (heap_log_function) {
        heap_log_function("=== Memory Allocator Status ===\n");
        heap_log_function("Total allocated: %zu bytes\n", allocation_stats.total_allocated);
        heap_log_function("Total freed: %zu bytes\n", allocation_stats.total_freed);
        heap_log_function("Active allocations: %zu\n", allocation_stats.allocation_count);
        heap_log_function("Peak memory usage: %zu bytes\n", allocation_stats.peak_allocation);
        heap_log_function("Small pool usage: %zu/%zu bytes\n", allocation_stats.small_pool_used, SMALL_POOL_SIZE);
        heap_log_function("Fragmentation: %.2f%%\n", HeapGetFragmentation() * 100.0f);

        // Add zero-on-free configuration to status report
        const char* depth_str;
        switch (zero_on_free_depth) {
            case ZERO_DEPTH_NONE:
                depth_str = "none (best performance)";
                break;
            case ZERO_DEPTH_SHALLOW:
                depth_str = "shallow (headers/pointers only)";
                break;
            case ZERO_DEPTH_MEDIUM:
                depth_str = "medium (50% of memory)";
                break;
            case ZERO_DEPTH_DEEP:
                depth_str = "deep (entire memory block)";
                break;
            default:
                depth_str = "unknown";
        }
        heap_log_function("Zero-on-free depth: %s\n", depth_str);
        if (zero_on_free_depth == ZERO_DEPTH_SHALLOW) {
            heap_log_function("Shallow zero size: %zu bytes\n", shallow_zero_size);
        }

        if (track_allocations) {
            heap_log_function("\n=== Active Allocations ===\n");
            segment_t *s = segments;
            while (s) {
                if (!s->is_free) {
                    if (s->allocation_file) {
                        heap_log_function("ID: %u, Size: %d blocks, Location: %s:%d\n",
                               s->allocation_id, s->size, s->allocation_file, s->allocation_line);
                    } else {
                        heap_log_function("ID: %u, Size: %d blocks, Location: unknown\n",
                               s->allocation_id, s->size);
                    }
                }
                s = s->next;
            }
        }

        heap_log_function("==============================\n");
    }
#endif
}

// Set the integrity check level
void HeapSetIntegrityCheckLevel(int level) {
    if (level >= 0 && level <= 3) {
        integrity_check_level = level;
        HEAP_LOG("Integrity check level set to %d\n", level);
    } else {
        HEAP_LOG("Invalid integrity check level: %d (valid range: 0-3)\n", level);
    }
}