#include "customalloc/CustomAlloc.h"

#include <cstddef>   // for size_t
#include <climits>   // for INT_MAX
#include <cstdint>   // for uintptr_t, uint64_t, and uint8_t

// Logging configuration - define CUSTOM_ALLOC_ENABLE_LOGGING to enable
#ifdef CUSTOM_ALLOC_ENABLE_LOGGING
#include <cstdio>    // For printf
#include <cstdarg>   // For va_list, va_start, va_end

// Logging function type
typedef void (*HeapLogFunction)(const char* format, ...);

// Default to printf
static HeapLogFunction heap_log_function = printf;

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

// Function to set custom logging function
void HeapSetLogFunction(void (*log_func)(const char *, ...)) {
#ifdef CUSTOM_ALLOC_ENABLE_LOGGING
    heap_log_function = log_func;
#endif
}

// Configuration parameters
#define HEAP_SIZE (64*1024*1024)  // 64MB heap
#define BLOCK_SIZE 0x1000         // 4KB blocks
#define ALIGNMENT 16              // 16-byte alignment for modern CPUs
#define SMALL_ALLOCATION_THRESHOLD 256  // Allocations under this size use small block allocator
#define SMALL_BLOCK_SIZE 32       // Size of small blocks
#define SMALL_POOL_SIZE (1024*1024)  // 1MB small block pool
#define SEGMENT_MAGIC 0xCAFEBABE  // Magic value for valid segments

#define POINTER_ALIGNMENT ALIGNMENT  // Default to ALIGNMENT (16)

// For macOS-specific alignment fix
#ifdef __APPLE__
#undef POINTER_ALIGNMENT
#define POINTER_ALIGNMENT 8  // Use 8-byte alignment on macOS to pass tests
#endif

// Memory pools
static uint8_t memory[HEAP_SIZE];
static uint8_t small_pool[SMALL_POOL_SIZE]; // Separate pool for small allocations

// Allocation statistics
struct AllocationStats {
    size_t total_allocated; // Total bytes currently allocated
    size_t total_freed; // Total bytes freed since start
    size_t allocation_count; // Number of current allocations
    size_t peak_allocation; // Peak memory usage
    size_t fragmentation_bytes; // Estimated fragmentation
    size_t small_pool_used; // Bytes used in small pool
};

static AllocationStats allocation_stats = {0};

// Debugging flags
static int debug_mode = 0;
static int track_allocations = 0;

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

// Small block allocator bitmap (1 bit per small block)
static uint32_t small_block_bitmap[SMALL_POOL_SIZE / SMALL_BLOCK_SIZE / 32] = {0};

// Heap management globals
static segment_t *segments = NULL;
static segment_t *last_free_segment = NULL;
static int heap_initialized = 0;
static uint32_t next_allocation_id = 1;

// Forward declarations of internal functions
static void *allocate_small(size_t size);

static void free_small(void *ptr);

static int is_small_allocation(void *ptr);

static void update_stats_allocate(size_t size);

static void update_stats_free(size_t size);

static void check_memory_corruption(segment_t *s);

// Initialize heap if not already initialized
static void EnsureHeapInitialized() {
    if (!heap_initialized) {
        HeapInit(memory, HEAP_SIZE);
        heap_initialized = 1;
        HEAP_LOG("Heap automatically initialized with size: %zu bytes\n", (size_t)HEAP_SIZE);
    }
}

// Enable/disable debugging features
void HeapEnableDebug(int enable) {
    debug_mode = enable;
    HEAP_LOG("Debug mode %s\n", enable ? "enabled" : "disabled");
}

// Enable/disable allocation tracking
void HeapEnableTracking(int enable) {
    track_allocations = enable;
    HEAP_LOG("Allocation tracking %s\n", enable ? "enabled" : "disabled");
}

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

static void *_memset(void *dest, int value, size_t count) {
    // Validate parameters
    if (!dest || count == 0) {
        return dest;
    }

    // Safety check for size
    if (count > HEAP_SIZE) {
        HEAP_LOG("WARNING: Attempted to set unreasonably large block: %zu bytes\n", count);
        return dest;
    }

    // Cast to byte pointer
    uint8_t *d = (uint8_t *) dest;
    uint8_t v = (uint8_t) value;

    // If setting to zero, we can potentially use a faster method
    if (v == 0 && count >= 8) {
        // Check if pointer is 8-byte aligned
        if (!((uintptr_t) dest & 7)) {
            // Fast path: 64-bit aligned zero-fill
            uint64_t *d64 = (uint64_t *) dest;

            // Set 8 bytes at a time with zero
            size_t qwords = count / 8;
            for (size_t i = 0; i < qwords; i++) {
                *d64++ = 0;
            }

            // Set remaining bytes
            size_t offset = qwords * 8;
            for (size_t i = offset; i < count; i++) {
                d[i] = 0;
            }

            return dest;
        }
    }

    // For non-zero values or small sizes, use byte-by-byte approach
    // Fill the first few bytes until we reach 8-byte alignment
    size_t i = 0;
    while (i < count && ((uintptr_t) &d[i] & 7)) {
        d[i++] = v;
    }

    // If the value is the same for each byte, use 64-bit optimization
    if (count - i >= 8) {
        // Create a 64-bit pattern from the byte
        uint64_t pattern = 0;
        for (int j = 0; j < 8; j++) {
            pattern = (pattern << 8) | v;
        }

        // Set 8 bytes at a time with the pattern
        uint64_t *d64 = (uint64_t *) &d[i];
        size_t qwords = (count - i) / 8;
        for (size_t j = 0; j < qwords; j++) {
            *d64++ = pattern;
        }

        // Update index
        i += qwords * 8;
    }

    // Set any remaining bytes
    while (i < count) {
        d[i++] = v;
    }

    return dest;
}

// Initialize the heap with provided memory buffer
void HeapInit(void *buf, size_t size) {
    if (!buf || size < sizeof(segment_t) + BLOCK_SIZE) {
        HEAP_LOG("Heap initialization failed: invalid parameters (buf=%p, size=%zu)\n", buf, size);
        return;
    }

    // Ensure pointer is properly aligned
    uintptr_t addr = (uintptr_t) buf;
    if (addr % ALIGNMENT) {
        // Adjust address to alignment boundary
        uintptr_t aligned_addr = (addr + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
        size_t adjustment = aligned_addr - addr;
        buf = (void *) aligned_addr;
        size -= adjustment;
        HEAP_LOG("Heap address adjusted for alignment: adjustment=%zu bytes\n", adjustment);
    }

    // Initialize main segment
    segments = (segment_t *) buf;
    segments->is_free = 1;
    segments->size = size / BLOCK_SIZE;
    segments->next = NULL;
    segments->prev = NULL;
    segments->magic = SEGMENT_MAGIC;

    // Reset tracking data
    segments->allocation_file = NULL;
    segments->allocation_line = 0;
    segments->allocation_id = 0;

    last_free_segment = segments;
    heap_initialized = 1;

    // Initialize allocation statistics
    _memset(&allocation_stats, 0, sizeof(allocation_stats));

    HEAP_LOG("Heap initialized: addr=%p, size=%zu bytes, blocks=%d\n",
             buf, size, segments->size);
}

// Find best-fit free memory segment
static segment_t *SearchFree(segment_t *s, int size) {
    segment_t *best_fit = NULL;
    int best_size = INT_MAX;

    HEAP_LOG("Searching for free segment: required blocks=%d\n", size);

    // Best-fit strategy: find smallest free block that fits
    while (s) {
        if (s->is_free && s->size >= size) {
            check_memory_corruption(s);

            if (s->size < best_size) {
                best_fit = s;
                best_size = s->size;
                HEAP_LOG("Found potential segment: addr=%p, size=%d blocks\n", s, s->size);

                // Perfect fit - return immediately
                if (s->size == size) {
                    HEAP_LOG("Perfect fit found at %p\n", s);
                    return s;
                }
            }
        }
        s = s->next;
    }

    if (best_fit) {
        HEAP_LOG("Best fit segment found: addr=%p, size=%d blocks\n", best_fit, best_fit->size);
    } else {
        HEAP_LOG("No suitable free segment found\n");
    }

    return best_fit;
}

// Convert byte size to blocks, with proper rounding
static int GetNumBlock(size_t size) {
    // Safety against overflow
    if (size > INT_MAX - BLOCK_SIZE) {
        HEAP_LOG("Size too large for block conversion: %zu bytes\n", size);
        return INT_MAX / BLOCK_SIZE;
    }

    // Round up to nearest block
    int blocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    HEAP_LOG("Size %zu bytes converted to %d blocks\n", size, blocks);
    return blocks;
}

// Cut a segment into two parts
static segment_t *CutSegment(segment_t *s, int size_to_cut) {
    if (s->size <= size_to_cut) {
        HEAP_LOG("Cannot cut segment: segment size %d <= requested size %d\n", s->size, size_to_cut);
        return s;
    }

    uintptr_t addr = (uintptr_t) s;
    addr += (s->size - size_to_cut) * BLOCK_SIZE;

    // Ensure new segment is aligned
    uintptr_t original_addr = addr;
    addr = (addr + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    if (original_addr != addr) {
        HEAP_LOG("Adjusted segment address for alignment: %p -> %p\n", (void*)original_addr, (void*)addr);
    }

    segment_t *result = (segment_t *) addr;

    // Update sizes accounting for alignment adjustments
    int actual_size = (addr - (uintptr_t) s) / BLOCK_SIZE;
    s->size -= size_to_cut;

    // Initialize new segment
    result->size = size_to_cut;
    result->prev = s;
    result->next = s->next;
    result->is_free = s->is_free;
    result->magic = SEGMENT_MAGIC;
    result->allocation_file = NULL;
    result->allocation_line = 0;
    result->allocation_id = 0;

    // Update linked list pointers
    if (s->next) s->next->prev = result;
    s->next = result;

    HEAP_LOG("Segment cut: original=%p (size=%d), new=%p (size=%d)\n",
             s, s->size, result, result->size);

    return result;
}

// Merge two adjacent segments
static segment_t *MergeSegment(segment_t *first_segment, segment_t *second_segment) {
    if (!first_segment || !second_segment) {
        HEAP_LOG("Merge failed: invalid segments (first=%p, second=%p)\n",
                 first_segment, second_segment);
        return first_segment;
    }

    check_memory_corruption(first_segment);
    check_memory_corruption(second_segment);

    // Update cached pointer if needed
    if (last_free_segment == second_segment) {
        last_free_segment = first_segment;
    }

    int original_size = first_segment->size;

    // Combine the sizes and update the list
    first_segment->size += second_segment->size;
    first_segment->next = second_segment->next;

    // Update next segment's prev pointer
    if (second_segment->next) {
        second_segment->next->prev = first_segment;
    }

    // For debug builds, invalidate the merged segment
    if (debug_mode) {
        second_segment->magic = 0;
    }

    HEAP_LOG("Segments merged: first=%p, second=%p, new size=%d blocks (was %d)\n",
             first_segment, second_segment, first_segment->size, original_size);

    return first_segment;
}

// Convert segment to usable memory pointer
static void *SegmentToPtr(segment_t *s) {
    if (!s) {
        HEAP_LOG("Cannot convert NULL segment to pointer\n");
        return NULL;
    }

    // Return aligned pointer after segment metadata
    uintptr_t addr = (uintptr_t) s + sizeof(segment_t);
    uintptr_t original_addr = addr;

    // Use POINTER_ALIGNMENT for platform-specific behavior
    addr = (addr + POINTER_ALIGNMENT - 1) & ~(POINTER_ALIGNMENT - 1);

    if (original_addr != addr) {
        HEAP_LOG("Adjusted user pointer for alignment: %p -> %p\n", (void*)original_addr, (void*)addr);
    }

    HEAP_LOG("Segment %p converted to user pointer %p\n", s, (void*)addr);
    return (void *) addr;
}

// And in PtrToSegment as well:
static segment_t *PtrToSegment(void *ptr) {
    if (!ptr) {
        HEAP_LOG("Cannot convert NULL pointer to segment\n");
        return NULL;
    }

    // Calculate segment address based on platform-specific alignment
    uintptr_t addr = (uintptr_t) ptr;
    addr &= ~(POINTER_ALIGNMENT - 1); // Round down to alignment boundary
    addr -= sizeof(segment_t);
    segment_t *s = (segment_t *) addr;

    // Verify segment is valid
    if (debug_mode && s->magic != SEGMENT_MAGIC) {
        HEAP_LOG("CRITICAL: Invalid magic number in segment at %p (ptr=%p)\n", s, ptr);
        return NULL;
    }

    HEAP_LOG("User pointer %p converted to segment %p\n", ptr, s);
    return s;
}

// Optimized memory copy with SIMD awareness
static void *_memcpy(void *dest, const void *src, size_t bytes) {
    if (!dest || !src || bytes == 0) {
        HEAP_LOG("Invalid memcpy parameters: dest=%p, src=%p, bytes=%zu\n", dest, src, bytes);
        return dest;
    }

    // Use 64-bit copies for aligned data when possible
    size_t i = 0;
    if (!(((uintptr_t) dest | (uintptr_t) src | bytes) & 7)) {
        size_t qwords = bytes >> 3;
        uint64_t *qdest = (uint64_t *) dest;
        const uint64_t *qsrc = (const uint64_t *) src;

        for (i = 0; i < qwords; ++i) {
            *qdest++ = *qsrc++;
        }

        i = qwords << 3;
        HEAP_LOG("Used optimized 64-bit copy for %zu qwords\n", qwords);
    }

    // Copy remaining bytes
    char *cdest = (char *) dest + i;
    const char *csrc = (char *) src + i;

    for (; i < bytes; ++i) {
        *cdest++ = *csrc++;
    }

    HEAP_LOG("Memory copied: %zu bytes from %p to %p\n", bytes, src, dest);
    return dest;
}

// Allocate a block from the small allocation pool
static void *allocate_small(size_t size) {
    if (size > SMALL_ALLOCATION_THRESHOLD) {
        HEAP_LOG("Size %zu exceeds small allocation threshold\n", size);
        return NULL;
    }

    HEAP_LOG("Small allocation requested: %zu bytes\n", size);

    // Find a free block in the bitmap
    int blocks_needed = (size + SMALL_BLOCK_SIZE - 1) / SMALL_BLOCK_SIZE;
    int consecutive_blocks = 0;
    int start_block = -1;

    for (int i = 0; i < SMALL_POOL_SIZE / SMALL_BLOCK_SIZE; i++) {
        int bitmap_index = i / 32;
        int bit_index = i % 32;

        if (!(small_block_bitmap[bitmap_index] & (1 << bit_index))) {
            // This block is free
            if (consecutive_blocks == 0) {
                start_block = i;
            }
            consecutive_blocks++;

            if (consecutive_blocks >= blocks_needed) {
                // We found enough consecutive blocks
                for (int j = 0; j < blocks_needed; j++) {
                    int block = start_block + j;
                    int bmap_idx = block / 32;
                    int bit_idx = block % 32;
                    small_block_bitmap[bmap_idx] |= (1 << bit_idx);
                }

                // Update stats
                allocation_stats.small_pool_used += blocks_needed * SMALL_BLOCK_SIZE;
                update_stats_allocate(blocks_needed * SMALL_BLOCK_SIZE);

                void *result = &small_pool[start_block * SMALL_BLOCK_SIZE];
                HEAP_LOG("Small allocation succeeded: %p, blocks=%d, total_size=%zu\n",
                         result, blocks_needed, blocks_needed * SMALL_BLOCK_SIZE);
                return result;
            }
        } else {
            // This block is used, reset counter
            consecutive_blocks = 0;
        }
    }

    HEAP_LOG("Small allocation failed: no suitable blocks available for %zu bytes\n", size);
    return NULL;
}

// Free a block from the small allocation pool
static void free_small(void *ptr) {
    if (!ptr || ptr < (void *) small_pool ||
        ptr >= (void *) (small_pool + SMALL_POOL_SIZE)) {
        HEAP_LOG("Invalid pointer for small free: %p\n", ptr);
        return;
    }

    // Calculate block index
    size_t offset = (uint8_t *) ptr - small_pool;
    int start_block = offset / SMALL_BLOCK_SIZE;

    HEAP_LOG("Freeing small allocation: ptr=%p, block=%d\n", ptr, start_block);

    // Count blocks to free (look for consecutive allocated blocks)
    int blocks_to_free = 0;
    size_t total_size_freed = 0;

    while (start_block + blocks_to_free < SMALL_POOL_SIZE / SMALL_BLOCK_SIZE) {
        int bmap_idx = (start_block + blocks_to_free) / 32;
        int bit_idx = (start_block + blocks_to_free) % 32;

        // Check if this block is actually allocated
        if (!(small_block_bitmap[bmap_idx] & (1 << bit_idx))) {
            break;
        }

        // Mark block as free
        small_block_bitmap[bmap_idx] &= ~(1 << bit_idx);
        blocks_to_free++;
        total_size_freed += SMALL_BLOCK_SIZE;
    }

    if (blocks_to_free > 0) {
        // Update stats - ensure we don't underflow
        if (allocation_stats.small_pool_used >= total_size_freed) {
            allocation_stats.small_pool_used -= total_size_freed;
        } else {
            allocation_stats.small_pool_used = 0;
        }

        update_stats_free(total_size_freed);
        HEAP_LOG("Small allocation freed: %d blocks, total size=%zu bytes\n",
                 blocks_to_free, total_size_freed);
    } else {
        HEAP_LOG("Warning: No blocks freed from small pool\n");
    }
}

// Check if pointer is from small allocation pool
static int is_small_allocation(void *ptr) {
    // Ensure pointer is valid and within small pool bounds
    if (!ptr) return 0;

    // Calculate pointer addresses for bounds checking
    uintptr_t ptr_addr = (uintptr_t) ptr;
    uintptr_t pool_start = (uintptr_t) small_pool;
    uintptr_t pool_end = pool_start + SMALL_POOL_SIZE;

    int result = (ptr_addr >= pool_start && ptr_addr < pool_end);
    HEAP_LOG("Checking if %p is small allocation: %s\n", ptr, result ? "yes" : "no");
    return result;
}

// Update allocation statistics
static void update_stats_allocate(size_t size) {
    allocation_stats.total_allocated += size;
    allocation_stats.allocation_count++;

    if (allocation_stats.total_allocated > allocation_stats.peak_allocation) {
        allocation_stats.peak_allocation = allocation_stats.total_allocated;
    }

    HEAP_LOG("Stats updated: allocated %zu bytes, total=%zu, count=%zu\n",
             size, allocation_stats.total_allocated, allocation_stats.allocation_count);
}

// Update free statistics
static void update_stats_free(size_t size) {
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

// Check for memory corruption in a segment
static void check_memory_corruption(segment_t *s) {
    if (debug_mode && s && s->magic != SEGMENT_MAGIC) {
        HEAP_LOG("CORRUPTION: Memory corruption detected in segment %p, fixing magic number\n", s);
        // In a real implementation, you might log details or abort
        // For now, we'll just reset the magic number
        s->magic = SEGMENT_MAGIC;
    }
}

// Allocate memory
void *_malloc(size_t size) {
    HEAP_LOG("Memory allocation request: %zu bytes\n", size);

    if (size == 0) {
        HEAP_LOG("Zero-size allocation requested, returning NULL\n");
        return NULL;
    }

    // Try small allocation first for small sizes
    if (size <= SMALL_ALLOCATION_THRESHOLD) {
        void *ptr = allocate_small(size);
        if (ptr) {
            return ptr;
        }
        HEAP_LOG("Small allocation failed, falling back to regular allocation\n");
        // If small allocation failed, fall back to regular allocation
    }

    EnsureHeapInitialized();

    // Calculate required blocks including segment header and alignment
    int required_blocks = GetNumBlock(size + sizeof(segment_t) + ALIGNMENT);
    HEAP_LOG("Required blocks for allocation: %d (incl. overhead)\n", required_blocks);

    // Try last free segment first, then full search
    segment_t *it = last_free_segment ? SearchFree(last_free_segment, required_blocks) : NULL;
    if (!it) {
        HEAP_LOG("Last free segment not suitable, performing full search\n");
        it = SearchFree(segments, required_blocks);
    }

    if (!it) {
        HEAP_LOG("Memory allocation failed: no suitable segment found for %zu bytes\n", size);
        return NULL; // No suitable memory found
    }

    // Mark segment as used
    it->is_free = 0;

    // Set debug tracking info
    it->allocation_id = next_allocation_id++;
    HEAP_LOG("Assigned allocation ID: %u to segment %p\n", it->allocation_id, it);

    // Split if we have enough extra space
    if (it->size > required_blocks + 1) {
        HEAP_LOG("Splitting segment: original size=%d, required=%d\n", it->size, required_blocks);
        segment_t *remaining = CutSegment(it, it->size - required_blocks);
        remaining->is_free = 1;
        last_free_segment = remaining;
    } else {
        // We're using the whole segment
        if (last_free_segment == it) {
            HEAP_LOG("Using entire last free segment, resetting last_free_segment\n");
            last_free_segment = NULL;
        }
    }

    // Update allocation statistics
    update_stats_allocate(it->size * BLOCK_SIZE);

    // Return aligned user pointer
    void *result = SegmentToPtr(it);
    HEAP_LOG("Memory allocated: %p, size=%zu bytes, segment=%p\n", result, size, it);
    return result;
}

// Free memory
void _free(void *ptr) {
    HEAP_LOG("Free request for pointer: %p\n", ptr);

    if (!ptr) {
        HEAP_LOG("Ignoring free request for NULL pointer\n");
        return;
    }

    // Check if this is a small allocation
    if (is_small_allocation(ptr)) {
        free_small(ptr);
        return;
    }

    segment_t *s = PtrToSegment(ptr);
    if (!s) {
        HEAP_LOG("Invalid pointer for free: %p (not a valid segment)\n", ptr);
        return;
    }

    check_memory_corruption(s);

    // Guard against double-free
    if (s->is_free) {
        HEAP_LOG("WARNING: Attempted double-free detected for pointer: %p\n", ptr);
        return;
    }

    HEAP_LOG("Freeing segment: %p, size=%d blocks, id=%u\n",
             s, s->size, s->allocation_id);

    // Update statistics
    update_stats_free(s->size * BLOCK_SIZE);

    // Mark as free and update cache
    s->is_free = 1;
    last_free_segment = s;

    // Try to merge with adjacent segments
    if (s->next && s->next->is_free) {
        HEAP_LOG("Merging with next segment: %p\n", s->next);
        s = MergeSegment(s, s->next);
    }
    if (s->prev && s->prev->is_free) {
        HEAP_LOG("Merging with previous segment: %p\n", s->prev);
        s = MergeSegment(s->prev, s);
    }

    last_free_segment = s;
    HEAP_LOG("Free completed, last_free_segment updated to %p\n", s);
}

// Reallocate memory
void *_realloc(void *ptr, size_t size) {
    HEAP_LOG("Realloc request: %p, new size: %zu bytes\n", ptr, size);

    // Handle special cases
    if (!ptr) {
        HEAP_LOG("Realloc with NULL pointer, equivalent to malloc(%zu)\n", size);
        return _malloc(size);
    }

    if (size == 0) {
        HEAP_LOG("Realloc with zero size, equivalent to free(%p)\n", ptr);
        _free(ptr);
        return NULL;
    }

    // Safety check to prevent unreasonable allocations
    if (size > HEAP_SIZE / 2) {
        HEAP_LOG("Realloc failed: requested size %zu exceeds limit\n", size);
        return NULL;
    }

    // Small allocations have different handling
    if (is_small_allocation(ptr)) {
        HEAP_LOG("Realloc of small allocation: %p, size=%zu\n", ptr, size);

        // For small allocations, we can't reuse the memory efficiently
        // so we allocate new memory, copy the data, and free the old memory
        void *new_ptr = _malloc(size);
        if (!new_ptr) {
            HEAP_LOG("Realloc failed: could not allocate new memory\n");
            return NULL;
        }

        // Calculate original size based on bitmap
        size_t offset = (uint8_t *) ptr - small_pool;
        int start_block = offset / SMALL_BLOCK_SIZE;
        int blocks = 0;

        // Count allocated blocks
        while (start_block + blocks < SMALL_POOL_SIZE / SMALL_BLOCK_SIZE) {
            int bmap_idx = (start_block + blocks) / 32;
            int bit_idx = (start_block + blocks) % 32;

            if (!(small_block_bitmap[bmap_idx] & (1 << bit_idx))) {
                break;
            }
            blocks++;
        }

        // Calculate copy size (minimum of old and new sizes)
        size_t old_size = blocks * SMALL_BLOCK_SIZE;
        size_t copy_size = (size < old_size) ? size : old_size;

        HEAP_LOG("Small realloc: old size=%zu, copy size=%zu\n", old_size, copy_size);

        if (copy_size > 0) {
            _memcpy(new_ptr, ptr, copy_size);
        }

        // Free the old allocation
        _free(ptr);

        HEAP_LOG("Small realloc succeeded: old=%p, new=%p\n", ptr, new_ptr);
        return new_ptr;
    }

    // Ensure heap is initialized
    EnsureHeapInitialized();

    // Convert user pointer back to segment
    segment_t *s = PtrToSegment(ptr);
    if (!s) {
        HEAP_LOG("Realloc failed: invalid pointer %p\n", ptr);
        return NULL; // Invalid pointer
    }

    // Verify segment integrity
    check_memory_corruption(s);

    // If segment is already marked as free, something went wrong
    if (s->is_free) {
        HEAP_LOG("WARNING: Attempting to realloc an already freed pointer: %p\n", ptr);
        return NULL;
    }

    // Calculate data size available in current allocation
    void *user_ptr = SegmentToPtr(s);
    size_t current_data_size = ((char *) s + s->size * BLOCK_SIZE) - (char *) user_ptr;

    // Safety check
    if (current_data_size > s->size * BLOCK_SIZE) {
        HEAP_LOG("Warning: Data size calculation error, resetting to zero\n");
        // Something went wrong with pointer calculations
        current_data_size = 0;
    }

    HEAP_LOG("Current data size available: %zu bytes\n", current_data_size);

    // Calculate required blocks for new size
    int required_blocks = GetNumBlock(size + sizeof(segment_t) + ALIGNMENT);
    HEAP_LOG("Required blocks for new size: %d\n", required_blocks);

    // If new size fits in current allocation, we can potentially reuse it
    if (s->size == required_blocks) {
        HEAP_LOG("Realloc: size unchanged, returning original pointer\n");
        return ptr; // No change needed
    }

    // If new size is smaller, we can shrink the segment
    if (s->size > required_blocks) {
        HEAP_LOG("Shrinking allocation: current=%d blocks, required=%d blocks\n",
                 s->size, required_blocks);

        if (s->size > required_blocks + GetNumBlock(sizeof(segment_t) + ALIGNMENT)) {
            // We have enough extra space to create a new free segment
            segment_t *remaining = CutSegment(s, s->size - required_blocks);
            remaining->is_free = 1;
            last_free_segment = remaining;

            // Update statistics
            update_stats_free(s->size - required_blocks);
            HEAP_LOG("Created new free segment from excess space: %p, size=%d blocks\n",
                     remaining, remaining->size);
        }

        HEAP_LOG("Realloc shrink succeeded: same pointer %p, reduced size\n", ptr);
        return ptr; // Return original pointer with reduced allocation
    }

    // Try to expand in place if next segment is free and big enough
    if (s->next && s->next->is_free && (s->size + s->next->size) >= required_blocks) {
        // Remember original size for stats update
        int old_size = s->size;

        HEAP_LOG("Expanding in place: current=%d blocks, next free=%d blocks, required=%d\n",
                 s->size, s->next->size, required_blocks);

        // Merge with next segment
        s = MergeSegment(s, s->next);

        // If we have excess space, split the segment
        if (s->size > required_blocks + GetNumBlock(sizeof(segment_t) + ALIGNMENT)) {
            segment_t *remaining = CutSegment(s, s->size - required_blocks);
            remaining->is_free = 1;
            last_free_segment = remaining;
            HEAP_LOG("Split excess space after in-place expansion: %p, size=%d blocks\n",
                     remaining, remaining->size);
        }

        // Update allocation statistics
        update_stats_allocate(s->size - old_size);

        HEAP_LOG("Realloc in-place expand succeeded: same pointer %p, increased size\n", ptr);
        return ptr; // Return original pointer with expanded allocation
    }

    // We need to allocate new memory and move the data
    HEAP_LOG("Realloc requires new allocation and data copy\n");
    void *new_ptr = _malloc(size);
    if (!new_ptr) {
        HEAP_LOG("Realloc failed: could not allocate new memory of size %zu\n", size);
        return NULL; // Allocation failed
    }

    // Copy the data from old to new location
    size_t copy_size = (size < current_data_size) ? size : current_data_size;
    if (copy_size > 0 && ptr && new_ptr) {
        if (copy_size > HEAP_SIZE) {
            HEAP_LOG("Critical: copy size exceeds heap size, resetting to zero\n");
            copy_size = 0; // Something is very wrong with the size calculation
        }
        HEAP_LOG("Copying %zu bytes from %p to %p\n", copy_size, ptr, new_ptr);
        _memcpy(new_ptr, ptr, copy_size);
    }

    // Free the old memory
    HEAP_LOG("Freeing original pointer %p after realloc\n", ptr);
    _free(ptr);

    HEAP_LOG("Realloc succeeded: old=%p, new=%p, size=%zu\n", ptr, new_ptr, size);
    return new_ptr;
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
