/* opcode.def.h
 * X(name, mask, operand types...)
 */

// Constants & Data Movement
X(CONST,        U64(1) << 0,  REG, IMM)
X(MOVE,         U64(1) << 1,  REG, REG)

// Integer Arithmetic
X(ADD,          U64(1) << 2,  REG, REG, REG)
X(SUB,          U64(1) << 3,  REG, REG, REG)
X(MUL,          U64(1) << 4,  REG, REG, REG)
X(DIV,          U64(1) << 5,  REG, REG, REG)
X(NEG,          U64(1) << 6,  REG, REG)
X(NOT,          U64(1) << 7,  REG, REG)

// Floating-Point Arithmetic
X(FADD,         U64(1) << 8,  REG, REG, REG)
X(FSUB,         U64(1) << 9,  REG, REG, REG)
X(FMUL,         U64(1) << 10, REG, REG, REG)
X(FDIV,         U64(1) << 11, REG, REG, REG)

// Bitwise & Shift Operations
X(BIT_AND,      U64(1) << 12, REG, REG, REG)
X(BIT_OR,       U64(1) << 13, REG, REG, REG)
X(SHL,          U64(1) << 14, REG, REG, REG)
X(SHR,          U64(1) << 15, REG, REG, REG)

// Comparisons
X(CMP_EQ,       U64(1) << 16, REG, REG, REG)
X(CMP_NE,       U64(1) << 17, REG, REG, REG)
X(CMP_LT,       U64(1) << 18, REG, REG, REG)
X(CMP_LE,       U64(1) << 19, REG, REG, REG)
X(CMP_GT,       U64(1) << 20, REG, REG, REG)
X(CMP_GE,       U64(1) << 21, REG, REG, REG)

// Control Flow
X(JMP,          U64(1) << 22, TARGET)
X(JMP_IF,       U64(1) << 23, REG, TARGET)
X(CALL,         U64(1) << 24, FUNC)
X(RET,          U64(1) << 25)
X(HALT,         U64(1) << 26)

// Memory Management & Pointers
X(ALLOC,        U64(1) << 27, REG, TYPE)
X(ALLOC_ARR,    U64(1) << 28, REG, TYPE, REG)
X(FREE,         U64(1) << 29, REG)

// Struct Fields & Array Elements
X(LOAD_FIELD,   U64(1) << 30, REG, REG, FIELD)
X(STORE_FIELD,  U64(1) << 31, REG, FIELD, REG)
X(LOAD_ELEM,    U64(1) << 32, REG, REG, REG)
X(STORE_ELEM,   U64(1) << 33, REG, REG, REG)

// Local Variables & Scopes
X(LOAD_LOCAL,   U64(1) << 34, REG, SLOT)
X(STORE_LOCAL,  U64(1) << 35, SLOT, REG)
X(ADDR_LOCAL,   U64(1) << 36, REG, SLOT) // Address-of local variable (&x)

// System, Casting & Debugging
X(CAST,         U64(1) << 37, REG, REG, TYPE)
X(PRINT,        U64(1) << 38, REG)
X(TRAP,         U64(1) << 39, IMM)
