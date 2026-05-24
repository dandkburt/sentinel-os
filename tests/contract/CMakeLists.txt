# Contract tests CMakeLists.txt
# Validates native C ABI boundaries directly.

add_executable(sentinel_contract_tests
    test_runtime_contract.cpp
)

target_link_libraries(sentinel_contract_tests
    gtest
    gtest_main
    sentinel_runtime_contract
)

target_include_directories(sentinel_contract_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/runtime
)

if(WIN32)
    # Ensure shared-library runtime dependencies (including sentinel_runtime_contract.dll)
    # are present beside the test executable when launched by ctest.
    add_custom_command(TARGET sentinel_contract_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sentinel_contract_tests>
            $<TARGET_FILE_DIR:sentinel_contract_tests>
        COMMAND_EXPAND_LISTS
    )
endif()

add_test(
    NAME RuntimeContractTests
    COMMAND $<TARGET_FILE:sentinel_contract_tests>
)

set_tests_properties(RuntimeContractTests PROPERTIES
    ENVIRONMENT "PATH=$<TARGET_FILE_DIR:sentinel_runtime_contract>;$ENV{PATH}"
)

set_target_properties(sentinel_contract_tests PROPERTIES
    FOLDER "tests"
)
