cmake_minimum_required(VERSION 3.16)
project(convert_fgd_dem_cpp VERSION 1.0.0 LANGUAGES CXX)

# Recommend Ninja for faster builds
if(NOT CMAKE_GENERATOR)
    find_program(NINJA_EXECUTABLE ninja)
    if(NINJA_EXECUTABLE)
        message(STATUS "Ninja found, recommending for faster builds. Use: cmake -G Ninja -B build")
    endif()
endif()

# C++20 standard (limited features for GCC 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(Threads REQUIRED)

# Try vcpkg first (for Windows), fall back to pkg-config (for Unix)
if(DEFINED VCPKG_TARGET_TRIPLET OR DEFINED ENV{VCPKG_ROOT})
    # Using vcpkg - use find_package
    message(STATUS "Using vcpkg for dependency management")
    find_package(GDAL CONFIG REQUIRED)
    find_package(libzip CONFIG REQUIRED)
    set(GDAL_LIBRARIES GDAL::GDAL)
    set(LIBZIP_LIBRARIES libzip::zip)
    set(GDAL_INCLUDE_DIRS "")
    set(LIBZIP_INCLUDE_DIRS "")
else()
    # Using pkg-config (macOS/Linux)
    message(STATUS "Using pkg-config for dependency management")
    find_package(PkgConfig REQUIRED)

    # Find GDAL using pkg-config (more reliable on macOS with Homebrew)
    pkg_check_modules(GDAL REQUIRED gdal)

    # Store as standard GDAL variables for compatibility
    set(GDAL_LIBRARIES ${GDAL_LIBRARIES})
    set(GDAL_INCLUDE_DIRS ${GDAL_INCLUDE_DIRS})

    # Find libzip using pkg-config
    pkg_check_modules(LIBZIP REQUIRED libzip)
endif()

# Find or fetch dependencies
include(FetchContent)

# Fetch cxxopts for command line parsing
FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG v3.1.1
)

FetchContent_MakeAvailable(cxxopts)

# Source files
set(SOURCES
    src/main.cpp
    src/converter.cpp
    src/dem.cpp
    src/geotiff.cpp
    src/xml_parser.cpp
    src/zip_handler.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GDAL_INCLUDE_DIRS}
    ${LIBZIP_INCLUDE_DIRS}
)

# Link libraries
if(DEFINED VCPKG_TARGET_TRIPLET OR DEFINED ENV{VCPKG_ROOT})
    # vcpkg provides proper targets
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${GDAL_LIBRARIES}
        ${LIBZIP_LIBRARIES}
        cxxopts::cxxopts
        Threads::Threads
    )
else()
    # pkg-config requires link directories
    target_link_directories(${PROJECT_NAME} PRIVATE
        ${GDAL_LIBRARY_DIRS}
        ${LIBZIP_LIBRARY_DIRS}
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${GDAL_LIBRARIES}
        ${LIBZIP_LIBRARIES}
        cxxopts::cxxopts
        Threads::Threads
    )
endif()

# Compiler flags
if(MSVC)
    # MSVC compiler flags
    target_compile_options(${PROJECT_NAME} PRIVATE
        /std:c++20
        /W4
        /permissive-
        /Zc:__cplusplus
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2 /Ob2 /DNDEBUG>
    )
else()
    # GCC/Clang compiler flags
    target_compile_options(${PROJECT_NAME} PRIVATE
        ${LIBZIP_CFLAGS_OTHER}
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        $<$<CONFIG:Debug>:-g -O0>
    )
endif()

# Platform-specific Release optimizations (GCC/Clang only)
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        # x86_64 specific optimizations with AVX2
        target_compile_options(${PROJECT_NAME} PRIVATE
            -O3 -march=native -mavx2 -mfma -DNDEBUG -flto -ffast-math -funroll-loops -ftree-vectorize -fomit-frame-pointer
        )
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        # ARM64 (Apple Silicon / M1/M2/M3) specific optimizations with NEON
        target_compile_options(${PROJECT_NAME} PRIVATE
            -O3 -mcpu=apple-m1 -DNDEBUG -flto -ffast-math -funroll-loops -ftree-vectorize -fomit-frame-pointer
        )
    else()
        # Generic optimizations for other architectures
        target_compile_options(${PROJECT_NAME} PRIVATE
            -O3 -march=native -DNDEBUG -flto -ffast-math -funroll-loops -ftree-vectorize -fomit-frame-pointer
        )
    endif()
endif()

# Enable Link-Time Optimization (LTO)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

# Parallel LTO compilation (suppress serial compilation warning) - GCC only
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(ProcessorCount)
    ProcessorCount(N)
    if(NOT N EQUAL 0)
        # AppleClang doesn't support -flto=N syntax, only GCC does
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
            target_link_options(${PROJECT_NAME} PRIVATE
                $<$<CONFIG:Release>:-flto=${N}>
            )
        endif()
    endif()
endif()

# Link TBB for parallel execution support
find_package(TBB QUIET)
if(TBB_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE TBB::tbb)
    message(STATUS "TBB Found: Parallel execution enabled")
else()
    message(WARNING "TBB not found: Falling back to sequential execution")
    # Fall back to standard library parallel execution
    target_link_libraries(${PROJECT_NAME} PRIVATE tbb)
endif()

# Check for specific GCC 11 features and disable if not available
include(CheckCXXCompilerFlag)

# Check for C++20 support
if(MSVC)
    check_cxx_compiler_flag("/std:c++20" HAS_CXX20)
else()
    check_cxx_compiler_flag("-std=c++20" HAS_CXX20)
endif()

if(NOT HAS_CXX20)
    message(FATAL_ERROR "C++20 support is required")
endif()

# For older compilers, we might need to add additional flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.0")
        message(FATAL_ERROR "GCC 11 or later is required")
    endif()

    # GCC 11 specific flags
    target_compile_options(${PROJECT_NAME} PRIVATE
        -fconcepts
    )
endif()

# Installation rules
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Enable testing
enable_testing()

# Optional: Build tests if test directory exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_subdirectory(tests)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "=== Configuration Summary ===")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "GDAL Version: ${GDAL_VERSION}")
message(STATUS "LibZip: Found")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "=============================")
message(STATUS "")