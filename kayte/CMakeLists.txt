# REXX Interpreter for OS/2 Warp ARM64

add_library(rexx STATIC
        lexer.c
        parser.c
        eval.c
        runtime.c
)

target_include_directories(rexx PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Inherit global freestanding flags automatically
target_compile_options(rexx PRIVATE
        -ffreestanding
)

# No stdlib allowed in kernel
target_link_options(rexx PRIVATE
        -nostdlib
)
