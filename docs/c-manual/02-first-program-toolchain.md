# 02 — Your first program and the toolchain

Goal: compile and run tiny programs on your PC, understand what `gcc` does in 4 stages, meet the three failure flavours, and map each stage onto this repo's `make test-host` and Game Boy build.

## 1. The smallest program

```c
/* hello.c */
#include <stdio.h>

int main(void) {
    printf("hello\n");
    return 0;
}
```

```bash
gcc -Wall -Wextra -o hello hello.c
./hello   # prints: hello
```

Line by line (Python brain translation):

- `#include <stdio.h>` — like `import sys` for printing. It pastes in the *declaration* of `printf` so the compiler knows it exists. The *definition* (the real code) is added later by the linker.
- `int main(void)` — like `if __name__ == "__main__":`. Program entry point. Returns an `int`: `0` means success (this is the exit code you see with `echo $?`).
- `{` … `}` — the block. `printf("hello\n");` — print plus newline. The `;` is mandatory: the compiler does not care about newlines, only semicolons and braces.
- `return 0;` — "everything fine". `main` is the one function whose return value escapes to the OS.

> **Try it.** Change `"hello\n"` to `"hello"` (no `\n`) and recompile. Notice the shell prompt lands on the same line. `\n` is one newline character, like Python's `"\n"`. Then try forgetting the `;` — read the `expected ';'` error and notice it points at the *next* line: the compiler only realises something is missing when the following token makes no sense. Error positions are "first confusion", not "your mistake".

Why `int main(void)` and not `void main()`? The OS expects an exit status (`echo $?` after running). `void main()` compiles on some toolchains but is non-standard; hosted C (your PC) requires `int main`. On the Game Boy `main` never returns (`while (1)` forever), yet the signature stays `void main(void)` per GBDK convention — the entry contract differs because there is no OS.

## 2. The 4 stages: preprocess → compile → assemble → link

When you run `gcc -o hello hello.c`, four tools run in sequence. You can watch each one:

```bash
gcc -E hello.c -o hello.i   # 1. PREPROCESS: expand #include, #define -> plain C
gcc -S hello.c -o hello.s   # 2. COMPILE: C -> assembly text (human-readable CPU ops)
gcc -c hello.c -o hello.o   # 3. ASSEMBLE: assembly -> object file (machine code, not runnable yet)
gcc hello.o -o hello        # 4. LINK: objects + C library -> runnable program
```

What each stage does:

1. **Preprocess.** Copy-pastes `#include` files, replaces `#define` names with values. `#define GRID_SIZE 9` means "before compiling, replace every `GRID_SIZE` with `9`". No memory, no logic — pure text substitution (chapter 08).
2. **Compile.** Translates C into assembly for your CPU (ARM on Apple Silicon, LR35902 for the Game Boy — different target, same idea). Try `gcc -S hello.c` and open `hello.s`: you will see your `printf("hello\n")` became an address load plus a `bl _printf` call. You never need to write assembly, but recognising `bl` (branch-and-link = function call) demystifies "what the machine really does".
3. **Assemble.** Turns assembly into an *object file* (`.o`): machine code plus a list of "promises I keep" (functions I define) and "promises I need" (functions I call but don't define, like `printf`). Inspect with `nm hello.o`: `U _printf` means "undefined here, linker must provide"; `T _main` means "defined here, in the text (code) section".
4. **Link.** Stitches all `.o` files plus libraries (the C standard library containing `printf`) into one runnable file. If a needed function has no definition anywhere, linking fails with `undefined reference`.

> **Repo link:** `make test-host` runs:
>
> ```bash
> gcc -Wall -Wextra -Isrc -o /tmp/sudoku_test tests/test_host.c src/board.c src/puzzles.c src/puzzles_gen.c && /tmp/sudoku_test
> ```
>
> Four `.c` files are compiled and linked **together** into one test program. `test_host.c` calls `board_load()` without knowing how it works — the linker connects the call to the definition in `board.c`. If you remove `src/board.c` from that command you get `undefined reference to 'board_load'`. Try it (it fails safely, nothing is overwritten).

## 3. Compiler errors vs warnings vs linker errors

Three failure flavours, three meanings. Learn to classify before reading:

```c
/* err1.c — COMPILE ERROR (syntax/type): compilation stops, no output */
int main(void) {
    int x = "hello";  /* error: incompatible types */
    return 0;
}
```

```c
/* err2.c — WARNING (legal but suspicious): output IS produced, fix anyway */
#include <stdio.h>
int main(void) {
    int x = 5;   /* warning: unused variable 'x' */
    printf("hi\n");
    return 0;
}
```

```bash
gcc -Wall -Wextra -o err2 err2.c   # compiles, but warns. Fix warnings anyway.
```

```c
/* err3a.c — LINK ERROR (promise without a body): objects fine, stitching fails */
/* declares but never defines get_value */
int get_value(void);
int main(void) { return get_value(); }
```

```bash
gcc -Wall -Wextra -o err3 err3a.c
# undefined reference to `get_value'  <- linker, not compiler
```

> **Rule used by this repo:** `make clean && make` must print **zero warnings**. On the Game Boy a warning (e.g. truncating `uint16_t` to `uint8_t`) can corrupt a level index silently. `-Wall -Wextra` turns the compiler's suspicions on; your job is to clear them. The full message Rosetta stone is in `13` §5.

The five warnings you will meet first (memorise the fixes):

| Warning text | Meaning | Fix |
|--------------|---------|-----|
| `unused variable 'x'` | declared, never read | delete it, or `(void)x;` if intentionally reserved |
| `implicit declaration of 'f'` | missing `#include`/prototype | include the header |
| `format '%d' expects int, argument is char *` | `printf` mismatch | match specifier to argument |
| `comparison between signed and unsigned` | `-1` would become huge | fence conversion in a helper (chapter 03 §2) |
| `control reaches end of non-void function` | some path lacks `return` | add the missing `return` |

## 4. Optimisation and debug info: `-O` and `-g`

Two flags change what the compiler produces without changing what your program means:

```bash
gcc -Wall -Wextra -O0 -g -o prog_debug prog.c   # no optimisation + debug info: best for debugging
gcc -Wall -Wextra -O2 -o prog_fast prog.c       # optimised: best for speed/size
```

- `-O0` compiles fast and keeps the machine code line-shaped like your source (debugger steps predictably). `-O2` reorders, inlines and deletes — faster ROM, confusing debugger.
- `-g` embeds file/line/variable names so `lldb ./prog` (macOS) or `gdb ./prog` (Linux) can show source, breakpoints and `print x`. Without `-g` you debug raw addresses.

GBDK builds optimise for size (32 KB ceiling) — one more reason warnings matter: at `-O2`-style optimisation, undefined behaviour (chapter 01 §6) is exploited *more* aggressively. Debug on PC with `-O0 -g`, ship on ROM optimised.

Five-minute debugger session (do this once; it pays forever):

```bash
gcc -Wall -Wextra -g -o guess guess.c
lldb ./guess            # or: gdb ./guess
(lldb) breakpoint set --name main
(lldb) run
(lldb) next             # step one line
(lldb) print guess      # inspect a variable
(lldb) continue         # run on
(lldb) quit
```

On the Game Boy there is no debugger — emulators (mGBA) offer breakpoints, but day-to-day GB debugging is `assert` on PC plus frame-by-frame PyBoy checks. Which leads to…

## 5. `assert`: executable assumptions

```c
#include <assert.h>
#include <stdint.h>

static uint8_t wrap_add(uint8_t v, int8_t d, uint8_t n) {
    return (uint8_t)((v + n + d) % n);
}

int main(void) {
    assert(wrap_add(0, -1, 10) == 9);   /* crash loudly if false */
    assert(wrap_add(9, 1, 10) == 0);
    return 0;
}
```

`assert(cond)` does nothing when `cond` is true and aborts with file+line when false. `tests/test_host.c` is 270 lines of exactly this: puzzle validity, conflict rules, mistake saturation, hint locking, bitmap boundaries, save round-trip. **Tests are the executable half of this manual** — every rule stated in prose is asserted in code. If you change `board.c`, `make test-host` tells you within a second whether the rules still hold. (On ROM builds `NDEBUG` typically disables `assert` so checks cost zero bytes; on PC they always run.)

## 6. A second program: functions + types + `printf` formats

```c
/* types_demo.c */
#include <stdio.h>
#include <stdint.h>

int main(void) {
    uint8_t cell = 5;      /* unsigned 0-255, one byte */
    uint16_t level = 299;  /* unsigned 0-65535, two bytes */
    int mistakes = 3;

    printf("cell %u level %u mistakes %d\n", cell, level, mistakes);
    printf("cell size: %lu byte(s)\n", (unsigned long)sizeof(cell));
    printf("int size here: %lu byte(s)\n", (unsigned long)sizeof(int));
    return 0;
}
```

```bash
gcc -Wall -Wextra -o types_demo types_demo.c && ./types_demo
```

Notes:

- `%u` prints unsigned, `%d` signed, `%s` strings, `%c` single chars, `%x` hex. Using `%d` for a string prints garbage — the compiler with `-Wall` often catches the mismatch (see warning table above).
- `sizeof(x)` is "how many bytes does this occupy". `sizeof(uint8_t)` is always 1. This operator is how C programmers reason about ROM/RAM budgets (chapter 10). Note `sizeof(int)` prints 4 on your Mac but is 2 under SDCC — never hardcode it.
- `<stdint.h>` is where `uint8_t`/`uint16_t` come from. Every header in this repo includes it via `types.h`.
- Small values promote: `uint8_t` arguments to `printf` are promoted to `int`, which is why `%u` works for them. Promotion rules bite in comparisons (chapter 03 §2) but are harmless in `printf`.

## 7. A third program: input, loop, and exit codes

```c
/* guess.c — guess a digit 1-9 */
#include <stdio.h>

int main(void) {
    int secret = 7, guess = 0;

    printf("guess 1-9: ");
    if (scanf("%d", &guess) != 1) {
        printf("not a number\n");
        return 1;   /* non-zero = failure */
    }
    if (guess == secret) {
        printf("correct!\n");
        return 0;
    }
    printf("wrong, it was %d\n", secret);
    return 2;
}
```

New things: `scanf("%d", &guess)` reads a number (`&` = "address of", chapter 06 — `scanf` needs to know *where* to store the answer; forgetting `&` is the #1 `scanf` crash). Always check its return (items successfully read): on `abc` input it returns 0 and leaves `guess` untouched. Return codes `0/1/2` are visible with `echo $?` after running. `main` returning non-zero is how `make` knows a test failed: `assert(...)` aborts non-zero on failure, and `make` stops at the first failing command.

> **Repo link:** `tests/test_host.c:259` ends with `printf("ALL HOST TESTS PASSED\n"); return 0;`. `make test-host` chains compile and run with `&&`: the tests only run if compilation succeeded, and `make` reports failure if the program returns non-zero. That `&&` is load-bearing — replace it with `;` and failures would be silently ignored.

Loop variant — add replay without rewriting (the `while` you will use everywhere in chapter 04):

```c
int guess = 0;
while (guess != secret) {
    printf("guess 1-9: ");
    if (scanf("%d", &guess) != 1) { printf("not a number\n"); return 1; }
    if (guess != secret) printf("try again\n");
}
printf("correct!\n");
```

## 8. Mapping to the Game Boy build

Your PC uses `gcc` (target: your Mac). The Game Boy build uses `lcc` (GBDK's compiler driver, target: Game Boy CPU):

```make
LCCFLAGS = -msm83:gb -Wm-yn"SUDOKU" -Wl-yt0x03 -Wl-ya1
$(LCC) $(LCCFLAGS) -o build/sudoku.gb $(CSOURCES)
```

Same 4 stages, different backend and extra linker flags that shape the cartridge header (title, mapper type MBC1+RAM+BATTERY, SRAM size). Chapter 09 dissects this line flag by flag. The mental model does not change: many `.c` → many `.o` → one output. Only the CPU and the output format (ROM instead of Mac executable) differ. And `make check` plays the role `assert` plays on PC: it verifies the output artefact (size, logo, cart bytes) instead of runtime behaviour.

Next: `03-types-variables-operators.md` — the full type vocabulary of the codebase.
