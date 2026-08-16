add_library(os2api STATIC
        os2_api.c
)

target_include_directories(os2api PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/kernel/include
)

target_compile_options(os2api PRIVATE
        -Wall
        -Wextra
        -Werror=implicit-function-declaration
)

target_link_libraries(rexx PRIVATE os2api)