#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "orion/vm.h"

static void put_u64(uint8_t *dst, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) dst[i] = (uint8_t)(value >> (8 * i));
}

int main(void) {
    VM vm;
    vm_init(&vm);

    uint64_t first = heap_alloc(&vm, 8);
    uint64_t second = heap_alloc(&vm, 16);
    heap_store_u64(&vm, first, 99);
    assert(heap_load_u64(&vm, first) == 99);
    size_t high_water = vm.heap_top;
    heap_free(&vm, first);
    uint64_t reused = heap_alloc(&vm, 8);
    assert(reused == first);
    assert(heap_load_u64(&vm, reused) == 0);
    assert(vm.heap_top == high_water);
    assert(second != reused);

    vm_destroy(&vm);
    vm_init(&vm);
    uint8_t code[] = {
        OP_CONST, 1, 0,0,0,0,0,0,0,0,
        OP_ALLOC, 0, 8,0,
        OP_STORE_FIELD, 0, 0,0, 1,
        OP_CONST, 3, 77,0,0,0,0,0,0,0,
        OP_LOAD_FIELD, 2, 0, 0,0,
        OP_HALT
    };
    put_u64(&code[2], 42);
    vm_execute(&vm, code, sizeof(code));
    assert(vm.regs[2] == 42);
    assert(vm.regs[3] == 77); /* STORE_FIELD consumed only its declared operands. */

    vm_destroy(&vm);
    puts("VM memory and instruction tests passed");
    return 0;
}
