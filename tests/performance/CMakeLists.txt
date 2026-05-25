add_executable(sentinel_performance_benchmarks
    benchmark_main.cpp
)

target_link_libraries(sentinel_performance_benchmarks
    core_runtime
    policy_service
    namespace_service
    shell_desktop
)

target_include_directories(sentinel_performance_benchmarks PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/runtime
    ${CMAKE_SOURCE_DIR}/src/services/policy
    ${CMAKE_SOURCE_DIR}/src/services/namespace
    ${CMAKE_SOURCE_DIR}/src/shell/desktop
)

set_target_properties(sentinel_performance_benchmarks PROPERTIES
    FOLDER "tests"
)