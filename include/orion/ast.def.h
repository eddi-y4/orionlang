/* ast.def.h
 * X(node_name, has_body, has_condition, has_child_expr)
 */

// Control flow & blocks (Directly maps to flowchart nodes)
X(AST_PROGRAM,     0, 0, 0)
X(AST_FUNC_DECL,   1, 0, 0) // Has a statement body block
X(AST_STRUCT_DECL, 1, 0, 0) // Named record layout
X(AST_FIELD_DECL,  0, 0, 1) // Record field
X(AST_IF_STMT,     1, 1, 0) // Has a condition expr and body/else blocks (Diamond node in Raylib)
X(AST_WHILE_STMT,  1, 1, 0) // Has a condition expr and loop body block (Loop backedge in Raylib)
X(AST_BREAK,      0, 0, 0)
X(AST_CONTINUE,   0, 0, 0)

// Statements
X(AST_VAR_DECL,    0, 0, 1) // Type, Name, Initializer Expression
X(AST_ASSIGN,      0, 0, 1) // LValue = Expression
X(AST_RETURN,      0, 0, 1) // Return Expression
X(AST_FREE,        0, 0, 1) // Free Expression
X(AST_INDEX_ACCESS,0, 0, 0) // Array indexing

// Expressions & Memory Operations
X(AST_BINARY,      0, 0, 0) // Left op Right
X(AST_UNARY,       0, 0, 0) // Prefix operator
X(AST_CALL,        0, 0, 0) // Function call with argument list
X(AST_ALLOC,       0, 0, 0) // Alloc struct instance
X(AST_FIELD_ACCESS,0, 0, 0) // Object field lookup (e.g. curr.next)
X(AST_LITERAL,     0, 0, 0) // Integer or null literal
X(AST_IDENT,       0, 0, 0) // Variable identifier
							//

