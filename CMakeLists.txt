cmake_minimum_required(VERSION 3.14)
project(CustomAlloc VERSION 1.3.2 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option to enable logging
option(CUSTOM_ALLOC_ENABLE_LOGGING "Enable logging in CustomAlloc" OFF)
if(CUSTOM_ALLOC_ENABLE_LOGGING)
    add_definitions(-DCUSTOM_ALLOC_ENABLE_LOGGING)
endif()

# Library sources
set(SOURCES
        src/custom_alloc_core.cpp
        src/custom_alloc_small.cpp
        src/custom_alloc_util.cpp
        src/custom_alloc_stats.cpp
        src/custom_alloc_debug.cpp
)
set(HEADERS
        include/customalloc/custom_alloc.h
        include/customalloc/custom_alloc_internal.h
)

# Create library target
add_library(customalloc ${SOURCES} ${HEADERS})
target_include_directories(customalloc
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Enable testing
option(BUILD_TESTS "Build the tests" ON)
if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

# Build examples
option(BUILD_EXAMPLES "Build the examples" ON)
if (BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

# Installation settings
include(GNUInstallDirs)
install(TARGETS customalloc
        EXPORT customallocTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Export the targets
install(EXPORT customallocTargets
        FILE customallocTargets.cmake
        NAMESPACE customalloc::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/customalloc
)

# Create and install the config file
include(CMakePackageConfigHelpers)
configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/customallocConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/customallocConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/customalloc
)

write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/customallocConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/customallocConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/customallocConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/customalloc
)