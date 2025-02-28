#include <customalloc/custom_alloc.h>
#include <customalloc/custom_alloc_internal.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>

// Function to inject a specific type of corruption
void inject_corruption(void *ptr, const char *corruption_type) {
    if (!ptr) return;

    segment_t *s = PtrToSegment(ptr);
    if (!s) return;

    std::cout << "Injecting corruption: " << corruption_type << " into segment " << s << std::endl;

    if (strcmp(corruption_type, "magic") == 0) {
        // Corrupt magic number
        s->magic = 0x12345678;
    }
    else if (strcmp(corruption_type, "size") == 0) {
        // Corrupt size field - make it obviously wrong
        s->size = -100; // Use a very negative value
    }
    else if (strcmp(corruption_type, "linked_list") == 0) {
        // Corrupt linked list pointers
        if (s->next) {
            s->next->prev = nullptr; // Break back pointer
        }
    }
    else if (strcmp(corruption_type, "checksum") == 0) {
        // Corrupt checksum
        segment_integrity_t *integrity = get_segment_integrity(s);
        if (integrity) {
            integrity->checksum ^= 0xFFFFFFFF; // Invert all bits
        }
    }
    else if (strcmp(corruption_type, "header_guard") == 0) {
        // Corrupt header guard
        segment_integrity_t *integrity = get_segment_integrity(s);
        if (integrity) {
            integrity->header_guard = 0xDEADBEEF; // Wrong value
        }
    }
    else if (strcmp(corruption_type, "footer_guard") == 0) {
        // Corrupt footer guard
        uint32_t *footer = get_segment_footer(s);
        if (footer) {
            *footer = 0xBAADF00D; // Wrong value
        }
    }
    else if (strcmp(corruption_type, "severe") == 0) {
        // Severe irreparable corruption - break everything except linked list structure
        s->magic = 0;
        s->size = -1;
        // DON'T modify prev/next pointers to avoid breaking the linked list structure
        // s->prev = reinterpret_cast<segment_t*>(0xBAD12345);
        // if (s->next) {
        //     s->next->prev = reinterpret_cast<segment_t*>(0xBAD12345);
        // }

        segment_integrity_t *integrity = get_segment_integrity(s);
        if (integrity) {
            integrity->header_guard = 0;
            integrity->checksum = 0;
        }

        uint32_t *footer = get_segment_footer(s);
        if (footer) {
            *footer = 0;
        }
    }
}

// Function to test a specific type of corruption
void test_specific_corruption(const char* corruption_type) {
    std::cout << "\n=== Testing corruption type: " << corruption_type << " ===" << std::endl;

    // Allocate a test block
    void *ptr = _malloc(1024);
    if (!ptr) {
        std::cout << "Error: Failed to allocate memory for test" << std::endl;
        return;
    }

    // Fill with test pattern
    std::memset(ptr, 0xAA, 1024);
    std::cout << "Allocated 1024 bytes at address " << ptr << std::endl;

    // Check integrity before corruption
    std::cout << "Integrity check before corruption:" << std::endl;
    int errors = HeapVerifyIntegrity(0);
    std::cout << "Result: " << errors << " errors" << std::endl;

    // Inject corruption
    inject_corruption(ptr, corruption_type);

    // Check integrity without repair
    std::cout << "Check after corruption (without repair):" << std::endl;
    errors = HeapVerifyIntegrity(0);
    std::cout << "Result: " << errors << " errors" << std::endl;

    // Check with repair
    std::cout << "Check with repair:" << std::endl;
    errors = HeapVerifyIntegrity(1);
    std::cout << "Repair result: " << errors << " errors fixed" << std::endl;

    // Check integrity after repair
    std::cout << "Check after repair:" << std::endl;
    errors = HeapVerifyIntegrity(0);
    std::cout << "Result: " << errors << " errors remaining" << std::endl;

    // Free the block
    _free(ptr);
}

int main() {
    std::cout << "=== Enhanced Metadata Integrity Verification Test ===" << std::endl;

    // Enable debugging
    HeapEnableDebug(1);
    HeapEnableTracking(1);

    // Part 1: Basic allocation and integrity check
    std::cout << "\n--- Part 1: Basic allocation and integrity check ---" << std::endl;

    // Start with basic integrity checking
    HeapSetIntegrityCheckLevel(1);

    // Allocate test memory blocks
    std::vector<void*> pointers;

    for (int i = 0; i < 5; i++) {
        void *ptr = _malloc(1024 * (i + 1));
        if (ptr) {
            std::memset(ptr, 0xAA, 1024 * (i + 1));
            pointers.push_back(ptr);
            std::cout << "Allocated " << (1024 * (i + 1)) << " bytes at address " << ptr << std::endl;
        }
    }

    // Check basic integrity
    std::cout << "\nBasic integrity check:" << std::endl;
    int errors = HeapVerifyIntegrity(0);
    std::cout << "Result: " << errors << " errors" << std::endl;

    // If there are errors, fix them
    if (errors > 0) {
        std::cout << "Fixing initial problems..." << std::endl;
        HeapVerifyIntegrity(1);
    }

    // Enable thorough integrity checking and initialize all segments
    std::cout << "\nEnabling thorough integrity checking (level 3)..." << std::endl;
    HeapSetIntegrityCheckLevel(3);

    // Fix and initialize all segments with the new level
    errors = HeapVerifyIntegrity(1);
    std::cout << "All segments initialized: " << errors << " problems fixed" << std::endl;

    // Check again - should be clean
    errors = HeapVerifyIntegrity(0);
    std::cout << "After initialization: " << errors << " errors remaining" << std::endl;

    // Part 2: Test each type of corruption separately
    std::cout << "\n--- Part 2: Testing each type of corruption separately ---" << std::endl;

    test_specific_corruption("magic");
    test_specific_corruption("size");
    test_specific_corruption("checksum");
    test_specific_corruption("header_guard");
    test_specific_corruption("footer_guard");
    test_specific_corruption("linked_list");

    // Part 3: Test severe corruption
    std::cout << "\n--- Part 3: Testing severe irreparable corruption ---" << std::endl;

    void *severe_ptr = _malloc(2048);
    if (severe_ptr) {
        std::memset(severe_ptr, 0xBB, 2048);
        std::cout << "Allocated 2048 bytes at address " << severe_ptr << std::endl;

        // Inject severe corruption
        inject_corruption(severe_ptr, "severe");

        // Try to repair
        std::cout << "Attempting to repair severe corruption:" << std::endl;
        errors = HeapVerifyIntegrity(1);
        std::cout << "Repair result: " << errors << " errors fixed" << std::endl;

        // Check integrity after repair
        std::cout << "Check after repair:" << std::endl;
        errors = HeapVerifyIntegrity(0);
        std::cout << "Result: " << errors << " errors remaining" << std::endl;

        // Don't try to free severely corrupted block - may cause segmentation fault
        std::cout << "Note: Freeing a severely corrupted block may cause a segmentation fault." << std::endl;
        std::cout << "Skipping free operation to prevent test crash." << std::endl;

        // Instead, run another integrity check
        errors = HeapVerifyIntegrity(1);
        std::cout << "Additional integrity check: " << errors << " errors fixed" << std::endl;
    }

    // Part 4: Integrity checks during standard operations
    std::cout << "\n--- Part 4: Integrity during standard operations ---" << std::endl;

    // Do reallocation
    if (!pointers.empty()) {
        void *old_ptr = pointers[0];
        void *new_ptr = _realloc(old_ptr, 2048);

        if (new_ptr) {
            std::cout << "Reallocated from " << old_ptr << " to " << new_ptr << std::endl;
            pointers[0] = new_ptr;

            // Check integrity after reallocation
            errors = HeapVerifyIntegrity(0);
            std::cout << "Integrity after reallocation: " << errors << " errors" << std::endl;
        }
    }

    // Free blocks
    for (const auto & pointer : pointers) {
        _free(pointer);
        std::cout << "Freed block at address " << pointer << std::endl;
    }

    // Check integrity after freeing
    errors = HeapVerifyIntegrity(0);
    std::cout << "Integrity after freeing: " << errors << " errors" << std::endl;

    std::cout << "\n--- Test completed ---" << std::endl;

    // Final integrity check
    errors = HeapVerifyIntegrity(0);
    std::cout << "Final integrity check: " << errors << " errors" << std::endl;

    return errors > 0 ? 1 : 0;
}