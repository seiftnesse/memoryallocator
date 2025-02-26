#include <customalloc/CustomAlloc.h>
#include <cstring>
#include <iostream>
#include <vector>

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "Assertion failed: " << message << std::endl; \
            return 1; \
        } \
    } while (0)

#define SMALL_ALLOC_SIZE 200
#define MEDIUM_ALLOC_SIZE 2000
#define LARGE_ALLOC_SIZE 20000
#define MEGA_ALLOC_SIZE 1024*1024

void print_memory_stats() {
    size_t allocated = 0;
    size_t freed = 0;
    size_t count = 0;
    size_t peak = 0;

    HeapGetStats(&allocated, &freed, &count, &peak);
    std::cout << "Memory stats:" << std::endl;
    std::cout << "  Allocated: " << allocated << " bytes" << std::endl;
    std::cout << "  Freed: " << freed << " bytes" << std::endl;
    std::cout << "  Active allocations: " << count << std::endl;
    std::cout << "  Peak usage: " << peak << " bytes" << std::endl;
    std::cout << "  Fragmentation: " << HeapGetFragmentation() * 100.0f << "%" << std::endl;
}

int main() {
    std::cout << "Enhanced memory allocator test" << std::endl;

    // Enable debug mode
    HeapEnableDebug(1);
    HeapEnableTracking(1);

    // Test 1: Test small allocations (should use small pool)
    std::cout << "\nTest 1: Small allocations" << std::endl;
    std::vector<void*> small_ptrs;
    for (int i = 0; i < 100; i++) {
        void* ptr = _malloc(SMALL_ALLOC_SIZE);
        TEST_ASSERT(ptr != nullptr, "Small allocation failed");
        memset(ptr, 0xAA, SMALL_ALLOC_SIZE);
        small_ptrs.push_back(ptr);
    }
    print_memory_stats();

    // Test 2: Test medium allocations
    std::cout << "\nTest 2: Medium allocations" << std::endl;
    std::vector<void*> medium_ptrs;
    for (int i = 0; i < 50; i++) {
        void* ptr = _malloc(MEDIUM_ALLOC_SIZE);
        TEST_ASSERT(ptr != nullptr, "Medium allocation failed");
        memset(ptr, 0xBB, MEDIUM_ALLOC_SIZE);
        medium_ptrs.push_back(ptr);
    }
    print_memory_stats();

    // Test 3: Test large allocations
    std::cout << "\nTest 3: Large allocations" << std::endl;
    std::vector<void*> large_ptrs;
    for (int i = 0; i < 10; i++) {
        void* ptr = _malloc(LARGE_ALLOC_SIZE);
        TEST_ASSERT(ptr != nullptr, "Large allocation failed");
        memset(ptr, 0xCC, LARGE_ALLOC_SIZE);
        large_ptrs.push_back(ptr);
    }
    print_memory_stats();

    // Test 4: Free in random order to test fragmentation handling
    std::cout << "\nTest 4: Free medium allocations" << std::endl;
    for (void* ptr : medium_ptrs) {
        _free(ptr);
    }
    medium_ptrs.clear();
    print_memory_stats();

    // Test 5: Reallocate memory
    std::cout << "\nTest 5: Reallocate memory" << std::endl;
    for (size_t i = 0; i < large_ptrs.size(); i++) {
        void* old_ptr = large_ptrs[i];
        void* new_ptr = _realloc(old_ptr, LARGE_ALLOC_SIZE * 2);
        TEST_ASSERT(new_ptr != nullptr, "Reallocation failed");

        // Verify first part data is preserved (should be 0xCC)
        unsigned char* bytes = static_cast<unsigned char*>(new_ptr);
        for (size_t j = 0; j < LARGE_ALLOC_SIZE; j++) {
            TEST_ASSERT(bytes[j] == 0xCC, "Memory content not preserved in reallocation");
        }

        large_ptrs[i] = new_ptr;
    }
    print_memory_stats();

    // Test 6: Mega allocation
    std::cout << "\nTest 6: Mega allocation" << std::endl;
    void* mega_ptr = _malloc(MEGA_ALLOC_SIZE);
    TEST_ASSERT(mega_ptr != nullptr, "Mega allocation failed");
    memset(mega_ptr, 0xDD, MEGA_ALLOC_SIZE);
    print_memory_stats();

    // Test 7: Debug malloc with source tracking
    std::cout << "\nTest 7: Debug malloc with tracking" << std::endl;
    void* debug_ptr = _malloc_debug(10000, "enhanced_test.cpp", 123);
    TEST_ASSERT(debug_ptr != nullptr, "Debug allocation failed");
    print_memory_stats();

    // Test 8: Print heap status
    std::cout << "\nTest 8: Heap status report" << std::endl;
    HeapPrintStatus();

    // Test 9: Free everything
    std::cout << "\nTest 9: Free all memory" << std::endl;
    for (void* ptr : small_ptrs) {
        _free(ptr);
    }
    for (void* ptr : large_ptrs) {
        _free(ptr);
    }
    _free(mega_ptr);
    _free(debug_ptr);
    print_memory_stats();

    std::cout << "\nAll tests passed successfully!" << std::endl;
    return 0;
}