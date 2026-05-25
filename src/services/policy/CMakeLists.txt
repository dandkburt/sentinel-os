add_library(policy_service STATIC
    policy_service.cpp
    policy.h
)

target_include_directories(policy_service
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(policy_service PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(policy_service PRIVATE /W4)
    target_link_libraries(policy_service PRIVATE Crypt32)
    target_link_libraries(policy_service PRIVATE Advapi32)
    target_link_libraries(policy_service PRIVATE Bcrypt)
else()
    target_compile_options(policy_service PRIVATE -Wall -Wextra -pedantic)
endif()

