#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "orion/vm.h"
#include "orion/lexer.h"
#include "orion/ast.h"
#include "orion/language.h"
#include "orion/listing.h"

int main(int argc, char **argv) {
    const char *source_path = (argc > 1) ? argv[1] : "examples/two_sum.ori";
    
    printf("----------------------------------------\n");
    printf("OrionLang DSA Runtime\n");
    printf("Running source file: %s\n", source_path);
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
    Parser parser = { .tokens = tokens.data, .current = 0, .token_count = tokens.count };
    ASTNode *ast_root = parse_program(&parser);
    printf("[Parser] Abstract Syntax Tree successfully constructed.\n");

    const char *base = strrchr(source_path, '/');
    base = base ? base + 1 : source_path;
    char stem[256];
    size_t stem_len = strcspn(base, ".");
    if (stem_len >= sizeof(stem)) stem_len = sizeof(stem) - 1;
    memcpy(stem, base, stem_len); stem[stem_len] = '\0';
    char listing_path[320];
    snprintf(listing_path, sizeof(listing_path), "build/%s.bytecode.txt", stem);
    if (!write_bytecode_listing(ast_root, listing_path)) {
        fprintf(stderr, "Could not write bytecode listing '%s'\n", listing_path);
        return 1;
    }
    printf("[Bytecode] Text listing written to: %s\n", listing_path);

    printf("\n[Runtime] Executing program...\n");
    int result = run_program(ast_root);

    // Clean up
    free_token_stream(&tokens);
    free(source_buffer);
    
    printf("----------------------------------------\n");
    printf("Program completed with exit value %d.\n", result);
    printf("----------------------------------------\n");
    return 0;
}
