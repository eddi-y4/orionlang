#include <stdio.h>
#include <stdlib.h>
#include "orion/listing.h"

typedef struct LoopTarget { unsigned break_label; unsigned continue_label; struct LoopTarget *parent; } LoopTarget;
typedef struct { FILE *out; unsigned label; LoopTarget *loop; int trace_markers; } Listing;
static void expression(Listing *, ASTNode *);
static void statements(Listing *, ASTNode *);

static void statement_marker(Listing *l, ASTNode *n) {
    if (l->trace_markers)
        fprintf(l->out, ";@ %d %s %p\n", n->opToken.line, n->opToken.lexeme, (void *)n);
}

static void expression(Listing *l, ASTNode *n) {
    if (!n) { fputs("PUSH_I64 0\n", l->out); return; }
    switch (n->type) {
        case AST_LITERAL:
            if (n->opToken.type == TOKEN_STR_LIT) fprintf(l->out, "PUSH_STRING %s\n", n->opToken.lexeme);
            else if (n->opToken.type == TOKEN_NULL) fputs("PUSH_NULL\n", l->out);
            else if (n->opToken.type == TOKEN_FLT_LIT) fprintf(l->out, "PUSH_F64 %s\n", n->opToken.lexeme);
            else fprintf(l->out, "PUSH_I64 %s\n", n->opToken.lexeme);
            break;
        case AST_IDENT: fprintf(l->out, "LOAD_LOCAL %s\n", n->opToken.lexeme); break;
        case AST_ALLOC:
            if (n->left) { expression(l, n->left); fprintf(l->out, "ALLOC_ARRAY %s\n", n->opToken.lexeme); }
            else fprintf(l->out, "ALLOC %s\n", n->opToken.lexeme);
            break;
        case AST_INDEX_ACCESS:
            expression(l, n->left); expression(l, n->right); fputs("LOAD_ELEM\n", l->out); break;
        case AST_FIELD_ACCESS:
            expression(l, n->left); fprintf(l->out, "LOAD_FIELD %s\n", n->opToken.lexeme); break;
        case AST_UNARY:
            expression(l, n->left); fprintf(l->out, "UNARY_%s\n", n->opToken.lexeme); break;
        case AST_BINARY:
            expression(l, n->left); expression(l, n->right); fprintf(l->out, "BINARY_%s\n", n->opToken.lexeme); break;
        case AST_CALL: {
            unsigned argc = 0;
            for (ASTNode *arg = n->left; arg; arg = arg->next) { expression(l, arg); ++argc; }
            fprintf(l->out, "CALL %s %u\n", n->opToken.lexeme, argc); break;
        }
        default: fputs("PUSH_I64 0 ; unsupported expression\n", l->out); break;
    }
}
static void store_target(Listing *l, ASTNode *target) {
    if (target && target->type == AST_IDENT) fprintf(l->out, "STORE_LOCAL %s\n", target->opToken.lexeme);
    else if (target && target->type == AST_INDEX_ACCESS) fputs("STORE_ELEM\n", l->out);
    else if (target && target->type == AST_FIELD_ACCESS) fprintf(l->out, "STORE_FIELD %s\n", target->opToken.lexeme);
}
static void statements(Listing *l, ASTNode *n) {
    for (; n; n = n->next) {
        statement_marker(l, n);
        switch (n->type) {
        case AST_VAR_DECL:
            expression(l, n->right); fprintf(l->out, "DECLARE_LOCAL %s\n", n->opToken.lexeme); break;
        case AST_ASSIGN:
            if (n->left && n->left->type == AST_INDEX_ACCESS) {
                expression(l, n->left->left); expression(l, n->left->right); expression(l, n->right);
            } else if (n->left && n->left->type == AST_FIELD_ACCESS) {
                expression(l, n->left->left); expression(l, n->right);
            } else expression(l, n->right);
            store_target(l, n->left); break;
        case AST_FREE: expression(l, n->left); fputs("FREE\n", l->out); break;
        case AST_RETURN: expression(l, n->left); fputs("RETURN\n", l->out); break;
        case AST_BREAK:
            if (l->loop) fprintf(l->out, "JUMP L%u ; break\n", l->loop->break_label);
            else fputs("; invalid break outside loop\n", l->out);
            break;
        case AST_CONTINUE:
            if (l->loop) fprintf(l->out, "JUMP L%u ; continue\n", l->loop->continue_label);
            else fputs("; invalid continue outside loop\n", l->out);
            break;
        case AST_CALL: expression(l, n); fputs("POP\n", l->out); break;
        case AST_IF_STMT: {
            unsigned else_id = l->label++, end_id = l->label++;
            expression(l, n->left); fprintf(l->out, "JUMP_IF_FALSE L%u\n", else_id);
            statements(l, n->right);
            if (n->extra) { statement_marker(l, n); fprintf(l->out, "JUMP L%u\nL%u:\n", end_id, else_id); statements(l, n->extra); statement_marker(l, n); fprintf(l->out, "L%u:\n", end_id); }
            else fprintf(l->out, "L%u:\n", else_id);
            break;
        }
        case AST_WHILE_STMT: {
            unsigned start_id = l->label++, end_id = l->label++;
            fprintf(l->out, "L%u:\n", start_id); expression(l, n->left);
            fprintf(l->out, "JUMP_IF_FALSE L%u\n", end_id);
            LoopTarget loop = {end_id, start_id, l->loop};
            l->loop = &loop;
            statements(l, n->right);
            l->loop = loop.parent;
            statement_marker(l, n); fprintf(l->out, "JUMP L%u\nL%u:\n", start_id, end_id); break;
        }
        default: fprintf(l->out, "; unsupported statement node %d\n", n->type); break;
        }
    }
}
static void emit_listing(FILE *out, ASTNode *program, int trace_markers) {
    Listing l = {.out = out, .trace_markers = trace_markers};
    fputs("; OrionLang text-form virtual instructions\n; Generated listing; current runtime executes the AST interpreter.\n\n", out);
    for (ASTNode *s = program->right; s; s = s->next) {
        fprintf(out, "STRUCT %s\n", s->opToken.lexeme);
        for (ASTNode *field = s->left; field; field = field->next) fprintf(out, "  FIELD %s\n", field->opToken.lexeme);
        fputs("END_STRUCT\n\n", out);
    }
    if (program->left) { fputs("FUNC <top-level>\n", out); statements(&l, program->left); fputs("END_FUNC\n\n", out); }
    for (ASTNode *fn = program->extra; fn; fn = fn->next) {
        fprintf(out, "FUNC %s\n", fn->opToken.lexeme);
        for (ASTNode *param = fn->extra; param; param = param->next) fprintf(out, "PARAM %s\n", param->opToken.lexeme);
        statements(&l, fn->left); fputs("END_FUNC\n\n", out);
    }
}

char *generate_bytecode_listing(ASTNode *program) {
    FILE *out = tmpfile();
    if (!out) return NULL;
    emit_listing(out, program, 1);
    if (fflush(out) != 0 || fseek(out, 0, SEEK_END) != 0) { fclose(out); return NULL; }
    long length = ftell(out);
    if (length < 0 || fseek(out, 0, SEEK_SET) != 0) { fclose(out); return NULL; }
    char *text = malloc((size_t)length + 1);
    if (!text) { fclose(out); return NULL; }
    size_t read_count = fread(text, 1, (size_t)length, out);
    text[read_count] = '\0';
    fclose(out);
    return text;
}

int write_bytecode_listing(ASTNode *program, const char *path) {
    FILE *out = fopen(path, "w");
    if (!out) { perror(path); return 0; }
    emit_listing(out, program, 0);
    return fclose(out) == 0;
}
