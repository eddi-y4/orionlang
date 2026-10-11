#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "orion/ast.h"
// Simulated label tracker for jump backpatching
typedef struct {
    int instruction_index;
    int target_label_id;
} JumpFixup;

typedef struct {
    JumpFixup fixups[256];
    int fixup_count;
    int label_counter;
} CodeGenContext;

// Helper to write human-readable bytecode instructions to a text file
void emit_instruction_text(FILE *f, int ip, const char *mnemonic, const char *format, ...) {
    fprintf(f, "[IP: 0x%04X] %-12s ", ip, mnemonic);
    
    if (format != NULL) {
        va_list args;
        va_start(args, format);
        vfprintf(f, format, args);
        va_end(args);
    }
    fprintf(f, "\n");
}

// AST Code Generation Walk Function
int generate_code(ASTNode *node, FILE *out_file, CodeGenContext *ctx, int current_ip) {
    if (!node) return current_ip;

    switch (node->type) {
		case AST_ASSIGN: {
            // Emits expression code, then stores it into a local slot
            current_ip = generate_code(node->left, out_file, ctx, current_ip);
            emit_instruction_text(out_file, current_ip, "STORE_LOCAL", "[slot_%s], r0", node->opToken.lexeme);
            current_ip += 4;
            break;
        }
        case AST_FIELD_ACCESS: {
            // Emits field load: r0 = r0 . field
            generate_code(node->left, out_file, ctx, current_ip);
            emit_instruction_text(out_file, current_ip, "LOAD_FIELD", "r0, r0, %s", node->opToken.lexeme);
            current_ip += 5;
            break;
        }
        case AST_LITERAL: {
            // Emits: CONST r0, IMM
            emit_instruction_text(out_file, current_ip, "CONST", "r0, %s", node->opToken.lexeme);
            current_ip += 10; // 1 byte opcode + 1 byte reg + 8 bytes immediate
            break;
        }
        case AST_IDENT: {
            // Emits: LOAD_LOCAL r0, slot_index
            emit_instruction_text(out_file, current_ip, "LOAD_LOCAL", "r0, [slot_%s]", node->opToken.lexeme);
            current_ip += 4;  // 1 byte opcode + 1 byte reg + 2 bytes slot
            break;
        }
        case AST_BINARY: {
            // Handle binary operation or unary prefix
            generate_code(node->left, out_file, ctx, current_ip);
            if (node->right) {
                // If it has a right child, it's a binary op
                current_ip = generate_code(node->right, out_file, ctx, current_ip);
                
                const char *op_name = "ADD";
                if (strcmp(node->opToken.lexeme, "-") == 0) op_name = "SUB";
                else if (strcmp(node->opToken.lexeme, "*") == 0) op_name = "MUL";
                else if (strcmp(node->opToken.lexeme, "==") == 0) op_name = "CMP_EQ";
                
                emit_instruction_text(out_file, current_ip, op_name, "r0, r0, r1");
                current_ip += 4; // 3 registers + 1 opcode
            } else {
                // Unary operation (e.g. negation !)
                emit_instruction_text(out_file, current_ip, "NOT", "r0, r0");
                current_ip += 3;
            }
            break;
        }
        case AST_WHILE_STMT: {
            int loop_start_ip = current_ip;
            int label_id = ctx->label_counter++;
            
            fprintf(out_file, "LABEL_%d:\n", label_id);
            
            // 1. Compile Condition
            current_ip = generate_code(node->left, out_file, ctx, current_ip);
            
            // 2. Jump if False out of loop (Backpatch target placeholder)
            emit_instruction_text(out_file, current_ip, "JMP_IF", "r0, EXIT_LABEL_%d", label_id);
            int jump_if_ip = current_ip;
            current_ip += 9; // 1 byte op + 1 reg + 8 byte target
            
            // 3. Compile Loop Body
            current_ip = generate_code(node->right, out_file, ctx, current_ip);
            
            // 4. Jump back to start of loop
            emit_instruction_text(out_file, current_ip, "JMP", "LABEL_%d", label_id);
            current_ip += 9; // 1 byte op + 8 byte target
            
            fprintf(out_file, "EXIT_LABEL_%d:\n", label_id);
            break;
        }
        case AST_PROGRAM: {
            // Traverse block statements sequentially
            // (Assuming child pointers or statement lists are handled here)
            ASTNode *curr = node;
            while (curr != NULL) {
                if (curr->left) {
                    current_ip = generate_code(curr->left, out_file, ctx, current_ip);
                }
                curr = curr->right; // Move to next statement in sequence
            }
            break;
        }
        default:
            break;
    }
    return current_ip;
}

// Main compilation trigger to write text file
void compile_to_text_bytecode(ASTNode *ast_root, const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Error: Could not open output file %s\n", filename);
        return;
    }

    fprintf(f, "; --- VM BYTECODE TEXT ASSEMBLY LISTING ---\n");
    fprintf(f, "; Target: Custom Imperative VM\n\n");

    CodeGenContext ctx = {0};
    generate_code(ast_root, f, &ctx, 0);

    fprintf(f, "\n[IP: 0xXXXX] HALT\n");
    fclose(f);
    printf("Bytecode successfully written to readable text file: %s\n", filename);
}
