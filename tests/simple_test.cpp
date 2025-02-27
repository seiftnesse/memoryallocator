#include <customalloc/custom_alloc.h>
#include <cstring>
#include <iostream>

#include <cstddef>

#define TEST_ASSERT(condition, message) \
do { \
    if (!(condition)) { \
        std::cerr << "Assertion failed: " << message << std::endl; \
        return 1; \
    } \
} while (0)

int main() {
    // Test 1: Basic allocation
    const size_t size = 1024;
    void *ptr = _malloc(size);
    TEST_ASSERT(ptr != nullptr, "Memory allocation failed");

    // Test 2: Write to allocated memory
    memset(ptr, 0xAA, size);

    // Test 3: Reallocation
    void *new_ptr = _realloc(ptr, size * 2);
    TEST_ASSERT(new_ptr != nullptr, "Memory reallocation failed");

    // Test 4: Check if content was preserved during reallocation
    unsigned char *bytes = static_cast<unsigned char *>(new_ptr);
    for (size_t i = 0; i < size; i++) {
        TEST_ASSERT(bytes[i] == 0xAA, "Memory content was not preserved during reallocation");
    }

    // Test 5: Free memory
    _free(new_ptr);

    // Test 6: Multiple allocations and frees
    const int num_allocations = 100;
    void *ptrs[num_allocations];

    for (int i = 0; i < num_allocations; i++) {
        ptrs[i] = _malloc((i + 1) * 128);
        TEST_ASSERT(ptrs[i] != nullptr, "Failed multiple allocation test");
    }

    for (int i = 0; i < num_allocations; i++) {
        _free(ptrs[i]);
    }

    std::cout << "All simple tests passed!" << std::endl;
    return 0;
}
