# REXX Interpreter for OS/2 Warp ARM64

add_library(rexx STATIC
        lexer.c
        parser.c
        eval.c
        runtime.c
        rexx_api.c
        orexx.c          # Object REXX support
)

target_include_directories(rexx PRIVATE
        ${PROJECT_SOURCE_DIR}/os2api
        ${PROJECT_SOURCE_DIR}/kernel/include
)

# Compile as freestanding C11
target_compile_features(rexx PRIVATE c_std_11)

target_compile_options(rexx PRIVATE
        -ffreestanding
        -fno-builtin
        -O2
)