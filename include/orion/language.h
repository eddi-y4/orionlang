#ifndef ORION_LANGUAGE_H
#define ORION_LANGUAGE_H

#include "ast.h"
#include "trace.h"

typedef struct {
    Token *tokens;
    int current;
    int token_count;
    int loop_depth;
} Parser;

ASTNode *parse_program(Parser *parser);
int run_program(ASTNode *program);
int run_program_traced(ASTNode *program, OrionTrace *trace);

#endif
