# 01 — From Python to C: the mental shift

If you know Python, you already know programming. C only changes *when* things happen and *who* is responsible for them. This chapter gives you the new mental model. Everything else in the manual is detail.

## 1. Interpreted vs compiled

Python:

```python
# Python runs this line by line, right now.
def add(a, b):
    return a + b

print(add(2, 3))   # works immediately with: python3 prog.py
```

C: you write text, a **compiler** translates the whole program to machine code first, then you run the result:

```c
/* add.c */
#include <stdio.h>

int add(int a, int b) {
    return a + b;
}

int main(void) {
    printf("%d\n", add(2, 3));
    return 0;
}
```

```bash
gcc -Wall -Wextra -o add add.c   # compile: text -> machine code
./add                            # run: prints 5
```

> **Python vs C:** Python checks types while running. C checks types while compiling. A C program that compiles has already passed a first test suite: the type checker.

Consequences you will feel immediately:

1. **Two-step workflow.** Edit → compile → run. Compiler errors are normal; they are the compiler refusing to guess what you meant. Professionals get dozens a day.
2. **No REPL for the full language.** (There is `tcc -run` and debuggers, but day to day you compile.) Prototype algorithms in Python if you like — `tools/gen_puzzles.py` does exactly that — then port the proven logic to C.
3. **Speed and size are explicit.** This repo builds a whole game into exactly 32768 bytes (`make check` asserts it). Python never asks you to think in bytes; C does. `sizeof` (chapter 02) becomes a design tool.

A compiled-language family tree, so C's neighbours make sense:

```text
C (1972, portable assembler) ─┬─ C++ (C + objects/templates)
                              ├─ Objective-C, C# (C + objects/GC, managed runtimes)
                              ├─ Go, Rust, Zig (C's heirs: each fixes one C pain)
                              └─ GBDK/SDCC C (this repo: a strict 8-bit dialect of C)
Python ─── CPython itself is written in C. `list.append` is a C function
           managing raw memory for you. This manual shows what it does.
```

## 2. Static types: every variable declares what it holds

Python:

```python
x = 5        # int today…
x = "hello"  # …string tomorrow. Allowed.
```

C:

```c
int x = 5;
x = 6;        /* fine: still an int */
x = "hello";  /* COMPILE ERROR: a string is not an int */
```

Why C is strict: the compiler must know **how many bytes** `x` occupies and **which machine instructions** work on it. It cannot decide that at runtime. The declaration *is* the allocation.

The types you will see most in this repo:

| C type | Meaning | Python analogy |
|--------|---------|----------------|
| `uint8_t` | unsigned 0–255, exactly 1 byte | a small `int` that the compiler keeps on a leash |
| `int8_t` | signed −128…127, 1 byte | small `int`, can be negative |
| `uint16_t` | unsigned 0–65535, 2 bytes | `int` for bigger counts (level 0–299 needs this) |
| `int` | signed, ≥2 bytes (usually 4 on PC, 2 on SDCC) | plain `int` — avoid for stored data here |
| `char` | one character byte (`'A'`) | one-character `str` (signedness is murky — chapter 03) |
| `char *` / `const char *` | address of text | roughly a `str`, but see chapter 06 |

Repo link: `src/types.h` uses almost nothing but `uint8_t` because cell values (0–9), rows (0–8) and tile indices all fit in one byte. On a machine with 8 KB of RAM, using 4 bytes where 1 suffices is a real waste.

```c
#define GRID_SIZE 9    /* board is 9x9 */
#define CELL_COUNT 81  /* 9*9 cells */
```

> **Rule of thumb:** if a value fits in 0–255, this codebase uses `uint8_t`. If it can reach 300 (level index 0–299), it uses `uint16_t`. Read any function signature and you already know the valid range. Example: `uint8_t puzzle_solution(uint16_t level, uint8_t idx)` tells you levels exceed a byte but cells do not — before reading a single line of body.

## 3. Values, not objects: what a variable *is*

```python
# Python: names point at heap objects with type, refcount, methods.
a = [1, 2]
b = a          # b points at the SAME object; b.append(3) changes a too
```

```c
/* C: a variable IS bytes in a fixed place. Assignment copies the bytes. */
uint8_t a = 5;
uint8_t b = a;   /* b is an independent copy; changing b never touches a */
b = 7;           /* a is still 5 */
```

There are no methods, no hidden headers on values, no reference counting. A `uint8_t` is one byte, period. This is why C is fast and why *you* must build every abstraction (chapter 07's structs, chapter 06's strings-as-arrays).

The one exception — pointers alias memory deliberately (chapter 06):

```c
uint8_t x = 5;
uint8_t *p = &x;  /* p points at x ON PURPOSE; *p = 7 changes x */
```

Aliasing in C is always explicit (`&`, `*`), never accidental like Python's shared-list surprise. When you see `&`, someone wants sharing; everywhere else, copies.

## 4. No garbage collector: memory is yours

Python allocates lists, strings and objects and frees them automatically.

C gives you these places to put data (full details in chapter 10), and **nothing is freed unless you say so**:

- **Local variables** (on the *stack*): created when a function runs, gone when it returns. Like Python locals, but the memory is raw bytes, not objects.
- **`static` / global variables**: live forever in RAM. The game's grid (`static uint8_t cells[81]` in `src/board.c:9`) is exactly this: 81 bytes that exist for the whole session.
- **`const` data**: lives in ROM (read-only). The 300 puzzles (`const Puzzle puzzles[300]`) are baked into the cartridge and can never change.
- **Heap** (`malloc`/`free`): Python's `list.append` equivalent. **This project never uses it** (chapter 10 explains why: on the Game Boy it wastes precious RAM and risks fragmentation; fixed-size arrays are simpler and provable).

Forgetting to free in C leaks; freeing twice corrupts; using after free is an exploitable bug. The repo's answer is architectural: *need no `free` by never `malloc`ing*. Fixed maxima (81 cells, 38 mark bytes, 300 levels) are known at compile time, so every buffer is declared, not allocated.

## 5. No objects, no exceptions, no conveniences

What Python gives you for free, C makes you write (or avoid):

| Python | C equivalent in this repo |
|--------|---------------------------|
| `obj.method()` | `module_verb(subject, ...)` e.g. `board_set(idx, value)` |
| `grid.copy()` | manual loop: `for (i = 0; i < 81; i++) dst[i] = src[i];` |
| `try/except` | return an error code (`0`/`1`) and check it: `if (!save_read(&slot)) return;` |
| `list`, `dict` | fixed arrays: `uint8_t cells[81]`, `uint8_t marks[38]` |
| `in` (`x in row`) | hand loop with early `return 1;` (see `board_conflicts`) |
| `str` methods | raw `char` arrays + manual loops (menus draw font tiles, never `print`) |
| `True`/`False` | `1`/`0` (C has `_Bool`/`bool`, but this codebase uses `uint8_t` for Game Boy reasons) |
| `None` | `NULL` for pointers, `0xFF` as "no cell" sentinel (see `pv_row = 0xFF` in `main.c`) |
| `len(x)` | a separate `#define COUNT` you maintain by hand |

Example — error handling without exceptions (`src/main.c:326`):

```c
static void apply_load(void) {
    if (!save_read(&slot)) {
        return; /* Should not happen: LOAD is only shown when valid. */
    }
    /* ... restore marks and board ... */
}
```

In Python you would raise or catch. In C you return `1`/`0` and the caller decides. Short, explicit, no hidden control flow. The price: **every** caller must check. The repo's discipline is that fallible operations (`save_read`) return a status and call sites test it immediately — never three calls later.

And the methods table — how "object behaviour" looks without objects (`src/board.h` is the "class", `board.c` the "methods", the hidden `cells[]` the "fields"):

```python
# Python fantasy version
board.load(5)
board.set(idx, 7)
if board.conflicts(idx): ...
```

```c
/* C reality: the module name prefixes every "method" */
board_load(5);
board_set(idx, 7);
if (board_conflicts(idx)) { /* ... */ }
```

## 6. Undefined behaviour: the contract you sign

Python defines almost everything (`[1,2][5]` raises `IndexError`). C leaves hundreds of cases **undefined**: the compiler may assume they never happen and optimise accordingly. Violating them is not "an error" — it is a void contract, and symptoms can appear ten thousand lines away.

The five you must never do (each appears as a `Warning` box where relevant later):

1. **Out-of-bounds access** — `cells[81] = 1` (chapter 06). Silent corruption, not an exception.
2. **Use of uninitialised locals** — `uint8_t x; printf("%u", x);` prints garbage (chapter 06 §1).
3. **Signed integer overflow** — `int8_t s = 127; s++;` is undefined (unsigned wrap is defined — one reason the repo prefers unsigned).
4. **Use after scope ends** — `return &local;` (chapter 05 §6). The frame is dead.
5. **Data races / wrong-format `printf`** — `%d` for a string, writing to string literals (chapter 03 §6).

The compiler + `-Wall -Wextra` + `assert` tests exist precisely to catch these *before* hardware does. Treat every warning as a contract review.

## 7. The compiler is your strict friend

Python lets this slide until runtime:

```python
def f(a, b):
    return a + b

f(1)          # TypeError only when this line RUNS
```

C refuses to build:

```c
int f(int a, int b) { return a + b; }

int main(void) {
    f(1);  /* COMPILE ERROR: too few arguments */
    return 0;
}
```

This strictness is why `make clean && make` in this repo must produce **zero warnings**. Warnings are the compiler saying "this is legal but probably not what you meant" (signed/unsigned mix, unused variable, missing return). Treat them as errors. Chapter 02 shows the three failure flavours (error / warning / link error) with reproductions.

> **Try it.** Save this as `strict.c` and compile with warnings on:
>
> ```c
> #include <stdio.h>
> int main(void) {
>     int x = 5;
>     printf("%d\n", x);
>     return 0;
> }
> ```
>
> ```bash
> gcc -Wall -Wextra -o strict strict.c && ./strict
> ```
>
> Now delete `#include <stdio.h>` and recompile. Read the warning: the compiler tells you `printf` is undeclared. Put the line back. Then try calling `printf("%d\n");` with no value and watch `-Wall` catch the format/argument mismatch — that single warning class prevents a whole family of garbage-output bugs.

## 8. How to read C if you read Python

- `{` … `}` is Python's indentation block. `;` ends a statement (like a newline).
- `// comment` and `/* comment */` are `# comment`.
- Function first line is like `def`, but with types on both sides: `uint8_t board_get(uint8_t idx);` reads as "def board_get(idx: u8) -> u8".
- `.h` files are the table of contents (what exists). `.c` files are the chapters (how it works). Chapter 08 covers this fully.
- `a->b` is `a.b` when `a` is a pointer (chapter 07). `*p` is "the thing `p` points at", `&x` is "where `x` lives" (chapter 06).
- `0xFF` is hex for 255 (chapter 07). Addresses and bit patterns are written in hex by convention.

Worked translation — read this Python, then its C twin, line by line:

```python
MARKS = bytearray(38)

def marks_set(bm, level):
    bm[level // 8] |= 1 << (level % 8)

def marks_get(bm, level):
    return (bm[level // 8] >> (level % 8)) & 1
```

```c
void marks_set(uint8_t *bm, uint16_t level) {
    bm[level >> 3] |= (uint8_t)(1u << (level & 7));
}
uint8_t marks_get(const uint8_t *bm, uint16_t level) {
    return (uint8_t)((bm[level >> 3] >> (level & 7)) & 1);
}
```

`//` became `>> 3`, `%` became `& 7`, `bytearray` became `uint8_t *`, and every value gained a width. Same algorithm, explicit bytes. (Full bit-idiom course in chapter 07.)

Next: `02-first-program-toolchain.md` — write, compile and run your first programs, and understand all four build stages.
