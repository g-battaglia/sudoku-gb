# 03 — Types, variables and operators

This chapter covers every type and operator this repo uses. Nothing more — C has extra corners (floats, `long long`, bit-fields) that the game never touches and you can learn later.

## 1. The integer family

C integers differ in **size** (bytes) and **signedness** (can they be negative?).

```c
#include <stdint.h>
#include <stdio.h>

int main(void) {
    uint8_t  a = 255;   /* 1 byte,  0..255 */
    int8_t   b = -5;    /* 1 byte,  -128..127 */
    uint16_t c = 300;   /* 2 bytes, 0..65535 */
    int      d = -1000; /* >= 2 bytes (4 on your Mac and on SDCC-int quirk: see below) */

    printf("%u %d %u %d\n", a, b, c, d);
    return 0;
}
```

> **Python vs C:** Python `int` grows forever. C ints wrap or saturate at fixed limits. `uint8_t x = 255; x = x + 1;` gives `0` (wraps modulo 256). This is defined behaviour for unsigned types — and a bug factory if you forget it.

Why the repo loves `uint8_t`:

- A Sudoku digit is 0–9. A row is 0–8. A cell index is 0–80. All fit in one byte.
- `static uint8_t cells[81]` = exactly 81 bytes. Using `int cells[81]` would cost 162+ bytes on the Game Boy for zero benefit, out of 8192 total WRAM bytes.

Why `uint16_t` exists here: levels are 0–299, which does **not** fit in `uint8_t` (max 255). So every level parameter is `uint16_t level` (`src/puzzles.h:50`, `src/board.c:25`). Read a signature and you know the range — this is a deliberate API convention, not decoration.

SDCC quirk (the Game Boy compiler): plain `int` is 16-bit on SDCC, 32-bit on your Mac's `gcc`. Portable code therefore never assumes `int` width for stored data — it uses `uint8_t`/`uint16_t` from `<stdint.h>`. That is why you see them everywhere here.

## 2. Signed vs unsigned: the #1 warning source

```c
uint8_t u = 5;
int8_t s = -1;

if (s < u) { /* WARNING-prone: s is converted to unsigned before compare! */
}
```

Mixing signed and unsigned in one expression triggers `-Wextra` warnings because C silently converts the signed value to unsigned, turning `-1` into `255`. On the Game Boy this class of bug moves cursors to insane positions.

Repo pattern — cursor steps use `int8_t d` (can be −1), positions use `uint8_t v`, and the conversion is fenced inside one helper (`src/main.c:152`):

```c
/* Wrap v + d into 0..n-1 (menu / select navigation). */
static uint8_t wrap_add(uint8_t v, int8_t d, uint8_t n) {
    return (uint8_t)((v + n + d) % n);
}
```

All signedness danger lives in this 3-line function, audited once. Everywhere else the code passes only unsigned values. **Fence dangerous conversions in tiny helpers** — a habit worth copying.

## 3. `char`, characters and numbers

```c
char letter = 'A';          /* single quotes = one character */
const char *word = "EASY";  /* double quotes = string (see chapter 06) */
uint8_t digit = '5' - '0';  /* '5' is code 53, '0' is 48 -> digit == 5 */
```

Characters *are* small numbers (ASCII codes). `'0'`–`'9'` are consecutive, so `'5' - '0' == 5` is the standard digit trick. The menus in `src/ui.c` convert numbers to tiles with exactly this kind of arithmetic (`ASCII c = tile c - 32` for the GBDK font).

## 4. Variables, `const`, `#define`

Three ways to name things, three different meanings:

```c
#define GRID_SIZE 9          /* 1. textual replacement, no memory, no type */
const uint8_t BOX = 3;       /* 2. read-only variable with a type */
uint8_t cursor_row = 0;      /* 3. normal variable, read/write */

cursor_row = 5;  /* fine */
/* BOX = 4; */   /* COMPILE ERROR: const */
/* GRID_SIZE = 4; */ /* COMPILE ERROR and conceptually wrong: it was never a variable */
```

| Form | Memory? | Type-checked? | Where used in repo |
|------|---------|---------------|--------------------|
| `#define GRID_SIZE 9` | No | No | `src/types.h` — dimensions, screen sizes |
| `const Puzzle puzzles[300]` | Yes, in ROM | Yes | `src/puzzles_gen.c` — level data, physically unchangeable |
| `const char *s` (parameter) | Pointer in RAM/regs | Yes | "I promise not to modify your text" (`difficulty_name`, `marks_get`) |
| `static uint8_t cells[81]` | Yes, in RAM | Yes | `src/board.c` — the live grid |

> **Python vs C:** Python `GRID = 9` is a runtime variable anyone can reassign. C `#define GRID_SIZE 9` disappears before compilation — there is nothing to reassign, nothing in memory, and no typo protection (a misspelled macro is a different macro). Prefer `const`/`enum` when you want type checking; use `#define` for dimensions and conditional compilation (chapter 08).

## 5. Operators you need (with repo sightings)

Arithmetic: `+ - * / %` ( `%` = remainder; `level % DIFF_LEVELS`, `idx % GRID_SIZE` are everywhere).

```c
uint8_t row = idx / GRID_SIZE;  /* integer division truncates: 80/9 == 8 */
uint8_t col = idx % GRID_SIZE;  /* remainder: 80%9 == 8 */
```

Comparison: `== != < <= > >=`. Result is `1` (true) or `0` (false) — an `int`, not a `bool`.

Logic: `&& || !`. Used for guards: `if (!board_is_locked(idx))`, `if (a && b)`.

Bitwise (chapter 07 goes deep): `& | ^ ~ << >>`. Used for bitmaps and nibbles:

```c
bm[level >> 3] |= (uint8_t)(1u << (level & 7));  /* marks_set: set bit `level` */
```

Assignment shorthands: `+= -= *= /= %= &= |= ++ --`. `error_count++` in `board_add_mistake()` is `error_count = error_count + 1`.

Ternary: `cond ? a : b` (Python's `a if cond else b`):

```c
origin[i] = (g != 0) ? ORIGIN_GIVEN : ORIGIN_PLAYER;  /* src/board.c:32 */
```

`sizeof`: bytes occupied. `sizeof(cells)` is 81. Used for budgets and `for (i = 0; i < MARKS_BYTES; i++)` loops.

Casts: `(uint8_t)(x)` forces a conversion and silences the warning — but only write one when you *mean* the truncation (as in `wrap_add` above). A cast is you telling the compiler "I checked, this fits".

## 6. `printf` formats (the only ones this project needs)

```c
printf("%s %d %c %u\n", "EASY", -3, 'A', (unsigned)200);
```

| Specifier | For | Repo note |
|-----------|-----|-----------|
| `%d` | signed `int` | mistake counts, debug prints |
| `%u` | unsigned | `uint8_t`/`uint16_t` promoted to `int` when printed |
| `%c` | single char | rarely; menus use tile indices, not stdio |
| `%s` | `const char *` string | `difficulty_name(diff)` in host tests |
| `%x` | hex | addresses, header bytes (`make check` prints `hex(d[0x147])`) |

> **No float in this codebase** (`PLAN.md` risk table). `%f` would pull a large floating-point library into a 32 KB ROM. Menus format integers only (`draw_num*` in `ui.c`). On constrained targets, every library function costs bytes — another Python-vs-C shock.

## 7. Overflow, saturation, and the mistake counter

Unsigned overflow wraps (`255 + 1 == 0` for `uint8_t`). Sometimes you want that (tile math), usually you don't (counters). The repo saturates the mistake counter instead of wrapping (`src/board.c:121`):

```c
void board_add_mistake(void) {
    if (error_count < 255) {
        error_count++;
    }
}
```

Wrapping to 0 after 255 mistakes would erase the player's history. The `if` costs 2 lines and removes a whole bug class. `tests/test_host.c:101` hammers this: 3 mistakes, then 252 more, asserting the count sticks at 255 and never wraps.

## Exercises

1. What happens for `uint8_t x = 250; x = x + 10;`? Compute the wrapped value by hand, then verify with a 5-line program.
2. Why is `puzzle_solution(uint16_t level, uint8_t idx)` split across two widths? What breaks if `level` were `uint8_t`? (Hint: how many levels exist?)
3. Find all `#define` lines in `src/types.h`, `src/puzzles.h`, `src/ui.h`. For each, say whether it *could* be a `const` instead and why the author likely chose `#define` (array sizes? no memory?).

Next: `04-control-flow.md` — decisions, loops, and the state machine that runs the whole game.
