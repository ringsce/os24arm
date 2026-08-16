#include "rexx.h"

#define AST_POOL_SIZE 256

static AST ast_pool[AST_POOL_SIZE];
static size_t ast_index = 0;
/* Forward declarations */
static AST* new_node(ASTType type);
static AST* parse_expression(Parser* p);
static AST* parse_primary(Parser* p);
static AST* parse_factor(Parser* p);
static AST* parse_term(Parser* p);
static AST* parse_comparison(Parser* p);

static AST* ast_alloc(ASTType type)
{
    if (ast_index >= AST_POOL_SIZE)
        return 0;  // out of memory (handle properly later)

    AST* n = &ast_pool[ast_index++];
    n->type = type;
    n->left = 0;
    n->right = 0;
    n->value = 0;
    n->length = 0;
    return n;
}

static AST* parse_primary(Parser* p);

static AST* parse_factor(Parser* p)
{
    AST* left = parse_primary(p);

    while (p->current.type == TOK_MUL ||
           p->current.type == TOK_DIV)
    {
        TokenType op = p->current.type;
        p->current = lexer_next(&p->lexer);

        AST* right = parse_primary(p);

        AST* node = new_node(AST_ADD);
        node->left = left;
        node->right = right;
        node->op = op;

        left = node;
    }

    return left;
}

static AST* parse_term(Parser* p)
{
    AST* left = parse_factor(p);

    while (p->current.type == TOK_PLUS ||
           p->current.type == TOK_MINUS)
    {
        TokenType op = p->current.type;
        p->current = lexer_next(&p->lexer);

        AST* right = parse_factor(p);

        AST* node = new_node(AST_ADD);
        node->left = left;
        node->right = right;
        node->op = op;

        left = node;
    }

    return left;
}

static AST* parse_comparison(Parser* p)
{
    AST* left = parse_term(p);

    while (p->current.type == TOK_GT ||
           p->current.type == TOK_LT ||
           p->current.type == TOK_EQ)
    {
        TokenType op = p->current.type;
        p->current = lexer_next(&p->lexer);

        AST* right = parse_term(p);

        AST* node = new_node(AST_ADD);
        node->left = left;
        node->right = right;
        node->op = op;

        left = node;
    }

    return left;
}

static AST* parse_expression(Parser* p)
{
    return parse_comparison(p);
}

AST* parse_program(Parser* p)
{
    AST* first = NULL;
    AST* last = NULL;

    while (p->current.type != TOK_EOF)
    {
        AST* stmt = parse_statement(p);
        if (!stmt)
            break;

        if (!first)
            first = stmt;
        else
            last->next = stmt;

        last = stmt;
    }

    AST* program = new_node(AST_BLOCK);
    program->left = first;
    return program;
}


static AST* new_node(ASTType type)
{
    AST* n = ast_alloc(type);
    n->type = type;
    n->left = NULL;
    n->right = NULL;
    n->third = NULL;
    n->value = NULL;
    n->length = 0;
    return n;
}

static AST* parse_block(Parser* p)
{
    AST* first = NULL;
    AST* last = NULL;

    while (p->current.type != TOK_END &&
           p->current.type != TOK_EOF)
    {
        AST* stmt = parse_statement(p);
        if (!stmt)
            break;

        if (!first) {
            first = stmt;
        } else {
            last->next = stmt;
        }

        last = stmt;
    }

    if (p->current.type == TOK_END)
        p->current = lexer_next(&p->lexer);

    AST* block = new_node(AST_BLOCK);
    block->left = first;
    return block;
}


void parser_init(Parser* p, const char* src)
{
    lexer_init(&p->lexer, src);
    p->current = lexer_next(&p->lexer);
}

static AST* parse_primary(Parser* p)
{
    if (p->current.type == TOK_STRING) {
        AST* n = new_node(AST_STRING);
        n->value = p->current.start;
        n->length = p->current.length;
        p->current = lexer_next(&p->lexer);
        return n;
    }

    if (p->current.type == TOK_NUMBER) {
        AST* n = new_node(AST_NUMBER);
        n->value = p->current.start;
        n->length = p->current.length;
        p->current = lexer_next(&p->lexer);
        return n;
    }

    if (p->current.type == TOK_IDENTIFIER) {
        AST* n = new_node(AST_VAR);
        n->value = p->current.start;
        n->length = p->current.length;
        p->current = lexer_next(&p->lexer);
        return n;
    }

    return NULL;
}

AST* parse_statement(Parser* p)
{
    // Try Object REXX syntax first
    AST *orexx_node = orexx_parse_statement(p);
    if (orexx_node) return orexx_node;
    /* SAY */
    if (p->current.type == TOK_SAY) {
        p->current = lexer_next(&p->lexer);

        AST* n = new_node(AST_SAY);
        n->left = parse_expression(p);
        return n;
    }

    /* BEGIN block */
    if (p->current.type == TOK_BEGIN) {
        p->current = lexer_next(&p->lexer);
        return parse_block(p);
    }

    /* EXIT */
    if (p->current.type == TOK_EXIT) {
        p->current = lexer_next(&p->lexer);
        return new_node(AST_EXIT);
    }

    /* IF */
    if (p->current.type == TOK_IF) {
        p->current = lexer_next(&p->lexer);

        AST* cond = parse_expression(p);

        if (p->current.type != TOK_THEN)
            return NULL;

        p->current = lexer_next(&p->lexer);

        AST* then_stmt = parse_statement(p);

        AST* else_stmt = NULL;

        if (p->current.type == TOK_ELSE) {
            p->current = lexer_next(&p->lexer);
            else_stmt = parse_statement(p);
        }

        AST* node = new_node(AST_IF);
        node->left = cond;
        node->right = then_stmt;
        node->third = else_stmt;

        return node;
    }

    /* DO */
    if (p->current.type == TOK_DO) {
        p->current = lexer_next(&p->lexer);

        AST* cond = parse_expression(p);

        AST* body;

        if (p->current.type == TOK_BEGIN) {
            p->current = lexer_next(&p->lexer);
            body = parse_block(p);
        } else {
            body = parse_statement(p);
        }

        AST* node = new_node(AST_DO);
        node->left = cond;
        node->right = body;

        return node;
    }

    /* PROC */
    if (p->current.type == TOK_PROC) {
        p->current = lexer_next(&p->lexer);

        Token name = p->current;
        p->current = lexer_next(&p->lexer);

        AST* body = parse_block(p);

        AST* node = new_node(AST_PROC);
        node->value = name.start;
        node->length = name.length;
        node->left = body;

        return node;
    }

    /* Call */
    if (p->current.type == TOK_CALL) {
    p->current = lexer_next(&p->lexer);

    Token name = p->current;
    p->current = lexer_next(&p->lexer);

    AST* node = new_node(AST_CALL);
    node->value = name.start;
    node->length = name.length;

    return node;
}


    /* ASSIGNMENT */
    if (p->current.type == TOK_IDENTIFIER) {
        Token ident = p->current;
        p->current = lexer_next(&p->lexer);

        if (p->current.type == TOK_ASSIGN) {
            p->current = lexer_next(&p->lexer);

            AST* assign = new_node(AST_ASSIGN);

            AST* var = new_node(AST_VAR);
            var->value = ident.start;
            var->length = ident.length;

            assign->left = var;
            assign->right = parse_expression(p);
            return assign;
        }
    }

    return NULL;
}
