# 08 — Preprocessor, headers and generated code

Before the compiler sees your C, a text processor (the *preprocessor*) runs: it pastes files together (`#include`) and replaces names (`#define`). Half of "how C projects fit together" lives in this stage.

## 1. `#include`: copy-paste with an address book

```c
#include <stdint.h>   /* system header: <...> searches system paths */
#include "board.h"    /* project header: "..." searches this project first */
```

> **Python vs C:** Python `import board` loads code at runtime with its own namespace. C `#include "board.h"` is a compile-time copy-paste of declarations into your file — no namespace, no runtime cost, and no code, only promises (the code arrives at link time).

Rule of thumb from `DEVELOPMENT.md`: to know *what a module offers*, read its `.h`; to know *how*, read its `.c`. `#include` is what lets `main.c` call `board_load()` while the body sits in `board.c`.

Self-include: `board.c` starts with `#include "board.h"` so the compiler checks the definition against its own declaration (mismatched signatures become compile errors instead of silent link-time corruption).

## 2. `#define`: text replacement (powerful, no type checking)

```c
#define GRID_SIZE 9
#define CELL_COUNT 81
#define MARKS_BYTES ((LEVEL_COUNT + 7) / 8)  /* 38 for 300 levels */
```

Before compiling, every `GRID_SIZE` becomes `9`. Uses in this repo:

- Dimensions that size arrays: `cells[CELL_COUNT]`, `marks[MARKS_BYTES]`. (Array sizes must be compile-time constants — a `#define` qualifies, a variable does not.)
- Derived constants: `MARKS_BYTES` is computed from `LEVEL_COUNT`, which comes from `DIFF_COUNT * DIFF_LEVELS` in `puzzles.h`. Change difficulties → bitmap resizes automatically.
- Parenthesise macro bodies: `((LEVEL_COUNT + 7) / 8)` — without parens, `MARKS_BYTES * 2` would expand to `LEVEL_COUNT + 7 / 8 * 2` (wrong). The double parens are not style; they are correctness.

Pitfall: misspelled macro names are *different* macros (or undeclared identifiers), and `#define` has no type — `GRID_SIZE` is just `9` wherever it lands. Prefer `const`/`enum` when you want checking; keep `#define` for sizes and conditional compilation.

## 3. Include guards: the `#ifndef` sandwich

Every header in this repo opens and closes the same way (`src/board.h:1`, `src/types.h:1`, `src/puzzles.h:1`):

```c
#ifndef BOARD_H
#define BOARD_H

/* ... declarations ... */

#endif /* BOARD_H */
```

Why: `board.h` includes `puzzles.h` (for `LEVEL_COUNT`), `save.h` includes `board.h`, `main.c` includes all three. Without guards, `puzzles.h` would be pasted twice and `typedef struct {…} Puzzle;` would error as a redefinition. The guard means "paste me once per compilation; skip me after". Forgetting it produces pages of `redefinition` errors from a single extra `#include` — when you see those, check guards first.

## 4. `.h` declares, `.c` defines (the contract)

```c
/* board.h — WHAT: names, signatures, constants, ownership notes */
void board_load(uint16_t level);
uint8_t board_get(uint8_t idx);
uint8_t board_conflicts(uint8_t idx);
/* ... */

/* board.c — HOW: storage + logic, hardware-free */
static uint8_t cells[CELL_COUNT];   /* the actual bytes live HERE, once */
void board_load(uint16_t level) { /* ... */ }
```

Consequences:

- Every public function is declared in exactly one header, defined in exactly one `.c`. Declaring twice is fine; **defining twice is a link error** (`multiple definition`).
- `static` functions/variables exist only in their `.c` and never appear in the header (`cell_index`, `wrap_add`, `sram_valid`, `cells`, `prev_state`). The header is the public menu; `static` is the kitchen.
- Headers carry the *contract comments*: valid ranges (`0-80`, `0-299`), who must call what first (`board_conflicts` BEFORE `board_set` is accepted — actually the reverse: caller checks conflicts after tentative set; read `confirm_editing`), which numbering is save-format-stable (`ORIGIN_*`).

What belongs where (house style):

| Header (`.h`) | Source (`.c`) |
|---------------|---------------|
| include guards, `#include`s needed for signatures | `#include` its own header first |
| `#define` constants, `enum`, `struct`, `extern` declarations | `static` storage + `static` helpers |
| function declarations + contract comments | function definitions |
| never: function bodies (except tiny `static inline`), never: non-`extern` globals | never: a second definition of a public symbol |

## 5. `extern`: one definition, many users

```c
/* puzzles.h:46 — "300 Puzzles exist SOMEWHERE (in puzzles_gen.c)" */
extern const Puzzle puzzles[LEVEL_COUNT];
```

`extern` declares without defining. All 300 levels are *defined* once in the generated `puzzles_gen.c`; every file that includes `puzzles.h` can read them, and the linker connects the uses to the single definition. Without `extern`, each including file would try to create its own copy (link error or 15.6 KB × N blowup).

## 6. Generated C: Python writes it, C compiles it

Two `.c` files are never edited by hand:

- `src/puzzles_gen.c` — 300 packed levels, written by `tools/gen_puzzles.py --seed=20260916` (random full grid + dig holes while a solver proves uniqueness, then pack nibbles + mask).
- `src/tiles_gen.c` — 230 grid tiles + 4 cursor tiles, written by `tools/gen_tiles.py` (paints each 16×16 cell artwork as bytes).

To the compiler they are ordinary `.c` files (they are listed in `CSOURCES`, chapter 09). To you they are build artefacts: change the generator, re-run `make regen-puzzles` / `make regen-tiles`, rebuild. Both generators are deterministic — running twice yields byte-identical files (verified by md5 in `COMPACT.md`).

> **Python vs C take:** the project uses each language where it wins — Python for offline generation (solver, image baking, ROM checks), C for the 32 KB runtime. `make check` itself is Python one-liners asserting ROM size/logo/cart bytes. Polyglot by design, not accident.

## Exercises

1. Remove the `#ifndef` guard from a scratch copy of a two-header toy project (not the repo!) and recompile. Read the `redefinition` errors, then restore the guard. Recognise this error on sight.
2. Find the `extern const Puzzle puzzles[LEVEL_COUNT];` line. Which file *defines* it? What precisely would break (quote the linker message) if `puzzles_gen.c` were dropped from `CSOURCES`?
3. Open the first 30 lines of `src/puzzles_gen.c` and `tools/gen_puzzles.py` side by side. Identify the hand-off: which Python structure becomes which C declaration?

Next: `09-separate-compilation-makefile.md` — many files, one ROM, and the `Makefile` that drives it.
