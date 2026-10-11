#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#include "orion/lexer.h"

static void add_token(TokenStream *stream, TokenType type, const char *lexeme, int line, int col) {
    if (stream->count >= stream->capacity) {
        stream->capacity = stream->capacity ? stream->capacity * 2 : 64;
        stream->data = realloc(stream->data, stream->capacity * sizeof(Token));
    }
    stream->data[stream->count++] = (Token){type, strdup(lexeme), line, col};
}

TokenStream lex_source(const char *source) {
    TokenStream stream = {0};
    int line = 1;
    int col = 1;
    int i = 0;
    int len = strlen(source);

    while (i < len) {
        char c = source[i];

        // Track lines
        if (c == '\n') {
            line++;
            col = 1;
            i++;
            continue;
        }
        // Skip whitespace
        if (isspace(c)) {
            col++;
            i++;
            continue;
        }
        // Skip line comments '//'
        if (c == '/' && source[i+1] == '/') {
            while (i < len && source[i] != '\n') i++;
            continue;
        }

        // Two-character operators & punctuation
        if (c == '=' && source[i+1] == '=') { add_token(&stream, TOKEN_EQ, "==", line, col); i += 2; col += 2; continue; }
        if (c == '!' && source[i+1] == '=') { add_token(&stream, TOKEN_NE, "!=", line, col); i += 2; col += 2; continue; }
        if (c == '<' && source[i+1] == '=') { add_token(&stream, TOKEN_LE, "<=", line, col); i += 2; col += 2; continue; }
        if (c == '>' && source[i+1] == '=') { add_token(&stream, TOKEN_GE, ">=", line, col); i += 2; col += 2; continue; }
        if (c == '<' && source[i+1] == '<') { add_token(&stream, TOKEN_SHL, "<<", line, col); i += 2; col += 2; continue; }
        if (c == '>' && source[i+1] == '>') { add_token(&stream, TOKEN_SHR, ">>", line, col); i += 2; col += 2; continue; }
        if (c == '&' && source[i+1] == '&') { add_token(&stream, TOKEN_AND, "&&", line, col); i += 2; col += 2; continue; }
        if (c == '|' && source[i+1] == '|') { add_token(&stream, TOKEN_OR, "||", line, col); i += 2; col += 2; continue; }
        if (c == '-' && source[i+1] == '>') { add_token(&stream, TOKEN_ARROW, "->", line, col); i += 2; col += 2; continue; }

        // Single character operators & punctuation
        switch (c) {
            case '=': add_token(&stream, TOKEN_ASSIGN, "=", line, col); i++; col++; continue;
            case '<': add_token(&stream, TOKEN_LT, "<", line, col); i++; col++; continue;
            case '>': add_token(&stream, TOKEN_GT, ">", line, col); i++; col++; continue;
            case '+': add_token(&stream, TOKEN_PLUS, "+", line, col); i++; col++; continue;
            case '-': add_token(&stream, TOKEN_MINUS, "-", line, col); i++; col++; continue;
            case '*': add_token(&stream, TOKEN_STAR, "*", line, col); i++; col++; continue;
            case '/': add_token(&stream, TOKEN_SLASH, "/", line, col); i++; col++; continue;
            case '!': add_token(&stream, TOKEN_NOT, "!", line, col); i++; col++; continue;
            case '&': add_token(&stream, TOKEN_AMP, "&", line, col); i++; col++; continue;
            case '{': add_token(&stream, TOKEN_LBRACE, "{", line, col); i++; col++; continue;
            case '}': add_token(&stream, TOKEN_RBRACE, "}", line, col); i++; col++; continue;
            case '(': add_token(&stream, TOKEN_LPAREN, "(", line, col); i++; col++; continue;
            case ')': add_token(&stream, TOKEN_RPAREN, ")", line, col); i++; col++; continue;
            case '[': add_token(&stream, TOKEN_LBRACK, "[", line, col); i++; col++; continue;
            case ']': add_token(&stream, TOKEN_RBRACK, "]", line, col); i++; col++; continue;
            case ';': add_token(&stream, TOKEN_SEMI, ";", line, col); i++; col++; continue;
            case ',': add_token(&stream, TOKEN_COMMA, ",", line, col); i++; col++; continue;
            case '.': add_token(&stream, TOKEN_DOT, ".", line, col); i++; col++; continue;
        }

        // Literals: Characters ('a')
        if (c == '\'') {
            char lexeme[4] = {source[i], source[i+1], source[i+2], '\0'};
            add_token(&stream, TOKEN_CHR_LIT, lexeme, line, col);
            i += 3; col += 3;
            continue;
        }

        // Numeric Literals (int / float)
        if (isdigit(c)) {
            int start = i;
            bool is_float = false;
            while (i < len && (isdigit(source[i]) || source[i] == '.')) {
                if (source[i] == '.') is_float = true;
                i++;
            }
            int len_num = i - start;
            char *lexeme = malloc(len_num + 1);
            memcpy(lexeme, &source[start], len_num);
            lexeme[len_num] = '\0';

            add_token(&stream, is_float ? TOKEN_FLT_LIT : TOKEN_INT_LIT, lexeme, line, col);
            free(lexeme);
            col += len_num;
            continue;
        }

        // Identifiers & Keywords
        if (isalpha(c) || c == '_') {
            int start = i;
            while (i < len && (isalnum(source[i]) || source[i] == '_')) i++;
            int len_id = i - start;
            char *lexeme = malloc(len_id + 1);
            memcpy(lexeme, &source[start], len_id);
            lexeme[len_id] = '\0';

            // Match keywords
            TokenType type = TOKEN_IDENT;
            if (strcmp(lexeme, "struct") == 0) type = TOKEN_STRUCT;
            else if (strcmp(lexeme, "fn") == 0) type = TOKEN_FN;
            else if (strcmp(lexeme, "if") == 0) type = TOKEN_IF;
            else if (strcmp(lexeme, "else") == 0) type = TOKEN_ELSE;
            else if (strcmp(lexeme, "while") == 0) type = TOKEN_WHILE;
            else if (strcmp(lexeme, "return") == 0) type = TOKEN_RETURN;
            else if (strcmp(lexeme, "alloc") == 0) type = TOKEN_ALLOC;
            else if (strcmp(lexeme, "free") == 0) type = TOKEN_FREE;
            else if (strcmp(lexeme, "null") == 0) type = TOKEN_NULL;
            else if (strcmp(lexeme, "int") == 0) type = TOKEN_INT_T;
            else if (strcmp(lexeme, "float") == 0) type = TOKEN_FLOAT_T;
            else if (strcmp(lexeme, "char") == 0) type = TOKEN_CHAR_T;

            add_token(&stream, type, lexeme, line, col);
            free(lexeme);
            col += len_id;
            continue;
        }

        // Fallback for unrecognized characters
        fprintf(stderr, "Lexer Error: Unexpected character '%c' at line %d, col %d\n", c, line, col);
        i++;
        col++;
    }

    add_token(&stream, TOKEN_EOF, "EOF", line, col);
    return stream;
}

void free_token_stream(TokenStream *stream) {
    for (int i = 0; i < stream->count; i++) {
        free((void*)stream->data[i].lexeme);
    }
    free(stream->data);
}
