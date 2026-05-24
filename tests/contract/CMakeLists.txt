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

add_test(
    NAME RuntimeContractTests
    COMMAND sentinel_contract_tests
)

set_target_properties(sentinel_contract_tests PROPERTIES
    FOLDER "tests"
)
