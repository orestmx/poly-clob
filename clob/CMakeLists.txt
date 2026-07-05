cmake_minimum_required(VERSION 3.16)
project(poly_clob CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    add_compile_options(-Wall -Wextra)
endif()

# ---- Engine as a reusable library (shared by the app and the tests) ----
add_library(clob_engine
    src/engine/OrderBook.cpp
)
target_include_directories(clob_engine PUBLIC src)

# ---- Demo executable ----
add_executable(simulator src/main.cpp)
target_link_libraries(simulator PRIVATE clob_engine)

# ---- Unit tests (GoogleTest fetched from source, so no system install needed) ----
option(CLOB_BUILD_TESTS "Build the unit tests" ON)

if(CLOB_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)            # we only use gtest, not gmock
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # needed on Windows/MSVC
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(unit_tests
        tests/test_orderbook.cpp
        tests/test_fuzz.cpp
        tests/support/OrderFlowGenerator.cpp)
    target_include_directories(unit_tests PRIVATE tests)
    target_link_libraries(unit_tests PRIVATE clob_engine GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(unit_tests)
endif()
