add_library(core_runtime STATIC
    runtime.cpp
    bootstrap.h
)

add_library(sentinel_runtime_contract SHARED
    runtime_c_api.cpp
    runtime_c_api.h
)

target_include_directories(core_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(sentinel_runtime_contract
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sentinel_runtime_contract
    PRIVATE
        core_runtime
    namespace_service
        policy_service
)

target_compile_definitions(sentinel_runtime_contract
    PRIVATE
        SENTINEL_RUNTIME_CONTRACT_EXPORTS
)

# Enable C++20 explicitly for this target
target_compile_features(core_runtime PUBLIC cxx_std_20)
target_compile_features(sentinel_runtime_contract PUBLIC cxx_std_20)

# Set compiler warnings
if(MSVC)
    target_compile_options(core_runtime PRIVATE /W4)
    target_compile_options(sentinel_runtime_contract PRIVATE /W4)
else()
    target_compile_options(core_runtime PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(sentinel_runtime_contract PRIVATE -Wall -Wextra -pedantic)
endif()

