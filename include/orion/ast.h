/* ast.def.h */

#ifndef AST_DEF_H
#define AST_DEF_H

// 1. Enum of Node Types (using X-Macros if desired, or plain list)
#define AST_NODE_TYPES \
    X(AST_PROGRAM) \
    X(AST_LITERAL) \
    X(AST_IDENT) \
    X(AST_BINARY) \
    X(AST_UNARY) \
    X(AST_ASSIGN) \
    X(AST_IF_STMT) \
    X(AST_WHILE_STMT) \
	X(AST_FREE) \
    X(AST_CALL) \
    X(AST_ALLOC) \
    X(AST_FIELD_ACCESS) \
    X(AST_INDEX_ACCESS)

typedef enum {
#define X(name) name,
    AST_NODE_TYPES
#undef X
    AST_MAX
} ASTNodeType;

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
} Token;


// 2. Forward declaration of Token if needed, or include tokens.def.h first
typedef struct ASTNode {
    ASTNodeType type;
    Token opToken;          
    struct ASTNode *left;   
    struct ASTNode *right;  
    struct ASTNode *extra;  
} ASTNode;

#endif
