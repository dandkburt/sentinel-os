add_library(shell_desktop STATIC
    desktop_shell.cpp
    shell.h
)

target_include_directories(shell_desktop
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(shell_desktop
    PUBLIC
        core_runtime
        policy_service
)

target_compile_features(shell_desktop PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(shell_desktop PRIVATE /W4)
else()
    target_compile_options(shell_desktop PRIVATE -Wall -Wextra -pedantic)
endif()

