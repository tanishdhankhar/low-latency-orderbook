cmake_minimum_required(VERSION 3.16)
project(LowLatencyOrderBook CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Compiler flags ─────────────────────────────────────────────────────────
set(COMMON_FLAGS
    -O3
    -march=native
    -Wall
    -Wextra
    -Wpedantic
    -pthread
)

# ── Include directories ────────────────────────────────────────────────────
include_directories(include)

# ── Benchmark binary ──────────────────────────────────────────────────────
add_executable(bench benchmarks/bench.cpp)
target_compile_options(bench PRIVATE ${COMMON_FLAGS})
target_link_libraries(bench pthread)

# ── Google Test ───────────────────────────────────────────────────────────
find_package(GTest QUIET)

if(GTest_FOUND)
    enable_testing()
    add_executable(tests tests/test_orderbook.cpp)
    target_compile_options(tests PRIVATE ${COMMON_FLAGS})
    target_link_libraries(tests GTest::gtest GTest::gtest_main pthread)
    include(GoogleTest)
    gtest_discover_tests(tests)
else()
    message(STATUS "GTest not found — downloading via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    add_executable(tests tests/test_orderbook.cpp)
    target_compile_options(tests PRIVATE ${COMMON_FLAGS})
    target_link_libraries(tests gtest gtest_main pthread)
    include(GoogleTest)
    gtest_discover_tests(tests)
endif()
