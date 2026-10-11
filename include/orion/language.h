#ifndef ORION_LANGUAGE_H
#define ORION_LANGUAGE_H

#include "ast.h"

typedef struct {
    Token *tokens;
    int current;
    int token_count;
} Parser;

ASTNode *parse_program(Parser *parser);
int run_program(ASTNode *program);

#endif
