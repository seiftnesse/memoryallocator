find_package(GTest QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG release-1.12.1
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(test_allocator test_allocator.cpp)
target_link_libraries(test_allocator PRIVATE customalloc GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(test_allocator)

# Add a simple allocation test that doesn't require GTest
add_executable(simple_test simple_test.cpp)
target_link_libraries(simple_test PRIVATE customalloc)
add_test(NAME simple_allocation_test COMMAND simple_test)

# Add the enhanced test
add_executable(enhanced_test enhanced_test.cpp)
target_link_libraries(enhanced_test PRIVATE customalloc)
add_test(NAME enhanced_allocation_test COMMAND enhanced_test)