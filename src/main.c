/*#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Correct preprocessor macro for bit shifts
#define U64(x) ((uint64_t)(x))

// Standard C typedefs (no '#' prefix)
typedef uint64_t REG;
typedef uint64_t IMM;
typedef uint64_t TARGET;
typedef uint64_t FUNC;
typedef uint64_t TYPE;
typedef uint64_t FIELD;
typedef uint64_t SLOT;

// Include or paste the VM structure and vm_execute code here
// (Assuming vm.c code is linked or included)
//#include "vm.c" 
#include "orion/vm.h"

int main(int argc, char **argv) {
    printf("Initializing Custom VM & Bytecode Engine...\n");

    VM vm;
    vm_init(&vm);

    // For demonstration, we can hand-craft or load a small bytecode stream
    // representing a program compiled from example_code.ori:
    // e.g., CONST r0, 42; PRINT r0; HALT;
    uint8_t sample_bytecode[] = {
        OP_CONST,      0,   42, 0, 0, 0, 0, 0, 0, 0, // r0 = 42
        OP_PRINT,      0,                            // print r0
        OP_HALT                                      // stop vm
    };

    size_t code_size = sizeof(sample_bytecode);

    printf("Executing bytecode on Virtual Machine...\n");
    vm_execute(&vm, sample_bytecode, code_size);

    printf("VM execution completed successfully.\n");
    return 0;
}*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "orion/vm.h"
#include "orion/lexer.h"
#include "orion/ast.h"

// Parser structure matching parser.c
typedef struct {
    Token *tokens;
    int current;
    int token_count;
} Parser;

ASTNode* parse_program(Parser *p);
void compile_to_text_bytecode(ASTNode *ast_root, const char *filename);

int main(int argc, char **argv) {
    const char *source_path = (argc > 1) ? argv[1] : "examples/example_code.ori";
    
    printf("----------------------------------------\n");
    printf("Orion Compiler & Virtual Machine\n");
    printf("Compiling source file: %s\n", source_path);
    printf("----------------------------------------\n");

    FILE *src = fopen(source_path, "r");
    if (!src) {
        fprintf(stderr, "Error: Could not open source file '%s'\n", source_path);
        return 1;
    }
    
    fseek(src, 0, SEEK_END);
    long length = ftell(src);
    fseek(src, 0, SEEK_SET);
    
    char *source_buffer = malloc(length + 1);
    fread(source_buffer, 1, length, src);
    source_buffer[length] = '\0';
    fclose(src);
    
    // 1. Lexing phase
    TokenStream tokens = lex_source(source_buffer);
    printf("[Lexer] Generated %d tokens from %s.\n", tokens.count, source_path);

    // 2. Parsing phase
    Parser parser = { tokens.data, 0, tokens.count };
    ASTNode *ast_root = parse_program(&parser);
    printf("[Parser] Abstract Syntax Tree successfully constructed.\n");

    // 3. Code Generation phase
    const char *output_bytecode_path = "build/program.vm.txt";
    system("mkdir -p build");
    
    compile_to_text_bytecode(ast_root, output_bytecode_path);
    printf("[CodeGen] Real assembly listing written to: %s\n", output_bytecode_path);

    // 4. Boot VM
    printf("\n[VM] Initializing Virtual Machine...\n");
    VM *vm = malloc(sizeof(VM));
    vm_init(vm);

    // Clean up
    free_token_stream(&tokens);
    free(source_buffer);
    free(vm);
    
    printf("----------------------------------------\n");
    printf("Compilation pipeline completed successfully!\n");
    printf("----------------------------------------\n");
    return 0;
}
