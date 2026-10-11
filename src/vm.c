#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "orion/vm.h"

// Initialize the Virtual Machine
void vm_init(VM *vm) {
    memset(vm, 0, sizeof(VM));
    vm->heap = calloc(HEAP_SIZE, 1);
    if (!vm->heap) {
        perror("allocating VM heap");
        exit(EXIT_FAILURE);
    }
    vm->heap_top = 0;
}

void vm_destroy(VM *vm) {
    if (!vm) return;
    free(vm->heap);
    vm->heap = NULL;
    vm->heap_top = 0;
}

// Allocate block on heap with metadata header
static size_t heap_align(size_t n) {
    return (n + 7u) & ~(size_t)7u;
}

uint64_t heap_alloc(VM *vm, uint32_t size) {
    size_t payload = heap_align(size ? size : 1);
    size_t cursor = 0;
    while (cursor < vm->heap_top) {
        HeapHeader *h = (HeapHeader *)(void *)(vm->heap + cursor);
        size_t step = sizeof(*h) + heap_align(h->capacity);
        if (h->magic != 0xDEADBEEF || step > vm->heap_top - cursor) {
            fprintf(stderr, "Runtime Error: Corrupt heap block chain\n");
            exit(1);
        }
        if (h->is_free && h->capacity >= payload) {
            h->size = size;
            h->is_free = false;
            memset(vm->heap + cursor + sizeof(*h), 0, h->capacity);
            return cursor + sizeof(*h);
        }
        cursor += step;
    }
    size_t total_size = sizeof(HeapHeader) + payload;
    if (total_size > HEAP_SIZE - vm->heap_top) {
        fprintf(stderr, "Runtime Error: Out of Memory (Heap Exhausted)\n");
        exit(1);
    }
    
    HeapHeader *header = (HeapHeader*)&vm->heap[vm->heap_top];
    header->size = size;
    header->is_free = false;
    header->magic = 0xDEADBEEF;
    header->capacity = (uint32_t)payload;
    
    uint64_t address = vm->heap_top + sizeof(HeapHeader);
    vm->heap_top += total_size;
    return address;
}

// Free heap block (enables visualization of dangling pointers / double frees)
void heap_free(VM *vm, uint64_t address) {
    if (address < sizeof(HeapHeader) || address >= vm->heap_top) {
        fprintf(stderr, "Runtime Error: Segmentation Fault / Invalid Free Address\n");
        exit(1);
    }
    
    size_t cursor = 0;
    HeapHeader *header = NULL;
    while (cursor < vm->heap_top) {
        HeapHeader *candidate = (HeapHeader *)(void *)(vm->heap + cursor);
        size_t step = sizeof(*candidate) + heap_align(candidate->capacity);
        if (candidate->magic != 0xDEADBEEF || step > vm->heap_top - cursor) break;
        if (address == cursor + sizeof(*candidate)) { header = candidate; break; }
        cursor += step;
    }
    if (!header) {
        fprintf(stderr, "Runtime Error: Heap Corruption / Invalid Pointer\n");
        exit(1);
    }
    if (header->is_free) {
        fprintf(stderr, "Runtime Error: Double Free Detected!\n");
        exit(1);
    }
    
    header->is_free = true;
}

static uint8_t *heap_resolve(VM *vm, uint64_t address, size_t width) {
    size_t cursor = 0;
    while (cursor < vm->heap_top) {
        HeapHeader *h = (HeapHeader *)(void *)(vm->heap + cursor);
        size_t step = sizeof(*h) + heap_align(h->capacity);
        if (h->magic != 0xDEADBEEF || step > vm->heap_top - cursor) break;
        uint64_t begin = cursor + sizeof(*h);
        if (address >= begin && address - begin <= h->size && width <= h->size - (size_t)(address - begin)) {
            if (h->is_free) {
                fprintf(stderr, "Runtime Error: Use after free\n");
                exit(1);
            }
            return vm->heap + address;
        }
        cursor += step;
    }
    fprintf(stderr, "Runtime Error: Heap access out of bounds or invalid pointer\n");
    exit(1);
}

uint64_t heap_load_u64(VM *vm, uint64_t address) {
    uint64_t value;
    memcpy(&value, heap_resolve(vm, address, sizeof(value)), sizeof(value));
    return value;
}

void heap_store_u64(VM *vm, uint64_t address, uint64_t value) {
    memcpy(heap_resolve(vm, address, sizeof(value)), &value, sizeof(value));
}

// Main VM Execution Interpreter Loop
void vm_execute(VM *vm, const uint8_t *bytecode, size_t code_size) {
    bool running = true;
    
    while (running && vm->ip < code_size) {
        uint8_t op = bytecode[vm->ip++];
        
        switch (op) {
            case OP_CONST: {
                uint8_t reg = bytecode[vm->ip++];
                uint64_t imm = *(uint64_t*)&bytecode[vm->ip];
                vm->ip += 8;
                vm->regs[reg] = imm;
                break;
            }
            case OP_MOVE: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                vm->regs[r1] = vm->regs[r2];
                break;
            }
            case OP_ADD: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                vm->regs[r1] = vm->regs[r2] + vm->regs[r3];
                break;
            }
            case OP_SUB: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                vm->regs[r1] = vm->regs[r2] - vm->regs[r3];
                break;
            }
            case OP_MUL: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                vm->regs[r1] = vm->regs[r2] * vm->regs[r3];
                break;
            }
            case OP_DIV: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                if (vm->regs[r3] == 0) {
                    fprintf(stderr, "Runtime Error: Division by Zero\n");
                    exit(1);
                }
                vm->regs[r1] = vm->regs[r2] / vm->regs[r3];
                break;
            }
            case OP_NEG: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                vm->regs[r1] = (uint64_t)(-((int64_t)vm->regs[r2]));
                break;
            }
            case OP_NOT: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                vm->regs[r1] = (vm->regs[r2] == 0) ? 1 : 0;
                break;
            }
            case OP_FADD: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                double f2 = *(double*)&vm->regs[r2];
                double f3 = *(double*)&vm->regs[r3];
                double res = f2 + f3;
                vm->regs[r1] = *(uint64_t*)&res;
                break;
            }
            case OP_FSUB: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                double f2 = *(double*)&vm->regs[r2];
                double f3 = *(double*)&vm->regs[r3];
                double res = f2 - f3;
                vm->regs[r1] = *(uint64_t*)&res;
                break;
            }
            case OP_FMUL: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                double f2 = *(double*)&vm->regs[r2];
                double f3 = *(double*)&vm->regs[r3];
                double res = f2 * f3;
                vm->regs[r1] = *(uint64_t*)&res;
                break;
            }
            case OP_FDIV: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                double f2 = *(double*)&vm->regs[r2];
                double f3 = *(double*)&vm->regs[r3];
                double res = f2 / f3;
                vm->regs[r1] = *(uint64_t*)&res;
                break;
            }
            case OP_BIT_AND: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                vm->regs[r1] = vm->regs[r2] & vm->regs[r3];
                break;
            }
            case OP_BIT_OR: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                vm->regs[r1] = vm->regs[r2] | vm->regs[r3];
                break;
            }
            case OP_SHL: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                vm->regs[r1] = vm->regs[r2] << vm->regs[r3];
                break;
            }
            case OP_SHR: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                vm->regs[r1] = vm->regs[r2] >> vm->regs[r3];
                break;
            }
            case OP_CMP_EQ: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                vm->regs[r1] = (vm->regs[r2] == vm->regs[r3]) ? 1 : 0;
                break;
            }
            case OP_CMP_NE: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                vm->regs[r1] = (vm->regs[r2] != vm->regs[r3]) ? 1 : 0;
                break;
            }
            case OP_CMP_LT: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                vm->regs[r1] = (vm->regs[r2] < vm->regs[r3]) ? 1 : 0;
                break;
            }
            case OP_CMP_LE: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                vm->regs[r1] = (vm->regs[r2] <= vm->regs[r3]) ? 1 : 0;
                break;
            }
            case OP_CMP_GT: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                vm->regs[r1] = (vm->regs[r2] > vm->regs[r3]) ? 1 : 0;
                break;
            }
            case OP_CMP_GE: {
                uint8_t r1 = bytecode[vm->ip++];
                uint8_t r2 = bytecode[vm->ip++];
                uint8_t r3 = bytecode[vm->ip++];
                vm->regs[r1] = (vm->regs[r2] >= vm->regs[r3]) ? 1 : 0;
                break;
            }
            case OP_JMP: {
                uint64_t target = *(uint64_t*)&bytecode[vm->ip];
                vm->ip = target;
                break;
            }
            case OP_JMP_IF: {
                uint8_t reg = bytecode[vm->ip++];
                uint64_t target = *(uint64_t*)&bytecode[vm->ip];
                vm->ip += 8;
                if (vm->regs[reg] != 0) {
                    vm->ip = target;
                }
                break;
            }
            case OP_CALL: {
                uint64_t func_target = *(uint64_t*)&bytecode[vm->ip];
                vm->ip += 8;
                if (vm->call_sp >= STACK_SIZE) {
                    fprintf(stderr, "Runtime Error: Call Stack Overflow\n");
                    exit(1);
                }
                vm->call_stack[vm->call_sp++] = vm->ip;
                vm->ip = func_target;
                break;
            }
            case OP_RET: {
                if (vm->call_sp <= 0) {
                    fprintf(stderr, "Runtime Error: Call Stack Underflow\n");
                    exit(1);
                }
                vm->ip = vm->call_stack[--vm->call_sp];
                break;
            }
            case OP_HALT: {
                running = false;
                break;
            }
            case OP_ALLOC: {
                uint8_t reg = bytecode[vm->ip++];
                uint16_t type_size = *(uint16_t*)&bytecode[vm->ip];
                vm->ip += 2;
                vm->regs[reg] = heap_alloc(vm, type_size);
                break;
            }
            case OP_ALLOC_ARR: {
                uint8_t reg = bytecode[vm->ip++];
                uint16_t type_size = *(uint16_t*)&bytecode[vm->ip];
                vm->ip += 2;
                uint8_t count_reg = bytecode[vm->ip++];
                uint64_t count = vm->regs[count_reg];
                if (count && type_size > UINT32_MAX / count) {
                    fprintf(stderr, "Runtime Error: Array allocation is too large\n");
                    exit(1);
                }
                vm->regs[reg] = heap_alloc(vm, (uint32_t)(type_size * count));
                break;
            }
            case OP_FREE: {
                uint8_t reg = bytecode[vm->ip++];
                if (vm->regs[reg] != 0) heap_free(vm, vm->regs[reg]);
                break;
            }
            case OP_LOAD_FIELD: {
                uint8_t r_dest = bytecode[vm->ip++];
                uint8_t r_src = bytecode[vm->ip++];
                uint16_t offset = *(uint16_t*)&bytecode[vm->ip];
                vm->ip += 2;
                
                uint64_t addr = vm->regs[r_src] + offset;
                memcpy(&vm->regs[r_dest], heap_resolve(vm, addr, sizeof(uint64_t)), sizeof(uint64_t));
                break;
            }
            case OP_STORE_FIELD: {
                uint8_t r_dest = bytecode[vm->ip++];
                uint16_t offset = *(uint16_t*)&bytecode[vm->ip];
                vm->ip += 2;
                uint8_t r_val = bytecode[vm->ip++];
                
                uint64_t addr = vm->regs[r_dest] + offset;
                memcpy(heap_resolve(vm, addr, sizeof(uint64_t)), &vm->regs[r_val], sizeof(uint64_t));
                break;
            }
            case OP_LOAD_ELEM: {
                uint8_t r_dest = bytecode[vm->ip++];
                uint8_t r_arr = bytecode[vm->ip++];
                uint8_t r_idx = bytecode[vm->ip++];
                if (vm->regs[r_idx] > UINT64_MAX / sizeof(uint64_t)) {
                    fprintf(stderr, "Runtime Error: Array index overflow\n"); exit(1);
                }
                uint64_t addr = vm->regs[r_arr] + vm->regs[r_idx] * sizeof(uint64_t);
                memcpy(&vm->regs[r_dest], heap_resolve(vm, addr, sizeof(uint64_t)), sizeof(uint64_t));
                break;
            }
            case OP_STORE_ELEM: {
                uint8_t r_arr = bytecode[vm->ip++];
                uint8_t r_idx = bytecode[vm->ip++];
                uint8_t r_val = bytecode[vm->ip++];
                if (vm->regs[r_idx] > UINT64_MAX / sizeof(uint64_t)) {
                    fprintf(stderr, "Runtime Error: Array index overflow\n"); exit(1);
                }
                uint64_t addr = vm->regs[r_arr] + vm->regs[r_idx] * sizeof(uint64_t);
                memcpy(heap_resolve(vm, addr, sizeof(uint64_t)), &vm->regs[r_val], sizeof(uint64_t));
                break;
            }
            case OP_LOAD_LOCAL: {
                uint8_t reg = bytecode[vm->ip++];
                uint16_t slot = *(uint16_t*)&bytecode[vm->ip];
                vm->ip += 2;
                vm->regs[reg] = vm->local_slots[slot];
                break;
            }
            case OP_STORE_LOCAL: {
                uint16_t slot = *(uint16_t*)&bytecode[vm->ip];
                vm->ip += 2;
                uint8_t reg = bytecode[vm->ip++];
                vm->local_slots[slot] = vm->regs[reg];
                break;
            }
            case OP_ADDR_LOCAL: {
                uint8_t reg = bytecode[vm->ip++];
                uint16_t slot = *(uint16_t*)&bytecode[vm->ip];
                vm->ip += 2;
                // Return the memory address reference of the local slot index
                vm->regs[reg] = (uint64_t)&vm->local_slots[slot];
                break;
            }
            case OP_CAST: {
                uint8_t r_dest = bytecode[vm->ip++];
                uint8_t r_src = bytecode[vm->ip++];
                uint16_t target_type = *(uint16_t*)&bytecode[vm->ip];
                vm->ip += 2;
                (void)target_type;
                // Basic type casting pass-through implementation
                vm->regs[r_dest] = vm->regs[r_src];
                break;
            }
            case OP_PRINT: {
                uint8_t reg = bytecode[vm->ip++];
                printf("[VM PRINT] r%d = %llu (0x%llX)\n", reg, (unsigned long long)vm->regs[reg], (unsigned long long)vm->regs[reg]);
                break;
            }
            case OP_TRAP: {
                uint8_t imm = bytecode[vm->ip++];
                printf("[VM TRAP] Triggered code: %d\n", imm);
                break;
            }
            default:
                fprintf(stderr, "Runtime Error: Unknown Opcode 0x%02X\n", op);
                exit(1);
        }
    }
}
