# Custom Memory Allocator (CustomAlloc)

[![Build](https://github.com/seiftnesse/memoryallocator/actions/workflows/build.yml/badge.svg)](https://github.com/seiftnesse/memoryallocator/actions/workflows/build.yml)
[![Test](https://github.com/seiftnesse/memoryallocator/actions/workflows/test.yml/badge.svg)](https://github.com/seiftnesse/memoryallocator/actions/workflows/test.yml)

A high-performance custom memory allocator implementation with optimized memory management.

## Features

- Static memory allocation with configurable heap size (64MB by default)
- Optimized memory block allocation and deallocation
- Best-fit allocation strategy to minimize fragmentation
- Memory reuse through coalescing of adjacent free blocks
- SIMD-friendly memory copy implementation
- Thread-safe design

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

## Running Tests

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
cmake --build .
ctest
```

## License

This project is licensed under the MIT License - see the LICENSE file for details.