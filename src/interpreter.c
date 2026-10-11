#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "orion/language.h"
#include "orion/vm.h"

#define VAR_LIMIT 256
#define CALL_LIMIT 256
#define STEP_LIMIT 10000000ULL

typedef struct { const char *name; uint64_t value; } Variable;
typedef struct Frame { Variable vars[VAR_LIMIT]; size_t count; struct Frame *parent; } Frame;
typedef struct { VM vm; ASTNode *functions; ASTNode *structs; uint64_t steps; unsigned depth; } Runtime;
typedef struct { int returned; uint64_t value; } Result;

static void runtime_error(const char *message, int line) {
    fprintf(stderr, "Runtime error at line %d: %s\n", line, message);
    exit(1);
}
static void step(Runtime *rt, Token t) { if (++rt->steps > STEP_LIMIT) runtime_error("instruction limit exceeded", t.line); }
static Variable *find_var(Frame *f, const char *name) {
    for (; f; f = f->parent) for (size_t i = 0; i < f->count; ++i) if (!strcmp(f->vars[i].name, name)) return &f->vars[i];
    return NULL;
}
static void set_var(Frame *f, const char *name, uint64_t value, int declaration, int line) {
    Variable *v = declaration ? NULL : find_var(f, name);
    if (!v) {
        if (!declaration) runtime_error("assignment to an undeclared variable", line);
        for (size_t i = 0; i < f->count; ++i)
            if (!strcmp(f->vars[i].name, name)) runtime_error("variable is already declared in this scope", line);
        if (f->count == VAR_LIMIT) runtime_error("too many local variables", line);
        v = &f->vars[f->count++]; v->name = name;
    }
    v->value = value;
}
static uint64_t eval(Runtime *, Frame *, ASTNode *);
static Result execute_list(Runtime *, Frame *, ASTNode *);
static int truth(uint64_t v) { return v != 0; }
static void print_string(const char *s) {
    size_t length = strlen(s);
    for (size_t i = 1; i + 1 < length; ++i) {
        if (s[i] == '\\' && i + 2 < length) {
            ++i;
            switch (s[i]) {
                case 'n': putchar('\n'); break;
                case 't': putchar('\t'); break;
                case 'r': putchar('\r'); break;
                default: putchar(s[i]); break;
            }
        } else putchar(s[i]);
    }
    putchar('\n');
}
static ASTNode *find_struct(Runtime *rt, const char *name) {
    ASTNode *s = rt->structs;
    while (s && strcmp(s->opToken.lexeme, name)) s = s->next;
    return s;
}
static int field_offset(Runtime *rt, const char *name) {
    int found = -1;
    for (ASTNode *s = rt->structs; s; s = s->next) {
        int offset = 0;
        for (ASTNode *field = s->left; field; field = field->next, offset += (int)sizeof(uint64_t)) {
            if (!strcmp(field->opToken.lexeme, name)) {
                if (found >= 0 && found != offset) return -2;
                found = offset;
            }
        }
    }
    return found;
}

static uint64_t eval(Runtime *rt, Frame *f, ASTNode *n) {
    if (!n) return 0;
    step(rt, n->opToken);
    switch (n->type) {
        case AST_LITERAL:
            if (n->opToken.type == TOKEN_NULL) return 0;
            if (n->opToken.type == TOKEN_STR_LIT) runtime_error("strings can only be passed to print for now", n->opToken.line);
            if (n->opToken.type == TOKEN_CHR_LIT) return (unsigned char)n->opToken.lexeme[1];
            return (uint64_t)strtoll(n->opToken.lexeme, NULL, 0);
        case AST_IDENT: {
            Variable *v = find_var(f, n->opToken.lexeme);
            if (!v) runtime_error("unknown variable", n->opToken.line);
            return v->value;
        }
        case AST_ALLOC: {
            ASTNode *s = find_struct(rt, n->opToken.lexeme);
            if (!s && n->opToken.type == TOKEN_IDENT && !n->left) runtime_error("unknown struct type", n->opToken.line);
            if (s && n->left) runtime_error("arrays of structs are not supported yet", n->opToken.line);
            uint64_t count = n->left ? eval(rt, f, n->left) : (s ? 0 : 1);
            if (!n->left && s) {
                uint64_t size = 0; for (ASTNode *field = s->left; field; field = field->next) size += sizeof(uint64_t);
                if (!size) size = sizeof(uint64_t);
                return heap_alloc(&rt->vm, (uint32_t)size);
            }
            if (count > UINT32_MAX / sizeof(uint64_t)) runtime_error("array is too large", n->opToken.line);
            return heap_alloc(&rt->vm, (uint32_t)(count * sizeof(uint64_t)));
        }
        case AST_INDEX_ACCESS: {
            uint64_t base = eval(rt, f, n->left), index = eval(rt, f, n->right);
            if (index > (UINT64_MAX - base) / sizeof(uint64_t)) runtime_error("array index overflow", n->opToken.line);
            return heap_load_u64(&rt->vm, base + index * sizeof(uint64_t));
        }
        case AST_UNARY: {
            uint64_t v = eval(rt, f, n->left);
            if (n->opToken.type == TOKEN_MINUS) return (uint64_t)(-((int64_t)v));
            if (n->opToken.type == TOKEN_NOT) return !truth(v);
            return v;
        }
        case AST_BINARY: {
            uint64_t a = eval(rt, f, n->left);
            if (n->opToken.type == TOKEN_AND && !truth(a)) return 0;
            if (n->opToken.type == TOKEN_OR && truth(a)) return 1;
            uint64_t b = eval(rt, f, n->right);
            int64_t sa = (int64_t)a, sb = (int64_t)b;
            switch (n->opToken.type) {
                case TOKEN_PLUS: return a + b;
                case TOKEN_MINUS: return a - b;
                case TOKEN_STAR: return a * b;
                case TOKEN_SLASH: if (!sb) runtime_error("division by zero", n->opToken.line); return (uint64_t)(sa / sb);
                case TOKEN_PERCENT: if (!sb) runtime_error("modulo by zero", n->opToken.line); return (uint64_t)(sa % sb);
                case TOKEN_EQ: return a == b; case TOKEN_NE: return a != b;
                case TOKEN_LT: return sa < sb; case TOKEN_LE: return sa <= sb;
                case TOKEN_GT: return sa > sb; case TOKEN_GE: return sa >= sb;
                case TOKEN_AND: return truth(a) && truth(b); case TOKEN_OR: return truth(a) || truth(b);
                case TOKEN_SHL: return a << (b & 63); case TOKEN_SHR: return (uint64_t)(sa >> (b & 63));
                default: runtime_error("unsupported operator", n->opToken.line);
            }
        }
        case AST_CALL: {
            if (!strcmp(n->opToken.lexeme, "print")) {
                for (ASTNode *arg = n->left; arg; arg = arg->next) {
                    if (arg->type == AST_LITERAL && arg->opToken.type == TOKEN_STR_LIT) print_string(arg->opToken.lexeme);
                    else printf("%" PRId64 "\n", (int64_t)eval(rt, f, arg));
                }
                return 0;
            }
            ASTNode *fn = rt->functions;
            while (fn && strcmp(fn->opToken.lexeme, n->opToken.lexeme)) fn = fn->next;
            if (!fn) runtime_error("unknown function", n->opToken.line);
            if (rt->depth >= CALL_LIMIT) runtime_error("call stack limit exceeded", n->opToken.line);
            Frame call = {.parent = f};
            ASTNode *param = fn->extra, *arg = n->left;
            while (param && arg) {
                set_var(&call, param->opToken.lexeme, eval(rt, f, arg), 1, param->opToken.line);
                param = param->next; arg = arg->next;
            }
            if (param || arg) runtime_error("argument count does not match function parameters", n->opToken.line);
            rt->depth++;
            Result result = execute_list(rt, &call, fn->left);
            rt->depth--;
            return result.value;
        }
        case AST_FIELD_ACCESS: {
            int offset = field_offset(rt, n->opToken.lexeme);
            if (offset == -2) runtime_error("field name is ambiguous across struct layouts", n->opToken.line);
            if (offset == -1) runtime_error("unknown struct field", n->opToken.line);
            uint64_t base = eval(rt, f, n->left);
            return heap_load_u64(&rt->vm, base + (uint64_t)offset);
        }
        default: runtime_error("expression expected", n->opToken.line);
    }
    return 0;
}

static void assign(Runtime *rt, Frame *f, ASTNode *target, ASTNode *value, int declaration) {
    uint64_t v = eval(rt, f, value);
    if (target->type == AST_IDENT) set_var(f, target->opToken.lexeme, v, declaration, target->opToken.line);
    else if (target->type == AST_INDEX_ACCESS) {
        uint64_t base = eval(rt, f, target->left), index = eval(rt, f, target->right);
        if (index > (UINT64_MAX - base) / sizeof(uint64_t)) runtime_error("array index overflow", target->opToken.line);
        heap_store_u64(&rt->vm, base + index * sizeof(uint64_t), v);
    } else if (target->type == AST_FIELD_ACCESS) {
        int offset = field_offset(rt, target->opToken.lexeme);
        if (offset == -2) runtime_error("field name is ambiguous across struct layouts", target->opToken.line);
        if (offset == -1) runtime_error("unknown struct field", target->opToken.line);
        uint64_t base = eval(rt, f, target->left);
        heap_store_u64(&rt->vm, base + (uint64_t)offset, v);
    } else runtime_error("assignment target must be a variable, array element, or struct field", target->opToken.line);
}
static Result execute_list(Runtime *rt, Frame *f, ASTNode *n) {
    for (; n; n = n->next) {
        step(rt, n->opToken);
        switch (n->type) {
            case AST_VAR_DECL: assign(rt, f, n->left, n->right, 1); break;
            case AST_ASSIGN: assign(rt, f, n->left, n->right, 0); break;
            case AST_FREE: { uint64_t p = eval(rt, f, n->left); if (p) heap_free(&rt->vm, p); break; }
            case AST_RETURN: return (Result){1, eval(rt, f, n->left)};
            case AST_IF_STMT: {
                ASTNode *body = truth(eval(rt, f, n->left)) ? n->right : n->extra;
                Frame scope = {.parent = f}; Result r = execute_list(rt, &scope, body); if (r.returned) return r; break;
            }
            case AST_WHILE_STMT:
                while (truth(eval(rt, f, n->left))) { Frame scope = {.parent = f}; Result r = execute_list(rt, &scope, n->right); if (r.returned) return r; }
                break;
            case AST_CALL: (void)eval(rt, f, n); break;
            default: runtime_error("unsupported statement", n->opToken.line);
        }
    }
    return (Result){0, 0};
}
int run_program(ASTNode *program) {
    Runtime rt = {0}; vm_init(&rt.vm); rt.functions = program->extra; rt.structs = program->right;
    ASTNode *main_fn = rt.functions;
    while (main_fn && strcmp(main_fn->opToken.lexeme, "main")) main_fn = main_fn->next;
    Frame global = {0};
    if (main_fn) {
        Frame frame = {.parent = &global}; rt.depth = 1;
        Result r = execute_list(&rt, &frame, main_fn->left);
        return (int)(int64_t)r.value;
    }
    Result r = execute_list(&rt, &global, program->left);
    return (int)(int64_t)r.value;
}
