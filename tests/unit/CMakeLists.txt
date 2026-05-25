# Unit tests CMakeLists.txt
# Finds or downloads Google Test framework and builds unit tests

include(FetchContent)

# Fetch Google Test
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

# For Windows: prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# On MSVC, avoid parallel PDB write contention when GoogleTest compiles.
if(MSVC)
    foreach(_gt_target IN ITEMS gtest gtest_main gmock gmock_main)
        if(TARGET ${_gt_target})
            target_compile_options(${_gt_target} PRIVATE /FS)
        endif()
    endforeach()
endif()

# Unit test executable
add_executable(sentinel_unit_tests
    test_bootstrap.cpp
    test_event_broker.cpp
    test_policy.cpp
    test_namespace.cpp
    test_shell.cpp
)

target_link_libraries(sentinel_unit_tests
    gtest
    gtest_main
    core_runtime
    policy_service
    namespace_service
    shell_desktop
)

# Include paths for test files
target_include_directories(sentinel_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/runtime
    ${CMAKE_SOURCE_DIR}/src/services/policy
    ${CMAKE_SOURCE_DIR}/src/services/namespace
    ${CMAKE_SOURCE_DIR}/src/shell/desktop
)

# Register tests with CTest
add_test(
    NAME AllUnitTests
    COMMAND sentinel_unit_tests
)

# Mark test executable for better IDE support
set_target_properties(sentinel_unit_tests PROPERTIES
    FOLDER "tests"
)
