//
// Created by seiftnesse on 2/27/2025.
//
#include "customalloc/custom_alloc_internal.h"

// Find best-fit free memory segment
segment_t *SearchFree(segment_t *s, int size) {
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
int GetNumBlock(size_t size) {
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
segment_t *CutSegment(segment_t *s, int size_to_cut) {
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
segment_t *MergeSegment(segment_t *first_segment, segment_t *second_segment) {
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
void *SegmentToPtr(segment_t *s) {
    if (!s) {
        HEAP_LOG("Cannot convert NULL segment to pointer\n");
        return NULL;
    }

    // Return aligned pointer after segment metadata
    uintptr_t addr = (uintptr_t) s + sizeof(segment_t);
    uintptr_t original_addr = addr;
    addr = (addr + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

    if (original_addr != addr) {
        HEAP_LOG("Adjusted user pointer for alignment: %p -> %p\n", (void*)original_addr, (void*)addr);
    }

    HEAP_LOG("Segment %p converted to user pointer %p\n", s, (void*)addr);
    return (void *) addr;
}

// Convert memory pointer back to segment
segment_t *PtrToSegment(void *ptr) {
    if (!ptr) {
        HEAP_LOG("Cannot convert NULL pointer to segment\n");
        return NULL;
    }

    // Calculate segment address based on alignment and metadata size
    uintptr_t addr = (uintptr_t) ptr;
    addr &= ~(ALIGNMENT - 1); // Round down to alignment boundary
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
void *_memcpy(void *dest, const void *src, size_t bytes) {
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

// Memory set operation
void *_memset(void *dest, int value, size_t count) {
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
