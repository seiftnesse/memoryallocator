//
// Created by seiftnesse on 2/27/2025.
//
#include "customalloc/custom_alloc_internal.h"

// Small block allocator bitmap (1 bit per small block)
uint32_t small_block_bitmap[SMALL_POOL_SIZE / SMALL_BLOCK_SIZE / 32] = {0};

// Allocate a block from the small allocation pool
void *allocate_small(size_t size) {
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
void free_small(void *ptr) {
    if (!ptr || ptr < (void *) small_pool ||
        ptr >= (void *) (small_pool + SMALL_POOL_SIZE)) {
        HEAP_LOG("Invalid pointer for small free: %p\n", ptr);
        return;
    }

    // Calculate block index
    size_t offset = (uint8_t *) ptr - small_pool;
    int start_block = offset / SMALL_BLOCK_SIZE;

    HEAP_LOG("Freeing small allocation: ptr=%p, block=%d\n", ptr, start_block);

    // First pass: Count blocks without freeing them
    int blocks_to_free = 0;
    size_t total_size_freed = 0;

    // This loop just counts the blocks that will be freed
    for (int i = 0; start_block + i < SMALL_POOL_SIZE / SMALL_BLOCK_SIZE; i++) {
        int bmap_idx = (start_block + i) / 32;
        int bit_idx = (start_block + i) % 32;

        // Check if this block is actually allocated
        if (!(small_block_bitmap[bmap_idx] & (1 << bit_idx))) {
            break;
        }
        blocks_to_free++;
        total_size_freed += SMALL_BLOCK_SIZE;
    }

    // Apply zero-on-free according to configured depth
    if (zero_on_free_depth > ZERO_DEPTH_NONE && blocks_to_free > 0) {
        size_t user_size = blocks_to_free * SMALL_BLOCK_SIZE;

        size_t zero_size = 0;
        switch (zero_on_free_depth) {
            case ZERO_DEPTH_SHALLOW:
                // Zero only the first portion (headers/pointers)
                zero_size = (shallow_zero_size < user_size) ? shallow_zero_size : user_size;
                break;

            case ZERO_DEPTH_MEDIUM:
                // Zero half the memory
                zero_size = user_size / 2;
                break;

            case ZERO_DEPTH_DEEP:
            default:
                // Zero all memory (most secure, but slowest)
                zero_size = user_size;
                break;
        }

        if (zero_size > 0) {
            HEAP_LOG("Zeroing %zu bytes on small free at %p (depth=%d)\n",
                     zero_size, ptr, zero_on_free_depth);
            _memset(ptr, 0, zero_size);
        }
    }

    // Second pass: Now mark the blocks as free in the bitmap
    for (int i = 0; i < blocks_to_free; i++) {
        int block = start_block + i;
        int bmap_idx = block / 32;
        int bit_idx = block % 32;
        small_block_bitmap[bmap_idx] &= ~(1 << bit_idx);
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
int is_small_allocation(void *ptr) {
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
