# 02 — Your first program and the toolchain

Goal: compile and run three tiny programs on your PC, understand what `gcc` does in 4 stages, and map each stage onto this repo's `make test-host` and Game Boy build.

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
- `{` … `}` — the block. `printf("hello\n");` — print plus newline. The `;` is mandatory.
- `return 0;` — "everything fine".

> **Try it.** Change `"hello\n"` to `"hello"` (no `\n`) and recompile. Notice the shell prompt lands on the same line. `\n` is one newline character, like Python's `"\n"`.

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
2. **Compile.** Translates C into assembly for your CPU (ARM on Apple Silicon, LR35902 for the Game Boy — different target, same idea).
3. **Assemble.** Turns assembly into an *object file* (`.o`): machine code plus a list of "promises I keep" (functions I define) and "promises I need" (functions I call but don't define, like `printf`).
4. **Link.** Stitches all `.o` files plus libraries (the C standard library containing `printf`) into one runnable file. If a needed function has no definition anywhere, linking fails with `undefined reference`.

> **Repo link:** `make test-host` runs:
>
> ```bash
> gcc -Wall -Wextra -Isrc -o /tmp/sudoku_test tests/test_host.c src/board.c src/puzzles.c src/puzzles_gen.c && /tmp/sudoku_test
> ```
>
> Four `.c` files are compiled and linked **together** into one test program. `test_host.c` calls `board_load()` without knowing how it works — the linker connects the call to the definition in `board.c`. If you remove `src/board.c` from that command you get `undefined reference to 'board_load'`. Try it (it fails safely, nothing is overwritten).

## 3. Compiler errors vs warnings vs linker errors

Three failure flavours, three meanings:

```c
/* err1.c — COMPILE ERROR (syntax/type) */
int main(void) {
    int x = "hello";  /* error: incompatible types */
    return 0;
}
```

```c
/* err2.c — WARNING (legal but suspicious) */
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
/* err3a.c — LINK ERROR (promise without a body) */
/* declares but never defines get_value */
int get_value(void);
int main(void) { return get_value(); }
```

```bash
gcc -Wall -Wextra -o err3 err3a.c
# undefined reference to `get_value'  <- linker, not compiler
```

> **Rule used by this repo:** `make clean && make` must print **zero warnings**. On the Game Boy a warning (e.g. truncating `uint16_t` to `uint8_t`) can corrupt a level index silently. `-Wall -Wextra` turns the compiler's suspicions on; your job is to clear them.

## 4. A second program: functions + types + `printf` formats

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
    return 0;
}
```

```bash
gcc -Wall -Wextra -o types_demo types_demo.c && ./types_demo
```

Notes:

- `%u` prints unsigned, `%d` signed, `%s` strings, `%c` single chars. Using `%d` for a string prints garbage — the compiler with `-Wall` often catches the mismatch.
- `sizeof(x)` is "how many bytes does this occupy". `sizeof(uint8_t)` is always 1. This operator is how C programmers reason about ROM/RAM budgets (chapter 10).
- `<stdint.h>` is where `uint8_t`/`uint16_t` come from. Every header in this repo includes it via `types.h`.

## 5. A third program: input, loop, and exit codes

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

New things: `scanf("%d", &guess)` reads a number (`&` = "address of", chapter 06 — `scanf` needs to know *where* to store the answer). Return codes `0/1/2` are visible with `echo $?` after running. `main` returning non-zero is how `make` knows a test failed: `tests/test_host.c` uses `assert(...)`, which aborts with non-zero status on failure.

> **Repo link:** `tests/test_host.c:259` ends with `printf("ALL HOST TESTS PASSED\n"); return 0;`. `make test-host` chains compile and run with `&&`: the tests only run if compilation succeeded, and `make` reports failure if the program returns non-zero. That `&&` is load-bearing.

## 6. Mapping to the Game Boy build

Your PC uses `gcc` (target: your Mac). The Game Boy build uses `lcc` (GBDK's compiler driver, target: Game Boy CPU):

```make
LCCFLAGS = -msm83:gb -Wm-yn"SUDOKU" -Wl-yt0x03 -Wl-ya1
$(LCC) $(LCCFLAGS) -o build/sudoku.gb $(CSOURCES)
```

Same 4 stages, different backend and extra linker flags that shape the cartridge header (title, mapper type MBC1+RAM+BATTERY, SRAM size). Chapter 09 dissects this line flag by flag. The mental model does not change: many `.c` → many `.o` → one output. Only the CPU and the output format (ROM instead of Mac executable) differ.

## Exercises

1. Compile `hello.c` with `gcc -E` and open `hello.i`. Find your `printf` line buried under hundreds of lines from `stdio.h`. That is what the compiler really sees.
2. Reproduce the linker error on purpose: compile `tests/test_host.c` **without** `src/board.c` and read the `undefined reference` lines. Then add the file back and watch it link.
3. Write a program that prints `sizeof(uint8_t)`, `sizeof(uint16_t)`, `sizeof(int)` on your machine. Why does this repo prefer `uint8_t` for grid data? (Answer in bytes × 81 cells.)

Next: `03-types-variables-operators.md` — the full type vocabulary of the codebase.
