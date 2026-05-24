add_library(namespace_service STATIC
    namespace_service.cpp
    namespace.h
)

target_include_directories(namespace_service
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(namespace_service
    PUBLIC
        core_runtime
)

target_compile_features(namespace_service PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(namespace_service PRIVATE /W4)
else()
    target_compile_options(namespace_service PRIVATE -Wall -Wextra -pedantic)
endif()

