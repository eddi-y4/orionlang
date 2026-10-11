/* vm.h */
#ifndef VM_H
#define VM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define STACK_SIZE 256
#define HEAP_SIZE  (16u * 1024u * 1024u) // 16 MiB managed heap
#define NUM_REGS   16

typedef struct {
    uint32_t size;          // Usable bytes in this block
    bool is_free;
    uint32_t magic;
    uint32_t capacity;      // Physical payload capacity (may exceed size after reuse)
} HeapHeader;

typedef struct {
    uint64_t ip;
    uint64_t regs[NUM_REGS];
    uint64_t call_stack[STACK_SIZE];
    int call_sp;
    uint64_t local_slots[STACK_SIZE];
    uint8_t *heap;
    size_t heap_top;
} VM;

// Generate Opcode Enum automatically via X-Macros
typedef enum {
#define X(name, mask, ...) OP_##name,
#include "opcode.def.h"
#undef X
    OP_COUNT
} Opcode;



void vm_init(VM *vm);
void vm_destroy(VM *vm);
uint64_t heap_alloc(VM *vm, uint32_t size);
void heap_free(VM *vm, uint64_t address);
uint64_t heap_load_u64(VM *vm, uint64_t address);
void heap_store_u64(VM *vm, uint64_t address, uint64_t value);
void vm_execute(VM *vm, const uint8_t *bytecode, size_t code_size);

#endif
