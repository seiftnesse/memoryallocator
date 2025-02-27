//
// Created by seiftnesse on 2/25/2025.
//

#ifndef CUSTOMALLOC_H
#define CUSTOMALLOC_H

#include <cstddef>  // for size_t
#include <cstdio>   // for printf

// Core memory allocator functions
void HeapInit(void *buf, size_t size);
void *_malloc(size_t size);
void _free(void *ptr);
void *_realloc(void *ptr, size_t size);

// Enhanced debugging functions
void HeapEnableDebug(int enable);
void HeapEnableTracking(int enable);
void HeapGetStats(size_t* allocated, size_t* freed, size_t* count, size_t* peak);
float HeapGetFragmentation();
void HeapPrintStatus();

// Debug version of malloc that tracks file and line
void* _malloc_debug(size_t size, const char* file, int line);

// Logging functions
void HeapSetLogFunction(void (*log_func)(const char*, ...));
void HeapEnableLogging(int enable);

// Zero-on-free configuration function
void HeapSetZeroOnFree(int depth, size_t shallow_size);

#endif // CUSTOMALLOC_H