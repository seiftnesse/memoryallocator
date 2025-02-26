# Custom Memory Allocator (CustomAlloc)

[![Build](https://github.com/seiftnesse/memoryallocator/actions/workflows/build.yml/badge.svg)](https://github.com/seiftnesse/memoryallocator/actions/workflows/build.yml)
[![Test](https://github.com/seiftnesse/memoryallocator/actions/workflows/test.yml/badge.svg)](https://github.com/seiftnesse/memoryallocator/actions/workflows/test.yml)

A high-performance custom memory allocator implementation with optimized memory management.

> This project is based on the original implementation by [XShar](https://github.com/XShar/Custom_Wchar_String/blob/master/Wstring/CustomAlloc.cpp) with significant enhancements and optimizations.

## Features

- Static memory allocation with configurable heap size (64MB by default, increased from original 2MB)
- Optimized memory block allocation and deallocation
- Best-fit allocation strategy to minimize fragmentation (improved from original first-fit approach)
- Memory reuse through coalescing of adjacent free blocks
- SIMD-friendly memory copy implementation
- Optimized memory alignment and addressing
- Thread-safe design
- Automatic heap initialization
- Small block allocator for efficient small memory requests
- Memory corruption detection
- Detailed memory usage tracking and statistics
- Configurable logging and debugging facilities

## Differences from Original Implementation

The current implementation significantly enhances the original basic allocator in many ways:

| Feature | Original Implementation | Enhanced Implementation |
|---------|-------------------------|-------------------------|
| Heap Size | 2MB static allocation | 64MB with configurable size |
| Allocation Strategy | First-fit | Best-fit for reduced fragmentation |
| Small Allocations | Not optimized | Dedicated small block allocator |
| Memory Operations | Basic byte-by-byte copy | SIMD-aware optimized operations |
| Alignment | Basic | Enhanced for modern CPU architecture (16-byte) |
| Debugging | None | Extensive debugging and tracking capabilities |
| Memory Safety | Minimal | Memory corruption detection and prevention |
| Statistics | None | Comprehensive memory usage statistics |

## Building the Library

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Integrating with Your Project

### Option 1: Using CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    customalloc
    GIT_REPOSITORY https://github.com/seiftnesse/memoryallocator.git
    GIT_TAG main
)
FetchContent_MakeAvailable(customalloc)

# Link with your target
target_link_libraries(your_target PRIVATE customalloc)
```

### Option 2: Installing the Library

```bash
mkdir build && cd build
cmake ..
cmake --build .
cmake --install .
```

Then in your project's CMakeLists.txt:

```cmake
find_package(customalloc REQUIRED)
target_link_libraries(your_target PRIVATE customalloc::customalloc)
```

## Usage

### Basic Memory Operations

```cpp
#include <customalloc/CustomAlloc.h>

int main() {
    // Allocate memory
    void* ptr = _malloc(1024);
    
    // Use the memory
    // ...
    
    // Reallocate to a different size
    ptr = _realloc(ptr, 2048);
    
    // Use the resized memory
    // ...
    
    // Free the memory
    _free(ptr);
    
    return 0;
}
```

### Advanced Usage with Debugging

```cpp
#include <customalloc/CustomAlloc.h>

int main() {
    // Enable debugging and tracking
    HeapEnableDebug(1);
    HeapEnableTracking(1);
    
    // Allocate with source tracking
    void* ptr = _malloc_debug(1024, __FILE__, __LINE__);
    
    // Get memory statistics
    size_t allocated, freed, count, peak;
    HeapGetStats(&allocated, &freed, &count, &peak);
    
    // Check fragmentation level
    float frag = HeapGetFragmentation();
    
    // Print overall heap status
    HeapPrintStatus();
    
    // Clean up
    _free(ptr);
    
    return 0;
}
```

## Running Tests

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
cmake --build .
ctest
```

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Roadmap

- **Version 1.1**
  - Memory statistics collection and reporting ✅
  - Performance benchmarks against system allocator ✅
  - Thread synchronization for multi-threaded applications

- **Version 1.2**
  - Allocator adaptors for STL containers
  - Memory leak detection ✅
  - Defragmentation utilities ✅

- **Version 1.3**
  - Pluggable allocation strategies (best-fit ✅, worst-fit, etc.)
  - Multiple heap support
  - Allocation debugging tools (allocation tracking ✅)

- **Version 2.0**
  - NUMA-aware allocations for high-performance computing
  - Memory compression for rarely accessed blocks
  - Customizable block sizes