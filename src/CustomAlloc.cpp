#include "customalloc/CustomAlloc.h"

#include <cstddef>   // for size_t
#include <climits>   // for INT_MAX
#include <cstdint>   // for uintptr_t and uint64_t

#define HEAP_SIZE (64*1024*1024)
#define BLOCK_SIZE 0x1000

typedef unsigned char uint8_t;
typedef unsigned long long uint64_t;

static uint8_t memory[HEAP_SIZE];
static int heap_initialized = 0;

typedef struct segment {
    int is_free;
    int size;
    struct segment *next;
    struct segment *prev;
} segment_t;

static segment_t *segments = NULL;
static segment_t *last_free_segment = NULL;

static void EnsureHeapInitialized() {
    if (!heap_initialized) {
        HeapInit(memory, HEAP_SIZE);
        heap_initialized = 1;
    }
}

void HeapInit(void *buf, size_t size) {
    if (!buf || size < sizeof(segment_t) + BLOCK_SIZE) {
        return;
    }

    segments = (segment_t *) buf;
    segments->is_free = 1;
    segments->size = size / BLOCK_SIZE;
    segments->next = NULL;
    segments->prev = NULL;
    last_free_segment = segments;
    heap_initialized = 1;
}

static segment_t *SearchFree(segment_t *s, int size) {
    segment_t *best_fit = NULL;
    int best_size = INT_MAX;

    while (s) {
        if (s->is_free && s->size >= size) {
            if (s->size < best_size) {
                best_fit = s;
                best_size = s->size;

                if (s->size == size) {
                    return s;
                }
            }
        }
        s = s->next;
    }

    return best_fit;
}

static int GetNumBlock(size_t size) {
    if (size > INT_MAX - BLOCK_SIZE) {
        return INT_MAX / BLOCK_SIZE;
    }

    return (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

static segment_t *CutSegment(segment_t *s, int size_to_cut) {
    if (s->size <= size_to_cut) {
        return s;
    }

    uintptr_t addr = (uintptr_t) s;
    addr += (s->size - size_to_cut) * BLOCK_SIZE;
    segment_t *result = (segment_t *) addr;

    s->size -= size_to_cut;

    result->size = size_to_cut;
    result->prev = s;
    result->next = s->next;

    if (s->next) s->next->prev = result;
    s->next = result;

    result->is_free = s->is_free;

    return result;
}

static segment_t *MergeSegment(segment_t *first_segment, segment_t *second_segment) {
    if (!first_segment || !second_segment) {
        return first_segment;
    }

    if (last_free_segment == second_segment) {
        last_free_segment = first_segment;
    }

    first_segment->size += second_segment->size;

    first_segment->next = second_segment->next;

    if (second_segment->next) {
        second_segment->next->prev = first_segment;
    }

    return first_segment;
}

static void *SegmentToPtr(segment_t *s) {
    if (!s) return NULL;
    return (char *) s + sizeof(segment_t);
}

static segment_t *PtrToSegment(void *ptr) {
    if (!ptr) return NULL;
    return (segment_t *) ((char *) ptr - sizeof(segment_t));
}

static void *_memcpy(void *dest, const void *src, size_t bytes) {
    if (!dest || !src) return dest;

    size_t i = 0;
    if (!(((uintptr_t) dest | (uintptr_t) src | bytes) & 7)) {
        size_t qwords = bytes >> 3;
        uint64_t *qdest = (uint64_t *) dest;
        const uint64_t *qsrc = (const uint64_t *) src;

        for (i = 0; i < qwords; ++i) {
            *qdest++ = *qsrc++;
        }

        i = qwords << 3;
    }

    char *cdest = (char *) dest + i;
    const char *csrc = (char *) src + i;

    for (; i < bytes; ++i) {
        *cdest++ = *csrc++;
    }

    return dest;
}

void *_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    EnsureHeapInitialized();

    int required_blocks = GetNumBlock(size + sizeof(segment_t));

    segment_t *it = last_free_segment ? SearchFree(last_free_segment, required_blocks) : NULL;

    if (!it) {
        it = SearchFree(segments, required_blocks);
    }

    if (!it) {
        return NULL;
    }

    it->is_free = 0;

    if (it->size > required_blocks + 1) {
        segment_t *remaining = CutSegment(it, it->size - required_blocks);
        remaining->is_free = 1;
        last_free_segment = remaining;
    } else {
        if (last_free_segment == it) {
            last_free_segment = NULL;
        }
    }

    return SegmentToPtr(it);
}

void _free(void *ptr) {
    if (!ptr) return;

    segment_t *s = PtrToSegment(ptr);
    if (!s) return;

    s->is_free = 1;
    last_free_segment = s;

    if (s->next && s->next->is_free) {
        s = MergeSegment(s, s->next);
    }
    if (s->prev && s->prev->is_free) {
        s = MergeSegment(s->prev, s);
    }

    last_free_segment = s;
}

void *_realloc(void *ptr, size_t size) {
    if (!ptr) {
        return _malloc(size);
    }

    if (size == 0) {
        _free(ptr);
        return NULL;
    }

    EnsureHeapInitialized();

    segment_t *s = PtrToSegment(ptr);
    if (!s) return NULL;

    size_t original_data_size = s->size * BLOCK_SIZE - sizeof(segment_t);

    int required_blocks = GetNumBlock(size + sizeof(segment_t));

    if (s->size == required_blocks) {
        return ptr;
    }

    if (s->size > required_blocks) {
        if (s->size > required_blocks + GetNumBlock(sizeof(segment_t))) {
            segment_t *remaining = CutSegment(s, s->size - required_blocks);
            remaining->is_free = 1;
            last_free_segment = remaining;
        }
        return ptr;
    }

    if (s->next && s->next->is_free && (s->size + s->next->size) >= required_blocks) {
        int old_size = s->size;
        s = MergeSegment(s, s->next);

        if (s->size > old_size) {
            if (s->size > required_blocks + GetNumBlock(sizeof(segment_t))) {
                segment_t *remaining = CutSegment(s, s->size - required_blocks);
                remaining->is_free = 1;
                last_free_segment = remaining;
            }
            return ptr;
        }
    }

    void *new_ptr = _malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    size_t copy_size = (size < original_data_size) ? size : original_data_size;
    if (copy_size > 0) {
        _memcpy(new_ptr, ptr, copy_size);
    }

    _free(ptr);

    return new_ptr;
}
