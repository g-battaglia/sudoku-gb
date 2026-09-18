# C Manual for Python Programmers — Index

A compact K&R-style C course built around a real project: **Sudoku GB**, a complete Sudoku game for the Game Boy Classic written in C (`src/`, ~1500 lines).

You only need Python to start. Each chapter explains one C idea in plain English, shows tiny examples you can compile on your PC with `gcc`, then shows where the same idea appears in this repo. Every example marked `c` was compiled with `gcc -Wall -Wextra` during writing; GBDK-only code is always labelled.

## How to read this manual

1. Chapters `01`–`07` are **pure C**. Read them in order. They work on any computer.
2. Chapters `08`–`10` are **how C programs are really built**: headers, separate compilation, `Makefile`, memory. This is the part Python hides from you.
3. Chapters `11`–`12` are **this project**: Game Boy hardware, GBDK, and guided traces through the actual game code.
4. Chapter `13` is an **appendix**: one-page K&R-style cheat sheet, SDCC-vs-gcc differences, and the error-message Rosetta stone.

Conventions used everywhere:

- `Python vs C` boxes translate something you know into the C equivalent.
- `Try it` boxes are things to type now. Do them — C is learned by compiler errors, not by reading.
- `Repo link` boxes point at a real file, e.g. `src/board.c:14`. Open the file; the manual quotes it exactly.
- `Warning` boxes mark undefined behaviour or silent-corruption traps. Slow down there.

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

Three clean-code rules run through everything (keep them in mind while reading):

- `board` and `puzzles` **never include `<gb/gb.h>`**, so they compile on your PC. Hardware is isolated in `input`, `ui`, `save`, `main`.
- One module = one job. One handler per game state in `main.c`. One screen per function in `ui.c`.
- Zero warnings on both compilers (`gcc -Wall -Wextra` and SDCC via `lcc`). A warning is a bug you have not met yet.

## Build commands you will see quoted

```bash
make              # build build/sudoku.gb (needs tools/gbdk/, see below)
make check        # verify ROM: 32768 bytes, Nintendo logo, cart type 0x03
make test-host    # compile + run logic tests on PC with gcc
make test-emulator # headless smoke test with PyBoy (optional)
make clean        # remove build output
```

`make test-host` is the important one for this manual: it proves the pure-C part of the game works without any Game Boy. Run it now to confirm your environment:

```bash
make test-host
# expect: ... ALL HOST TESTS PASSED
```

## Chapter map (with time estimates)

| # | File | What you learn | Time |
|---|------|----------------|------|
| 1 | `01-python-to-c.md` | Mental shift: compiled vs interpreted, static types, no GC, no exceptions, undefined behaviour | 30 min |
| 2 | `02-first-program-toolchain.md` | First programs, 4 build stages, errors vs warnings, `-O`/`-g`, debuggers, `assert` | 45 min |
| 3 | `03-types-variables-operators.md` | `int`/`char`/`uint8_t`, promotion rules, precedence table, `printf`/`scanf`, saturation | 45 min |
| 4 | `04-control-flow.md` | `if`/`switch`/`for`/`while`, sentinels, wrap trick, guard clauses, full frame trace | 40 min |
| 5 | `05-functions-scope-storage.md` | Declaration vs definition, pass-by-value, `static` × 2, stack frames, recursion (and why not) | 40 min |
| 6 | `06-arrays-strings-pointers.md` | Arrays, row-major grid, C strings + `<string.h>`, pointers, `NULL`, segfault causes | 60 min |
| 7 | `07-struct-enum-typedef-bits.md` | `struct` layout + padding, `enum` vs `#define`, 6 bit ops, set/clear/toggle/extract idioms | 50 min |
| 8 | `08-preprocessor-headers.md` | `#include`, macro functions + pitfalls, guards, `#ifdef`, `extern`, generated code | 40 min |
| 9 | `09-separate-compilation-makefile.md` | Objects/symbols, link errors, `make` syntax primer, this `Makefile` flag by flag | 50 min |
| 10 | `10-memory-rom-ram-stack.md` | Stack/WRAM/ROM/SRAM/VRAM, heap + `malloc` (and why not here), `volatile`, endianness | 50 min |
| 11 | `11-gameboy-gbdk-hardware.md` | 10 hardware ideas in depth: 2bpp tiles, LCDC bits, timing, palettes, input, LCD safety | 60 min |
| 12 | `12-real-project-traces-debug.md` | 6 guided traces, debugging without `printf`, 7 regressions, safe-change recipes, glossary | 60 min |
| 13 | `13-appendix-cheatsheet.md` | Cheat sheet, gcc-vs-SDCC table, error-message Rosetta stone | reference |

Total: roughly 9–10 focused hours from zero to "can modify this codebase safely".

## The 40 most-used symbols (your compass)

| Symbol | Kind | Lives in | Meaning |
|--------|------|----------|---------|
| `GRID_SIZE` / `CELL_COUNT` | macro | `types.h` | 9 / 81 |
| `SCREEN_COLS` / `SCREEN_ROWS` | macro | `types.h` | 20 / 18 tiles |
| `DIFF_LEVELS` / `DIFF_COUNT` / `LEVEL_COUNT` | macro | `puzzles.h` | 100 / 3 / 300 |
| `Puzzle` / `puzzles` | struct / table | `puzzles.h` / `puzzles_gen.c` | packed level / 300 of them |
| `puzzle_solution` / `puzzle_given` | function | `puzzles.c` | hidden digit / clue-or-empty |
| `difficulty_name` | function | `puzzles.c` | `0/1/2` → `"EASY"/…` |
| `ORIGIN_PLAYER/GIVEN/HINT` | macro | `board.h` | 0 editable / 1 clue / 2 locked reveal |
| `board_load/get/set` | function | `board.c` | reset level / read / unchecked write |
| `board_conflicts/is_solved` | function | `board.c` | duplicate? / full-and-valid? |
| `board_errors/add_mistake` | function | `board.c` | saturating tally |
| `board_reveal/origin/restore` | function | `board.c` | lock hint / read code / LOAD snapshot |
| `marks_clear/set/get/count` | function | `board.c` | 38-byte completion bitmap |
| `MARKS_BYTES` | macro | `board.h` | 38 |
| `SaveSlot` | struct | `save.h` | whole save: game + marks |
| `save_checksum/fields_valid` | function | `save_format.c` | layout math, no hardware |
| `SAVE_OFF_*` / `SAVE_VERSION` | macro | `save_format.h` | offsets + version |
| `save_present/read/write` | function | `save.c` | validate / load / store SRAM slot |
| `input_poll(_init)` | function | `input.c` | snapshot joypad once per frame |
| `input_pressed/dir` | function | `input.c` | edge actions / repeat movement |
| `input_reset_combo` | function | `input.c` | soft-reset chord edge |
| `ui_init/diff/select/pause/win/saved` | function | `ui.c` | full screens (hidden map + present) |
| `ui_cell/preview/cursor(_hide)` | function | `ui.c` | delta updates (visible map / OAM) |
| `GRID_X/Y`, `LEVELS_PER_PAGE` | macro | `ui.h` | map origin / 10 |
| `state/level/sel_diff` | static | `main.c` | session state machine data |
| `cursor` (`Cursor`) | static struct | `main.c` | row/col/entry/editing |
| `pv` (`Preview`) | static struct | `main.c` | blink tracker (row = `PV_NONE` = untracked) |
| `pend` (`Pending`) | static struct | `main.c` | deferred win/save screens |
| `start_level/resume_game/save_store` | static fn | `main.c` | transitions + SRAM snapshot |
| `enter/confirm/cancel/editing` | static fn | `main.c` | modal digit-pick flow |
| `do_hint/apply_load/win_now` | static fn | `main.c` | hint / load / win-record |

## FAQ before you start

**Do I need the Game Boy toolchain to read this manual?** No. Chapters 1–10 need only `gcc` + `make`. `tools/gbdk/` (`make setup-gbdk`) is needed only to produce `build/sudoku.gb`.

**Why no floats, no `malloc`, almost no `int`?** Each costs bytes, RAM, or portability on a 4 MHz CPU with 8 KB RAM and a strict compiler. The manual explains every refusal where it occurs — these constraints are the course, not obstacles to it.

**I hit an error not in the manual.** Read the message aloud, find its class in `13` §5 (compiler / warning / linker / `make`), fix the class first. Nine times out of ten the class tells you the file.

## Requirements

- A C compiler: `gcc` (macOS: `xcode-select --install`). Check with `gcc --version`.
- `make` (preinstalled on macOS, check with `make --version`).
- Nothing else for chapters 1–10. The Game Boy toolchain (`tools/gbdk/`, `make setup-gbdk`) is only needed to build the ROM itself.

Start with `01-python-to-c.md`.
