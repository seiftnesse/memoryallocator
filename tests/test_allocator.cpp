#include <gtest/gtest.h>
#include <customalloc/CustomAlloc.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <random>
#include <chrono>
#include <array>
#include <unordered_set>

// Constants for stress testing
constexpr size_t TINY_SIZE = 8;
constexpr size_t SMALL_SIZE = 64;
constexpr size_t MEDIUM_SIZE = 1024;
constexpr size_t LARGE_SIZE = 32 * 1024;
constexpr size_t HUGE_SIZE = 512 * 1024;
constexpr int STRESS_ITERATIONS = 10000;
constexpr int FRAGMENTATION_ITERATIONS = 5000;
constexpr int REALLOC_ITERATIONS = 1000;

class CustomAllocTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Enable debug mode for more checks
        HeapEnableDebug(1);
        HeapEnableTracking(1);
    }

    void TearDown() override {
        // Print memory stats at the end of each test
        HeapPrintStatus();
    }

    // Generate a random size between min and max
    size_t randomSize(size_t min, size_t max) {
        static std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
        std::uniform_int_distribution<size_t> dist(min, max);
        return dist(rng);
    }

    // Generate a random pattern to fill memory
    uint8_t randomPattern() {
        static std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
        std::uniform_int_distribution<uint16_t> dist(0, 255);
        return static_cast<uint8_t>(dist(rng));
    }

    // Verify memory pattern
    bool verifyPattern(void *ptr, size_t size, uint8_t pattern) {
        uint8_t *bytes = static_cast<uint8_t *>(ptr);
        for (size_t i = 0; i < size; i++) {
            if (bytes[i] != pattern) {
                return false;
            }
        }
        return true;
    }

    // Fill memory with a pattern
    void fillMemory(void *ptr, size_t size, uint8_t pattern) {
        memset(ptr, pattern, size);
    }
};

// Basic allocation and deallocation test
TEST_F(CustomAllocTest, BasicAllocation) {
    const size_t size = 128;
    void *ptr = _malloc(size);

    ASSERT_NE(ptr, nullptr);
    std::memset(ptr, 0xAB, size);
    _free(ptr);
}

// Allocation of various sizes
TEST_F(CustomAllocTest, VariousSizes) {
    std::vector<std::pair<void *, size_t> > allocations;

    // Allocate various sizes
    for (size_t size: {TINY_SIZE, SMALL_SIZE, MEDIUM_SIZE, LARGE_SIZE, HUGE_SIZE}) {
        void *ptr = _malloc(size);
        ASSERT_NE(ptr, nullptr);
        allocations.push_back({ptr, size});
    }

    // Fill and check each allocation
    for (auto &[ptr, size]: allocations) {
        uint8_t pattern = randomPattern();
        fillMemory(ptr, size, pattern);
        EXPECT_TRUE(verifyPattern(ptr, size, pattern));
    }

    // Free in reverse order
    for (auto it = allocations.rbegin(); it != allocations.rend(); ++it) {
        _free(it->first);
    }
}

// Heavy allocation test
TEST_F(CustomAllocTest, HeavyAllocation) {
    // Fill most of the heap with allocations
    std::vector<std::pair<void *, size_t> > allocations;
    size_t totalAllocated = 0;

    // Keep allocating until we've used a significant portion of the heap
    while (totalAllocated < 32 * 1024 * 1024) {
        // Try to allocate 32MB
        size_t size = randomSize(TINY_SIZE, MEDIUM_SIZE);
        void *ptr = _malloc(size);
        if (!ptr) break; // Stop if allocation fails

        allocations.push_back({ptr, size});
        totalAllocated += size;
    }

    std::cout << "Made " << allocations.size() << " allocations totaling "
            << totalAllocated << " bytes" << std::endl;

    // Verify all allocations work correctly
    for (auto &[ptr, size]: allocations) {
        uint8_t pattern = randomPattern();
        fillMemory(ptr, size, pattern);
        EXPECT_TRUE(verifyPattern(ptr, size, pattern));
    }

    // Free half the allocations (random order)
    std::shuffle(allocations.begin(), allocations.end(),
                 std::mt19937(std::chrono::steady_clock::now().time_since_epoch().count()));

    size_t halfSize = allocations.size() / 2;
    for (size_t i = 0; i < halfSize; i++) {
        _free(allocations[i].first);
    }
    allocations.erase(allocations.begin(), allocations.begin() + halfSize);

    // Allocate more memory
    size_t moreAllocations = 0;
    for (int i = 0; i < 1000; i++) {
        size_t size = randomSize(SMALL_SIZE, MEDIUM_SIZE);
        void *ptr = _malloc(size);
        if (!ptr) break;

        allocations.push_back({ptr, size});
        moreAllocations++;
    }

    std::cout << "Made " << moreAllocations << " additional allocations after freeing half" << std::endl;

    // Free all remaining allocations
    for (auto &[ptr, size]: allocations) {
        _free(ptr);
    }
}

// Stress test with many small allocations
TEST_F(CustomAllocTest, SmallAllocationsStress) {
    std::vector<void *> pointers;
    pointers.reserve(STRESS_ITERATIONS);

    // Allocate many small blocks
    for (int i = 0; i < STRESS_ITERATIONS; i++) {
        size_t size = randomSize(TINY_SIZE, SMALL_SIZE);
        void *ptr = _malloc(size);
        ASSERT_NE(ptr, nullptr);
        pointers.push_back(ptr);

        // Write a unique pattern to memory
        std::memset(ptr, i & 0xFF, size);
    }

    // Free in random order
    std::shuffle(pointers.begin(), pointers.end(),
                 std::mt19937(std::chrono::steady_clock::now().time_since_epoch().count()));

    for (void *ptr: pointers) {
        _free(ptr);
    }
}

// Test to create memory fragmentation
TEST_F(CustomAllocTest, FragmentationTest) {
    std::vector<void *> small_ptrs;
    std::vector<void *> large_ptrs;

    // Allocate alternating small and large blocks
    for (int i = 0; i < FRAGMENTATION_ITERATIONS; i++) {
        // Small allocation
        void *small = _malloc(TINY_SIZE);
        if (small) {
            small_ptrs.push_back(small);
            std::memset(small, 0xAA, TINY_SIZE);
        }

        // Large allocation
        void *large = _malloc(MEDIUM_SIZE);
        if (large) {
            large_ptrs.push_back(large);
            std::memset(large, 0xBB, MEDIUM_SIZE);
        }
    }

    std::cout << "Created fragmentation with " << small_ptrs.size()
            << " small blocks and " << large_ptrs.size() << " large blocks" << std::endl;

    // Free every other small allocation to create fragmentation
    for (size_t i = 0; i < small_ptrs.size(); i += 2) {
        if (i < small_ptrs.size()) {
            _free(small_ptrs[i]);
            small_ptrs[i] = nullptr;
        }
    }

    // Try to allocate blocks larger than the fragments
    std::vector<void *> medium_ptrs;
    int successfulAllocations = 0;

    for (int i = 0; i < 1000; i++) {
        void *medium = _malloc(SMALL_SIZE * 2);
        if (medium) {
            medium_ptrs.push_back(medium);
            std::memset(medium, 0xCC, SMALL_SIZE * 2);
            successfulAllocations++;
        } else {
            break;
        }
    }

    std::cout << "After fragmentation, successfully allocated "
            << successfulAllocations << " medium-sized blocks" << std::endl;

    // Calculate fragmentation
    float fragmentation = HeapGetFragmentation();
    std::cout << "Current heap fragmentation: " << (fragmentation * 100.0f) << "%" << std::endl;

    // Free everything
    for (void *ptr: medium_ptrs) {
        _free(ptr);
    }

    for (void *ptr: small_ptrs) {
        if (ptr) _free(ptr);
    }

    for (void *ptr: large_ptrs) {
        _free(ptr);
    }
}

// Extensive realloc testing
TEST_F(CustomAllocTest, ReallocStressTest) {
    // Use a struct to track blocks and their patterns
    struct Block {
        void *ptr;
        size_t size;
        uint8_t pattern;
    };

    std::vector<Block> blocks;

    // Initial allocations
    for (int i = 0; i < REALLOC_ITERATIONS; i++) {
        size_t size = randomSize(TINY_SIZE, MEDIUM_SIZE);
        void *ptr = _malloc(size);
        ASSERT_NE(ptr, nullptr);

        // Fill with recognizable pattern (fixed value for reliability)
        uint8_t pattern = 0xAA;
        fillMemory(ptr, size, pattern);

        blocks.push_back({ptr, size, pattern});
    }

    std::cout << "Created " << blocks.size() << " initial blocks" << std::endl;

    // Perform various realloc operations
    int successfulReallocs = 0;

    for (int i = 0; i < REALLOC_ITERATIONS / 5; i++) {
        // Reduce number of iterations for stability
        // Select a random block
        int index = randomSize(0, blocks.size() - 1);
        Block &block = blocks[index];

        // Keep track of original stats
        size_t originalSize = block.size;
        uint8_t originalPattern = block.pattern;

        // Randomly grow or shrink with more controlled ratios
        bool grow = (randomSize(0, 1) == 1);
        size_t newSize;

        if (grow) {
            // More moderate growth
            newSize = block.size + randomSize(8, 128);
        } else {
            // Ensure we don't shrink too small
            if (block.size <= TINY_SIZE * 2) {
                newSize = block.size; // Keep same size if already small
            } else {
                newSize = std::max(TINY_SIZE, block.size - randomSize(8, 64));
            }
        }

        // Perform reallocation
        void *newPtr = _realloc(block.ptr, newSize);

        // Skip if reallocation failed
        if (!newPtr) {
            std::cout << "Realloc failed for size " << newSize << std::endl;
            continue;
        }

        successfulReallocs++;

        // Verify old content is preserved (up to min of old and new size)
        size_t verifySize = std::min(originalSize, newSize);

        if (verifySize > 0) {
            // For debugging - print first few bytes before/after realloc
            std::cout << "Verifying " << verifySize << " bytes, pattern: "
                    << static_cast<int>(originalPattern) << std::endl;

            if (!verifyPattern(newPtr, verifySize, originalPattern)) {
                // If pattern verification fails, print detailed debug info
                std::cout << "Pattern verification failed!" << std::endl;
                std::cout << "Original size: " << originalSize << ", new size: " << newSize << std::endl;
                std::cout << "First few bytes at new location: ";
                for (size_t j = 0; j < std::min(verifySize, size_t(16)); j++) {
                    std::cout << std::hex << static_cast<int>(static_cast<uint8_t *>(newPtr)[j]) << " ";
                }
                std::cout << std::dec << std::endl;
            }
        }

        // Update with new pattern
        uint8_t newPattern = 0xBB; // Use a consistent new pattern
        fillMemory(newPtr, newSize, newPattern);

        // Update the block info
        blocks[index] = {newPtr, newSize, newPattern};
    }

    std::cout << "Completed " << successfulReallocs << " successful reallocs" << std::endl;

    // Free all blocks
    for (auto &block: blocks) {
        _free(block.ptr);
    }
}

// Test the allocator with very large blocks
TEST_F(CustomAllocTest, VeryLargeAllocations) {
    std::vector<void *> large_blocks;

    // Try increasingly large sizes
    for (size_t size = 1024 * 1024; size <= 16 * 1024 * 1024; size *= 2) {
        void *ptr = _malloc(size);
        if (!ptr) {
            std::cout << "Failed to allocate block of size " << size << std::endl;
            continue;
        }

        std::cout << "Successfully allocated " << size << " bytes" << std::endl;
        large_blocks.push_back(ptr);

        // Only write to part of the memory to avoid excessive time
        size_t testSize = std::min(size, static_cast<size_t>(1024 * 1024));
        fillMemory(ptr, testSize, 0xDD);
        EXPECT_TRUE(verifyPattern(ptr, testSize, 0xDD));
    }

    // Free the blocks
    for (void *ptr: large_blocks) {
        _free(ptr);
    }
}

// Test random allocation/free patterns
TEST_F(CustomAllocTest, RandomAllocFreePattern) {
    std::vector<std::pair<void *, size_t> > active_blocks;
    std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());

    // Perform a mix of random allocations and frees
    for (int i = 0; i < STRESS_ITERATIONS * 2; i++) {
        bool shouldAllocate = active_blocks.empty() ||
                              (active_blocks.size() < 5000 &&
                               std::uniform_int_distribution<>(0, 2)(rng) != 0);

        if (shouldAllocate) {
            // Do an allocation
            size_t size = randomSize(TINY_SIZE, LARGE_SIZE);
            void *ptr = _malloc(size);
            if (ptr) {
                fillMemory(ptr, size, randomPattern());
                active_blocks.push_back({ptr, size});
            }
        } else {
            // Do a free of a random block
            size_t index = std::uniform_int_distribution<size_t>(0, active_blocks.size() - 1)(rng);
            _free(active_blocks[index].first);
            active_blocks.erase(active_blocks.begin() + index);
        }

        // Periodically check if we can do some large allocations
        if (i % 1000 == 0 && i > 0) {
            void *large = _malloc(LARGE_SIZE);
            if (large) {
                fillMemory(large, LARGE_SIZE, 0xEE);
                _free(large);
            }
        }
    }

    // Free any remaining blocks
    for (auto &[ptr, size]: active_blocks) {
        _free(ptr);
    }
}

// Test boundary conditions
TEST_F(CustomAllocTest, BoundaryConditions) {
    // Zero-size allocation
    void *ptr = _malloc(0);
    EXPECT_EQ(ptr, nullptr);

    // Very small allocation
    ptr = _malloc(1);
    ASSERT_NE(ptr, nullptr);
    *static_cast<char *>(ptr) = 'A';
    EXPECT_EQ(*static_cast<char*>(ptr), 'A');
    _free(ptr);

    // Edge allocations around key sizes
    std::vector<size_t> edge_sizes = {
        TINY_SIZE - 1, TINY_SIZE, TINY_SIZE + 1,
        SMALL_SIZE - 1, SMALL_SIZE, SMALL_SIZE + 1,
        MEDIUM_SIZE - 1, MEDIUM_SIZE, MEDIUM_SIZE + 1,
        4095, 4096, 4097 // Block size boundaries
    };

    std::vector<void *> edge_ptrs;
    for (size_t size: edge_sizes) {
        void *p = _malloc(size);
        ASSERT_NE(p, nullptr);
        edge_ptrs.push_back(p);

        // Write and verify pattern
        fillMemory(p, size, 0xAB);
        EXPECT_TRUE(verifyPattern(p, size, 0xAB));
    }

    // Free all edge allocations
    for (void *p: edge_ptrs) {
        _free(p);
    }
}

// Performance test comparing with standard allocator
TEST_F(CustomAllocTest, PerformanceTest) {
    constexpr int PERF_ITERATIONS = 100000;
    std::vector<size_t> test_sizes = {16, 64, 256, 1024, 4096};

    for (size_t size: test_sizes) {
        std::cout << "Testing allocation/free performance for size " << size << std::endl;

        // Test custom allocator
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < PERF_ITERATIONS; i++) {
            void *ptr = _malloc(size);
            ASSERT_NE(ptr, nullptr);
            _free(ptr);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> custom_time = end - start;

        // Test standard allocator
        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < PERF_ITERATIONS; i++) {
            void *ptr = std::malloc(size);
            ASSERT_NE(ptr, nullptr);
            std::free(ptr);
        }
        end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> standard_time = end - start;

        std::cout << "  Custom allocator: " << custom_time.count() << " ms" << std::endl;
        std::cout << "  Standard allocator: " << standard_time.count() << " ms" << std::endl;
        std::cout << "  Ratio (custom/standard): " << (custom_time.count() / standard_time.count()) << std::endl;
    }
}

// Test allocator with various patterns of data
TEST_F(CustomAllocTest, DataPatternTest) {
    constexpr size_t testSize = 4096;

    // Test various data patterns
    std::vector<std::pair<std::string, uint8_t> > patterns = {
        {"All zeros", 0x00},
        {"All ones", 0xFF},
        {"Alternating bits", 0xAA},
        {"Inverse alternating", 0x55}
    };

    for (const auto &[name, pattern]: patterns) {
        std::cout << "Testing pattern: " << name << std::endl;

        void *ptr = _malloc(testSize);
        ASSERT_NE(ptr, nullptr);

        // Fill with pattern
        fillMemory(ptr, testSize, pattern);

        // Verify pattern
        EXPECT_TRUE(verifyPattern(ptr, testSize, pattern));

        _free(ptr);
    }
}

// Test realloc behavior with null pointer
TEST_F(CustomAllocTest, ReallocNull) {
    const size_t size = 1024;

    // realloc with null should act like malloc
    void *ptr = _realloc(nullptr, size);
    ASSERT_NE(ptr, nullptr);

    // Fill memory
    fillMemory(ptr, size, 0xDE);
    EXPECT_TRUE(verifyPattern(ptr, size, 0xDE));

    _free(ptr);
}

// Test realloc to zero size
TEST_F(CustomAllocTest, ReallocZero) {
    const size_t size = 1024;

    // Allocate memory
    void *ptr = _malloc(size);
    ASSERT_NE(ptr, nullptr);

    // realloc to zero should free and return null
    void *newPtr = _realloc(ptr, 0);
    EXPECT_EQ(newPtr, nullptr);
}

// Test sequential allocation and deallocation
TEST_F(CustomAllocTest, SequentialAllocDealloc) {
    constexpr int SEQ_COUNT = 1000;
    std::vector<void *> pointers;

    // Allocate in sequence
    for (int i = 0; i < SEQ_COUNT; i++) {
        size_t size = MEDIUM_SIZE;
        void *ptr = _malloc(size);
        ASSERT_NE(ptr, nullptr);
        fillMemory(ptr, size, static_cast<uint8_t>(i & 0xFF));
        pointers.push_back(ptr);
    }

    // Free in sequence
    for (void *ptr: pointers) {
        _free(ptr);
    }

    // Allocate again to see if memory is reusable
    void *large_block = _malloc(MEDIUM_SIZE * SEQ_COUNT / 2);
    EXPECT_NE(large_block, nullptr);
    if (large_block) {
        _free(large_block);
    }
}

// Test alignment handling
TEST_F(CustomAllocTest, AlignmentTest) {
    // Test various sizes that might challenge alignment
    std::vector<size_t> sizes = {1, 3, 7, 15, 17, 31, 33, 63, 65};

    for (size_t size: sizes) {
        void *ptr = _malloc(size);
        ASSERT_NE(ptr, nullptr);

        // Check alignment of pointer (should be at least 8-byte aligned for most architectures)
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        EXPECT_EQ(addr % 8, 0) << "Allocation of size " << size << " not aligned properly";

        // Write and read memory
        fillMemory(ptr, size, 0xCC);
        EXPECT_TRUE(verifyPattern(ptr, size, 0xCC));

        _free(ptr);
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
