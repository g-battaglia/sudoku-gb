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

1. **Two-step workflow.** Edit → compile → run. Compiler errors are normal; they are the compiler refusing to guess what you meant.
2. **No REPL for the full language.** (There is `tcc -run` and debuggers, but day to day you compile.)
3. **Speed and size are explicit.** This repo builds a whole game into exactly 32768 bytes (`make check` asserts it). Python never asks you to think in bytes; C does.

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

Why C is strict: the compiler must know **how many bytes** `x` occupies and **which machine instructions** work on it. It cannot decide that at runtime.

The types you will see most in this repo:

| C type | Meaning | Python analogy |
|--------|---------|----------------|
| `uint8_t` | unsigned 0–255, exactly 1 byte | a small `int` that the compiler keeps on a leash |
| `int8_t` | signed −128…127, 1 byte | small `int`, can be negative |
| `uint16_t` | unsigned 0–65535, 2 bytes | `int` for bigger counts (level 0–299 needs this) |
| `int` | signed, ≥2 bytes (usually 4 on PC) | plain `int` |
| `char` | one character byte (`'A'`) | one-character `str` |
| `char *` / `const char *` | address of text | roughly a `str`, but see chapter 06 |

Repo link: `src/types.h` uses almost nothing but `uint8_t` because cell values (0–9), rows (0–8) and tile indices all fit in one byte. On a machine with 8 KB of RAM, using 4 bytes where 1 suffices is a real waste.

```c
#define GRID_SIZE 9    /* board is 9x9 */
#define CELL_COUNT 81  /* 9*9 cells */
```

> **Rule of thumb:** if a value fits in 0–255, this codebase uses `uint8_t`. If it can reach 300 (level index 0–299), it uses `uint16_t`. Read any function signature and you already know the valid range.

## 3. No garbage collector: memory is yours

Python allocates lists, strings and objects and frees them automatically.

C gives you three places to put data (full details in chapter 10), and **nothing is freed unless you say so**:

- **Local variables** (on the *stack*): created when a function runs, gone when it returns. Like Python locals, but the memory is raw bytes, not objects.
- **`static` / global variables**: live forever in RAM. The game's grid (`static uint8_t cells[81]` in `src/board.c:9`) is exactly this: 81 bytes that exist for the whole session.
- **`const` data**: lives in ROM (read-only). The 300 puzzles (`const Puzzle puzzles[300]`) are baked into the cartridge and can never change.

There is a fourth option, heap allocation with `malloc`/`free`, which is Python's `list.append` equivalent. **This project never uses it** (chapter 10 explains why: on the Game Boy it wastes precious RAM and risks fragmentation; fixed-size arrays are simpler and provable).

## 4. No objects, no exceptions, no conveniences

What Python gives you for free, C makes you write (or avoid):

| Python | C equivalent in this repo |
|--------|---------------------------|
| `obj.method()` | `module_verb(subject, ...)` e.g. `board_set(idx, value)` |
| `try/except` | return an error code (`0`/`1`) and check it: `if (!save_read(&slot)) return;` |
| `list`, `dict` | fixed arrays: `uint8_t cells[81]`, `uint8_t marks[38]` |
| `str` methods | raw `char` arrays + manual loops (menus draw font tiles, never `print`) |
| `True`/`False` | `1`/`0` (C has `_Bool`/`bool`, but this codebase uses `uint8_t` for Game Boy reasons) |
| `None` | `NULL` for pointers, `0xFF` as "no cell" sentinel (see `pv_row = 0xFF` in `main.c`) |

Example — error handling without exceptions (`src/main.c:326`):

```c
static void apply_load(void) {
    if (!save_read(&slot)) {
        return; /* Should not happen: LOAD is only shown when valid. */
    }
    /* ... restore marks and board ... */
}
```

In Python you would raise or catch. In C you return `1`/`0` and the caller decides. Short, explicit, no hidden control flow.

## 5. The compiler is your strict friend

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

This strictness is why `make clean && make` in this repo must produce **zero warnings**. Warnings are the compiler saying "this is legal but probably not what you meant" (signed/unsigned mix, unused variable, missing return). Treat them as errors.

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
> Now delete `#include <stdio.h>` and recompile. Read the warning: the compiler tells you `printf` is undeclared. Put the line back.

## 6. How to read C if you read Python

- `{` … `}` is Python's indentation block. `;` ends a statement (like a newline).
- `// comment` and `/* comment */` are `# comment`.
- Function first line is like `def`, but with types on both sides: `uint8_t board_get(uint8_t idx);` reads as "def board_get(idx: u8) -> u8".
- `.h` files are the table of contents (what exists). `.c` files are the chapters (how it works). Chapter 08 covers this fully.

## Exercises

1. Explain in one sentence why `uint16_t level` is used for the level index (0–299) while `uint8_t idx` suffices for a cell index (0–80).
2. Find `static uint8_t cells[CELL_COUNT];` in `src/board.c`. Why is the grid `static` and not a local variable inside each function? (Hint: what would happen to your game on every function return?)
3. Find one function in `src/board.h` returning `uint8_t` that Python would write as returning `bool`. Why do you think the codebase uses `uint8_t` instead?

Next: `02-first-program-toolchain.md` — write, compile and run your first programs, and understand all four build stages.
