/* operators.def.h
 * X(name, string_rep, mask, precedence, associativity)
 */

// Logical OR / AND
X(OP_OR,   "||", MASK_BINARY, 1, ASSOC_LEFT)
X(OP_AND,  "&&", MASK_BINARY, 2, ASSOC_LEFT)

// Assignment
X(OP_ASSIGN, "=", MASK_BINARY, 3, ASSOC_RIGHT)

// Equality
X(OP_EQ,   "==", MASK_BINARY, 4, ASSOC_LEFT)
X(OP_NE,   "!=", MASK_BINARY, 4, ASSOC_LEFT)

// Relational
X(OP_LT,   "<",  MASK_BINARY, 5, ASSOC_LEFT)
X(OP_LE,   "<=", MASK_BINARY, 5, ASSOC_LEFT)
X(OP_GT,   ">",  MASK_BINARY, 5, ASSOC_LEFT)
X(OP_GE,   ">=", MASK_BINARY, 5, ASSOC_LEFT)

// Bitwise Shifts (<< and >>)
X(OP_SHL,  "<<", MASK_BINARY, 6, ASSOC_LEFT)
X(OP_SHR,  ">>", MASK_BINARY, 6, ASSOC_LEFT)

// Additive
X(OP_PLUS, "+",  MASK_UNARY | MASK_BINARY, 7, ASSOC_LEFT)
X(OP_MINUS,"-",  MASK_UNARY | MASK_BINARY, 7, ASSOC_LEFT)

// Multiplicative
X(OP_STAR, "*",  MASK_UNARY | MASK_BINARY, 8, ASSOC_LEFT)
X(OP_SLASH,"/",  MASK_BINARY, 8, ASSOC_LEFT)

// Unary Logical NOT / Address-Of / Bitwise
X(OP_NOT,  "!",  MASK_UNARY, 9, ASSOC_RIGHT)
X(OP_AMP,  "&",  MASK_UNARY | MASK_BINARY, 9, ASSOC_LEFT)
