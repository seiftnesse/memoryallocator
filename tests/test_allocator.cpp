#include <gtest/gtest.h>
#include <customalloc/CustomAlloc.h>
#include <vector>
#include <algorithm>

// Test fixture for CustomAlloc tests
class CustomAllocTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Custom allocator is auto-initialized on first use
    }

    void TearDown() override {
        // Nothing to tear down
    }
};

// Test basic allocation and deallocation
TEST_F(CustomAllocTest, BasicAllocation) {
    // Allocate memory
    const size_t size = 128;
    void* ptr = _malloc(size);

    // Verify allocation succeeded
    ASSERT_NE(ptr, nullptr);

    // Write to the allocated memory to ensure it's usable
    std::memset(ptr, 0xAB, size);

    // Free the memory
    _free(ptr);
}

// Test multiple allocations
TEST_F(CustomAllocTest, MultipleAllocations) {
    const int numAllocs = 100;
    std::vector<void*> pointers;

    // Perform multiple allocations of different sizes
    for (int i = 0; i < numAllocs; i++) {
        size_t size = 64 * (i + 1);  // Different sizes
        void* ptr = _malloc(size);
        ASSERT_NE(ptr, nullptr);
        pointers.push_back(ptr);

        // Write unique pattern to each allocation
        std::memset(ptr, static_cast<int>(i & 0xFF), size);
    }

    // Free the allocations in random order to test fragmentation handling
    std::random_shuffle(pointers.begin(), pointers.end());
    for (void* ptr : pointers) {
        _free(ptr);
    }
}

// Test reallocation functionality
TEST_F(CustomAllocTest, Reallocation) {
    // Allocate initial memory
    const size_t initialSize = 256;
    void* ptr = _malloc(initialSize);
    ASSERT_NE(ptr, nullptr);

    // Fill memory with pattern
    std::memset(ptr, 0xCD, initialSize);

    // Reallocate to larger size
    const size_t newSize = 512;
    void* newPtr = _realloc(ptr, newSize);
    ASSERT_NE(newPtr, nullptr);

    // Validate first part of memory still has our pattern
    for (size_t i = 0; i < initialSize; i++) {
        ASSERT_EQ(static_cast<unsigned char*>(newPtr)[i], 0xCD)
            << "Memory content changed after reallocation at position " << i;
    }

    // Free the memory
    _free(newPtr);
}

// Test allocation of zero bytes
TEST_F(CustomAllocTest, ZeroSizeAllocation) {
    void* ptr = _malloc(0);
    ASSERT_EQ(ptr, nullptr);
}

// Test reallocation with null pointer (should act like malloc)
TEST_F(CustomAllocTest, ReallocWithNullPointer) {
    const size_t size = 128;
    void* ptr = _realloc(nullptr, size);
    ASSERT_NE(ptr, nullptr);
    _free(ptr);
}

// Test reallocation to zero size (should act like free)
TEST_F(CustomAllocTest, ReallocToZeroSize) {
    void* ptr = _malloc(128);
    ASSERT_NE(ptr, nullptr);

    void* result = _realloc(ptr, 0);
    ASSERT_EQ(result, nullptr);
    // No need to free ptr as realloc(ptr, 0) should have freed it
}

// Test large allocations
TEST_F(CustomAllocTest, LargeAllocation) {
    // 1 MB allocation
    const size_t size = 1024 * 1024;
    void* ptr = _malloc(size);
    ASSERT_NE(ptr, nullptr);

    // Write to entire allocation to ensure it's valid
    std::memset(ptr, 0xEF, size);

    _free(ptr);
}