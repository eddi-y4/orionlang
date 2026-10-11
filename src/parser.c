#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// --- Enums & Data Structures (Normally imported from headers) ---

typedef enum {
#define X(name, str, cat) name,
#include "orion/tokens.def.h"
#undef X
    TOKEN_MAX
} TokenType;

typedef enum {
#define X(name, body, cond, expr) name,
#include "orion/ast.def.h"
#undef X
    AST_MAX
} ASTNodeType;

typedef enum {
    ASSOC_LEFT,
    ASSOC_RIGHT
} Associativity;

typedef struct {
    TokenType type;
    const char *lexeme;
    int line;
} Token;

// AST Node Structure
typedef struct ASTNode {
    ASTNodeType type;
    Token opToken;          // For operators or identifiers
    struct ASTNode *left;   // Left child or condition
    struct ASTNode *right;  // Right child or body
    struct ASTNode *extra;  // Else block or extra expressions
} ASTNode;

// Parser State Context
typedef struct {
    Token *tokens;
    int current;
    int token_count;
} Parser;

// --- Operator Precedence & Mask Lookups ---

int get_operator_precedence(TokenType type) {
    switch (type) {
        case TOKEN_OR:   return 1;
        case TOKEN_AND:  return 2;
        case TOKEN_ASSIGN: return 3;
        case TOKEN_EQ:   
        case TOKEN_NE:   return 4;
        case TOKEN_LT:   
        case TOKEN_LE:   
        case TOKEN_GT:   
        case TOKEN_GE:   return 5;
        case TOKEN_SHL:  
        case TOKEN_SHR:  return 6;
        case TOKEN_PLUS: 
        case TOKEN_MINUS:return 7;
        case TOKEN_STAR: 
        case TOKEN_SLASH:return 8;
        default:         return -1;
    }
}

Associativity get_operator_assoc(TokenType type) {
    if (type == TOKEN_ASSIGN) return ASSOC_RIGHT;
    return ASSOC_LEFT;
}

// --- Forward Declarations ---
ASTNode* parse_expr(Parser *p, int min_precedence);
ASTNode* parse_stmt(Parser *p);
ASTNode* parse_block(Parser *p);

// --- Parser Helper Functions ---

Token parser_peek(Parser *p) {
    return p->tokens[p->current];
}

Token parser_advance(Parser *p) {
    if (p->tokens[p->current].type != TOKEN_EOF) {
        p->current++;
    }
    return p->tokens[p->current - 1];
}

bool parser_check(Parser *p, TokenType type) {
    return parser_peek(p).type == type;
}

Token parser_expect(Parser *p, TokenType type, const char *err_msg) {
    if (parser_check(p, type)) {
        return parser_advance(p);
    }
    fprintf(stderr, "Parser Error: %s at line %d\n", err_msg, parser_peek(p).line);
    exit(1);
}

ASTNode* create_node(ASTNodeType type) {
    ASTNode *node = (ASTNode*)malloc(sizeof(ASTNode));
    memset(node, 0, sizeof(ASTNode));
    node->type = type;
    return node;
}

// --- Expression Parsing (Precedence Climbing) ---

ASTNode* parse_primary(Parser *p) {
    Token tok = parser_peek(p);

    // Literals: Integers, floats, null
    if (tok.type == TOKEN_INT_LIT || tok.type == TOKEN_FLT_LIT || tok.type == TOKEN_NULL) {
        parser_advance(p);
        ASTNode *node = create_node(AST_LITERAL);
        node->opToken = tok;
        return node;
    }

    // Identifiers or Function Calls / Allocations
    if (tok.type == TOKEN_IDENT) {
        parser_advance(p);
        ASTNode *node = create_node(AST_IDENT);
        node->opToken = tok;

        // Check for function call: ident(args)
        if (parser_check(p, TOKEN_LPAREN)) {
            parser_advance(p); // consume '('
            node->type = AST_CALL;
            // Parse arguments list here if needed...
            parser_expect(p, TOKEN_RPAREN, "Expected ')' after function arguments");
        }
        return node;
    }

    // Alloc keyword: alloc StructName
    if (tok.type == TOKEN_ALLOC) {
        parser_advance(p);
        ASTNode *node = create_node(AST_ALLOC);
        node->opToken = parser_expect(p, TOKEN_IDENT, "Expected struct name after 'alloc'");
        return node;
    }

    // Parenthesized Expression: ( expr )
    if (tok.type == TOKEN_LPAREN) {
        parser_advance(p);
        ASTNode *node = parse_expr(p, 0);
        parser_expect(p, TOKEN_RPAREN, "Expected ')'");
        return node;
    }

    fprintf(stderr, "Parser Error: Unexpected token '%s' in expression\n", tok.lexeme);
    exit(1);
}

ASTNode* parse_unary(Parser *p) {
    Token tok = parser_peek(p);
    
    // Handle Unary operators: !, -, +, *, &
    if (tok.type == TOKEN_MINUS || tok.type == TOKEN_PLUS || tok.type == TOKEN_STAR || tok.type == TOKEN_AMP || tok.type == TOKEN_NOT) {
        parser_advance(p);
        ASTNode *node = create_node(AST_BINARY); // Reusing binary node for unary container
        node->opToken = tok;
        node->left = parse_unary(p); // Recursive unary
        return node;
    }

    ASTNode *left = parse_primary(p);

    // Handle postfix expressions (Field access '.' and Array indexing '[ ]')
    while (true) {
        if (parser_check(p, TOKEN_DOT)) {
            parser_advance(p); // consume '.'
            Token field = parser_expect(p, TOKEN_IDENT, "Expected field name after '.'");
            ASTNode *node = create_node(AST_FIELD_ACCESS);
            node->left = left;
            node->opToken = field;
            left = node;
        } else if (parser_check(p, TOKEN_LBRACK)) {
            parser_advance(p); // consume '['
            ASTNode *index_expr = parse_expr(p, 0);
            parser_expect(p, TOKEN_RBRACK, "Expected ']' after array index");
            ASTNode *node = create_node(AST_BINARY); // Array index operation
            node->left = left;
            node->right = index_expr;
            left = node;
        } else {
            break;
        }
    }

    return left;
}

ASTNode* parse_expr(Parser *p, int min_precedence) {
    ASTNode *left = parse_unary(p);

    while (true) {
        Token op_token = parser_peek(p);
        int prec = get_operator_precedence(op_token.type);
        if (prec < min_precedence) break;

        Associativity assoc = get_operator_assoc(op_token.type);
        int next_min_prec = (assoc == ASSOC_LEFT) ? prec + 1 : prec;

        parser_advance(p); // consume operator
        ASTNode *right = parse_expr(p, next_min_prec);

        ASTNode *bin_node = create_node(AST_BINARY);
        bin_node->opToken = op_token;
        bin_node->left = left;
        bin_node->right = right;
        left = bin_node;
    }

    return left;
}

// --- Statement & Control Flow Parsing ---

//ASTNode* parse_block(Parser *p) {
//    parser_expect(p, TOKEN_LBRACE, "Expected '{' to start block");
//    ASTNode *block = create_node(AST_PROGRAM); // Container for statements
//    
//    // Parse statements until '}'
//    while (!parser_check(p, TOKEN_RBRACE) && !parser_check(p, TOKEN_EOF)) {
//        // Link statements sequentially...
//        parse_stmt(p);
//    }
//    
//    parser_expect(p, TOKEN_RBRACE, "Expected '}' to close block");
//    return block;
//}

ASTNode* parse_if_stmt(Parser *p) {
    parser_advance(p); // consume 'if'
    parser_expect(p, TOKEN_LPAREN, "Expected '(' after 'if'");
    ASTNode *condition = parse_expr(p, 0);
    parser_expect(p, TOKEN_RPAREN, "Expected ')' after if condition");

    ASTNode *body = parse_block(p);
    ASTNode *else_body = NULL;

    if (parser_check(p, TOKEN_ELSE)) {
        parser_advance(p); // consume 'else'
        else_body = parse_block(p);
    }

    ASTNode *node = create_node(AST_IF_STMT);
    node->left = condition;
    node->right = body;
    node->extra = else_body;
    return node;
}

ASTNode* parse_while_stmt(Parser *p) {
    parser_advance(p); // consume 'while'
    parser_expect(p, TOKEN_LPAREN, "Expected '(' after 'while'");
    ASTNode *condition = parse_expr(p, 0);
    parser_expect(p, TOKEN_RPAREN, "Expected ')' after while condition");

    ASTNode *body = parse_block(p);

    ASTNode *node = create_node(AST_WHILE_STMT);
    node->left = condition;
    node->right = body;
    return node;
}

//ASTNode* parse_stmt(Parser *p) {
//    if (parser_check(p, TOKEN_IF)) return parse_if_stmt(p);
//    if (parser_check(p, TOKEN_WHILE)) return parse_while_stmt(p);
//    
//    // Fallback expression statement or assignment
//    ASTNode *expr = parse_expr(p, 0);
//    parser_expect(p, TOKEN_SEMI, "Expected ';' after statement");
//    return expr;
//}

// --- Top-Level Program Parser ---
//
//ASTNode* parse_program(Parser *p) {
//    ASTNode *program = create_node(AST_PROGRAM);
//    while (!parser_check(p, TOKEN_EOF)) {
//        // Can parse struct_decl or func_decl here
//        parser_advance(p);
//    }
//    return program;
//}

// Forward declarations
ASTNode* parse_stmt(Parser *p);
ASTNode* parse_expr(Parser *p, int min_precedence);

// Parse a block of statements { stmt1; stmt2; }
ASTNode* parse_block(Parser *p) {
    parser_expect(p, TOKEN_LBRACE, "Expected '{' to start block");
    ASTNode *head = NULL;
    ASTNode *tail = NULL;
    
    while (!parser_check(p, TOKEN_RBRACE) && !parser_check(p, TOKEN_EOF)) {
        ASTNode *stmt = parse_stmt(p);
        if (stmt) {
            if (!head) {
                head = stmt;
                tail = stmt;
            } else {
                tail->right = stmt; // Link next statement in sequence
                tail = stmt;
            }
        }
    }
    
    parser_expect(p, TOKEN_RBRACE, "Expected '}' to close block");
    return head;
}

// Helper to check if current tokens represent a type declaration (e.g., int, float, char, or Node*)
bool is_type_declaration(Parser *p) {
    Token tok = parser_peek(p);
    
    // Primitive types are always declarations
    if (tok.type == TOKEN_INT_T || tok.type == TOKEN_FLOAT_T || tok.type == TOKEN_CHAR_T) {
        return true;
    }
    
    // Custom types (e.g. Node* myNode or Node myNode)
    if (tok.type == TOKEN_IDENT) {
        if (p->current + 1 < p->token_count) {
            TokenType next_type = p->tokens[p->current + 1].type;
            // If followed by '*' or an identifier, it's a type declaration
            if (next_type == TOKEN_STAR || next_type == TOKEN_IDENT) {
                return true;
            }
        }
    }
    return false;
}

ASTNode* parse_var_or_assignment(Parser *p) {
    // If it's a variable declaration
    if (is_type_declaration(p)) {
        parser_advance(p); // consume type name/keyword
        
        // Handle pointer star if present (e.g., Node*)
        if (parser_check(p, TOKEN_STAR)) {
            parser_advance(p);
        }
        
        Token ident = parser_expect(p, TOKEN_IDENT, "Expected identifier after type");
        
        ASTNode *assign_node = create_node(AST_ASSIGN);
        assign_node->opToken = ident; // Store variable name
        
        if (parser_check(p, TOKEN_ASSIGN)) {
            parser_advance(p); // consume '='
            assign_node->left = parse_expr(p, 0);
        }
        parser_expect(p, TOKEN_SEMI, "Expected ';' after variable declaration");
        return assign_node;
    }

    // Otherwise, treat as a regular expression statement or assignment
    ASTNode *expr = parse_expr(p, 0);
    parser_expect(p, TOKEN_SEMI, "Expected ';' after expression");
    return expr;
}
// Parse variable declarations or assignments (e.g., int i = 0; or accumulator = ...)
//ASTNode* parse_var_or_assignment(Parser *p) {
//    Token tok = parser_peek(p);
//    
//    // Check if it's a type declaration (int, float, char, or custom struct name like Node*)
//    if (tok.type == TOKEN_INT_T || tok.type == TOKEN_FLOAT_T || tok.type == TOKEN_CHAR_T || tok.type == TOKEN_IDENT) {
//        // Could be a type keyword or a custom type pointer like Node*
//        TokenType type_tok = tok.type;
//        parser_advance(p);
//        
//        // Handle pointer star if present (e.g., Node*)
//        if (parser_check(p, TOKEN_STAR)) {
//            parser_advance(p);
//        }
//        
//        Token ident = parser_expect(p, TOKEN_IDENT, "Expected identifier after type");
//        
//        ASTNode *assign_node = create_node(AST_ASSIGN);
//        assign_node->opToken = ident; // Store variable name
//        
//        if (parser_check(p, TOKEN_ASSIGN)) {
//            parser_advance(p); // consume '='
//            assign_node->left = parse_expr(p, 0);
//        }
//        parser_expect(p, TOKEN_SEMI, "Expected ';' after variable declaration");
//        return assign_node;
//    }
//
//    // Regular expression statement or assignment
//    ASTNode *expr = parse_expr(p, 0);
//    parser_expect(p, TOKEN_SEMI, "Expected ';' after expression");
//    return expr;
//}

ASTNode* parse_stmt(Parser *p) {
    if (parser_check(p, TOKEN_IF)) {
        parser_advance(p); // consume 'if'
        parser_expect(p, TOKEN_LPAREN, "Expected '(' after 'if'");
        ASTNode *cond = parse_expr(p, 0);
        parser_expect(p, TOKEN_RPAREN, "Expected ')' after condition");
        ASTNode *body = parse_block(p);
        
        ASTNode *node = create_node(AST_IF_STMT);
        node->left = cond;
        node->right = body;
        return node;
    }
    
    if (parser_check(p, TOKEN_WHILE)) {
        parser_advance(p); // consume 'while'
        parser_expect(p, TOKEN_LPAREN, "Expected '(' after 'while'");
        ASTNode *cond = parse_expr(p, 0);
        parser_expect(p, TOKEN_RPAREN, "Expected ')' after condition");
        ASTNode *body = parse_block(p);
        
        ASTNode *node = create_node(AST_WHILE_STMT);
        node->left = cond;
        node->right = body;
        return node;
    }
    
    // Handle free statement: free(ptr);
    if (parser_check(p, TOKEN_FREE)) {
        parser_advance(p); // consume 'free'
        parser_expect(p, TOKEN_LPAREN, "Expected '(' after 'free'");
        ASTNode *expr = parse_expr(p, 0);
        parser_expect(p, TOKEN_RPAREN, "Expected ')' after free argument");
        parser_expect(p, TOKEN_SEMI, "Expected ';' after free statement");
        
        ASTNode *node = create_node(AST_FREE);
        node->left = expr;
        return node;
    }
    
    if (parser_check(p, TOKEN_RETURN)) {
        parser_advance(p);
        ASTNode *ret_node = create_node(AST_CALL);
        ret_node->left = parse_expr(p, 0);
        parser_expect(p, TOKEN_SEMI, "Expected ';'");
        return ret_node;
    }

    return parse_var_or_assignment(p);
}

// Top-level program parser (parses structs and functions)
ASTNode* parse_program(Parser *p) {
    ASTNode *program = create_node(AST_PROGRAM);

    while (!parser_check(p, TOKEN_EOF)) {
        if (parser_check(p, TOKEN_STRUCT)) {
            parser_advance(p);
            parser_expect(p, TOKEN_IDENT, "Expected struct name");
            parse_block(p); 
            continue;
        }
        
        if (parser_check(p, TOKEN_FN)) {
            parser_advance(p); // consume 'fn'
            parser_expect(p, TOKEN_IDENT, "Expected function name");
            parser_expect(p, TOKEN_LPAREN, "Expected '('");
            
            while (!parser_check(p, TOKEN_RPAREN) && !parser_check(p, TOKEN_EOF)) {
                parser_advance(p);
            }
            parser_expect(p, TOKEN_RPAREN, "Expected ')'");
            
            if (parser_check(p, TOKEN_ARROW)) {
                parser_advance(p);
                parser_advance(p);
            }
            
            ASTNode *body = parse_block(p);
            
            // Append function body statements to the program's main statement chain
            if (!program->left) {
                program->left = body;
            } else {
                ASTNode *tail = program->left;
                while (tail->right != NULL) {
                    tail = tail->right;
                }
                tail->right = body;
            }
            continue;
        }
        
        parser_advance(p);
    }
    return program;
}
