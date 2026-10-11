#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "orion/language.h"
#include "orion/vm.h"

#define VAR_LIMIT 256
#define CALL_LIMIT 256
#define STEP_LIMIT 10000000ULL

typedef struct { const char *name; char type[ORION_TRACE_NAME]; uint64_t value; uint64_t address; int is_pointer; } Variable;
typedef struct Frame { Variable vars[VAR_LIMIT]; size_t count; struct Frame *parent; const char *function; } Frame;
typedef struct { uint64_t address; const char *type; ASTNode *definition; int freed; int is_array; size_t element_count; } HeapObject;
typedef struct { VM vm; ASTNode *functions; ASTNode *structs; uint64_t steps; unsigned depth; OrionTrace *trace; size_t current_trace_index; int has_current_trace; HeapObject objects[128]; size_t object_count; } Runtime;
typedef struct { int returned; int loop_control; uint64_t value; } Result;

static uint64_t next_slot_address = 0x100000;

static HeapObject *find_object(Runtime *rt, uint64_t address) {
    for (size_t i = 0; i < rt->object_count; ++i)
        if (rt->objects[i].address == address) return &rt->objects[i];
    return NULL;
}
static const char *metadata_type(ASTNode *metadata, int *is_pointer) {
    if (is_pointer) *is_pointer = metadata && metadata->right;
    return metadata ? metadata->opToken.lexeme : "?";
}

static uint64_t float_to_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static double bits_to_float(uint64_t bits) {
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int type_is_float(const char *type) { return type && strcmp(type, "float") == 0; }

static void format_float(char *out, size_t out_size, double value) {
    snprintf(out, out_size, "%.15g", value);
    if (isfinite(value) && !strpbrk(out, ".eE")) {
        size_t len=strlen(out);
        if(len+2<out_size) { out[len]='.'; out[len+1]='0'; out[len+2]='\0'; }
    }
}

static void set_variable_type(Variable *var, ASTNode *metadata) {
    int pointer = 0;
    const char *type = metadata_type(metadata, &pointer);
    snprintf(var->type, sizeof(var->type), "%s%s", type, pointer ? "*" : "");
    var->is_pointer = pointer;
}

static void trace_statement(Runtime *rt, Frame *current, ASTNode *node) {
    if (!rt->trace || !node) return;
    OrionTrace *trace = rt->trace;
    if (trace->count == trace->capacity) {
        size_t next = trace->capacity ? trace->capacity * 2 : 128;
        OrionTraceStep *steps = realloc(trace->steps, next * sizeof(*steps));
        if (!steps) { perror("realloc trace"); exit(1); }
        trace->steps = steps;
        trace->capacity = next;
    }
    OrionTraceStep *step = &trace->steps[trace->count++];
    memset(step, 0, sizeof(*step));
    rt->current_trace_index = trace->count - 1;
    rt->has_current_trace = 1;
    step->source_line = node->opToken.line;
    snprintf(step->instruction, sizeof(step->instruction), "%s", node->opToken.lexeme);

    Frame *call_frames[ORION_TRACE_MAX_FRAMES];
    size_t call_count = 0;
    for (Frame *f = current; f; f = f->parent) {
        if (f->function && call_count < ORION_TRACE_MAX_FRAMES) call_frames[call_count++] = f;
    }
    step->frames = call_count ? calloc(call_count, sizeof(*step->frames)) : NULL;
    if (call_count && !step->frames) { perror("calloc trace frames"); exit(1); }
    for (size_t i = 0; i < call_count; ++i) {
        Frame *function_frame = call_frames[call_count - 1 - i];
        OrionTraceFrame *out = &step->frames[step->frame_count++];
        snprintf(out->function, sizeof(out->function), "%s", function_frame->function);
        Frame *scope = (i + 1 == call_count) ? current : call_frames[call_count - 2 - i]->parent;
        for (; scope; scope = scope->parent) {
            for (size_t v = 0; v < scope->count && out->var_count < ORION_TRACE_MAX_VARS; ++v) {
                Variable *var = &scope->vars[v];
                int shadowed = 0;
                for (size_t old = 0; old < out->var_count; ++old)
                    if (!strcmp(out->vars[old].name, var->name)) shadowed = 1;
                if (shadowed) continue;
                OrionTraceVariable *dest = &out->vars[out->var_count++];
                snprintf(dest->name, sizeof(dest->name), "%s", var->name);
                snprintf(dest->type, sizeof(dest->type), "%s", var->type);
                dest->raw_value = var->value;
                dest->address = var->address;
                dest->is_pointer = var->is_pointer;
                if (var->is_pointer) {
                    if (var->value) {
                        dest->pointer_target = var->value;
                        snprintf(dest->value, sizeof(dest->value), "0x%08" PRIx64, var->value);
                    } else snprintf(dest->value, sizeof(dest->value), "null");
                } else if (type_is_float(var->type))
                    format_float(dest->value, sizeof(dest->value), bits_to_float(var->value));
                else snprintf(dest->value, sizeof(dest->value), "%" PRId64, (int64_t)var->value);
            }
            if (scope == function_frame) break;
        }
    }
    for (size_t i = 0; i < step->frame_count; ++i) {
        for (size_t v = 0; v < step->frames[i].var_count; ++v) {
            OrionTraceVariable *pointer = &step->frames[i].vars[v];
            if (!pointer->is_pointer || !pointer->raw_value) continue;
            for (size_t target_frame = 0; target_frame < step->frame_count; ++target_frame) {
                for (size_t target_var = 0; target_var < step->frames[target_frame].var_count; ++target_var) {
                    OrionTraceVariable *target = &step->frames[target_frame].vars[target_var];
                    if (target->address == pointer->raw_value) {
                        snprintf(pointer->target_name, sizeof(pointer->target_name), "%s", target->name);
                        snprintf(pointer->value, sizeof(pointer->value), "0x%08" PRIx64 " (%s)", pointer->raw_value, pointer->target_name);
                        break;
                    }
                }
                if (pointer->target_name[0]) break;
            }
        }
    }
        step->object_count = rt->object_count < ORION_TRACE_MAX_OBJECTS ? rt->object_count : ORION_TRACE_MAX_OBJECTS;
    step->objects = step->object_count ? calloc(step->object_count, sizeof(*step->objects)) : NULL;
    if (step->object_count && !step->objects) { perror("calloc trace objects"); exit(1); }
    for (size_t i = 0; i < step->object_count; ++i) {
        HeapObject *object = &rt->objects[i];
        OrionTraceObject *out = &step->objects[i];
        snprintf(out->type, sizeof(out->type), "%s", object->type);
        out->address = object->address;
        out->freed = object->freed;
        out->is_array = object->is_array;
        if (object->is_array) {
            out->total_elements = object->element_count;
            out->field_count = object->element_count < ORION_TRACE_MAX_FIELDS ? object->element_count : ORION_TRACE_MAX_FIELDS;
            out->has_more = object->element_count > out->field_count;
            out->fields = out->field_count ? calloc(out->field_count, sizeof(*out->fields)) : NULL;
            if (out->field_count && !out->fields) { perror("calloc trace array fields"); exit(1); }
            for (size_t j = 0; j < out->field_count; ++j) {
                OrionTraceField *dest = &out->fields[j];
                snprintf(dest->name, sizeof(dest->name), "[%zu]", j);
                const char *element_type = !strncmp(object->type, "float", 5) ? "float" :
                                           (!strncmp(object->type, "char", 4) ? "char" : "int");
                snprintf(dest->type, sizeof(dest->type), "%s", element_type);
                uint64_t value = object->freed ? 0 : heap_load_u64(&rt->vm, object->address + j * sizeof(uint64_t));
                dest->raw_value = value;
                dest->pointer_target = !strcmp(element_type, "float") ? 0 : (find_object(rt, value) ? value : 0);
                if (dest->pointer_target) snprintf(dest->value, sizeof(dest->value), "0x%08" PRIx64, value);
                else if (!strcmp(element_type, "float")) format_float(dest->value, sizeof(dest->value), bits_to_float(value));
                else if (value == 0) snprintf(dest->value, sizeof(dest->value), "0");
                else snprintf(dest->value, sizeof(dest->value), "%" PRId64, (int64_t)value);
            }
        } else {
            size_t total_fields = 0;
            for (ASTNode *field = object->definition ? object->definition->left : NULL;
                 field; field = field->next) ++total_fields;
            out->field_count = total_fields < ORION_TRACE_MAX_FIELDS ? total_fields : ORION_TRACE_MAX_FIELDS;
            out->has_more = total_fields > out->field_count;
            out->fields = out->field_count ? calloc(out->field_count, sizeof(*out->fields)) : NULL;
            if (out->field_count && !out->fields) { perror("calloc trace struct fields"); exit(1); }
            size_t field_index = 0;
            for (ASTNode *field = object->definition ? object->definition->left : NULL;
                 field && field_index < out->field_count; field = field->next, ++field_index) {
                OrionTraceField *dest = &out->fields[field_index];
                snprintf(dest->name, sizeof(dest->name), "%s", field->opToken.lexeme);
                int field_pointer = 0;
                snprintf(dest->type, sizeof(dest->type), "%s%s", metadata_type(field->extra, &field_pointer), field_pointer ? "*" : "");
                dest->is_pointer = field_pointer;
                uint64_t value = object->freed ? 0 : heap_load_u64(&rt->vm, object->address + field_index * sizeof(uint64_t));
                dest->raw_value = value;
                int field_is_float = type_is_float(metadata_type(field->extra, NULL)) && !field_pointer;
                dest->pointer_target = field_pointer && value ? value : 0;
                if (dest->pointer_target) snprintf(dest->value, sizeof(dest->value), "0x%08" PRIx64, value);
                else if (field_pointer && value == 0) snprintf(dest->value, sizeof(dest->value), "null");
                else if (field_is_float) format_float(dest->value, sizeof(dest->value), bits_to_float(value));
                else snprintf(dest->value, sizeof(dest->value), "%" PRId64, (int64_t)value);
            }
        }
    }
    for (size_t i = 0; i < step->object_count; ++i) {
        OrionTraceObject *object = &step->objects[i];
        for (size_t f = 0; f < object->field_count; ++f) {
            OrionTraceField *pointer = &object->fields[f];
            if (!pointer->is_pointer || !pointer->raw_value) continue;
            for (size_t frame = 0; frame < step->frame_count; ++frame) {
                for (size_t v = 0; v < step->frames[frame].var_count; ++v) {
                    OrionTraceVariable *target = &step->frames[frame].vars[v];
                    if (target->address == pointer->raw_value) {
                        snprintf(pointer->target_name, sizeof(pointer->target_name), "%s", target->name);
                        snprintf(pointer->value, sizeof(pointer->value), "0x%08" PRIx64 " (%s)", pointer->raw_value, pointer->target_name);
                        break;
                    }
                }
                if (pointer->target_name[0]) break;
            }
        }
    }
}

static void trace_variable_read(Runtime *rt, Variable *var) {
    OrionTraceStep *step = rt->trace && rt->has_current_trace ? &rt->trace->steps[rt->current_trace_index] : NULL;
    if (!step || !var) return;
    for (size_t i = 0; i < step->frame_count; ++i)
        for (size_t v = 0; v < step->frames[i].var_count; ++v)
            if (step->frames[i].vars[v].address == var->address) step->frames[i].vars[v].read = 1;
}

static void trace_field_read(Runtime *rt, uint64_t address, const char *field_name) {
    OrionTraceStep *step = rt->trace && rt->has_current_trace ? &rt->trace->steps[rt->current_trace_index] : NULL;
    if (!step) return;
    char array_name[ORION_TRACE_NAME];
    for (size_t i = 0; i < step->object_count; ++i) {
        OrionTraceObject *object = &step->objects[i];
        if (object->address != address) continue;
        const char *target = field_name;
        if (object->is_array) {
            snprintf(array_name, sizeof(array_name), "[%s]", field_name);
            target = array_name;
        }
        for (size_t f = 0; f < object->field_count; ++f)
            if (!strcmp(object->fields[f].name, target)) object->fields[f].read = 1;
    }
}

void orion_trace_free(OrionTrace *trace) {
    if (!trace) return;
    for (size_t i = 0; i < trace->count; ++i) {
        free(trace->steps[i].frames);
        for (size_t o = 0; o < trace->steps[i].object_count; ++o)
            free(trace->steps[i].objects[o].fields);
        free(trace->steps[i].objects);
    }
    free(trace->steps);
    memset(trace, 0, sizeof(*trace));
}

static void runtime_error(const char *message, int line) {
    fprintf(stderr, "Runtime error at line %d: %s\n", line, message);
    exit(1);
}
static void step(Runtime *rt, Token t) { if (++rt->steps > STEP_LIMIT) runtime_error("instruction limit exceeded", t.line); }
static Variable *find_var(Frame *f, const char *name) {
    for (; f; f = f->parent) for (size_t i = 0; i < f->count; ++i) if (!strcmp(f->vars[i].name, name)) return &f->vars[i];
    return NULL;
}
static void set_var(Frame *f, const char *name, uint64_t value, int declaration, int line, ASTNode *metadata) {
    Variable *v = declaration ? NULL : find_var(f, name);
    if (!v) {
        if (!declaration) runtime_error("assignment to an undeclared variable", line);
        for (size_t i = 0; i < f->count; ++i)
            if (!strcmp(f->vars[i].name, name)) runtime_error("variable is already declared in this scope", line);
        if (f->count == VAR_LIMIT) runtime_error("too many local variables", line);
        v = &f->vars[f->count++]; v->name = name; v->address = next_slot_address; next_slot_address += 8;
        set_variable_type(v, metadata);
    }
    v->value = value;
}
static uint64_t eval(Runtime *, Frame *, ASTNode *);
static Result execute_list(Runtime *, Frame *, ASTNode *);
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

static const char *expression_type(Runtime *rt, Frame *f, ASTNode *n, int *is_pointer);

static ASTNode *field_declaration(Runtime *rt, Frame *f, ASTNode *base, const char *name) {
    const char *base_type = expression_type(rt, f, base, NULL);
    char struct_name[ORION_TRACE_NAME];
    snprintf(struct_name, sizeof(struct_name), "%s", base_type ? base_type : "");
    size_t length = strlen(struct_name);
    if (length && struct_name[length - 1] == '*') struct_name[length - 1] = '\0';
    ASTNode *structure = find_struct(rt, struct_name);
    for (ASTNode *field = structure ? structure->left : NULL; field; field = field->next)
        if (!strcmp(field->opToken.lexeme, name)) return field;
    return NULL;
}

static const char *expression_type(Runtime *rt, Frame *f, ASTNode *n, int *is_pointer) {
    if (is_pointer) *is_pointer = 0;
    if (!n) return "?";
    if (n->type == AST_IDENT) {
        Variable *v = find_var(f, n->opToken.lexeme);
        if (v) {
            if (is_pointer) *is_pointer = v->is_pointer;
            return v->type;
        }
    }
    if (n->type == AST_ALLOC) {
        if (is_pointer) *is_pointer = 1;
        return n->opToken.lexeme;
    }
    if (n->type == AST_FIELD_ACCESS) {
        ASTNode *field = field_declaration(rt, f, n->left, n->opToken.lexeme);
        if (field && field->extra) return metadata_type(field->extra, is_pointer);
    }
    if (n->type == AST_INDEX_ACCESS) {
        int pointer = 0;
        const char *base_type = expression_type(rt, f, n->left, &pointer);
        if (pointer && base_type) {
            size_t len = strlen(base_type);
            if (len == 6 && !strcmp(base_type, "float*")) base_type = "float";
            if (is_pointer) *is_pointer = 0;
            return base_type;
        }
    }
    if(n->type==AST_CALL) {
        for(ASTNode *fn=rt->functions;fn;fn=fn->next)
            if(!strcmp(fn->opToken.lexeme,n->opToken.lexeme) && fn->right)
                return metadata_type(fn->right,is_pointer);
    }
    if (n->type == AST_LITERAL && n->opToken.type == TOKEN_FLT_LIT) return "float";
    return "?";
}

static int expression_is_float(Runtime *rt, Frame *f, ASTNode *n) {
    if (!n) return 0;
    if (n->type == AST_LITERAL) return n->opToken.type == TOKEN_FLT_LIT;
    if (n->type == AST_UNARY) return expression_is_float(rt, f, n->left);
    if (n->type == AST_IDENT || n->type == AST_INDEX_ACCESS || n->type == AST_FIELD_ACCESS || n->type == AST_CALL) {
        int pointer = 0;
        return type_is_float(expression_type(rt, f, n, &pointer)) && !pointer;
    }
    if (n->type == AST_BINARY) {
        switch (n->opToken.type) {
            case TOKEN_PLUS: case TOKEN_MINUS: case TOKEN_STAR: case TOKEN_SLASH:
                return expression_is_float(rt, f, n->left) || expression_is_float(rt, f, n->right);
            default: return 0;
        }
    }
    return 0;
}

static double numeric_as_double(Runtime *rt, Frame *f, ASTNode *n, uint64_t bits) {
    return expression_is_float(rt, f, n) ? bits_to_float(bits) : (double)(int64_t)bits;
}

static uint64_t coerce_assignment(Runtime *rt, Frame *f, ASTNode *value, uint64_t bits,
                                  const char *destination_type, int destination_pointer) {
    if (destination_pointer) return bits;
    int destination_float = type_is_float(destination_type);
    int source_float = expression_is_float(rt, f, value);
    if (destination_float && !source_float) return float_to_bits((double)(int64_t)bits);
    if (!destination_float && source_float) return (uint64_t)(int64_t)bits_to_float(bits);
    return bits;
}

static int expression_truth(Runtime *rt, Frame *f, ASTNode *n, uint64_t bits) {
    return expression_is_float(rt, f, n) ? bits_to_float(bits) != 0.0 : bits != 0;
}

static uint64_t eval(Runtime *rt, Frame *f, ASTNode *n) {
    if (!n) return 0;
    step(rt, n->opToken);
    switch (n->type) {
        case AST_LITERAL:
            if (n->opToken.type == TOKEN_NULL) return 0;
            if (n->opToken.type == TOKEN_STR_LIT) runtime_error("strings can only be passed to print for now", n->opToken.line);
            if (n->opToken.type == TOKEN_CHR_LIT) return (unsigned char)n->opToken.lexeme[1];
            if (n->opToken.type == TOKEN_FLT_LIT) return float_to_bits(strtod(n->opToken.lexeme, NULL));
            return (uint64_t)strtoll(n->opToken.lexeme, NULL, 0);
        case AST_IDENT: {
            Variable *v = find_var(f, n->opToken.lexeme);
            if (!v) runtime_error("unknown variable", n->opToken.line);
            trace_variable_read(rt, v);
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
                uint64_t address = heap_alloc(&rt->vm, (uint32_t)size);
                if (rt->object_count < sizeof(rt->objects) / sizeof(rt->objects[0]))
                    rt->objects[rt->object_count++] = (HeapObject){address, s->opToken.lexeme, s, 0, 0, 0};
                return address;
            }
            if (count > UINT32_MAX / sizeof(uint64_t)) runtime_error("array is too large", n->opToken.line);
            uint64_t address = heap_alloc(&rt->vm, (uint32_t)(count * sizeof(uint64_t)));
            if (rt->object_count < sizeof(rt->objects) / sizeof(rt->objects[0])) {
                const char *array_type = n->opToken.type == TOKEN_INT_T ? "int[]" :
                                         n->opToken.type == TOKEN_FLOAT_T ? "float[]" : "char[]";
                rt->objects[rt->object_count++] = (HeapObject){address, array_type, NULL, 0, 1, (size_t)count};
            }
            return address;
        }
        case AST_INDEX_ACCESS: {
            uint64_t base = eval(rt, f, n->left), index = eval(rt, f, n->right);
            if (index > (UINT64_MAX - base) / sizeof(uint64_t)) runtime_error("array index overflow", n->opToken.line);
            char index_name[24]; snprintf(index_name, sizeof(index_name), "%" PRIu64, index);
            trace_field_read(rt, base, index_name);
            return heap_load_u64(&rt->vm, base + index * sizeof(uint64_t));
        }
        case AST_UNARY: {
            uint64_t v = eval(rt, f, n->left);
            if (n->opToken.type == TOKEN_MINUS)
                return expression_is_float(rt, f, n->left) ? float_to_bits(-bits_to_float(v)) : (uint64_t)(-((int64_t)v));
            if (n->opToken.type == TOKEN_NOT) return !expression_truth(rt, f, n->left, v);
            return v;
        }
        case AST_BINARY: {
            uint64_t a = eval(rt, f, n->left);
            if (n->opToken.type == TOKEN_AND && !expression_truth(rt, f, n->left, a)) return 0;
            if (n->opToken.type == TOKEN_OR && expression_truth(rt, f, n->left, a)) return 1;
            uint64_t b = eval(rt, f, n->right);
            int any_float=expression_is_float(rt,f,n->left) || expression_is_float(rt,f,n->right);
            if(any_float && (n->opToken.type==TOKEN_PERCENT || n->opToken.type==TOKEN_SHL || n->opToken.type==TOKEN_SHR))
                runtime_error("operator is only defined for integers",n->opToken.line);
            int numeric_operator=0;
            switch(n->opToken.type) {
                case TOKEN_PLUS: case TOKEN_MINUS: case TOKEN_STAR: case TOKEN_SLASH:
                case TOKEN_EQ: case TOKEN_NE: case TOKEN_LT: case TOKEN_LE: case TOKEN_GT: case TOKEN_GE:
                    numeric_operator=1; break;
                default: break;
            }
            int use_float=any_float && numeric_operator;
            if (use_float) {
                double da=numeric_as_double(rt,f,n->left,a), db=numeric_as_double(rt,f,n->right,b);
                switch (n->opToken.type) {
                    case TOKEN_PLUS: return float_to_bits(da+db);
                    case TOKEN_MINUS: return float_to_bits(da-db);
                    case TOKEN_STAR: return float_to_bits(da*db);
                    case TOKEN_SLASH: if (db==0.0) runtime_error("division by zero",n->opToken.line); return float_to_bits(da/db);
                    case TOKEN_EQ: return da==db;
                    case TOKEN_NE: return da!=db;
                    case TOKEN_LT: return da<db;
                    case TOKEN_LE: return da<=db;
                    case TOKEN_GT: return da>db;
                    case TOKEN_GE: return da>=db;
                    default: runtime_error("operator is not defined for floats",n->opToken.line);
                }
            }
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
                case TOKEN_AND: return expression_truth(rt, f, n->left, a) && expression_truth(rt, f, n->right, b);
                case TOKEN_OR: return expression_truth(rt, f, n->left, a) || expression_truth(rt, f, n->right, b);
                case TOKEN_SHL: return a << (b & 63); case TOKEN_SHR: return (uint64_t)(sa >> (b & 63));
                default: runtime_error("unsupported operator", n->opToken.line);
            }
        }
        case AST_CALL: {
            if (!strcmp(n->opToken.lexeme, "print")) {
                for (ASTNode *arg = n->left; arg; arg = arg->next) {
                    if (arg->type == AST_LITERAL && arg->opToken.type == TOKEN_STR_LIT) print_string(arg->opToken.lexeme);
                    else {
                        uint64_t value=eval(rt,f,arg);
                        if(expression_is_float(rt,f,arg)) {
                            char formatted[64]; format_float(formatted,sizeof(formatted),bits_to_float(value));
                            printf("%s\n",formatted);
                        }
                        else printf("%" PRId64 "\n",(int64_t)value);
                    }
                }
                return 0;
            }
            ASTNode *fn = rt->functions;
            while (fn && strcmp(fn->opToken.lexeme, n->opToken.lexeme)) fn = fn->next;
            if (!fn) runtime_error("unknown function", n->opToken.line);
            if (rt->depth >= CALL_LIMIT) runtime_error("call stack limit exceeded", n->opToken.line);
            Frame call = {.parent = f, .function = fn->opToken.lexeme};
            ASTNode *param = fn->extra, *arg = n->left;
            while (param && arg) {
                uint64_t value=eval(rt,f,arg);
                value=coerce_assignment(rt,f,arg,value,metadata_type(param->extra,NULL),param->extra && param->extra->right);
                set_var(&call, param->opToken.lexeme, value, 1, param->opToken.line, param->extra);
                param = param->next; arg = arg->next;
            }
            if (param || arg) runtime_error("argument count does not match function parameters", n->opToken.line);
            size_t caller_step_index = rt->current_trace_index;
            int had_caller_step = rt->has_current_trace;
            rt->depth++;
            Result result = execute_list(rt, &call, fn->left);
            rt->depth--;
            rt->current_trace_index = caller_step_index;
            rt->has_current_trace = had_caller_step;
            return result.value;
        }
        case AST_FIELD_ACCESS: {
            int offset = field_offset(rt, n->opToken.lexeme);
            if (offset == -2) runtime_error("field name is ambiguous across struct layouts", n->opToken.line);
            if (offset == -1) runtime_error("unknown struct field", n->opToken.line);
            uint64_t base = eval(rt, f, n->left);
            trace_field_read(rt, base, n->opToken.lexeme);
            return heap_load_u64(&rt->vm, base + (uint64_t)offset);
        }
        default: runtime_error("expression expected", n->opToken.line);
    }
    return 0;
}

static void assign(Runtime *rt, Frame *f, ASTNode *target, ASTNode *value, int declaration, ASTNode *metadata) {
    uint64_t v = eval(rt, f, value);
    if (target->type == AST_IDENT) {
        const char *destination_type=metadata_type(metadata,NULL);
        int destination_pointer=metadata && metadata->right;
        if(!declaration) {
            Variable *existing=find_var(f,target->opToken.lexeme);
            if(existing) { destination_type=existing->type; destination_pointer=existing->is_pointer; }
        }
        v=coerce_assignment(rt,f,value,v,destination_type,destination_pointer);
        set_var(f, target->opToken.lexeme, v, declaration, target->opToken.line, metadata);
    }
    else if (target->type == AST_INDEX_ACCESS) {
        uint64_t base = eval(rt, f, target->left), index = eval(rt, f, target->right);
        if (index > (UINT64_MAX - base) / sizeof(uint64_t)) runtime_error("array index overflow", target->opToken.line);
        int pointer=0;
        const char *element_type=expression_type(rt,f,target,&pointer);
        v=coerce_assignment(rt,f,value,v,element_type,0);
        heap_store_u64(&rt->vm, base + index * sizeof(uint64_t), v);
    } else if (target->type == AST_FIELD_ACCESS) {
        int offset = field_offset(rt, target->opToken.lexeme);
        if (offset == -2) runtime_error("field name is ambiguous across struct layouts", target->opToken.line);
        if (offset == -1) runtime_error("unknown struct field", target->opToken.line);
        uint64_t base = eval(rt, f, target->left);
        ASTNode *field=field_declaration(rt,f,target->left,target->opToken.lexeme);
        v=coerce_assignment(rt,f,value,v,metadata_type(field ? field->extra : NULL,NULL),field && field->extra && field->extra->right);
        heap_store_u64(&rt->vm, base + (uint64_t)offset, v);
    } else runtime_error("assignment target must be a variable, array element, or struct field", target->opToken.line);
}
static Result execute_list(Runtime *rt, Frame *f, ASTNode *n) {
    for (; n; n = n->next) {
        step(rt, n->opToken);
        trace_statement(rt, f, n);
        switch (n->type) {
            case AST_VAR_DECL: assign(rt, f, n->left, n->right, 1, n->extra); break;
            case AST_ASSIGN: assign(rt, f, n->left, n->right, 0, NULL); break;
            case AST_FREE: { uint64_t p = eval(rt, f, n->left); if (p) { heap_free(&rt->vm, p); HeapObject *object = find_object(rt, p); if (object) object->freed = 1; } break; }
            case AST_RETURN: {
                uint64_t value=eval(rt,f,n->left);
                Frame *function_frame=f;
                while(function_frame && !function_frame->function) function_frame=function_frame->parent;
                for(ASTNode *fn=rt->functions;fn;fn=fn->next)
                    if(function_frame && !strcmp(fn->opToken.lexeme,function_frame->function)) {
                        value=coerce_assignment(rt,f,n->left,value,metadata_type(fn->right,NULL),fn->right && fn->right->right);
                        break;
                    }
                return (Result){1,0,value};
            }
            case AST_BREAK: return (Result){0,1,0};
            case AST_CONTINUE: return (Result){0,2,0};
            case AST_IF_STMT: {
                uint64_t condition=eval(rt,f,n->left);
                ASTNode *body = expression_truth(rt,f,n->left,condition) ? n->right : n->extra;
                Frame scope = {.parent = f}; Result r = execute_list(rt, &scope, body); if (r.returned || r.loop_control) return r; break;
            }
            case AST_WHILE_STMT:
                for (;;) {
                    uint64_t condition=eval(rt,f,n->left);
                    if(!expression_truth(rt,f,n->left,condition)) break;
                    Frame scope = {.parent = f}; Result r = execute_list(rt, &scope, n->right);
                    if (r.returned) return r;
                    if (r.loop_control == 1) break;
                    if (r.loop_control == 2) continue;
                }
                break;
            case AST_CALL: (void)eval(rt, f, n); break;
            default: runtime_error("unsupported statement", n->opToken.line);
        }
    }
    return (Result){0, 0, 0};
}
static int run_program_internal(ASTNode *program, OrionTrace *trace) {
    next_slot_address = 0x100000;
    Runtime rt = {0}; vm_init(&rt.vm); rt.functions = program->extra; rt.structs = program->right; rt.trace = trace;
    ASTNode *main_fn = rt.functions;
    while (main_fn && strcmp(main_fn->opToken.lexeme, "main")) main_fn = main_fn->next;
    Frame global = {0};
    if (main_fn) {
        Frame frame = {.parent = &global, .function = main_fn->opToken.lexeme}; rt.depth = 1;
        Result r = execute_list(&rt, &frame, main_fn->left);
        return (int)(int64_t)r.value;
    }
    Result r = execute_list(&rt, &global, program->left);
    return (int)(int64_t)r.value;
}

int run_program(ASTNode *program) { return run_program_internal(program, NULL); }
int run_program_traced(ASTNode *program, OrionTrace *trace) {
    if (trace) memset(trace, 0, sizeof(*trace));
    return run_program_internal(program, trace);
}
