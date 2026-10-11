/* tokens.def.h
 * X(name, string_representation, bit_mask)
 */

#define TK_CAT_KW     (U64(1) << 0)
#define TK_CAT_TYPE   (U64(1) << 1)
#define TK_CAT_LIT    (U64(1) << 2)
#define TK_CAT_OP     (U64(1) << 3)
#define TK_CAT_PUNCT  (U64(1) << 4)

// Keywords
X(TOKEN_STRUCT,  "struct",  TK_CAT_KW)
X(TOKEN_FN,      "fn",      TK_CAT_KW)
X(TOKEN_IF,      "if",      TK_CAT_KW)
X(TOKEN_ELSE,    "else",    TK_CAT_KW)
X(TOKEN_WHILE,   "while",   TK_CAT_KW)
X(TOKEN_BREAK,   "break",   TK_CAT_KW)
X(TOKEN_CONTINUE, "continue", TK_CAT_KW)
X(TOKEN_RETURN,  "return",  TK_CAT_KW)
X(TOKEN_ALLOC,   "alloc",   TK_CAT_KW)
X(TOKEN_FREE,    "free",    TK_CAT_KW)
X(TOKEN_NULL,    "null",    TK_CAT_KW)

// Basic Primitive Types
X(TOKEN_INT_T,   "int",     TK_CAT_TYPE)
X(TOKEN_FLOAT_T, "float",   TK_CAT_TYPE)
X(TOKEN_CHAR_T,  "char",    TK_CAT_TYPE)

// Literals & Identifiers
X(TOKEN_IDENT,   "IDENT",   TK_CAT_LIT)
X(TOKEN_INT_LIT, "INT_LIT", TK_CAT_LIT)
X(TOKEN_FLT_LIT, "FLT_LIT", TK_CAT_LIT)
X(TOKEN_CHR_LIT, "CHR_LIT", TK_CAT_LIT)
X(TOKEN_STR_LIT, "STR_LIT", TK_CAT_LIT)

// Operators & Symbols
X(TOKEN_OR,      "||",      TK_CAT_OP)
X(TOKEN_AND,     "&&",      TK_CAT_OP)
X(TOKEN_ASSIGN,  "=",       TK_CAT_OP)
X(TOKEN_EQ,      "==",      TK_CAT_OP)
X(TOKEN_NE,      "!=",      TK_CAT_OP)
X(TOKEN_LT,      "<",       TK_CAT_OP)
X(TOKEN_LE,      "<=",      TK_CAT_OP)
X(TOKEN_GT,      ">",       TK_CAT_OP)
X(TOKEN_GE,      ">=",      TK_CAT_OP)
X(TOKEN_SHL,     "<<",      TK_CAT_OP)
X(TOKEN_SHR,     ">>",      TK_CAT_OP)
X(TOKEN_PLUS,    "+",       TK_CAT_OP)
X(TOKEN_MINUS,   "-",       TK_CAT_OP)
X(TOKEN_STAR,    "*",       TK_CAT_OP)
X(TOKEN_SLASH,   "/",       TK_CAT_OP)
X(TOKEN_PERCENT, "%",       TK_CAT_OP)
X(TOKEN_NOT,     "!",       TK_CAT_OP)
X(TOKEN_AMP,     "&",       TK_CAT_OP)

// Punctuation & Brackets
X(TOKEN_LBRACE,  "{",       TK_CAT_PUNCT)
X(TOKEN_RBRACE,  "}",       TK_CAT_PUNCT)
X(TOKEN_LPAREN,  "(",       TK_CAT_PUNCT)
X(TOKEN_RPAREN,  ")",       TK_CAT_PUNCT)
X(TOKEN_LBRACK,  "[",       TK_CAT_PUNCT)
X(TOKEN_RBRACK,  "]",       TK_CAT_PUNCT)
X(TOKEN_SEMI,    ";",       TK_CAT_PUNCT)
X(TOKEN_COMMA,   ",",       TK_CAT_PUNCT)
X(TOKEN_DOT,     ".",       TK_CAT_PUNCT)
X(TOKEN_ARROW,   "->",      TK_CAT_PUNCT)

X(TOKEN_EOF,     "EOF",     0)
