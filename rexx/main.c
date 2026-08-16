#include "rexx.h"
#include <stdio.h>

int main(void)
{
    const char* program =
        "SAY \"Hello from ARM64 REXX\"\n"
        "EXIT\n";

    Parser parser;
    parser_init(&parser, program);

    Context ctx;
    runtime_init(&ctx);

    AST* stmt = parse_statement(&parser);
    eval(&ctx, stmt);

    return 0;
}
