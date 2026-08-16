#include "rexx.h"
#include <string.h>

#include "console.h"

/* Forward declarations */
static int eval_int(Context* ctx, AST* node);
static int to_int(const char* s);

static void kprint_token(const char* s, size_t len)
{
    for (size_t i = 0; i < len; i++)
        kputc(s[i]);
    kputc('\n');
}

static int katoi(const char* s)
{
    int result = 0;
    int sign = 1;

    if (*s == '-') {
        sign = -1;
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }

    return result * sign;
}

static void kernel_halt(void)
{
    while (1) {
        __asm__ volatile("wfe");
    }
}



static int to_int(const char* s)
{
    return katoi(s);
}

void eval(Context* ctx, AST* node)
{
    if (!node) return;

    switch (node->type) {

    case AST_SAY:
        kprint_token(node->left->value, node->left->length);
        break;

    case AST_ASSIGN:
        runtime_set(ctx, node->left->value, node->right->value);
        break;

    case AST_EXIT:
        kernel_halt();

    case AST_IF: {
            int cond = eval_int(ctx, node->left);

            if (cond) {
                eval(ctx, node->right);
            } else if (node->third) {
                eval(ctx, node->third);
            }

            return;
    }
    case AST_BLOCK: {
            AST* stmt = node->left;
            while (stmt) {
                eval(ctx, stmt);
                stmt = stmt->next;
            }
            return;
    }

    case AST_PROC:
        if (ctx->proc_count < MAX_PROCS) {
            Procedure* p = &ctx->procs[ctx->proc_count++];
            copy_name(p->name, node->value, node->length);
            p->body = node->left;
        }
        return;

    case AST_CALL:
        {
            for (size_t i = 0; i < ctx->builtin_count; i++) {
                if (names_equal(ctx->builtins[i].name, node->value)) {
                    ctx->builtins[i].fn(ctx, node->left);
                    return;
                }
            }

            /* user procedure fallback */
        }
    default:
        break;
    }
}
