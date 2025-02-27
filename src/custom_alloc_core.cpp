//
// Created by seiftnesse on 2/27/2025.
//

#include "customalloc/custom_alloc_internal.h"

// Memory pools
uint8_t memory[HEAP_SIZE];
uint8_t small_pool[SMALL_POOL_SIZE]; // Separate pool for small allocations

// Heap management globals
segment_t *segments = NULL;
segment_t *last_free_segment = NULL;
int heap_initialized = 0;
uint32_t next_allocation_id = 1;

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

    if (zero_on_free_depth > ZERO_DEPTH_NONE) {
        void *user_ptr = SegmentToPtr(s);
        size_t total_size = s->size * BLOCK_SIZE;
        size_t user_data_offset = (char *) user_ptr - (char *) s;
        size_t user_data_size = total_size - user_data_offset;

        if (user_data_size > 0) {
            size_t zero_size = 0;

            switch (zero_on_free_depth) {
                case ZERO_DEPTH_SHALLOW:
                    // Zero only the first portion (headers/pointers)
                    zero_size = (shallow_zero_size < user_data_size) ? shallow_zero_size : user_data_size;
                    break;

                case ZERO_DEPTH_MEDIUM:
                    // Zero half the memory
                    zero_size = user_data_size / 2;
                    break;

                case ZERO_DEPTH_DEEP:
                default:
                    // Zero all memory (most secure, but slowest)
                    zero_size = user_data_size;
                    break;
            }

            if (zero_size > 0) {
                HEAP_LOG("Zeroing %zu bytes on free at %p (depth=%d)\n",
                         zero_size, user_ptr, zero_on_free_depth);
                _memset(user_ptr, 0, zero_size);
            }
        }
    }

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

// Initialize heap if not already initialized
void EnsureHeapInitialized() {
    if (!heap_initialized) {
        HeapInit(memory, HEAP_SIZE);
        heap_initialized = 1;
        HEAP_LOG("Heap automatically initialized with size: %zu bytes\n", (size_t)HEAP_SIZE);
    }
}
