# dos/CMakeLists.txt - DOS-style command implementations

add_library(dos STATIC
        dos_commands.c
)

target_include_directories(dos PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(dos PUBLIC
        os2api
)

target_compile_options(dos PRIVATE
        -Wall
        -Wextra
        -Werror=implicit-function-declaration
)

message(STATUS "dos command set configured")
