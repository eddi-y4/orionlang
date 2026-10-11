#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "orion/language.h"

static Token peek(Parser *p) { return p->tokens[p->current]; }
static Token take(Parser *p) { Token t = peek(p); if (t.type != TOKEN_EOF) p->current++; return t; }
static int match(Parser *p, TokenType t) { if (peek(p).type != t) return 0; take(p); return 1; }
static Token expect(Parser *p, TokenType t, const char *what) {
    if (peek(p).type != t) {
        fprintf(stderr, "Parser error at %d:%d: expected %s, found '%s'\n", peek(p).line, peek(p).column, what, peek(p).lexeme);
        exit(1);
    }
    return take(p);
}
static ASTNode *node(ASTNodeType type, Token token) {
    ASTNode *n = calloc(1, sizeof(*n));
    if (!n) { perror("calloc"); exit(1); }
    n->type = type; n->opToken = token; return n;
}
static ASTNode *parse_expr(Parser *p, int min_prec);
static ASTNode *parse_stmt(Parser *p);
static ASTNode *parse_block(Parser *p);

static int precedence(TokenType t) {
    switch (t) {
        case TOKEN_OR: return 1; case TOKEN_AND: return 2;
        case TOKEN_EQ: case TOKEN_NE: return 3;
        case TOKEN_LT: case TOKEN_LE: case TOKEN_GT: case TOKEN_GE: return 4;
        case TOKEN_SHL: case TOKEN_SHR: return 5;
        case TOKEN_PLUS: case TOKEN_MINUS: return 6;
        case TOKEN_STAR: case TOKEN_SLASH: case TOKEN_PERCENT: return 7;
        default: return -1;
    }
}
static ASTNode *append(ASTNode *head, ASTNode *item) {
    if (!head) return item;
    ASTNode *tail = head; while (tail->next) tail = tail->next; tail->next = item; return head;
}

static ASTNode *parse_primary(Parser *p) {
    Token t = peek(p);
    if (t.type == TOKEN_INT_LIT || t.type == TOKEN_FLT_LIT || t.type == TOKEN_CHR_LIT || t.type == TOKEN_STR_LIT || t.type == TOKEN_NULL) {
        take(p); return node(AST_LITERAL, t);
    }
    if (match(p, TOKEN_ALLOC)) {
        Token type = peek(p);
        if (type.type == TOKEN_IDENT || type.type == TOKEN_INT_T || type.type == TOKEN_FLOAT_T || type.type == TOKEN_CHAR_T) take(p);
        else expect(p, TOKEN_IDENT, "type name after alloc");
        ASTNode *a = node(AST_ALLOC, type);
        if (match(p, TOKEN_LBRACK)) { a->left = parse_expr(p, 0); expect(p, TOKEN_RBRACK, "']'"); }
        return a;
    }
    if (match(p, TOKEN_LPAREN)) { ASTNode *e = parse_expr(p, 0); expect(p, TOKEN_RPAREN, "')'"); return e; }
    if (t.type == TOKEN_IDENT) {
        take(p);
        ASTNode *e = node(AST_IDENT, t);
        if (match(p, TOKEN_LPAREN)) {
            e->type = AST_CALL;
            ASTNode *args = NULL;
            if (!match(p, TOKEN_RPAREN)) {
                do { args = append(args, parse_expr(p, 0)); } while (match(p, TOKEN_COMMA));
                expect(p, TOKEN_RPAREN, "')'");
            }
            e->left = args;
        }
        for (;;) {
            if (match(p, TOKEN_LBRACK)) {
                ASTNode *idx = node(AST_INDEX_ACCESS, t); idx->left = e; idx->right = parse_expr(p, 0);
                expect(p, TOKEN_RBRACK, "']'"); e = idx;
            } else if (match(p, TOKEN_DOT)) {
                ASTNode *field = node(AST_FIELD_ACCESS, expect(p, TOKEN_IDENT, "field name")); field->left = e; e = field;
            } else break;
        }
        return e;
    }
    fprintf(stderr, "Parser error at %d:%d: unexpected '%s' in expression\n", t.line, t.column, t.lexeme);
    exit(1);
}
static ASTNode *parse_unary(Parser *p) {
    Token t = peek(p);
    if (t.type == TOKEN_MINUS || t.type == TOKEN_NOT || t.type == TOKEN_PLUS) {
        take(p); ASTNode *u = node(AST_UNARY, t); u->left = parse_unary(p); return u;
    }
    return parse_primary(p);
}
static ASTNode *parse_expr(Parser *p, int min_prec) {
    ASTNode *left = parse_unary(p);
    for (;;) {
        int prec = precedence(peek(p).type); if (prec < min_prec) break;
        Token op = take(p); ASTNode *right = parse_expr(p, prec + 1);
        ASTNode *b = node(AST_BINARY, op); b->left = left; b->right = right; left = b;
    }
    return left;
}

static ASTNode *parse_block(Parser *p) {
    expect(p, TOKEN_LBRACE, "'{'"); ASTNode *head = NULL;
    while (peek(p).type != TOKEN_RBRACE && peek(p).type != TOKEN_EOF) head = append(head, parse_stmt(p));
    expect(p, TOKEN_RBRACE, "'}'"); return head;
}
static int is_type(Parser *p) {
    if (peek(p).type == TOKEN_INT_T || peek(p).type == TOKEN_FLOAT_T || peek(p).type == TOKEN_CHAR_T) return 1;
    return peek(p).type == TOKEN_IDENT && p->tokens[p->current + 1].type == TOKEN_STAR;
}
static ASTNode *parse_stmt(Parser *p) {
    Token t = peek(p);
    if (match(p, TOKEN_IF)) {
        expect(p, TOKEN_LPAREN, "'('"); ASTNode *n = node(AST_IF_STMT, t); n->left = parse_expr(p, 0); expect(p, TOKEN_RPAREN, "')'"); n->right = parse_block(p);
        if (match(p, TOKEN_ELSE)) n->extra = parse_block(p); return n;
    }
    if (match(p, TOKEN_WHILE)) {
        expect(p, TOKEN_LPAREN, "'('"); ASTNode *n = node(AST_WHILE_STMT, t); n->left = parse_expr(p, 0); expect(p, TOKEN_RPAREN, "')'"); n->right = parse_block(p); return n;
    }
    if (match(p, TOKEN_RETURN)) { ASTNode *n = node(AST_RETURN, t); if (!match(p, TOKEN_SEMI)) { n->left = parse_expr(p, 0); expect(p, TOKEN_SEMI, "';'"); } return n; }
    if (match(p, TOKEN_FREE)) { ASTNode *n = node(AST_FREE, t); expect(p, TOKEN_LPAREN, "'('"); n->left = parse_expr(p, 0); expect(p, TOKEN_RPAREN, "')'"); expect(p, TOKEN_SEMI, "';'"); return n; }
    if (is_type(p)) {
        take(p); (void)match(p, TOKEN_STAR);
        Token name = expect(p, TOKEN_IDENT, "variable name"); ASTNode *n = node(AST_VAR_DECL, name);
        n->left = node(AST_IDENT, name);
        if (match(p, TOKEN_ASSIGN)) n->right = parse_expr(p, 0);
        expect(p, TOKEN_SEMI, "';'"); return n;
    }
    ASTNode *expr = parse_expr(p, 0);
    if (match(p, TOKEN_ASSIGN)) {
        ASTNode *n = node(AST_ASSIGN, expr->opToken); n->left = expr; n->right = parse_expr(p, 0); expect(p, TOKEN_SEMI, "';'"); return n;
    }
    expect(p, TOKEN_SEMI, "';'"); return expr;
}
static ASTNode *parse_function(Parser *p) {
    Token fn = take(p); Token name = expect(p, TOKEN_IDENT, "function name"); ASTNode *f = node(AST_FUNC_DECL, name);
    expect(p, TOKEN_LPAREN, "'('"); ASTNode *params = NULL;
    if (!match(p, TOKEN_RPAREN)) {
        do {
            if (peek(p).type == TOKEN_INT_T || peek(p).type == TOKEN_FLOAT_T || peek(p).type == TOKEN_CHAR_T || peek(p).type == TOKEN_IDENT) take(p);
            match(p, TOKEN_STAR);
            params = append(params, node(AST_IDENT, expect(p, TOKEN_IDENT, "parameter name")));
        } while (match(p, TOKEN_COMMA));
        expect(p, TOKEN_RPAREN, "')'");
    }
    f->extra = params;
    if (match(p, TOKEN_ARROW)) { if (peek(p).type != TOKEN_LBRACE) take(p); (void)match(p, TOKEN_STAR); }
    f->left = parse_block(p); (void)fn; return f;
}
static ASTNode *parse_struct(Parser *p) {
    take(p); ASTNode *s = node(AST_STRUCT_DECL, expect(p, TOKEN_IDENT, "struct name"));
    expect(p, TOKEN_LBRACE, "'{'");
    while (peek(p).type != TOKEN_RBRACE && peek(p).type != TOKEN_EOF) {
        if (peek(p).type == TOKEN_INT_T || peek(p).type == TOKEN_FLOAT_T || peek(p).type == TOKEN_CHAR_T || peek(p).type == TOKEN_IDENT) take(p);
        else expect(p, TOKEN_IDENT, "field type");
        (void)match(p, TOKEN_STAR);
        ASTNode *field = node(AST_FIELD_DECL, expect(p, TOKEN_IDENT, "field name"));
        expect(p, TOKEN_SEMI, "';'"); s->left = append(s->left, field);
    }
    expect(p, TOKEN_RBRACE, "'}'");
    (void)match(p, TOKEN_SEMI);
    return s;
}
ASTNode *parse_program(Parser *p) {
    Token nil = {TOKEN_EOF, "", 1, 1}; ASTNode *root = node(AST_PROGRAM, nil);
    while (peek(p).type != TOKEN_EOF) {
        if (peek(p).type == TOKEN_STRUCT) root->right = append(root->right, parse_struct(p));
        else if (match(p, TOKEN_FN)) { --p->current; root->extra = append(root->extra, parse_function(p)); }
        else root->left = append(root->left, parse_stmt(p));
    }
    return root;
}
