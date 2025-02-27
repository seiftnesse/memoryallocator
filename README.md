# Custom Memory Allocator (CustomAlloc)

[![Build](https://github.com/seiftnesse/memoryallocator/actions/workflows/build.yml/badge.svg)](https://github.com/seiftnesse/memoryallocator/actions/workflows/build.yml)
[![Test](https://github.com/seiftnesse/memoryallocator/actions/workflows/test.yml/badge.svg)](https://github.com/seiftnesse/memoryallocator/actions/workflows/test.yml)

A high-performance custom memory allocator implementation with optimized memory management.

> This project is based on the original implementation
> by [XShar](https://github.com/XShar/Custom_Wchar_String/blob/master/Wstring/CustomAlloc.cpp) with significant
> enhancements and optimizations.

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

| Feature             | Original Implementation | Enhanced Implementation                        |
|---------------------|-------------------------|------------------------------------------------|
| Heap Size           | 2MB static allocation   | 64MB with configurable size                    |
| Allocation Strategy | First-fit               | Best-fit for reduced fragmentation             |
| Small Allocations   | Not optimized           | Dedicated small block allocator                |
| Memory Operations   | Basic byte-by-byte copy | SIMD-aware optimized operations                |
| Alignment           | Basic                   | Enhanced for modern CPU architecture (16-byte) |
| Debugging           | None                    | Extensive debugging and tracking capabilities  |
| Memory Safety       | Minimal                 | Memory corruption detection and prevention     |
| Statistics          | None                    | Comprehensive memory usage statistics          |

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

### Version 1.4 (Near-term)

- **Memory Protection Enhancements**
    - Implement segregated memory regions for different size classes to prevent cross-class exploitation
    - Add randomized guard pages between slabs to mitigate overflow attacks
    - Develop more sophisticated address space layout randomization for memory allocations
- **Security Features**
    - Improve use-after-free detection mechanisms with configurable quarantine system
    - Implement comprehensive metadata integrity verification
    - Add configurable zero-on-free depth with performance options

### Version 1.5

- **Advanced Exploitation Prevention**
    - Add slot randomization within slabs to make exploitation less deterministic
    - Implement fully out-of-line metadata storage with protection from corruption
    - Develop detection mechanisms for write-after-free attacks
- **Performance Improvements**
    - Implement scalable multi-arena design for improved concurrency
    - Optimize small allocation pathways for reduced latency
    - Add size class tuning based on application profiles

### Version 2.0

- **Memory Tagging Support**
    - Implement support for ARM Memory Tagging Extension (MTE) when available
    - Add hardware-assisted use-after-free detection
    - Develop overflow detection using hardware capabilities
- **Advanced Features**
    - Create isolated memory regions for allocations to prevent cross-region exploits
    - Implement configurable security vs. performance tradeoffs with preset modes
    - Develop comprehensive hardening against heap metadata attacks

### Version 2.1

- **System Integration**
    - Add deeper OS integration with seccomp-bpf filter support
    - Implement controlled memory mapping strategies
    - Develop randomness improvements using hardware sources when available
- **Diagnostic Capabilities**
    - Create detailed allocation statistics reporting
    - Add optional memory corruption detection with forensic information
    - Implement memory usage visualization tools

### Version 3.0

- **Next-Generation Security**
    - Implement Memory Protection Keys support (MPK on x86_64)
    - Develop fine-grained permission management for memory regions
    - Create adaptive security policies based on runtime threat assessment
- **Enterprise Features**
    - Add support for centralized security policy management
    - Implement security event logging and monitoring integrations
    - Develop remote attestation capabilities for memory allocator integrity