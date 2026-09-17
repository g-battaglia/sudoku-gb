# C Manual for Python Programmers — Index

A compact K&R-style C course built around a real project: **Sudoku GB**, a complete Sudoku game for the Game Boy Classic written in C (`src/`, ~1500 lines).

You only need Python to start. Each chapter explains one C idea in plain English, shows a tiny example you can compile on your PC with `gcc`, then shows where the same idea appears in this repo.

## How to read this manual

1. Chapters `01`–`07` are **pure C**. Read them in order. They work on any computer.
2. Chapters `08`–`10` are **how C programs are really built**: headers, separate compilation, `Makefile`, memory. This is the part Python hides from you.
3. Chapters `11`–`12` are **this project**: Game Boy hardware, GBDK, and guided traces through the actual game code.

Conventions used everywhere:

- `Python vs C` boxes translate something you know into the C equivalent.
- `Try it` boxes are things to type now.
- `Repo link` boxes point at a real file, e.g. `src/board.c:9`.
- Code blocks marked `c` compile with `gcc -Wall -Wextra` unless noted (GBDK-only code is marked).

## The repo in 30 seconds

```text
src/types.h        constants (GRID_SIZE 9, screen 20x18)
src/puzzles.h/.c   packed level format + helpers (52 bytes/level)
src/puzzles_gen.c  GENERATED 300 levels (never edit, see tools/gen_puzzles.py)
src/board.h/.c     rules: grid, origins, conflicts, mistakes, marks (no hardware!)
src/input.h/.c     joypad edge detection + auto-repeat
src/ui.h/.c        all drawing (tile maps, atomic screen swaps)
src/tiles.h/.c     VRAM layout + copy (art in tiles_gen.c, GENERATED)
src/save.h/.c      battery save slot in cartridge SRAM
src/main.c         state machine: DIFF -> SELECT -> GAME <-> PAUSE -> WIN
tests/test_host.c  PC tests with gcc + assert (executable documentation)
Makefile           build: lcc for Game Boy, gcc for tests
tools/             gen_puzzles.py, gen_tiles.py, smoke_pyboy.py
```

Two clean-code rules run through everything (keep them in mind while reading):

- `board` and `puzzles` **never include `<gb/gb.h>`**, so they compile on your PC. Hardware is isolated in `input`, `ui`, `save`, `main`.
- One module = one job. One handler per game state in `main.c`. One screen per function in `ui.c`.

## Build commands you will see quoted

```bash
make              # build build/sudoku.gb (needs tools/gbdk/, see below)
make check        # verify ROM: 32768 bytes, Nintendo logo, cart type 0x03
make test-host    # compile + run logic tests on PC with gcc
make test-emulator # headless smoke test with PyBoy (optional)
make clean        # remove build output
```

`make test-host` is the important one for this manual: it proves the pure-C part of the game works without any Game Boy.

## Chapter map

| # | File | What you learn |
|---|------|----------------|
| 1 | `01-python-to-c.md` | The mental shift: compiled vs interpreted, static types, no GC, no exceptions |
| 2 | `02-first-program-toolchain.md` | Your first program, the 4 build stages, compiler errors, warnings |
| 3 | `03-types-variables-operators.md` | `int`, `char`, `uint8_t`, signed/unsigned, overflow, `#define`, `const` |
| 4 | `04-control-flow.md` | `if`, `switch`, `for`, `while`, `enum` states, modular wrap |
| 5 | `05-functions-scope-storage.md` | Declaration vs definition, pass-by-value, `return`, `static`, the stack |
| 6 | `06-arrays-strings-pointers.md` | Arrays, indexing, C strings + `\0`, pointers, `const T *` |
| 7 | `07-struct-enum-typedef-bits.md` | `struct`, `enum`, bit operations, packed bitmaps and nibbles |
| 8 | `08-preprocessor-headers.md` | `#include`, `#define`, include guards, `.h` vs `.c`, generated code |
| 9 | `09-separate-compilation-makefile.md` | Compiling many files, linking, `undefined reference`, reading the `Makefile`, GBDK flags |
| 10 | `10-memory-rom-ram-stack.md` | Stack, static RAM, ROM (`const`), battery SRAM, VRAM/OAM, checksum saves, why no `malloc` here |
| 11 | `11-gameboy-gbdk-hardware.md` | The 10 ideas that explain `ui.c`: tiles, maps, sprites, VBlank, LCD safety |
| 12 | `12-real-project-traces-debug.md` | 4 guided code traces (boot, input frame, digit edit, HINT/save/swap) + debug checklist + glossary + exercises |

## Requirements

- A C compiler: `gcc` (macOS: `xcode-select --install`). Check with `gcc --version`.
- `make` (preinstalled on macOS, check with `make --version`).
- Nothing else for chapters 1–10. The Game Boy toolchain (`tools/gbdk/`, `make setup-gbdk`) is only needed to build the ROM itself.

Start with `01-python-to-c.md`.
