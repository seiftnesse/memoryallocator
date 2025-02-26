//
// Created by seiftnesse on 2/25/2025.
//

#ifndef CUSTOMALLOC_H
#define CUSTOMALLOC_H

#include <cstddef>  // for size_t

// Custom memory allocator functions
void HeapInit(void *buf, size_t size);

void *_malloc(size_t size);

void _free(void *ptr);

void *_realloc(void *ptr, size_t size);

#endif //CUSTOMALLOC_H
