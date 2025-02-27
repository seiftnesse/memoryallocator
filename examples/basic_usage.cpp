#include <customalloc/custom_alloc.h>
#include <iostream>
#include <cstring>

struct MyStruct {
    int id;
    double value;
    char name[64];
};

int main() {
    std::cout << "CustomAlloc Basic Usage Example" << std::endl;

    // Allocate a single integer
    int* pInt = static_cast<int*>(_malloc(sizeof(int)));
    if (pInt) {
        *pInt = 42;
        std::cout << "Allocated int value: " << *pInt << std::endl;
        _free(pInt);
    }

    // Allocate an array of integers
    const int arraySize = 10;
    int* pArray = static_cast<int*>(_malloc(arraySize * sizeof(int)));
    if (pArray) {
        for (int i = 0; i < arraySize; i++) {
            pArray[i] = i * i;
        }

        std::cout << "Allocated array values: ";
        for (int i = 0; i < arraySize; i++) {
            std::cout << pArray[i] << " ";
        }
        std::cout << std::endl;

        // Resize the array
        int* pResized = static_cast<int*>(_realloc(pArray, arraySize * 2 * sizeof(int)));
        if (pResized) {
            std::cout << "Resized array (first part should be preserved): ";
            for (int i = 0; i < arraySize; i++) {
                std::cout << pResized[i] << " ";
            }
            std::cout << std::endl;

            // Add more values
            for (int i = arraySize; i < arraySize * 2; i++) {
                pResized[i] = i * i;
            }

            std::cout << "Complete resized array: ";
            for (int i = 0; i < arraySize * 2; i++) {
                std::cout << pResized[i] << " ";
            }
            std::cout << std::endl;

            _free(pResized);
        }
    }

    // Allocate a custom struct
    MyStruct* pStruct = static_cast<MyStruct*>(_malloc(sizeof(MyStruct)));
    if (pStruct) {
        pStruct->id = 1001;
        pStruct->value = 3.14159;
        strcpy(pStruct->name, "Custom Allocator Example");

        std::cout << "Struct data:" << std::endl;
        std::cout << "  ID: " << pStruct->id << std::endl;
        std::cout << "  Value: " << pStruct->value << std::endl;
        std::cout << "  Name: " << pStruct->name << std::endl;

        _free(pStruct);
    }

    std::cout << "All memory operations completed successfully!" << std::endl;
    return 0;
}