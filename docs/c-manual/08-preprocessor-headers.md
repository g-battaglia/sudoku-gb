# 08 — Preprocessor, headers and generated code

Before the compiler sees your C, a text processor (the *preprocessor*) runs: it pastes files together (`#include`), replaces names (`#define`), and includes or skips regions (`#ifdef`). Half of "how C projects fit together" lives in this stage — and half of the confusing error messages originate here too, because the compiler reports the *expanded* text, not what you wrote.

## 1. The model: dumb text in, expanded text out

```bash
gcc -E hello.c -o hello.i   # stop after preprocessing, inspect the output
```

`hello.i` is hundreds of lines: your 10 lines plus the pasted contents of `stdio.h` (plus everything *it* includes). The compiler never sees `#include` or `#define` — only their expansion. Keep this picture fixed: **preprocessor = text, compiler = meaning**. A "redefinition" error is pasted text twice; an "undeclared identifier" after a macro typo is text that never became what you meant.

## 2. `#include`: copy-paste with an address book

```c
#include <stdint.h>   /* system header: <...> searches system paths */
#include "board.h"    /* project header: "..." searches this project first */
```

> **Python vs C:** Python `import board` loads code at runtime with its own namespace. C `#include "board.h"` is a compile-time copy-paste of declarations into your file — no namespace, no runtime cost, and no code, only promises (the code arrives at link time, chapter 09).

Search order matters: `"…"` looks beside the including file first, then the `-I` paths (`-Isrc` in `make test-host` is what makes `#include "board.h"` resolve from `tests/`); `<…>` looks only in system/toolchain paths. GBDK headers (`<gb/gb.h>`, `<gbdk/platform.h>`) use `<…>` because they belong to the toolchain, not the repo. Quoting a system header with `"…"` usually still works (fallback search) but misstates ownership — reviewers will flag it.

Rule of thumb from `DEVELOPMENT.md`: to know *what a module offers*, read its `.h`; to know *how*, read its `.c`. `#include` is what lets `main.c` call `board_load()` while the body sits in `board.c`.

Self-include: `board.c` starts with `#include "board.h"` so the compiler checks the definition against its own declaration (mismatched signatures become compile errors instead of silent link-time corruption). Include-what-you-use otherwise: `board.c` includes `puzzles.h` because it *calls* `puzzle_given`, not transitively through `board.h` by accident — though here `board.h` does provide it, and the code relies on that documented chain.

Include order convention (house style, top to bottom): own header first (`#include "board.h"` in `board.c`), then project headers, then system headers (`<stdint.h>` comes via `types.h` here), then toolchain headers (`<gb/gb.h>` only in hardware files). Own-first guarantees the header stands alone (if `board.h` forgot an include it needs, `board.c` fails immediately rather than whichever file includes it second).

## 3. `#define`: object-like macros (text replacement, no type checking)

```c
#define GRID_SIZE 9
#define CELL_COUNT 81
#define MARKS_BYTES ((LEVEL_COUNT + 7) / 8)  /* 38 for 300 levels */
```

Before compiling, every `GRID_SIZE` becomes `9`. Uses in this repo:

- Dimensions that size arrays: `cells[CELL_COUNT]`, `marks[MARKS_BYTES]`. (Array sizes must be compile-time constants — a `#define` qualifies, a variable does not.)
- Derived constants: `MARKS_BYTES` is computed from `LEVEL_COUNT`, which comes from `DIFF_COUNT * DIFF_LEVELS` in `puzzles.h`. Change difficulties → bitmap resizes automatically, including the save-slot layout that embeds `MARKS_BYTES`.
- Parenthesise macro bodies: `((LEVEL_COUNT + 7) / 8)` — without parens, `MARKS_BYTES * 2` would expand to `LEVEL_COUNT + 7 / 8 * 2` (wrong: `/` binds tighter than `+`). The double parens are not style; they are correctness. Same for every macro containing operators.

Pitfall: misspelled macro names are *different* macros (or undeclared identifiers), and `#define` has no type — `GRID_SIZE` is just `9` wherever it lands. Prefer `const`/`enum` when you want checking (chapter 03 §4 table); keep `#define` for sizes, offsets (`OFF_VALUES` in `save.c`), and conditional compilation. `#undef` removes a macro (rare; used to confine a temporary definition to part of a file).

## 4. Function-like macros: power with three traps

```c
#define MIN(a, b) ((a) < (b) ? (a) : (b))   /* every param parenthesised */
```

Macros expand textually at each call site — no call overhead, no type checks, and *repeated evaluation*: `MIN(x++, y)` increments once or twice depending on the comparison. The three rules:

1. **Parenthesise every parameter and the whole body** (`((a) < (b) ? (a) : (b))`, not `(a < b ? a : b)` — call `MIN(x & 3, y)` unparenthesised and `&` vs `<` precedence (§6 chapter 03) corrupts it).
2. **Never pass side effects** (`++`, assignments, function calls with effects) — each parameter may expand 0, 1, or 2+ times.
3. **Prefer a `static` function** unless you need genericity over types or guaranteed zero call overhead on an 8-bit CPU. This repo defines *no* function-like macros — even `cell_index` is a real function, because SDCC inlines tiny `static` functions anyway and the debugger can step into them. That absence is itself the guidance: you will read macro-heavy C elsewhere, but write functions here.

The multi-statement macro shape, for recognition (`do { … } while (0)` forces one-statement behaviour under `if/else`):

```c
/* Elsewhere-style macro (NOT in this repo — recognise only): */
#define SWAP(a, b, t) do { t _tmp = (a); (a) = (b); (b) = _tmp; } while (0)
```

Without the `do/while`, `if (x) SWAP(…); else …` would attach the `else` to the macro's inner `if` (if any) or break on the bare braces. When you inherit macro-heavy code, this wrapper signals "the author knew".

## 5. Conditional compilation: `#if / #ifdef / #ifndef / #else / #elif / #endif`

```c
/* GBDK vs PC split (pattern; this repo isolates instead — see below) */
#ifdef GAMEBOY
#include <gb/gb.h>
#else
#include <stdio.h>
#endif
```

Directives test *macro definedness* (`#ifdef X`, `#ifndef X`, `#if defined(X)`) or *constant expressions* (`#if LEVEL_COUNT > 255`). Skipped regions never reach the compiler — they can contain anything, even unbalanced braces. Uses: platform splits, debug scaffolding (`#ifdef DEBUG` extra checks), feature flags, and header guards (§6).

This repo's stance is notable: it barely uses conditionals in game code, isolating hardware behind module boundaries instead (`board`/`puzzles` compile everywhere; `input`/`ui`/`save`/`main` are GB-only; `test_host` simply excludes the latter). Conditional compilation is powerful and untestable-by-construction (each configuration is a different program) — file-level isolation keeps every configuration buildable and every portable module testable with plain `gcc`. Prefer separate files over `#ifdef` mazes; reach for `#ifdef` for toolchain quirks and debug-only code.

Two companions: `#error "message"` aborts preprocessing with your text (guard impossible configurations: `#if MARKS_BYTES > 64 / #error "slot overflow" / #endif` — a compile-time budget assert), and `#pragma once` (non-standard but universal guard alternative; this repo uses classic `#ifndef` guards for maximum SDCC compatibility).

Predefined macros (debugging aids): `__FILE__` (current filename), `__LINE__` (line number), `__func__` (enclosing function, C99), `__DATE__`/`__TIME__` (build stamp). `assert` prints `__FILE__`/`__LINE__` on failure — now you know where those strings come from.

## 6. Include guards: the `#ifndef` sandwich

Every header in this repo opens and closes the same way (`src/board.h:1`, `src/types.h:1`, `src/puzzles.h:1`):

```c
#ifndef BOARD_H
#define BOARD_H

/* ... declarations ... */

#endif /* BOARD_H */
```

Why: `board.h` includes `puzzles.h` (for `LEVEL_COUNT`), `save.h` includes `board.h`, `main.c` includes all three. Without guards, `puzzles.h` would be pasted twice into one translation unit and `typedef struct {…} Puzzle;` would error as a redefinition. The guard means "paste me once per compilation; skip me after": first inclusion defines `BOARD_H`, later ones see it defined and contribute nothing. Forgetting a guard produces pages of `redefinition` errors from a single extra `#include` — when you see those, check guards first, in the header named by the error.

Guard naming: `BOARD_H` = filename, uppercased, dots to underscores. Unique per file, never a common word (a guard named `TYPES` could collide with a toolchain macro and silently empty your header — the `_H` suffix prevents exactly that).

## 7. `.h` declares, `.c` defines (the contract)

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

- Every public function is declared in exactly one header, defined in exactly one `.c`. Declaring twice is fine; **defining twice is a link error** (`multiple definition`, chapter 09 §2).
- `static` functions/variables exist only in their `.c` and never appear in the header (`cell_index`, `wrap_add`, `sram_valid`, `cells`, `prev_state`). The header is the public menu; `static` is the kitchen (chapter 05 §4).
- Headers carry the *contract comments*: valid ranges (`0-80`, `0-299`), call order (`board_conflicts` validates a tentative `board_set` — read `confirm_editing` for the sequence), save-format stability (`ORIGIN_*` numbering, `SaveSlot` field order).

What belongs where (house style):

| Header (`.h`) | Source (`.c`) |
|---------------|---------------|
| include guards, `#include`s needed for signatures | `#include` its own header first |
| `#define` constants, `enum`, `struct`, `extern` declarations | `static` storage + `static` helpers |
| function declarations + contract comments | function definitions |
| never: function bodies (except tiny `static inline`), never: non-`extern` globals | never: a second definition of a public symbol |

`static inline` in headers (mentioned for completeness): hints the compiler to inline tiny accessors defined in the header. Unused here — plain declarations plus SDCC's own inlining suffice, and `inline` semantics differ subtly across C standards (another portability wrinkle avoided).

## 8. `extern`: one definition, many users

```c
/* puzzles.h:46 — "300 Puzzles exist SOMEWHERE (in puzzles_gen.c)" */
extern const Puzzle puzzles[LEVEL_COUNT];
```

`extern` declares without defining. All 300 levels are *defined* once in the generated `puzzles_gen.c`; every file including `puzzles.h` can read them, and the linker connects the uses to the single definition (chapter 09 §2 symbols). Without `extern`, each including file would try to create its own 15.6 KB copy — link error at best, ROM blowup at worst. `extern` + one definition is the pattern for every legitimately global table; mutable globals shared this way are avoided (shared state here is file-`static`, chapter 05 §6).

## 9. Generated C: Python writes it, C compiles it

Two `.c` files are never edited by hand:

- `src/puzzles_gen.c` — 300 packed levels, written by `tools/gen_puzzles.py --seed=20260916` (random full grid + dig holes while a backtracking+MRV solver proves uniqueness with cap 2, then pack nibbles + mask; chapter 07 §3 idiom B is the packing the script emits).
- `src/tiles_gen.c` — 230 grid tiles + 4 cursor tiles, written by `tools/gen_tiles.py` (paints each 16×16 cell variant as 2bpp bytes; chapter 11 §2 is the format it emits).

To the compiler they are ordinary `.c` files (listed in `CSOURCES`, chapter 09 §3). To you they are build artefacts: change the generator, re-run `make regen-puzzles` / `make regen-tiles`, rebuild. Both generators are deterministic — running twice yields byte-identical files (verified by md5 in `COMPACT.md`). The "GENERATED, do not edit" banner plus deterministic output is the complete protocol: review the generator, never the output, and diffing two regens must show nothing.

> **Python vs C take:** the project uses each language where it wins — Python for offline generation (solver, image baking) and verification (`make check`'s ROM asserts, `smoke_pyboy.py`'s frame checks), C for the 32 KB runtime. Polyglot by design, not accident: the boundary runs exactly where determinism meets bytes.

Next: `09-separate-compilation-makefile.md` — many files, one ROM, and the `Makefile` that drives it.
