# OrionLang

OrionLang is an experimental, manually memory-managed language for practicing data structures and algorithms.

## Build and run

```sh
make
./ori_vm examples/two_sum.ori
```

Run the regression checks with `make test`.
Each run also writes a readable instruction listing to `build/<source-name>.bytecode.txt`.

Build the raylib execution visualizer with `make ori_stack_viz`, then pass it any Orion source file, for example `./ori_stack_viz examples/reverse_list.ori`. You can also launch the default example with `make visualizer`, or select one with `make visualizer ORI_FILE=examples/reverse_list.ori`. It runs the program once to capture its execution, then opens a step-by-step view of the source line, bytecode instruction queue and executed history, call stack, local values, variable addresses, and allocated struct fields. Use the Source, Bytecode, and Flow tabs (C and F switch to bytecode and flow). In Bytecode view, each N/B step animates one instruction moving between the queued and executed columns; playback animates instructions at the selected rate. The visualized instructions are generated from the AST execution trace; the runtime still executes the AST interpreter. Use the on-screen N and B buttons or N/B keys to step forward/backward; R resets. Scroll over either panel to browse long listings and memory views. Run it from the project directory so it can load its font asset.

### Linux

Install a C compiler, `make`, `pkg-config`, and the raylib development package for your distribution. Then run:

```sh
make clean
make
make ori_stack_viz
./ori_stack_viz
```

The visualizer target uses `pkg-config` to find raylib and adds the usual Linux graphics and window-system libraries when linking. You can override `RAYLIB_CFLAGS` or `RAYLIB_LIBS` if raylib is installed in a nonstandard location.

The visualizer is named Orion Rigel and uses the dark palette by default. Select a palette at compile time by defining one macro:

```sh
make -B ori_stack_viz CFLAGS='-Wall -Wextra -g -Iinclude -DORION_COLORS_GRUVBOX'
# or
make -B ori_stack_viz CFLAGS='-Wall -Wextra -g -Iinclude -DORION_COLORS_DARK'
# or
make -B ori_stack_viz CFLAGS='-Wall -Wextra -g -Iinclude -DORION_COLORS_LIGHT'
# or
make -B ori_stack_viz CFLAGS='-Wall -Wextra -g -Iinclude -DORION_COLORS_HIGH_CONTRAST'
```

The palette definitions live in `include/orion/colors.h`; define at most one `ORION_COLORS_*` selector. `-B` makes Make rebuild the visualizer when you switch selectors.

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


## Meissa: compilation pipeline visualizer

Meissa is a separate raylib program for exploring how an Orion source file is processed. It keeps four panes visible together: source on the left, then lexer tokens, parser AST, and generated virtual instructions. Press **N** (or Right/Space) to animate one source lexeme into a typed token; after lexing finishes, subsequent steps animate AST nodes from the token stream, then instructions from their matching AST nodes. For example, a `BINARY_+` instruction selects the matching `AST_BINARY` node, while a `DECLARE_LOCAL` selects its variable declaration. **B** (or Left) reverses the pipeline. Hold N/B for key repeat. The **Play/Pause** button advances automatically; **− / +** sets the speed from 0.5 to 20 steps per second. Each pane scrolls independently with the mouse wheel over it or by dragging its scrollbar. The Codegen pane shows Orion's readable virtual instruction listing; the current interpreter executes the AST directly. Meissa uses the same palette definitions as Orion Rigel in `include/orion/colors.h`; choose a palette when building:

```sh
make meissa
./meissa examples/hello.ori
# or choose a file through the Makefile variable
make meissa-run ORI_FILE=examples/merge_sort.ori
```

```sh
make -B meissa CFLAGS='-Wall -Wextra -g -Iinclude -DORION_COLORS_GRUVBOX'
# Other shared Rigel palettes: ORION_COLORS_DARK, ORION_COLORS_LIGHT,
# or ORION_COLORS_HIGH_CONTRAST
```
