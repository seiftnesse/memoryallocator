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

    initialize_segment_integrity(result);

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

    // Update integrity metadata for the merged segment
    initialize_segment_integrity(first_segment);

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

// Integrity verification level
// 0 = Disabled, 1 = Basic (magic only), 2 = Standard, 3 = Thorough
int integrity_check_level = 1;

// Simple FNV-1a hash function for checksum
uint32_t fnv1a_hash(const void *data, size_t len) {
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    uint32_t hash = 2166136261u; // FNV offset basis

    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 16777619; // FNV prime
    }

    return hash;
}

// Calculate metadata checksum for a segment
uint32_t calculate_segment_checksum(segment_t *s) {
    if (!s) return 0;

    // Create a temporary structure with the fields we want to include in the checksum
    struct {
        int is_free;
        int size;
        uintptr_t next;
        uintptr_t prev;
        uint32_t allocation_id;
        uint32_t magic;
    } checksum_data;

    // Fill the structure with segment data
    checksum_data.is_free = s->is_free;
    checksum_data.size = s->size;
    checksum_data.next = reinterpret_cast<uintptr_t>(s->next);
    checksum_data.prev = reinterpret_cast<uintptr_t>(s->prev);
    checksum_data.allocation_id = s->allocation_id;
    checksum_data.magic = s->magic;

    return fnv1a_hash(&checksum_data, sizeof(checksum_data));
}

// Access the integrity structure for a segment (immediately follows segment_t)
segment_integrity_t *get_segment_integrity(segment_t *s) {
    if (!s) return nullptr;

    // In our layout, the integrity data comes after the segment_t structure
    // but before the user data begins.
    // Calculate the proper aligned offset for the integrity structure
    uintptr_t segment_addr = reinterpret_cast<uintptr_t>(s);
    uintptr_t user_data_addr = reinterpret_cast<uintptr_t>(SegmentToPtr(s));

    // The integrity structure is placed between the segment header and the user data
    uintptr_t integrity_addr = segment_addr + sizeof(segment_t);

    // Make sure there's enough space for the integrity structure
    if (integrity_addr + sizeof(segment_integrity_t) > user_data_addr) {
        HEAP_LOG("Warning: Not enough space for integrity data in segment %p\n", s);
        return nullptr;
    }

    return reinterpret_cast<segment_integrity_t *>(integrity_addr);
}

// Get the address where the footer guard should be placed
// This is at the end of the segment's memory block

uint32_t *get_segment_footer(segment_t *s) {
    if (!s) return nullptr;

    // Calculate the end of the segment's memory block
    uintptr_t segment_end = reinterpret_cast<uintptr_t>(s) + (s->size * BLOCK_SIZE);

    // Check for reasonable segment size
    if (s->size <= 0 || s->size > MAX_REASONABLE_BLOCKS) {
        HEAP_LOG("WARNING: Cannot get footer - segment %p has unreasonable size: %d\n", s, s->size);
        return nullptr;
    }

    // Subtract the size of the guard value and return the pointer
    uintptr_t footer_addr = segment_end - sizeof(uint32_t);

    // Check that the footer is within the segment bounds
    uintptr_t segment_start = reinterpret_cast<uintptr_t>(s);
    if (footer_addr <= segment_start || footer_addr >= segment_end) {
        HEAP_LOG("WARNING: Footer address %p is outside segment bounds [%p-%p]\n",
                 (void*)footer_addr, s, (void*)segment_end);
        return nullptr;
    }

    HEAP_LOG("Footer guard address for segment %p: %p\n", s, (void*)footer_addr);
    return reinterpret_cast<uint32_t *>(footer_addr);
}


// Set the footer guard value for a segment
void set_segment_footer(segment_t *s) {
    if (!s || integrity_check_level < 2) return;

    uint32_t *footer = get_segment_footer(s);
    if (footer) {
        *footer = FOOTER_GUARD_VALUE;
        HEAP_LOG("Set footer guard at %p for segment %p\n", footer, s);
    }
}

// Initialize integrity metadata for a segment
void initialize_segment_integrity(segment_t *s) {
    if (!s || integrity_check_level < 2) return;

    // Calculate addresses to check if we have enough space for integrity data
    uintptr_t segment_addr = reinterpret_cast<uintptr_t>(s);
    uintptr_t user_data_addr = reinterpret_cast<uintptr_t>(SegmentToPtr(s));
    uintptr_t integrity_addr = segment_addr + sizeof(segment_t);

    // Check if we have enough space between segment header and user data
    if (integrity_addr + sizeof(segment_integrity_t) > user_data_addr) {
        HEAP_LOG("Warning: Not enough space for integrity metadata in segment %p\n", s);
        return;
    }

    segment_integrity_t *integrity = reinterpret_cast<segment_integrity_t *>(integrity_addr);
    integrity->header_guard = HEADER_GUARD_VALUE;
    integrity->checksum = calculate_segment_checksum(s);

    // Set footer guard value if thorough checking is enabled
    if (integrity_check_level >= 3) {
        set_segment_footer(s);
    }

    HEAP_LOG("Initialized integrity for segment %p: checksum=0x%08X\n",
             s, integrity->checksum);
}

// Verify the integrity of a single segment
int verify_segment_integrity(segment_t *s, int repair) {
    if (!s) return 0;

    int errors = 0;

    // Basic check - magic number (always performed)
    if (s->magic != SEGMENT_MAGIC) {
        HEAP_LOG("CORRUPTION: Invalid magic number in segment %p: 0x%08X != 0x%08X\n",
                 s, s->magic, SEGMENT_MAGIC);
        errors++;

        if (repair) {
            s->magic = SEGMENT_MAGIC;
            HEAP_LOG("Repaired: Reset magic number for segment %p\n", s);
        }
    }

    // Size sanity check (basic)
    if (s->size <= 0 || s->size > MAX_REASONABLE_BLOCKS) {
        HEAP_LOG("CORRUPTION: Unreasonable size in segment %p: %d blocks\n", s, s->size);
        errors++;

        if (repair && s->next) {
            // Try to determine reasonable size from distance to next segment
            uintptr_t next_addr = reinterpret_cast<uintptr_t>(s->next);
            uintptr_t this_addr = reinterpret_cast<uintptr_t>(s);
            int corrected_size = (next_addr - this_addr) / BLOCK_SIZE;

            if (corrected_size > 0 && corrected_size <= MAX_REASONABLE_BLOCKS) {
                s->size = corrected_size;
                HEAP_LOG("Repaired: Corrected size for segment %p to %d blocks based on next segment\n",
                         s, s->size);
            }
        }
    }

    // Stop here if only basic checks are enabled
    if (integrity_check_level < 2) return errors;

    // Get the integrity structure - ВАЖНО: добавлен детальный вывод для отладки
    segment_integrity_t *integrity = get_segment_integrity(s);
    if (!integrity) {
        HEAP_LOG("WARNING: Could not get integrity structure for segment %p\n", s);

        if (repair) {
            HEAP_LOG("Attempting to initialize integrity for segment %p\n", s);
            initialize_segment_integrity(s);
            integrity = get_segment_integrity(s);
        }

        if (!integrity) {
            HEAP_LOG("CRITICAL: Cannot perform integrity checks - no integrity structure available\n");
            return errors;
        }
    }

    HEAP_LOG("Checking integrity for segment %p: header_guard=0x%08X, checksum=0x%08X\n",
             s, integrity->header_guard, integrity->checksum);

    // Check header guard
    if (integrity->header_guard != HEADER_GUARD_VALUE) {
        HEAP_LOG("CORRUPTION: Invalid header guard in segment %p: 0x%08X != 0x%08X\n",
                 s, integrity->header_guard, HEADER_GUARD_VALUE);
        errors++;

        if (repair) {
            integrity->header_guard = HEADER_GUARD_VALUE;
            HEAP_LOG("Repaired: Reset header guard for segment %p\n", s);
        }
    }

    // Check checksum
    uint32_t current_checksum = calculate_segment_checksum(s);
    if (integrity->checksum != current_checksum) {
        HEAP_LOG("CORRUPTION: Invalid checksum in segment %p: 0x%08X != 0x%08X\n",
                 s, integrity->checksum, current_checksum);
        errors++;

        if (repair) {
            integrity->checksum = current_checksum;
            HEAP_LOG("Repaired: Reset checksum for segment %p\n", s);
        }
    }

    // Check footer guard only if level 3 checks are enabled
    if (integrity_check_level >= 3) {
        uint32_t *footer = get_segment_footer(s);
        if (footer) {
            HEAP_LOG("Footer guard check for segment %p: current=0x%08X, expected=0x%08X\n",
                     s, *footer, FOOTER_GUARD_VALUE);

            if (*footer != FOOTER_GUARD_VALUE) {
                HEAP_LOG("CORRUPTION: Invalid footer guard in segment %p: 0x%08X != 0x%08X\n",
                         s, *footer, FOOTER_GUARD_VALUE);
                errors++;

                if (repair) {
                    *footer = FOOTER_GUARD_VALUE;
                    HEAP_LOG("Repaired: Reset footer guard for segment %p\n", s);
                }
            }
        } else {
            HEAP_LOG("WARNING: Could not get footer pointer for segment %p\n", s);
        }

        // Linked list checks - добавлена проверка на null
        if (s->next) {
            HEAP_LOG("Checking next segment link: %p->next = %p, %p->next->prev = %p\n",
                     s, s->next, s->next, s->next->prev);

            if (s->next->prev != s) {
                HEAP_LOG("CORRUPTION: Broken linked list: s->next->prev != s for segment %p\n", s);
                errors++;

                if (repair) {
                    s->next->prev = s;
                    HEAP_LOG("Repaired: Fixed broken linked list for segment %p\n", s);
                }
            }
        }

        if (s->prev) {
            HEAP_LOG("Checking prev segment link: %p->prev = %p, %p->prev->next = %p\n",
                     s, s->prev, s->prev, s->prev->next);

            if (s->prev->next != s) {
                HEAP_LOG("CORRUPTION: Broken linked list: s->prev->next != s for segment %p\n", s);
                errors++;

                if (repair) {
                    s->prev->next = s;
                    HEAP_LOG("Repaired: Fixed broken linked list for segment %p\n", s);
                }
            }
        }

        // Check that segment is within heap bounds
        uintptr_t heap_start = reinterpret_cast<uintptr_t>(memory);
        uintptr_t heap_end = heap_start + HEAP_SIZE;
        uintptr_t segment_addr = reinterpret_cast<uintptr_t>(s);

        if (segment_addr < heap_start || segment_addr >= heap_end) {
            HEAP_LOG("CORRUPTION: Segment %p is outside heap bounds [%p-%p]\n",
                     s, (void*)heap_start, (void*)heap_end);
            errors++;
            // Cannot repair this automatically
        }
    }

    return errors;
}

// Verify the integrity of the entire heap
int verify_heap_integrity(int repair, int *segments_verified, int *segments_repaired) {
    if (!heap_initialized) {
        HEAP_LOG("Cannot verify heap integrity - heap not initialized\n");
        return -1;
    }

    int total_errors = 0;
    int verified = 0;
    int repaired = 0;
    segment_t *s = segments;

    while (s) {
        int errors = verify_segment_integrity(s, repair);
        total_errors += errors;
        verified++;

        if (errors > 0 && repair) {
            repaired++;
        }

        s = s->next;
    }

    HEAP_LOG("Heap integrity verification complete: %d segments checked, %d errors found, %d segments repaired\n",
             verified, total_errors, repaired);

    if (segments_verified) *segments_verified = verified;
    if (segments_repaired) *segments_repaired = repaired;

    return total_errors;
}


// Perform an on-demand heap integrity check
int HeapVerifyIntegrity(int repair) {
    int segments_verified = 0;
    int segments_repaired = 0;

    int errors = verify_heap_integrity(repair, &segments_verified, &segments_repaired);

    HEAP_LOG("Integrity verification results: %d segments, %d errors, %d repaired\n",
             segments_verified, errors, segments_repaired);

    // Return the number of errors found (0 = no errors)
    return errors;
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
