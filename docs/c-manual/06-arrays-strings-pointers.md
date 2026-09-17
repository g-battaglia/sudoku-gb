# 06 — Arrays, strings and pointers

This is the chapter that unlocks C. Arrays hold the grid, strings are arrays of characters, and pointers are addresses. The repo uses all three constantly but in a small, learnable subset — this chapter covers exactly that subset.

## 1. Arrays: fixed-size rows of bytes

```c
static uint8_t cells[81];  /* 81 bytes in a row, indices 0..80 */

cells[0] = 5;      /* first element */
cells[80] = 9;     /* last element */
cells[81] = 1;     /* BUG: out of bounds. No error, silent corruption! */
```

> **Python vs C:** Python `cells = [0]*81` knows its length, grows, and raises `IndexError`. C arrays are dumb memory: no length stored, no growth, no bounds check. `cells[81]` compiles and scribbles on whatever lives after the array (here, the `origin` array — a real game-corrupting bug). **You** are the bounds checker. Loops use `< CELL_COUNT`, never `<=`.

The 9×9 grid is stored row by row (`src/board.c:18`):

```c
static uint8_t cell_index(uint8_t row, uint8_t col) {
    return (uint8_t)(row * GRID_SIZE + col);
}
/* cell (row, col) == cells[row * 9 + col]; index = row-major */
```

Draw it: row 0 occupies indices 0–8, row 1 indices 9–17, …, row 8 indices 72–80. `board_conflicts()` divides and modulos the index back into `(row, col)` to scan (`idx / 9`, `idx % 9`). Row scan = stride 1, column scan = stride 9, box scan = 3×3 window from `(row/3*3, col/3*3)`. Read `src/board.c:63` with this picture in mind.

Initialisation:

```c
uint8_t marks[38];              /* garbage contents (local)! */
uint8_t clean[38] = {0};        /* all zeros */
static uint8_t cells[81];       /* static: zeros automatically */
for (i = 0; i < 38; i++) marks[i] = 0;  /* or via marks_clear(marks); */
```

Locals start with **garbage** (whatever bytes were there). `static`/global storage starts zeroed. Forgetting this is a classic first-boot bug — the save code never trusts SRAM contents for the same reason (chapter 10).

## 2. C strings: arrays with a `\0` at the end

```c
const char *name = "MEDIUM";  /* 7 bytes: M E D I U M \0 */
```

A C string is `char` bytes ending with a zero byte (`'\0'`, value 0). `"MEDIUM"` costs 7 bytes, not 6. Every string function (`printf("%s")`, `strlen`, tile-text writers) scans forward until it finds `\0`. If the terminator is missing, it keeps reading into adjacent memory.

Repo sighting — the exact bug class, caught during development (`COMPACT.md` §6): code reading `name[k]` past the `"EASY"` terminator picked up the `'M'` of the next literal `"MEDIUM"` in ROM. Fix: always stop at `\0` when padding rows. In Python a string knows its length; in C the `\0` *is* the length.

```c
/* Safe pattern: bounded copy, always terminate */
char buf[8];
uint8_t i;
for (i = 0; i < 7 && src[i] != '\0'; i++) buf[i] = src[i];
buf[i] = '\0';
```

`difficulty_name()` (`src/puzzles.c:31`) returns `const char *`: a pointer to a ROM literal the caller may read but never modify (`const` = read-only promise). Returning a string is returning its *address* (next section).

## 3. Pointers: addresses, `&` and `*`

Every byte of memory has an address (a number). A pointer is a variable holding an address.

```c
uint8_t x = 5;
uint8_t *p = &x;   /* p = "address of x".  & takes an address. */
*p = 7;            /* *p = "the byte at p". * follows the address. */
/* now x == 7 */
```

Three characters, three jobs: `*` in a *declaration* makes a pointer (`uint8_t *p`), `*` in an *expression* follows it (`*p = 7`), `&` takes an address (`&x`). `NULL` (address 0) means "points nowhere".

Why pointers exist (three uses; the repo needs all three):

**Use 1 — let a function modify the caller's data** (chapter 05):

```c
void board_restore(const uint8_t *values, const uint8_t *origins, uint8_t mistakes);
/* called as: board_restore(values, origins, 2); */
```

`board_restore` receives *addresses* of two 81-byte snapshots plus a mistake count, then copies them into the live grid (`src/board.c:142`). Passing 162 bytes by value would waste stack; passing two addresses costs a few bytes. `tests/test_host.c:224` builds dirty snapshots, trashes the board with `board_load(200)`, restores, and asserts every byte matches — read that test, it is executable documentation for pointers.

**Use 2 — walk through arrays with arithmetic:**

```c
#define SRAM ((uint8_t *)0xA000)  /* src/save.c:24 — SRAM starts at address 0xA000 */
SRAM[off] = value;                /* byte at address 0xA000 + off */
```

`SRAM` is a pointer to hardware memory. `SRAM[n]` is `*(SRAM + n)` — "the byte `n` past the start". `sram_read`/`sram_write` (`src/save.c:43`) are explicit `for` loops over addresses. Pointer arithmetic scales by element size automatically (`ptr + 1` moves one `uint8_t` = 1 byte; for `uint16_t` it would move 2 — a frequent confusion source, but this codebase sticks to bytes).

**Use 3 — strings and buffers** (`const char *s` = "address of text I will only read"). `const uint8_t *marks` in `ui_select(page, row, marks, diff)` is the same idea for non-text bytes: 38 read-only bytes.

## 4. Arrays vs pointers (the one subtlety you need)

In most expressions, an array name *becomes* the address of its first element:

```c
uint8_t m[38];
marks_set(m, 5);        /* m becomes &m[0], type uint8_t * */
marks_get(m, 5);        /* same */
uint8_t *p = m;         /* p points at m[0] */
p[5] == m[5];           /* identical: p[i] IS *(p + i) */
```

Differences that matter: `sizeof(m)` is 38 (whole array) while `sizeof(p)` is 2 (an address on the Game Boy); you cannot reassign `m = p` (arrays are not pointers, they only *decay* to pointers when passed). When a function parameter is written `uint8_t bm[]` or `uint8_t *bm`, it is really a pointer — the size information is lost, which is why every function here also takes or knows the count (`MARKS_BYTES`, `CELL_COUNT`).

## 5. `const` with pointers: read the two positions

```c
const uint8_t *r;   /* pointer to const: cannot write *r (the DATA is read-only) */
uint8_t *const f;   /* const pointer: cannot repoint f (the ADDRESS is fixed) */
```

The repo uses the first form: `const uint8_t *marks`, `const uint8_t *values`, `const char *s` — "I will read your bytes, never modify them". Dropping `const` (or casting it away) to write into ROM or a caller's read-only buffer is a crash/corruption bug. Keep `const` and the compiler enforces the promise.

## 6. Worked example: the completion bitmap

`marks_*` (`src/board.c:156`) packs 300 level completions into 38 bytes, one bit per level, LSB-first. This is pointers + arrays + bit ops together:

```c
void marks_set(uint8_t *bm, uint16_t level) {
    bm[level >> 3] |= (uint8_t)(1u << (level & 7));
}
uint8_t marks_get(const uint8_t *bm, uint16_t level) {
    return (uint8_t)((bm[level >> 3] >> (level & 7)) & 1);
}
```

Level 10 → byte `10 >> 3 = 1`, bit `10 & 7 = 2`. `>> 3` is `/8`, `& 7` is `%8` (chapter 07). `tests/test_host.c:190` asserts boundaries (levels 0, 299, bits 7/8 straddling bytes 0/1) and counting — the test to read when bits confuse you.

## Exercises

1. Write `uint8_t idx = row * 9 + col;` inversely: given `idx = 47`, compute `row` and `col` by hand with `/` and `%`, then check with a program.
2. Explain why `board_restore` takes `const uint8_t *values` and not `uint8_t values[81]` *by value*. What would a by-value 81-byte parameter cost on every LOAD?
3. `char name[5] = "MEDIUM";` — how many bytes does `"MEDIUM"` need, and what goes wrong here? Write the safe declaration and explain where the missing `\0` would lead `printf("%s")`.

Next: `07-struct-enum-typedef-bits.md` — grouping values and packing bits.
