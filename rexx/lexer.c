#include "rexx.h"
#include <ctype.h>
#include <string.h>

static int match_keyword(const char* s, size_t len, const char* kw)
{
    return strlen(kw) == len && strncmp(s, kw, len) == 0;
}

void lexer_init(Lexer* l, const char* src)
{
    l->src = src;
    l->pos = 0;
}

Token lexer_next(Lexer* l)
{
    while (isspace(l->src[l->pos])) l->pos++;

    Token t = {TOK_EOF, NULL, 0};

    char c = l->src[l->pos];
    if (!c) return t;

    size_t start = l->pos;

    if (isalpha(c)) {
        while (isalnum(l->src[l->pos])) l->pos++;
        size_t len = l->pos - start;

        if (match_keyword(&l->src[start], len, "SAY")) t.type = TOK_SAY;
        else if (match_keyword(&l->src[start], len, "EXIT")) t.type = TOK_EXIT;
        else if (match_keyword(&l->src[start], len, "IF")) t.type = TOK_IF;
        else if (match_keyword(&l->src[start], len, "THEN")) t.type = TOK_THEN;
        else if (match_keyword(&l->src[start], len, "ELSE")) t.type = TOK_ELSE;
        else t.type = TOK_IDENTIFIER;

        t.start = &l->src[start];
        t.length = len;
        return t;
    }

    if (isdigit(c)) {
        while (isdigit(l->src[l->pos])) l->pos++;
        t.type = TOK_NUMBER;
        t.start = &l->src[start];
        t.length = l->pos - start;
        return t;
    }

    if (c == '"') {
        l->pos++;
        start = l->pos;
        while (l->src[l->pos] != '"') l->pos++;
        t.type = TOK_STRING;
        t.start = &l->src[start];
        t.length = l->pos - start;
        l->pos++;
        return t;
    }

    l->pos++;
    return t;
}
