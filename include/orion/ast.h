#ifndef AST_DEF_H
#define AST_DEF_H

#include <stdint.h>

typedef enum {
#define X(name, body, cond, expr) name,
#include "ast.def.h"
#undef X
    AST_MAX
} ASTNodeType;

#define U64(x) ((uint64_t)(x))
typedef enum {
#define X(name, str, cat) name,
#include "tokens.def.h"
#undef X
    TOKEN_MAX
} TokenType;


typedef struct {
    TokenType type;
    const char *lexeme;
    int line;
    int column;
} Token;


// 2. Forward declaration of Token if needed, or include tokens.def.h first
typedef struct ASTNode {
    ASTNodeType type;
    Token opToken;          
    struct ASTNode *left;   
    struct ASTNode *right;  
    struct ASTNode *extra;  
    struct ASTNode *next;
} ASTNode;

#endif
