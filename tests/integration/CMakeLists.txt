# Integration tests CMakeLists.txt
# Builds integration tests using Google Test framework

# GoogleTest is fetched once in tests/unit/CMakeLists.txt and reused here.

# Integration test executable
add_executable(sentinel_integration_tests
    test_integration.cpp
)

target_link_libraries(sentinel_integration_tests
    gtest
    gtest_main
    core_runtime
    policy_service
    namespace_service
    shell_desktop
)

# Include paths for test files
target_include_directories(sentinel_integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/runtime
    ${CMAKE_SOURCE_DIR}/src/services/policy
    ${CMAKE_SOURCE_DIR}/src/services/namespace
    ${CMAKE_SOURCE_DIR}/src/shell/desktop
)

# Register tests with CTest
add_test(
    NAME AllIntegrationTests
    COMMAND sentinel_integration_tests
)

# Mark test executable for better IDE support
set_target_properties(sentinel_integration_tests PROPERTIES
    FOLDER "tests"
)
