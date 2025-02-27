#include <customalloc/CustomAlloc.h>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <cstring>

// Global counter for tracking allocations across threads
std::atomic<int> successful_allocs(0);
std::atomic<int> successful_frees(0);
std::atomic<int> failed_allocs(0);

// Configuration
constexpr int NUM_THREADS = 8;
constexpr int ALLOCS_PER_THREAD = 1000;
constexpr int MIN_ALLOC_SIZE = 8;
constexpr int MAX_ALLOC_SIZE = 16 * 1024;
constexpr int REALLOC_CHANCE = 30; // Percentage chance of realloc vs free

// Thread function to test concurrent allocations
void worker_thread(int thread_id) {
    std::vector<void*> allocations;
    allocations.reserve(ALLOCS_PER_THREAD);

    // Create thread-local random number generator
    unsigned int seed = thread_id * 1000 + static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count());
    std::mt19937 gen(seed);
    std::uniform_int_distribution<size_t> size_dist(MIN_ALLOC_SIZE, MAX_ALLOC_SIZE - 1);
    std::uniform_int_distribution<int> chance_dist(0, 99);

    std::cout << "Thread " << thread_id << " started\n";

    for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
        // Randomly decide size of allocation
        size_t size = size_dist(gen);

        // Allocate memory
        void* ptr = _malloc(size);

        if (ptr) {
            // Successful allocation
            successful_allocs.fetch_add(1);

            // Write to memory to verify it's usable
            memset(ptr, thread_id & 0xFF, size);

            // Add to our allocation list
            allocations.push_back(ptr);
        } else {
            failed_allocs.fetch_add(1);
        }

        // Randomly free or realloc some previous allocations
        if (!allocations.empty() && (chance_dist(gen) < 40)) {
            std::uniform_int_distribution<int> index_dist(0, allocations.size() - 1);
            int index = index_dist(gen);
            void* old_ptr = allocations[index];

            // Decide whether to realloc or free
            if (chance_dist(gen) < REALLOC_CHANCE) {
                // Realloc to a new random size
                size_t new_size = size_dist(gen);
                void* new_ptr = _realloc(old_ptr, new_size);

                if (new_ptr) {
                    // Write to memory to verify it's usable
                    memset(new_ptr, (thread_id * 2) & 0xFF, new_size);
                    allocations[index] = new_ptr;
                } else {
                    // Realloc failed but old pointer is still valid
                    failed_allocs.fetch_add(1);
                }
            } else {
                // Free the memory
                _free(old_ptr);
                successful_frees.fetch_add(1);
                allocations.erase(allocations.begin() + index);
            }
        }
    }

    // Clean up any remaining allocations
    for (void* ptr : allocations) {
        _free(ptr);
        successful_frees.fetch_add(1);
    }

    std::cout << "Thread " << thread_id << " completed\n";
}

int main() {
    std::cout << "Thread Safety Test for CustomAlloc" << std::endl;

    // Enable thread safety and debugging
    HeapEnableThreadSafety(1);
    HeapEnableDebug(1);
    HeapEnableTracking(1);

    // Create threads
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker_thread, i);
    }

    // Join all threads
    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Print statistics
    std::cout << "\nThread Safety Test Results:" << std::endl;
    std::cout << "-------------------------" << std::endl;
    std::cout << "Threads: " << NUM_THREADS << std::endl;
    std::cout << "Time: " << duration.count() << " ms" << std::endl;
    std::cout << "Successful allocations: " << successful_allocs.load() << std::endl;
    std::cout << "Successful frees: " << successful_frees.load() << std::endl;
    std::cout << "Failed allocations: " << failed_allocs.load() << std::endl;

    // Print heap status
    std::cout << "\nFinal Heap Status:" << std::endl;
    HeapPrintStatus();

    // Now run a performance comparison with thread safety disabled
    std::cout << "\nRunning comparison with thread safety disabled..." << std::endl;

    // Reset counters
    successful_allocs = 0;
    successful_frees = 0;
    failed_allocs = 0;

    // Disable thread safety
    HeapEnableThreadSafety(0);

    // Single-threaded test for comparison
    auto start_time_st = std::chrono::high_resolution_clock::now();

    // Run single thread with same total workload
    for (int i = 0; i < NUM_THREADS; ++i) {
        worker_thread(i);
    }

    auto end_time_st = std::chrono::high_resolution_clock::now();
    auto duration_st = std::chrono::duration_cast<std::chrono::milliseconds>(end_time_st - start_time_st);

    std::cout << "\nSingle-Threaded Results (Thread Safety Disabled):" << std::endl;
    std::cout << "-------------------------------------------" << std::endl;
    std::cout << "Time: " << duration_st.count() << " ms" << std::endl;
    std::cout << "Successful allocations: " << successful_allocs.load() << std::endl;
    std::cout << "Successful frees: " << successful_frees.load() << std::endl;
    std::cout << "Failed allocations: " << failed_allocs.load() << std::endl;

    // Performance comparison
    float speedup = (float)duration_st.count() / duration.count();
    std::cout << "\nMulti-threaded speedup: " << speedup << "x" << std::endl;
    std::cout << "Thread safety overhead: " << ((speedup < NUM_THREADS) ?
        ((float)NUM_THREADS / speedup - 1) * 100 : 0) << "%" << std::endl;

    return 0;
}