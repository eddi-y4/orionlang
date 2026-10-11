#set document(title: "OrionLang: A Small Book", author: "OrionLang project")
#set page(paper: "us-letter", margin: (x: 0.8in, y: 0.75in))
#set text(font: "New Computer Modern", size: 10pt)
#set par(leading: 0.68em, justify: true)
#set heading(numbering: "1.")
#show heading.where(level: 1): it => block(above: 1.1em, below: 0.55em)[#it]
#show raw: set text(font: "DejaVu Sans Mono", size: 8.4pt)

#align(center)[
  #v(1.4in)
  #text(size: 32pt, weight: "bold")[OrionLang]
  #v(0.2in)
  #text(size: 17pt)[A small language book]
  #v(0.3in)
  #text(size: 11pt, fill: gray)[A practical introduction to memory, pointers, and data structures]
  #v(2.2in)
  #text(size: 10pt)[Language specification and guided tutorial]
  #v(0.1in)
  #text(size: 9pt, fill: gray)[Project snapshot: experimental interpreter subset]
]
#pagebreak()

#outline(title: "Contents", depth: 2)
#pagebreak()

= About this book

OrionLang is an experimental language for learning data structures and algorithms while seeing memory management directly. This book documents the language implemented in the project today. It distinguishes that working subset from features that are only tokens or ideas, so examples can be run as written.

The runtime currently interprets a parsed syntax tree. It also writes a human-readable instruction listing, but that listing is not executable bytecode. The separate raylib stack visualizer accepts a source path, runs the interpreter to capture execution steps, then lets you replay source lines, active function frames, and local values.

#block(fill: luma(245), inset: 10pt, radius: 4pt)[The language is still evolving. There is no static type checker, module system, input facility, or complete language reference beyond this current subset. Treat this as a snapshot, not a promise of future compatibility.]

= Quick start

Build the interpreter from the project directory with `make`, then run a source file:

```sh
make
./ori_vm examples/hello.ori
```

The interpreter expects a `fn main()` function. Its integer return value becomes the program's exit value. To run a different example, pass its path as the first argument. A readable instruction listing is written under `build/` for each source file.

A first program:

```orion
fn main() -> int {
    print("Hello, OrionLang!");
    return 0;
}
```

`print` writes a string argument as text. A numeric argument is printed as a signed integer followed by a newline.

= Language specification

== Source and comments

Source is UTF-8-compatible text at the file level, but the lexer is byte-oriented and identifiers are ASCII letters or underscore followed by ASCII letters, digits, or underscore. Whitespace separates tokens. A line comment starts with `//` and continues to the end of that line. There are no block comments.

Statements end with a semicolon, except compound constructs and declarations whose bodies are enclosed in braces. Braces group function bodies and control-flow blocks.

== Names and reserved words

Names are case-sensitive. The lexer reserves `struct`, `fn`, `if`, `else`, `while`, `break`, `continue`, `return`, `alloc`, `free`, `null`, `int`, `float`, and `char`.

The interpreter supports signed 64-bit integers and 64-bit floating-point numbers (`float`) in the same 64-bit storage. Character literals currently evaluate to their integer character codes; `char` is accepted as a type name, but character-specific operations are not implemented.

== Types and declarations

The scalar types are `int` and `float`; pointers include `int*`, `float*`, and pointers to declared structs, such as `Node*`. Array allocation also uses an element type, for example `alloc int[count]` or `alloc float[count]`. Float values use 64-bit double-precision representation. Integer values assigned to a float are converted to floating point; floats assigned to integers are truncated toward zero. Mixed integer and float arithmetic promotes the integer operand to floating point. A struct declaration lists named fields:

```orion
struct Node {
    int value;
    Node* next;
}
```

Each field occupies one 64-bit word in the runtime heap, including pointer fields. Field names should be unique throughout a program because field lookup currently uses the name alone. Struct declarations may optionally end with a semicolon.

Local variables are declared with a type, an identifier, an optional initializer, and a semicolon:

```orion
int count = 3;
Node* head = null;
```

The current interpreter does not enforce declared types. The type spelling is primarily descriptive/parser input; values are stored as 64-bit words. Use compatible values yourself. Blocks create nested local scopes, and a block local stops being visible when that block ends.

== Functions

A function has a name, comma-separated parameters, an optional arrow return-type annotation, and a body:

```orion
fn add(int left, int right) -> int {
    return left + right;
}
```

Parameter type annotations are accepted and shown in examples. The parser also accepts a bare parameter name. The interpreter binds arguments by position and checks the argument count, but does not check their types or the annotated return type. Functions can call other functions and can recurse. `main` is the entry point.

A function can return a value with `return expression;`. A bare `return;` returns zero in the current interpreter. Falling off the end of a function also yields zero. There is no `void` keyword with special semantics.

== Statements

- Variable declaration: `int total = 0;`
- Assignment: `total = total + value;`
- Expression statement: `work();`
- Conditional: `if (condition) { ... } else { ... }`
- Loop: `while (condition) { ... }`
- Loop control: `break;` exits the innermost enclosing `while`; `continue;` starts its next condition check. Both are errors outside a loop.
- Return: `return value;`
- Deallocation: `free(pointer);`

Conditions use zero for false and any nonzero value for true. `else` is optional. A `while` condition is reevaluated before every iteration. OrionLang has no `for` statement in the current language parser.

== Expressions and operators

Expressions include integer and decimal float literals (including `.5` and exponent notation such as `1.25e-3`), character and string literals, and `null`; variable names; calls; array indexing; struct field access; parentheses; unary `-`, `+`, `!`; and binary operators below. Float values support `+`, `-`, `*`, `/`, and numeric comparisons. Remainder and bit shifts are integer-only. Strings are supported only as direct arguments to `print`.

#table(
  columns: (1.1fr, 2fr, 2.5fr),
  [Precedence], [Operators], [Meaning],
  [highest], [`*` `/` `%`], [Multiply, divide, remainder],
  [], [`+` `-`], [Add and subtract],
  [], [`<<` `>>`], [Left and signed right shift],
  [], [`<` `<=` `>` `>=`], [Numeric comparisons],
  [], [`==` `!=`], [Equality comparisons],
  [], [`&&`], [Logical AND; short-circuits],
  [lowest], [`||`], [Logical OR; short-circuits],
)

Binary operators associate left-to-right. Unary plus, minus, and logical not are supported. Logical operators return 0 or 1. Shifts use the low six bits of the shift count. Division and remainder by zero produce a runtime error. Assignment is a statement rather than an expression.

The lexer recognizes `&`, but the current expression parser does not implement address-of or pointer dereference. Pointers are manipulated through `null`, allocation, function arguments, array indexing, and struct fields.

== Arrays, pointers, and manual memory

Allocate an integer array with a runtime count:

```orion
int* values = alloc int[4];
values[0] = 12;
print(values[0]);
free(values);
```

Arrays are zero-initialized by the VM's memory arena. Indexing is zero-based, and the runtime checks accesses against allocated heap blocks. The language does not currently retain an array length in the pointer, so an allocation is a raw address and indexing checks allocation bounds rather than the original element count in all cases.

Allocate a struct object with `alloc TypeName`:

```orion
Node* item = alloc Node;
item.value = 7;
item.next = null;
free(item);
```

`null` is the zero pointer. Accessing invalid or freed heap storage, freeing an invalid pointer, or freeing the same block twice results in a runtime error. `free(null)` is harmless. The programmer is responsible for freeing every live allocation exactly once and ensuring no variable is used after its allocation is freed. Assigning a pointer copies its address; it does not copy the pointed-to object.

The runtime has a finite heap and a limit on executed instructions to stop runaway programs. These checks help expose common mistakes but do not replace a static lifetime or ownership checker.

== Built-in output

`print` accepts one or more arguments:

```orion
print("count:");
print(count);
```

String literals can only be passed directly to `print`; strings are not general values and cannot be assigned or returned. Basic escapes include `\n`, `\t`, `\r`, `\\`, and `\"`. Character literals use a single quoted character, such as `'A'`, and print as their integer character code when passed to `print`.

== Current grammar sketch

This compact grammar describes the main shapes accepted by the parser. `type` below means a built-in type or a struct name; pointer stars are accepted in declaration and parameter positions.

```text
program       := (struct-decl | function | statement)* EOF
struct-decl   := "struct" IDENT "{" field* "}" ";"?
field         := type "*"? IDENT ";"
function      := "fn" IDENT "(" parameters? ")" ("->" type "*"?)? block
parameters    := parameter ("," parameter)*
parameter     := (type "*"?)? IDENT
block         := "{" statement* "}"
statement     := declaration | assignment ";" | expression ";" | "break" ";" | "continue" ";"
               | "if" "(" expression ")" block ("else" block)?
               | "while" "(" expression ")" block
               | "return" expression? ";" | "free" "(" expression ")" ";"
declaration   := type "*"? IDENT ("=" expression)? ";"
type          := "int" | "float" | "char" | IDENT
expression    := literals, names, calls, indexing, fields, unary and binary operators
```

This is a guide rather than a complete formal grammar: some parser branches are more permissive than runtime behavior, and static type rules are not yet defined.

= Guided tutorial: Two Sum

We will write a small function that searches an integer array for two entries whose sum is 9. This version returns the two indices packed as `10 * first + second`; for the example indices 0 and 1, the result is 1. Packing keeps the example inside the current language's simple integer return model.

== Allocate and initialize the input

```orion
int* nums = alloc int[4];
nums[0] = 2;
nums[1] = 7;
nums[2] = 11;
nums[3] = 15;
```

The pointer `nums` refers to the first word of a heap allocation. Each assignment writes one array element. The allocation belongs to the caller until it is freed.

== Search with nested loops

```orion
fn twoSum(int* nums, int count, int target) -> int {
    int i = 0;
    while (i < count) {
        int j = i + 1;
        while (j < count) {
            if (nums[i] + nums[j] == target) {
                return i * 10 + j;
            }
            j = j + 1;
        }
        i = i + 1;
    }
    return -1;
}
```

For each index `i`, the inner loop starts at `i + 1`, so it tests every later element without comparing an item to itself or repeating a pair in reverse order. The first matching pair returns immediately. If no pair matches, the function returns `-1`.

== Call the function and release memory

Put the function and this `main` in the same `.ori` file:

```orion
fn main() -> int {
    int* nums = alloc int[4];
    nums[0] = 2;
    nums[1] = 7;
    nums[2] = 11;
    nums[3] = 15;

    int packed = twoSum(nums, 4, 9);
    print("Packed indices (0, 1):");
    print(packed);

    free(nums);
    return 0;
}
```

Run it with `./ori_vm path/to/two_sum.ori`. You should see the label followed by `1`. The `free(nums)` call happens after the search has finished using the array. Returning from `main` does not automatically free heap allocations.

== Complexity and improvements

The nested search performs up to `n * (n - 1) / 2` pair checks, so its time complexity is O(n²). It uses O(1) extra space apart from the input array. To return the indices in a more explicit shape, a later version of OrionLang could add structs by value or a standard container convention; for now, a caller-owned integer buffer is the practical way to return multiple values.

= Tutorial: build and reverse a linked list

A linked list makes pointer relationships visible. Each node stores an integer and an address of the next node:

```orion
struct Node {
    int value;
    Node* next;
}
```

Create three nodes and connect them:

```orion
Node* first = alloc Node;
Node* second = alloc Node;
Node* third = alloc Node;
first.value = 1;
first.next = second;
second.value = 2;
second.next = third;
third.value = 3;
third.next = null;
```

The variables `first`, `second`, and `third` each hold an address. The `next` fields hold copies of those addresses. All three objects are separate allocations.

Reverse the links iteratively:

```orion
fn reverse(Node* head) -> Node* {
    Node* previous = null;
    Node* current = head;
    while (current != null) {
        Node* next = current.next;
        current.next = previous;
        previous = current;
        current = next;
    }
    return previous;
}
```

The temporary `next` preserves the remainder of the list before `current.next` is overwritten. Each iteration moves one node from the unreversed suffix to the reversed prefix. Afterward, the returned head points to `3 -> 2 -> 1 -> null`.

Free every node once:

```orion
Node* reversed = reverse(first);
free(reversed.next.next);
free(reversed.next);
free(reversed);
```

Traverse and retain the next pointer before freeing the current node; otherwise reading its `next` field after `free` is a use-after-free. This explicit cleanup is part of the algorithm, not something OrionLang inserts automatically.

= Debugging checklist

- #strong[Parser error:] Check braces, parentheses, commas, and statement-ending semicolons. `if` and `while` conditions require parentheses.
- #strong[Unknown function:] Confirm the function name matches and is declared in the source file.
- #strong[Wrong argument count:] Compare the call's arguments with the function's parameters.
- #strong[Unexpected value or behavior:] Runtime types are not checked. Verify that pointers, integer values, indices, and return values are used consistently.
- #strong[Use after free or invalid free:] Track every pointer that aliases the allocation. After freeing an object, do not access it or free it again.
- #strong[Infinite loop:] Ensure loop state changes and eventually makes the condition false. The instruction limit will terminate runaway execution.
- #strong[Bytecode listing confusion:] `build/*.bytecode.txt` is a generated view for learning and inspection; the current program still runs through the AST interpreter.

= Reference card

#table(
  columns: (1.5fr, 3.5fr),
  [Task], [Pattern],
  [Entry point], [`fn main() -> int { ... }`],
  [Function], [`fn name(int x) -> int { ... }`],
  [Local], [`int x = 0;`],
  [Struct], [`struct Node { int value; Node* next; }`],
  [Array allocation], [`int* a = alloc int[count];`],
  [Struct allocation], [`Node* n = alloc Node;`],
  [Array element], [`a[index]`],
  [Struct field], [`n.next`],
  [Branch / loop], [`if (condition) { ... } else { ... }` / `while (condition) { ... }`],
  [Null pointer], [`null`],
  [Release allocation], [`free(pointer);`],
  [Output], [`print(value);`],
)

#text(size: 8pt, fill: gray)[End of current language snapshot. Update this book as the implementation evolves.]
