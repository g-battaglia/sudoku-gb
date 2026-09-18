# 03 — Types, variables and operators

This chapter covers every type and operator this repo uses: integers in all widths, characters, `const` vs `#define`, the full operator set with precedence, `printf`/`scanf` formats, and how overflow is handled. C has extra corners (floats, `long long`, bit-fields) that the game never touches — they are mentioned only so you recognise them elsewhere.

## 1. The integer family

C integers differ in **size** (bytes) and **signedness** (can they be negative?).

```c
#include <stdint.h>
#include <stdio.h>

int main(void) {
    uint8_t  a = 255;   /* 1 byte,  0..255 */
    int8_t   b = -5;    /* 1 byte,  -128..127 */
    uint16_t c = 300;   /* 2 bytes, 0..65535 */
    int      d = -1000; /* >= 2 bytes: 4 on your Mac, 2 on SDCC (see below) */

    printf("%u %d %u %d\n", a, b, c, d);
    return 0;
}
```

> **Python vs C:** Python `int` grows forever. C ints wrap or saturate at fixed limits. `uint8_t x = 255; x = x + 1;` gives `0` (wraps modulo 256). This is defined behaviour for unsigned types — and a bug factory if you forget it.

Why the repo loves `uint8_t`:

- A Sudoku digit is 0–9. A row is 0–8. A cell index is 0–80. All fit in one byte.
- `static uint8_t cells[81]` = exactly 81 bytes. Using `int cells[81]` would cost 162+ bytes on the Game Boy for zero benefit, out of 8192 total WRAM bytes.

Why `uint16_t` exists here: levels are 0–299, which does **not** fit in `uint8_t` (max 255). So every level parameter is `uint16_t level` (`src/puzzles.h:50`, `src/board.c:32`). Read a signature and you know the range — this is a deliberate API convention, not decoration. The one place a `uint8_t` level would silently break is level 256+: it would wrap to 0 and load the wrong puzzle with no error message.

SDCC quirk (the Game Boy compiler): plain `int` is 16-bit on SDCC, 32-bit on your Mac's `gcc`. Portable code therefore never assumes `int` width for stored data — it uses `uint8_t`/`uint16_t` from `<stdint.h>`. That is why you see them everywhere here. `int` still appears for loop-free arithmetic, return codes, and anything the standard library dictates (`printf` returns `int`, `main` returns `int`).

The full width ladder (recognise, rarely need beyond the first three rows here):

| Type | Size | Range | Used in repo for |
|------|------|-------|------------------|
| `uint8_t` | 1 byte | 0…255 | digits, rows, indices, origins, tiles |
| `int8_t` | 1 byte | −128…127 | cursor steps (`-1`), deltas |
| `uint16_t` | 2 bytes | 0…65535 | level 0…299, checksum loop offsets |
| `int` | ≥2 bytes | machine-dependent | `main` return, `printf` return, throwaway arithmetic |
| `uint32_t` / `int32_t` | 4 bytes | billions | absent here (overkill on 8-bit CPU) |
| `char` | 1 byte | murky (see §3) | characters only, never arithmetic |

## 2. Signed vs unsigned: the #1 warning source

```c
uint8_t u = 5;
int8_t s = -1;

if (s < u) { /* DANGER: s is converted to unsigned before compare! */
}
```

Mixing signed and unsigned in one expression triggers `-Wextra` warnings because of **integer promotion + usual arithmetic conversions**: small integers are first promoted to `int`, and if one side is unsigned `int`-sized or larger, the signed side is converted to unsigned — turning `-1` into `255` (or 65535). The comparison then answers backwards. On the Game Boy this class of bug moves cursors to insane positions with no crash to alert you.

Repo pattern — cursor steps use `int8_t d` (can be −1), positions use `uint8_t v`, and the conversion is fenced inside one helper (`src/main.c:167`):

```c
/* Wrap v + d into 0..n-1 (menu / select navigation). */
static uint8_t wrap_add(uint8_t v, int8_t d, uint8_t n) {
    return (uint8_t)((v + n + d) % n);
}
```

All signedness danger lives in this 3-line function, audited once: `v + n + d` promotes to `int` (all values small and non-negative after adding `n`), `% n` is well-defined, and the single explicit `(uint8_t)` cast documents "yes, this fits". Everywhere else the code passes only unsigned values. **Fence dangerous conversions in tiny helpers** — a habit worth copying into every C project.

Two more promotion traps to recognise:

```c
uint8_t a = 200, b = 100;
uint8_t c = (uint8_t)(a + b);   /* a+b promotes to int (300), then truncates to 44 */
if (c == a + b) { /* FALSE: left is 44, right promotes back to int 300 */ }

uint8_t i;
for (i = 81; i >= 0; i--) { /* INFINITE LOOP: i is never < 0, wraps 0 -> 255 */ }
```

The `for` bug is a classic: counting *down* to zero with an unsigned counter never terminates. Count down with a signed type, or count up, or compare against `> 0` with a pre-decrement. The repo's loops all count up with `< COUNT` — now you know one reason why.

## 3. `char`, characters and numbers

```c
char letter = 'A';          /* single quotes = one character */
const char *word = "EASY";  /* double quotes = string (see chapter 06) */
uint8_t digit = '5' - '0';  /* '5' is code 53, '0' is 48 -> digit == 5 */
```

Characters *are* small numbers (ASCII codes). `'0'`–`'9'` are consecutive, so `'5' - '0' == 5` is the standard digit trick, and `'A' + 32 == 'a'` folds case. The menus in `src/ui.c` convert numbers to tiles with exactly this kind of arithmetic (`ASCII c = tile c - 32` for the GBDK font: tile number = character code minus 32).

> **Warning:** plain `char` may be signed or unsigned — the compiler chooses. Never use `char` for arithmetic or array indices; use `uint8_t`/`int8_t` explicitly. Reserve `char` for text (`char buf[…]`, `const char *`). This codebase follows that rule without exception, which is why `char` appears only in string contexts.

Escape sequences (one character each, starting with backslash): `'\n'` newline, `'\t'` tab, `'\0'` zero terminator (chapter 06), `'\\'` backslash, `'\''` quote. `"A"` (string, 2 bytes with terminator) and `'A'` (character, 1 byte) are different types — mixing them is a compile error the message states confusingly ("incompatible pointer/integer"); now you can decode it.

## 4. Variables, initialisation, `const`, `#define`

Three ways to name things, three different meanings:

```c
#define GRID_SIZE 9          /* 1. textual replacement, no memory, no type */
const uint8_t BOX = 3;       /* 2. read-only variable with a type */
uint8_t pos = 0;             /* 3. normal variable, read/write */

pos = 5;  /* fine */
/* BOX = 4; */   /* COMPILE ERROR: const */
/* GRID_SIZE = 4; */ /* COMPILE ERROR and conceptually wrong: it was never a variable */
```

| Form | Memory? | Type-checked? | Where used in repo |
|------|---------|---------------|--------------------|
| `#define GRID_SIZE 9` | No | No | `src/types.h` — dimensions, screen sizes |
| `const Puzzle puzzles[300]` | Yes, in ROM | Yes | `src/puzzles_gen.c` — level data, physically unchangeable |
| `const char *s` (parameter) | Pointer in RAM/regs | Yes | "I promise not to modify your text" (`difficulty_name`, `marks_get`) |
| `static uint8_t cells[81]` | Yes, in RAM | Yes | `src/board.c` — the live grid |

> **Python vs C:** Python `GRID = 9` is a runtime variable anyone can reassign. C `#define GRID_SIZE 9` disappears before compilation — there is nothing to reassign, nothing in memory, and no typo protection (a misspelled macro is a different macro). Prefer `const`/`enum` when you want type checking; use `#define` for array sizes and conditional compilation (chapter 08).

Initialisation discipline — always initialise locals at declaration:

```c
uint8_t i = 0;               /* good: known value */
uint8_t row = idx / GRID_SIZE, col = idx % GRID_SIZE;  /* good: derived immediately */
uint8_t x;                   /* BAD: garbage until assigned; -Wmaybe-uninitialized may warn */
```

Reading an uninitialised local is undefined behaviour (chapter 01 §6): it "works" in testing and fails on hardware, because the garbage differs. The repo declares SDCC-style (all locals at function top) but assigns before every read — follow both halves.

## 5. Operators you need (with repo sightings)

Arithmetic: `+ - * / %` (`%` = remainder; `level / DIFF_LEVELS`, `idx % GRID_SIZE` are everywhere).

```c
uint8_t row = idx / GRID_SIZE;  /* integer division truncates: 80/9 == 8, never 8.9 */
uint8_t col = idx % GRID_SIZE;  /* remainder: 80%9 == 8 */
```

Integer division truncates toward zero (`7/2 == 3`, `-7/2 == -2`), and `%` takes the sign of the left side (`-1 % 10 == -1` in C, unlike Python's `9` — the reason `wrap_add` adds `n` first; chapter 04 §4). Division or `%` by zero crashes — the repo only ever divides by nonzero constants.

Comparison: `== != < <= > >=`. Result is `1` (true) or `0` (false) — an `int`, not a `bool`. Never confuse `=` with `==` in conditions; `-Wall` warns (`suggest parentheses around assignment used as truth value`).

Logic: `&& || !` with **short-circuit**: `if (p && p[0])` never dereferences a null `p`, because the right side runs only if the left is true. Used for guards: `if (!board_is_locked(idx))`, `if (!save_read(&slot))`. Note `&&`/`||` yield `1`/`0`, while bitwise `&`/`|` (chapter 07) work on every bit — `&&` vs `&` confusion is a real bug source; read the operator twice in conditions.

Assignment shorthands: `+= -= *= /= %= &= |= ^= <<= >>= ++ --`. `error_count++` in `board_add_mistake()` is `error_count = error_count + 1`. Prefer `++`/`--` only standalone; inside larger expressions (`a[i++] = b[++j]`) they invite undefined order-of-evaluation bugs — the repo never nests them.

Ternary: `cond ? a : b` (Python's `a if cond else b`):

```c
cell_origin[i] = (g != 0) ? ORIGIN_GIVEN : ORIGIN_PLAYER;  /* src/board.c:39 */
cursor.entry = current ? current : 1;                      /* src/main.c:299 */
```

Use it for *values*, not control flow; nested ternaries are banned by taste everywhere.

`sizeof`: bytes occupied. `sizeof(cells)` is 81, `sizeof(Puzzle)` is 52, `sizeof(SaveSlot)` is ~210. Operand of `sizeof` is unevaluated (`sizeof(x++)` never increments) and needs no parens for variables (`sizeof x` works, though everyone writes parens). Used for budgets and for loop bounds via named constants rather than magic numbers.

Casts: `(uint8_t)(x)` forces a conversion and silences the warning — but only write one when you *mean* the truncation (as in `wrap_add`). A cast is you telling the compiler "I checked, this fits". Casts that silence warnings you do not understand convert a compile-time alert into a runtime mystery.

Comma operator and compound literals exist in C but never appear here; ignore them until a compiler message forces the encounter.

## 6. Precedence: the 8 rows that matter

Full C precedence has 15 levels; you need eight, plus one rule: **when in doubt, parenthesise** — the repo does liberally (`(uint8_t)((v + n + d) % n)`).

| Priority | Operators | Example in repo |
|----------|-----------|-----------------|
| 1 (highest) | `() [] -> .` postfix, `! ~ ++ --` unary, casts | `cells[i]`, `s->level`, `(uint8_t)x` |
| 2 | `* / %` | `row * 9 + col`, `idx % GRID_SIZE` |
| 3 | `+ -` | `v + n + d` |
| 4 | `<< >>` | `1u << (level & 7)`, `level >> 3` |
| 5 | `< <= > >=` | `i < CELL_COUNT`, `error_count < 255` |
| 6 | `== !=` | `cell_origin[idx] == ORIGIN_GIVEN` |
| 7 | `&` then `^` then `\|` (bitwise, in that order) | `mask & (1 << k)`, `now & ~prev` |
| 8 (lowest here) | `&&` then `\|\|` then `? :` then `=` | `!locked && empty`, `g ? A : B` |

Two rows cause 90% of precedence bugs: shifts bind *looser* than `+`/`-` (`a << 1 + 1` is `a << 2`, not `(a << 1) + 1`), and `&` binds *looser* than `==` (`flags & 1 == 1` parses as `flags & (1 == 1)` — accidentally correct! — but `flags & 3 == 2` parses as `flags & 1`, wrong). The repo parenthesises every shift/mask expression. Do the same and this table stays theoretical.

## 7. `printf` and `scanf` formats (the only ones this project needs)

```c
printf("%s %d %c %u 0x%X\n", "EASY", -3, 'A', (unsigned)200, 255);
```

| Specifier | For | Repo note |
|-----------|-----|-----------|
| `%d` | signed `int` | mistake counts, debug prints |
| `%u` | unsigned int (covers promoted `uint8_t`/`uint16_t`) | grid values, levels |
| `%c` | single char | rarely; menus use tile indices, not stdio |
| `%s` | `const char *` string, up to `\0` | `difficulty_name(diff)` in host tests |
| `%x` / `%X` | hex lower/upper | addresses, header bytes (`make check` prints `hex(d[0x147])`) |
| `%lu` | `unsigned long` | printing `sizeof` results portably |
| `%%` | a literal `%` | `"DONE %u/100"`-style progress needs `%%` for a real percent |

> **No float in this codebase** (`PLAN.md` risk table). `%f` would pull a large floating-point library into a 32 KB ROM. Menus format integers only (`draw_dec3`/`draw_num2` in `ui.c`). On constrained targets, every library function costs bytes — another Python-vs-C shock. `scanf` appears in chapter 02's `guess.c` only; the game reads the joypad, never stdin.

`printf` returns the characters written (or negative on error) — ignored here. Mismatched specifier/argument is undefined behaviour that `-Wall` usually catches (`format '%d' expects argument of type 'int'`). On the Game Boy there is no `printf` at all in game code: text is font tiles via `set_bkg_*` (chapter 11), because stdio would drag in console machinery the ROM cannot afford.

## 8. Overflow, saturation, and the mistake counter

Unsigned overflow wraps (`255 + 1 == 0` for `uint8_t`) — defined, sometimes wanted (checksums, tile math), usually not (counters). Signed overflow is *undefined* — never rely on it; the compiler may optimise `s + 1 > s` to "always true" and delete your check.

The repo saturates the mistake counter instead of wrapping (`src/board.c:134`):

```c
void board_add_mistake(void) {
    if (error_count < 255) {
        error_count++;
    }
}
```

Wrapping to 0 after 255 mistakes would erase the player's history. The `if` costs 2 lines and removes a whole bug class. `tests/test_host.c:101` hammers this: 3 mistakes, then 252 more, asserting the count sticks at 255 and never wraps. Saturation (`if (x < MAX) x++`), wrapping (`x++` modulo 2ⁿ), and trapping (`assert` before increment) are the three policies — counters saturate, hashes wrap, invariants assert.

The same thinking shapes `marks_count` (returns `uint8_t` because the maximum countable, 100, fits) and the checksum (`uint8_t sum` wraps deliberately — overflow *is* the hash mixing).

Next: `04-control-flow.md` — decisions, loops, and the state machine that runs the whole game.
