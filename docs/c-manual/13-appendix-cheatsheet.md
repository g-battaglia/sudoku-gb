# 13 — Appendix: cheat sheet, compiler differences, error Rosetta stone

Reference material for the whole manual. No new concepts — pointers back to the chapter that teaches each row.

## 1. One-page C cheat sheet (K&R-compact)

```c
#include "board.h"    /* project header (copy-paste declarations) */
#include <stdint.h>   /* system header: uint8_t, uint16_t, ... */

#define N 81          /* text replacement, no memory, no type (ch.08) */

typedef enum { ST_A, ST_B } State;   /* named ints: 0, 1 (ch.07) */
typedef struct {                     /* grouped values (ch.07) */
    uint8_t v[N];
    uint16_t n;
} Bag;

static uint8_t store[N];             /* file-private, forever, zeroed (ch.05) */
extern const Bag table[10];          /* defined elsewhere (ch.08) */

uint8_t get(uint8_t i);              /* declaration: promise (ch.05) */

uint8_t get(uint8_t i) {             /* definition: body */
    if (i >= N) return 0;            /* guard clause (ch.04) */
    return store[i];                 /* array read (ch.06) */
}

void set(uint8_t i, uint8_t v) {
    store[i] = v;
}

uint8_t *find(uint8_t v) {           /* pointer return: address or NULL */
    uint8_t i;
    for (i = 0; i < N; i++) {        /* counted loop (ch.04) */
        if (store[i] == v) return &store[i];  /* & = address of (ch.06) */
    }
    return NULL;                     /* sentinel: not found (ch.04) */
}

int main(void) {
    uint8_t *p;
    set(0, 5);
    p = find(5);
    if (p != NULL) *p = 7;           /* * = follow the address (ch.06) */
    switch (*p) {                    /* one value, many branches (ch.04) */
    case 7: break;
    default: break;
    }
    while (1) { /* ... */ break; }   /* open loop + break (ch.04) */
    return 0;
}
```

Bit idioms (chapter 07 §3): test `(b >> k) & 1`, set `b |= 1u << k`, clear `b &= ~(1u << k)`, toggle `b ^= 1u << k`, byte/bit `i >> 3` / `i & 7`, nibbles `x >> 4` / `x & 0x0F`.

## 2. Types at a glance (chapter 03)

| Type | Bytes | Range | Holds here |
|------|-------|-------|------------|
| `uint8_t` | 1 | 0…255 | digits, rows, indices, origins, tiles |
| `int8_t` | 1 | −128…127 | steps (`-1`), deltas |
| `uint16_t` | 2 | 0…65535 | level 0…299, offsets |
| `int` | 4 PC / 2 SDCC | machine | return codes, throwaway arithmetic |
| `char` | 1 | text only | characters/strings, never arithmetic |
| `T *` / `const T *` | 2 (GB) | addresses | buffers, strings, out-params |

Precedence essentials (chapter 03 §6): postfix/cast → `* / %` → `+ -` → `<< >>` → comparisons → `== !=` → `&` → `^` → `|` → `&&` → `||` → `? :` → `=`. Parenthesise shifts and masks, always.

`printf` essentials (chapter 03 §7): `%d` signed, `%u` unsigned, `%c` char, `%s` string-to-`\0`, `%x` hex, `%lu` for `sizeof`, `%%` for percent. No `%f` in ROM code.

## 3. Storage and linkage (chapters 05, 08, 10)

```text
local (auto)      per-call, garbage init, dies at return      → small scratch
static local      forever, zero init, keeps value             → rare (prefer file state)
static file       forever, zero init, this file only          → game state, helpers
extern            defined in exactly one other .c             → shared ROM tables
const file-scope  ROM, read-only                              → levels, literals, MAGIC
heap (malloc)     manual lifetime                             → NOT USED here
```

Headers declare (guards + `extern` + prototypes + contracts); sources define (own header first + `static` storage/helpers + bodies). Array parameters are pointers; lengths travel separately.

## 4. gcc vs SDCC/GBDK (chapters 02, 09, 11)

| Topic | gcc (PC, tests) | SDCC via lcc (Game Boy, ROM) |
|-------|-----------------|------------------------------|
| `int` width | 32-bit | 16-bit — use `uint8_t`/`uint16_t` for stored data |
| `enum` width | `int` (4) | `int` (2) — `#define` codes stored in bulk |
| Target | your Mac (ARM/x86) | LR35902 (`-msm83:gb`) |
| Output | executable | 32 KB ROM via linker + `makebin` header flags |
| `malloc` / VLA | available | avoid — fixed arrays only |
| float `printf` | fine | banned — ROM cost |
| Declarations | anywhere (C99+) | historically top-of-block — house style keeps it |
| `inline` | standard | dialect quirks — plain functions + `static` instead |
| Warnings | `-Wall -Wextra`, zero tolerated | same bar — clean on both or it does not ship |
| Debugging | `lldb`/`gdb` + `-g`, `assert` live | no debugger — asserts on PC + PyBoy frame checks |

## 5. Error-message Rosetta stone (chapters 02, 09)

**Compiler (before the `:` is your file+line; read the first error first — later ones cascade):**

| Message fragment | Class | Meaning → fix |
|------------------|-------|---------------|
| `expected ';' before …` | syntax | missing `;`/`}` on a previous line — look *up*, not at the line |
| `undeclared identifier 'x'` | scope/include | typo, missing `#include`, or used before declaration |
| `incompatible types …` | types | e.g. string into `int` — check both sides of `=`/call |
| `implicit declaration of 'f'` | missing prototype | include the header declaring `f` |
| `conflicting types for 'f'` | header/body drift | `.c` signature differs from `.h` — reconcile (self-include catches it) |
| `redefinition of 'T'` | preprocessor | missing include guard — add `#ifndef` sandwich (ch.08 §6) |
| `unused variable 'x'` | warning | delete or justify; never leave |
| `comparison between signed and unsigned` | warning | fence in a helper with explicit cast (ch.03 §2) |
| `format '%d' expects …, argument is …` | warning | match specifier to argument (ch.03 §7) |
| `control reaches end of non-void function` | warning | a path lacks `return` — add it |
| `function returns address of local variable` | warning | return value / caller buffer / static (ch.05 §6) |

**Linker (all objects compiled; stitching failed):**

| Message | Meaning → fix |
|---------|---------------|
| `undefined reference to 'f'` | no body for a promise — add the defining `.c` to the build (`CSOURCES`) or fix the spelling |
| `multiple definition of 'x'` | two bodies — `extern` in header, define once (ch.08 §8); or make file-local `static` |
| size/overflow from `makebin`, or `check` size assert | ROM budget blown — shrink data/code, pack tighter (ch.07/10 §7) |

**`make` (not a C message at all):**

| Symptom | Meaning → fix |
|---------|---------------|
| `No rule to make target 'x'` | prerequisite/file misspelled or missing — check names and `CSOURCES` |
| `missing separator` | recipe indented with spaces — must be a Tab (ch.09 §3) |
| `up to date` but stale | timestamps confused it — `touch` the input or `make clean && make` |
| `clean` does nothing, file `clean` exists | missing `.PHONY` — add it |
| recipe failed, build stops | read the *first* failing command's own error above it |

## 6. Build and verify playbook (chapters 09, 12)

```bash
make clean && make          # zero warnings on SDCC
make check                  # 32768 bytes, logo OK, cart 0x03, SRAM 8KB
make test-host              # ALL HOST TESTS PASSED (gcc + assert)
make test-emulator          # SMOKE PASSED (optional, needs pyboy+pillow)
make run                    # human pass in mGBA
make -n                     # dry run: what WOULD build
git stash && make test-host # bisect: is it my change? (then git stash pop)
```

Regression order (§8 chapter 12): wrong map → OAM after present → tile-index drift → NUL overrun → hardware include in portable module → nested full-screen draw → stale preview tracker.

## 7. Numbers worth memorising

```text
81          cells (9x9)                    0..80 in uint8_t
300         levels (3 x 100)               0..299 needs uint16_t
52          bytes/level (41 nibbles + 11 mask)
38          MARKS_BYTES ((300+7)/8)
0xD2 (210)  save-slot bytes used of 8KB SRAM at 0xA000
32768       ROM bytes exactly (32KB, 2 banks, no banking)
160x144     pixels; 20x18 tiles of 8x8; 32x32 maps (2x); 40 sprites
60          frames/sec; ~70k cycles/frame @4.19MHz
18 / 6      input REPEAT_DELAY / REPEAT_RATE (frames)
32 / 24/20  preview blink / mistake flash / locked blink (frames)
0x9800/0x9C00  maps; 0x8000 grid tiles / 0x9000 font; OAM 0xFE00
0x81/0x89 menus, 0x93/0x9B game, 0x13 init-only LCDC values
```

## 8. Where to go next

- Re-read the header comment of any module before touching it — the contract lives there (chapter 12 §1 order).
- K&R proper (*The C Programming Language*, 2nd ed.) for the language beyond embedded scope: the manual's structure mirrors its first five chapters deliberately.
- GBDK docs + Pan Docs for hardware past this game's needs (banking, audio, colour, serial) — the LCD-safety and timing instincts from chapter 11 transfer directly.
- Your next change: pick one safe-change recipe (chapter 12 §9), implement it, and watch `make test-host && make check` stay green. That loop is the course's final exam, and you already know how to pass it.
