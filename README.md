# OrionLang

OrionLang is an experimental, manually memory-managed language for practicing data structures and algorithms.

## Build and run

```sh
make
./ori_vm examples/two_sum.ori
```

Run the regression checks with `make test`.
Each run also writes a readable instruction listing to `build/<source-name>.bytecode.txt`.

Build the raylib execution visualizer with `make ori_stack_viz`, then pass it any Orion source file, for example `./ori_stack_viz examples/reverse_list.ori`. You can also launch the default example with `make visualizer`, or select one with `make visualizer ORI_FILE=examples/reverse_list.ori`. It runs the program once to capture its execution, then opens a step-by-step view of the source line, call stack, local values, variable addresses, and allocated struct fields. Use the on-screen N and B buttons or N/B keys to step forward/backward; R resets. Scroll over either panel to browse long source files and memory views. Run it from the project directory so it can load `assets/FreeSans.ttf`.

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
- `if` / `else`, `while`, nearest-loop `break` and `continue`, arithmetic, comparisons, logical operators, and `print(value)`
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
