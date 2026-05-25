cmake_minimum_required(VERSION 3.28)

project(SentinelOS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SENTINEL_BUILD_TESTS "Build Sentinel OS tests" ON)

add_subdirectory(src/core/runtime)
add_subdirectory(src/services/policy)
add_subdirectory(src/services/namespace)
add_subdirectory(src/shell/desktop)

if(SENTINEL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
    add_subdirectory(tests/integration)
    add_subdirectory(tests/contract)
    add_subdirectory(tests/performance)
endif()
