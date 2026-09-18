# 06 — Arrays, strings and pointers

This is the chapter that unlocks C. Arrays hold the grid, strings are arrays of characters with a terminator, and pointers are addresses. The repo uses all three constantly but in a small, learnable subset — this chapter covers exactly that subset plus the standard string functions you will meet elsewhere and the crash causes you must recognise.

## 1. Arrays: fixed-size rows of bytes

```c
static uint8_t cells[81];  /* 81 bytes in a row, indices 0..80 */

cells[0] = 5;      /* first element */
cells[80] = 9;     /* last element */
cells[81] = 1;     /* BUG: out of bounds. No error, silent corruption! */
```

> **Python vs C:** Python `cells = [0]*81` knows its length, grows, and raises `IndexError`. C arrays are dumb memory: no length stored, no growth, no bounds check. `cells[81]` compiles and scribbles on whatever the linker placed after the array (here, plausibly the `origin` array — clues turning into hints, a real game-corrupting bug with no crash). **You** are the bounds checker. Loops use `< CELL_COUNT`, never `<=`.

Declaration forms and what they mean:

```c
uint8_t a[81];              /* 81 garbage bytes (local) — must fill before reading */
uint8_t b[81] = {0};        /* 81 zeros (explicit) */
uint8_t c[] = {1, 2, 3};    /* size inferred: 3 (initialiser counts) */
static uint8_t d[81];       /* 81 zeros (static storage auto-zeroes) */
```

Partial initialisers zero the rest (`uint8_t e[81] = {1};` → `e[0]==1`, rest 0). No resizing, ever: "append" means tracking a separate length and writing `buf[len++] = v` with a manual capacity check — the save code's fixed offsets (§5 in chapter 10) are this discipline applied to hardware.

The 9×9 grid is stored row by row, *row-major* (`src/board.c:25`):

```c
static uint8_t cell_index(uint8_t row, uint8_t col) {
    return (uint8_t)(row * GRID_SIZE + col);
}
/* cell (row, col) == cells[row * 9 + col] */
```

Draw it: row 0 occupies indices 0–8, row 1 indices 9–17, …, row 8 indices 72–80. `board_conflicts()` divides and modulos the index back into `(row, col)` to scan (`idx / 9`, `idx % 9`). Consequences of row-major: a row scan strides by 1 (cache- and cycle-friendly), a column scan strides by 9, and a box scan walks a 3×3 window from a precomputed `(box_row0, box_col0)`. Read `src/board.c:72` with this picture taped to your monitor.

Multi-dimensional arrays exist (`uint8_t grid[9][9]`, `grid[r][c]`) and lay out identically row-major — `grid[r][c]` *is* `*(&grid[0][0] + r*9 + c)`. The repo uses flat `[81]` with `cell_index` instead: one index type (`uint8_t idx`) flows through board, save, puzzle and UI code, while `[9][9]` would force `(row, col)` pairs through every signature including the 38-byte bitmap helpers. One index, fewer parameters, smaller code.

Initialisation recap with the rule that matters:

```c
uint8_t marks[38];              /* garbage contents (local)! */
uint8_t clean[38] = {0};        /* all zeros */
static uint8_t cells[81];       /* static: zeros automatically */
for (i = 0; i < 38; i++) marks[i] = 0;  /* or via marks_clear(marks); */
```

Locals start with **garbage** (whatever bytes the last frame left). `static`/global storage starts zeroed. Forgetting this is a classic first-boot bug — the save code never trusts SRAM contents for the same reason (chapter 10 §5: magic + version + checksum before believing a single byte).

## 2. C strings: arrays with a `\0` at the end

```c
const char *name = "MEDIUM";  /* 7 bytes: M E D I U M \0 */
```

A C string is `char` bytes ending with a zero byte (`'\0'`, value 0). `"MEDIUM"` costs 7 bytes, not 6. Every string function (`printf("%s")`, `strlen`, the tile-text writers) scans forward until it finds `\0`. If the terminator is missing, it keeps reading into adjacent memory — which is exactly what happened during development (`COMPACT.md` §6): code reading `name[k]` past the `"EASY"` terminator picked up the `'M'` of the next literal `"MEDIUM"` in ROM. Fix: always stop at `\0` when padding rows. In Python a string knows its length; in C the `\0` *is* the length.

The safe-copy pattern (bounded, always terminated):

```c
/* Copy at most dst_size-1 chars, always terminate. */
char buf[8];
uint8_t i;
for (i = 0; i < 7 && src[i] != '\0'; i++) buf[i] = src[i];
buf[i] = '\0';
```

Generalise `7` to `dst_size - 1` and you have the shape of `snprintf(dst, size, …)` and `strncpy` used right (see §6). `difficulty_name()` (`src/puzzles.c:35`) returns `const char *`: a pointer to a ROM literal the caller may read but never modify (`const` = read-only promise). Returning a string is returning its *address* — which is safe here only because literals outlive everything (§4 in chapter 05).

Char vs string literal (one byte vs two-plus): `'A'` is the number 65; `"A"` is bytes `{65, 0}` somewhere in ROM. `char c = "A";` is a type error; `printf("%c", "A")` is undefined behaviour. The compiler's message ("incompatible pointer to integer conversion") is precise once you know the two literal kinds.

## 3. Pointers: addresses, `&` and `*`

Every byte of memory has an address (a number). A pointer is a variable holding an address.

```c
uint8_t x = 5;
uint8_t *p = &x;   /* p = "address of x".  & takes an address. */
*p = 7;            /* *p = "the byte at p". * follows the address. */
/* now x == 7 */
```

Three characters, three jobs, and you must keep them apart: `*` in a *declaration* makes a pointer (`uint8_t *p` — "p will point at a byte"); `*` in an *expression* follows it (`*p = 7` — "store through p"); `&` takes an address (`&x` — "where x lives"). `NULL` (address 0) means "points nowhere" — dereferencing it crashes, which is at least loud (see §7 crash guide).

Why pointers exist (three uses; the repo needs all three):

**Use 1 — let a function modify the caller's data** (chapter 05 §3):

```c
void board_restore(const uint8_t *values, const uint8_t *origins, uint8_t mistakes);
/* called as: board_restore(values, origins, 2); */
```

`board_restore` receives *addresses* of two 81-byte snapshots plus a mistake count, then copies them into the live grid (`src/board.c`). Passing 162 bytes by value would waste stack and cycles; passing two addresses costs a few bytes. `tests/test_host.c` builds dirty snapshots, trashes the board with `board_load(200)`, restores, and asserts every byte matches — read that test, it is executable documentation for pointers: snapshot, trash, restore, verify.

**Use 2 — walk through memory with arithmetic:**

```c
#define SRAM ((uint8_t *)0xA000)  /* src/save.c — SRAM starts at address 0xA000 */
SRAM[off] = value;                /* byte at address 0xA000 + off */
```

`SRAM` is a pointer to hardware memory. `SRAM[n]` is `*(SRAM + n)` — "the byte `n` past the start". `sram_read`/`sram_write` (`src/save.c`) are explicit `for` loops over addresses. Pointer arithmetic scales by element size automatically (`ptr + 1` moves one `uint8_t` = 1 byte; for a `uint16_t *` it would move 2 — a frequent confusion source when mixing widths, and one more reason this codebase sticks to byte pointers for raw memory). Field offsets live in `src/save_format.h` (`SAVE_OFF_*`); the checksum and field validation there are hardware-free and host-tested.

**Use 3 — strings and buffers** (`const char *s` = "address of text I will only read"). `const uint8_t *marks` in `ui_select(page, row, marks, diff)` is the same idea for non-text bytes: 38 read-only bytes starting at that address, length carried by the known constant `MARKS_BYTES`.

## 4. Arrays vs pointers (the one subtlety you need)

In most expressions, an array name *becomes* the address of its first element ("decays"):

```c
uint8_t m[38];
marks_set(m, 5);        /* m becomes &m[0], type uint8_t * */
marks_get(m, 5);        /* same */
uint8_t *p = m;         /* p points at m[0] */
p[5] == m[5];           /* identical: p[i] IS *(p + i) */
```

Differences that matter, each a known bug source:

- `sizeof(m)` is 38 (whole array) while `sizeof(p)` is 2 (an address on the Game Boy). Sizeof-through-pointer is how buffers "shrink" silently.
- You cannot reassign `m = p` (arrays are not modifiable l-values; they only *decay* when used).
- A function parameter written `uint8_t bm[]` or `uint8_t *bm` is really a pointer — the size is lost at the call boundary, which is why every function here also takes or knows the count (`MARKS_BYTES`, `CELL_COUNT`). Python's `len()` travels with the object; C's length travels (if at all) in a sibling argument or constant.

## 5. `const` with pointers: read the two positions

```c
const uint8_t *r;         /* pointer to const: cannot write *r (DATA read-only) */
uint8_t *const f = buf;   /* const pointer: cannot repoint f (ADDRESS fixed) */
const uint8_t *const b;   /* both fixed (ROM tables referenced immutably) */
```

Read right-to-left: "`r` is a pointer to `uint8_t` that is const". The repo uses the first form throughout: `const uint8_t *marks`, `const uint8_t *values`, `const char *s` — "I will read your bytes, never modify them". Dropping `const` (or casting it away) to write into ROM or a caller's read-only buffer is a crash/corruption bug. Keep `const` and the compiler enforces the promise at every call site — including catching `marks_set` (takes mutable `uint8_t *`) being passed a ROM pointer, which would be a hardware fault on the Game Boy.

## 6. `<string.h>`: the standard string toolkit (recognise, use carefully)

The game draws text as tiles and barely needs these, but every C codebase does, so learn the safe shapes:

| Function | Does | Safe shape |
|----------|------|------------|
| `strlen(s)` | length up to `\0` (not counting it) | ensure `s` is terminated first |
| `strcpy(dst, src)` | copy incl. `\0` | **avoid**: unbounded — use the bounded loop/`snprintf` |
| `strncpy(dst, src, n)` | copy up to `n`, maybe unterminated | follow with `dst[n-1] = '\0'` always |
| `strcmp(a, b)` | 0 if equal, <0/>0 by ordering | `if (strcmp(a, b) == 0)` — never `if (strcmp(…))` for equality |
| `memcpy(dst, src, n)` | copy exactly `n` bytes (any data) | `n` must fit `dst`; no overlap |
| `memmove(dst, src, n)` | copy with overlap allowed | shifting bytes inside one buffer |
| `memset(dst, v, n)` | fill `n` bytes with `v` | `memset(marks, 0, MARKS_BYTES)` ≡ `marks_clear` |

`marks_clear` is a hand loop instead of `memset` (2 lines, zero dependencies, identical output) — the codebase prefers explicit loops for tiny jobs and reserves `<string.h>` for real string work. `memcpy`'s contract (no overlap, exact `n`) versus `memmove`'s is a favourite interview trap and an occasional real bug: overlapping `memcpy` "usually works" until optimisation reorders it.

## 7. Crash guide: the four pointer failures

| Failure | Symptom on PC | Symptom on Game Boy | Cause → fix |
|---------|---------------|---------------------|-------------|
| Null dereference (`*p`, `p == NULL`) | segfault (loud, debugger shows line) | freeze/garbage | missing allocation or unchecked return → check before use |
| Dangling pointer (address of dead local/frame) | garbage that changes between runs | Heisenbugs | `return &local` → return value / caller buffer / static |
| Out-of-bounds (`a[81]`, `p + n` past end) | sometimes segfault, often silent | silent corruption | manual bounds: `< COUNT`, sentinel checks |
| Unterminated string (no `\0`) | garbage tail / over-read crash | neighbouring ROM text leaks in | always terminate; bounded copies |

Debugging order on PC: reproduce under `lldb`/`gdb`, read the faulting address (`0x0` = null; small = null+offset; huge/garbage = use-after-scope or overrun), then walk back to the last assignment of that pointer. On hardware there is no signal — chapter 12 §6 teaches the no-`printf` substitutes (assert on PC, PyBoy frame checks, tile-level reasoning).

## 8. Worked example: the completion bitmap

`marks_*` (`src/board.c:174`) packs 300 level completions into 38 bytes, one bit per level, LSB-first. This is pointers + arrays + bit ops together (bits fully taught in chapter 07):

```c
void marks_set(uint8_t *bm, uint16_t level) {
    bm[level >> 3] |= (uint8_t)(1u << (level & 7));
}
uint8_t marks_get(const uint8_t *bm, uint16_t level) {
    return (uint8_t)((bm[level >> 3] >> (level & 7)) & 1);
}
```

Level 10 → byte `10 >> 3 = 1`, bit `10 & 7 = 2`. `>> 3` is `/8`, `& 7` is `%8`. Levels 7 and 8 straddle bytes 0 and 1 — `tests/test_host.c:190` sets exactly those and asserts neighbours 6 and 9 stay clear, the boundary test to read when bits confuse you. `marks_count(bm, first, n)` then loops `marks_get` — O(n) bit reads, trivially fast for n ≤ 300, zero extra storage. A Python `set()` of completed levels would cost more RAM than the entire save slot.

Next: `07-struct-enum-typedef-bits.md` — grouping values and packing bits.
