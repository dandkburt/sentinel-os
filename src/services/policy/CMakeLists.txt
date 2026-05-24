add_library(policy_service STATIC
    policy_service.cpp
    policy.h
)

target_include_directories(policy_service
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(policy_service
    PUBLIC
        core_runtime
)

target_compile_features(policy_service PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(policy_service PRIVATE /W4)
else()
    target_compile_options(policy_service PRIVATE -Wall -Wextra -pedantic)
endif()

