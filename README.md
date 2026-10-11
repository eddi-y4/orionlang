# OrionLang

OrionLang is an experimental, manually memory-managed language for practicing data structures and algorithms.

## Build and run

```sh
make
./ori_vm examples/two_sum.ori
```

Run the regression checks with `make test`.
Each run also writes a readable instruction listing to `build/<source-name>.bytecode.txt`.

Build and open the separate raylib stack stepper with `make ori_stack_viz`, or use `make visualizer` to build and launch it. Use the on-screen buttons or the left/right arrow keys (Space advances, R resets).

### Linux

Install a C compiler, `make`, `pkg-config`, and the raylib development package for your distribution. Then run:

```sh
make clean
make
make ori_stack_viz
./ori_stack_viz
```

The visualizer target uses `pkg-config` to find raylib and adds the usual Linux graphics and window-system libraries when linking. You can override `RAYLIB_CFLAGS` or `RAYLIB_LIBS` if raylib is installed in a nonstandard location.

## Current language slice

- `int` variables and `int*` heap arrays
- simple structs with 64-bit fields and pointers, including linked lists
- `alloc int[count]` and explicit `free(pointer)`
- array indexing and bounds/use-after-free checks
- functions with integer/pointer parameters and return values
- `if` / `else`, `while`, arithmetic, comparisons, logical operators, and `print(value)`
- string literals printed with `print("...")` (basic newline, tab, and quote escaping)
- a runtime instruction limit to stop accidental infinite loops

```orion
fn sum(int* values, int count) -> int {
    int i = 0;
    int total = 0;
    while (i < count) {
        total = total + values[i];
        i = i + 1;
    }
    return total;
}
```

The current runtime is an interpreter for this subset. String values are only supported as direct arguments to `print`; user input and compiled bytecode execution are not implemented yet. Struct field names should be unique across the program for now; static type checking is still future work.
