#ifndef LEXER_H
#define LEXER_H

#include "ast.h"

typedef struct {
    Token *data;
    int count;
    int capacity;
} TokenStream;

TokenStream lex_source(const char *source);
void free_token_stream(TokenStream *stream);

#endif
