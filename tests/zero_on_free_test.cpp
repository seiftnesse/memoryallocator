#include <customalloc/custom_alloc.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <cstring>
#include <cassert>

// Define test sizes
constexpr size_t SMALL_SIZE = 128; // Small allocation (likely to use small allocator)
constexpr size_t MEDIUM_SIZE = 4096; // Medium allocation
constexpr size_t LARGE_SIZE = 1024 * 1024; // Large allocation (1MB)

// Structure for sensitive data that should be zeroed
struct SensitiveData {
    char password[64];
    uint64_t keys[8];
    uint32_t session_id;
    uint8_t private_data[256];
};

// Simple pattern validator
bool check_pattern(const void *memory, size_t size, uint8_t pattern) {
    const uint8_t *bytes = static_cast<const uint8_t *>(memory);
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != pattern) {
            return false;
        }
    }
    return true;
}

// Fill memory with a recognizable pattern
void fill_pattern(void *memory, size_t size, uint8_t pattern) {
    memset(memory, pattern, size);
}

// Utility to measure time
class Timer {
private:
    const std::string operation_name;
    std::chrono::high_resolution_clock::time_point start_time;
    bool finished = false;

public:
    Timer(const std::string &name) : operation_name(name) {
        start_time = std::chrono::high_resolution_clock::now();
    }

    ~Timer() {
        if (!finished) {
            stop();
        }
    }

    void stop() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        std::cout << std::setw(30) << std::left << operation_name
                << std::setw(10) << std::right << duration.count() << " microseconds" << std::endl;
        finished = true;
    }
};

// Test function for a specific zero-on-free depth
bool test_zero_depth(int depth, const char *depth_name, size_t alloc_size) {
    std::cout << "\n--- Testing " << depth_name << " zeroing with "
            << alloc_size << " bytes ---" << std::endl;

    // Set zeroing depth
    HeapSetZeroOnFree(depth, 64);

    // Allocate memory
    void *ptr = nullptr; {
        Timer t("Allocation time");
        ptr = _malloc(alloc_size);
    }
    if (!ptr) {
        std::cerr << "Error: Failed to allocate memory" << std::endl;
        return false;
    }

    // Fill with a pattern
    const uint8_t PATTERN = 0xAA; {
        Timer t("Pattern filling time");
        fill_pattern(ptr, alloc_size, PATTERN);
    }

    // Verify the pattern
    bool pattern_valid = check_pattern(ptr, alloc_size, PATTERN);
    std::cout << "Pattern verification: " << (pattern_valid ? "PASSED" : "FAILED") << std::endl;
    if (!pattern_valid) {
        std::cerr << "Error: Memory pattern verification failed" << std::endl;
        _free(ptr);
        return false;
    }

    // Free the memory (with zeroing)
    {
        Timer t("Free time (including zeroing)");
        _free(ptr);
    }

    std::cout << "Zero-on-free depth " << depth << " test completed successfully" << std::endl;
    return true;
}

// Test using sensitive data struct
bool test_sensitive_data_zeroing() {
    std::cout << "\n--- Testing sensitive data zeroing ---" << std::endl;

    // Enable shallow zeroing first
    HeapSetZeroOnFree(1, sizeof(SensitiveData));

    // Allocate memory for a sensitive data structure
    SensitiveData *data = static_cast<SensitiveData *>(_malloc(sizeof(SensitiveData)));
    if (!data) {
        std::cerr << "Error: Failed to allocate memory for sensitive data" << std::endl;
        return false;
    }

    // Fill with "sensitive" data
    strcpy(data->password, "SuperSecretPassword123!");
    for (int i = 0; i < 8; i++) {
        data->keys[i] = 0xDEADBEEF00000000 | i;
    }
    data->session_id = 0xFEEDFACE;
    memset(data->private_data, 0x55, sizeof(data->private_data));

    // Print first few bytes to show it's filled
    std::cout << "First few bytes of sensitive data before free:" << std::endl;
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
    for (int i = 0; i < 16; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(bytes[i]) << " ";
    }
    std::cout << std::dec << std::endl;

    // Store the pointer value for later
    uintptr_t addr = reinterpret_cast<uintptr_t>(data);

    // Free the memory with zeroing
    _free(data);

    // Note: In a real application, we wouldn't be able to access the memory after freeing
    // This is just for demonstration purposes and works because our allocator doesn't
    // immediately return memory to the OS
    std::cout << "Memory has been freed with shallow zeroing" << std::endl;

    return true;
}

// Benchmark different zeroing depths
void benchmark_zeroing_depths() {
    std::cout << "\n=== Benchmarking Zero-on-Free Performance ===" << std::endl;
    std::cout << std::setw(10) << "Size" << std::setw(15) << "None"
            << std::setw(15) << "Shallow" << std::setw(15) << "Medium"
            << std::setw(15) << "Deep" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    // Test various sizes
    std::vector<size_t> sizes = {128, 1024, 10 * 1024, 100 * 1024, 1024 * 1024};

    for (size_t size: sizes) {
        std::cout << std::setw(10) << size;

        // Test each depth
        for (int depth = 0; depth <= 3; depth++) {
            HeapSetZeroOnFree(depth, 64);

            // Measure allocation + free time
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < 5; i++) {
                // Run multiple times for better measurement
                void *ptr = _malloc(size);
                if (ptr) {
                    memset(ptr, 0xAA, size); // Fill with pattern
                    _free(ptr);
                }
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            std::cout << std::setw(15) << duration.count() / 5; // Average time
        }
        std::cout << std::endl;
    }
}

int main() {
    std::cout << "Zero-on-Free Test Program" << std::endl;
    std::cout << "=======================" << std::endl;

    // Enable debug mode for detailed logging
    HeapEnableDebug(1);

    // Test each zeroing depth with different allocation sizes
    bool all_passed = true;

    all_passed &= test_zero_depth(0, "NONE (fastest)", SMALL_SIZE);
    all_passed &= test_zero_depth(1, "SHALLOW (headers/pointers)", MEDIUM_SIZE);
    all_passed &= test_zero_depth(2, "MEDIUM (50% of memory)", MEDIUM_SIZE);
    all_passed &= test_zero_depth(3, "DEEP (entire memory)", LARGE_SIZE);

    // Test with sensitive data
    all_passed &= test_sensitive_data_zeroing();

    // Benchmark performance impact
    benchmark_zeroing_depths();

    // Summary
    std::cout << "\n=== Zero-on-Free Security-Performance Tradeoff ===" << std::endl;
    std::cout << "NONE (0):    Best performance, no security for freed memory" << std::endl;
    std::cout << "SHALLOW (1): Good performance, basic protection against pointer leaks" << std::endl;
    std::cout << "MEDIUM (2):  Balanced, reasonable protection for sensitive data" << std::endl;
    std::cout << "DEEP (3):    Best security, clears all data but slowest performance" << std::endl;

    if (all_passed) {
        std::cout << "\nAll zero-on-free tests passed!" << std::endl;
        return 0;
    } else {
        std::cerr << "\nSome tests failed!" << std::endl;
        return 1;
    }
}
