# 09 — Separate compilation, linking and the Makefile

A real C program is many `.c` files compiled separately and stitched together. This chapter explains that pipeline and reads this repo's `Makefile` line by line — including the Game Boy flags.

## 1. Why many files?

Python splits code into modules for readability; C additionally splits for **compile time and hardware isolation**:

- Change `ui.c` → only `ui.c` recompiles; `board.c`'s object is reused. (This `Makefile` recompiles everything each time for simplicity — 9 small files take seconds — but the *model* is separate.)
- `board.c` + `puzzles.c` compile with desktop `gcc` for tests; `ui.c` + `input.c` need GBDK headers and only build for the Game Boy. File boundaries *are* portability boundaries.

Each `.c` compiles **alone**: it sees only its `#include`s. `board.c` knows nothing about `main.c` — the header promises were enough. The linker introduces them afterwards.

## 2. Objects, symbols and the two classic link errors

Compiling `board.c` produces `board.o`: machine code plus two lists — *symbols I define* (`board_load`, `board_get`, …) and *symbols I need* (`puzzle_given`, `puzzle_solution` from `puzzles.c`). Linking merges all `.o` files and resolves every need to exactly one definition.

Error 1 — `undefined reference` (a promise with no body):

```bash
gcc -Wall -Wextra -Isrc -o /tmp/t tests/test_host.c src/puzzles.c src/puzzles_gen.c
# undefined reference to `board_load'  <- test_host calls it, nobody defines it
# (fix: add src/board.c to the command)
```

Error 2 — `multiple definition` (two bodies for one promise): defining a non-`static` global or function in two `.c` files, or (equivalently) defining a variable in a header included twice. Fix: declare with `extern` in the header, define once in one `.c` (chapter 08).

> **Try it.** Run the failing command above (it cannot damage anything; it only fails to link), read the `undefined reference` lines naming every missing `board_*`/`marks_*` symbol, then run the real `make test-host` and watch it pass.

## 3. The `Makefile`, line by line

```make
GBDK = tools/gbdk
LCC = $(GBDK)/bin/lcc
PROJECT = sudoku
ROM = build/$(PROJECT).gb
CSOURCES = src/board.c src/input.c src/main.c src/puzzles.c src/puzzles_gen.c src/save.c src/tiles.c src/tiles_gen.c src/ui.c
```

- `GBDK`/`LCC`: the vendored Game Boy toolchain lives *inside the repo* (`tools/gbdk/`, gitignored, fetched once with `make setup-gbdk`). No global install, reproducible builds.
- `CSOURCES`: the complete file list. **Only these files are compiled.** Add a new `.c` and forget it here → its functions become `undefined reference`. Generated files are listed like hand-written ones — the compiler cannot tell the difference.

```make
LCCFLAGS = -msm83:gb -Wm-yn"SUDOKU" -Wl-yt0x03 -Wl-ya1
```

| Flag | Meaning |
|------|---------|
| `-msm83:gb` | Target the Game Boy CPU/LR35902 (full platform syntax; bare `-mgb` does not work with this `lcc`) |
| `-Wm-yn"SUDOKU"` | ROM title in the cartridge header (shows in emulator menus) |
| `-Wl-yt0x03` | Cartridge type `0x03` = MBC1 + RAM + BATTERY (enables saves) |
| `-Wl-ya1` | 8 KB SRAM (`-Wm-` = makebin options, `-Wl-` = linker options passed through `lcc`) |

```make
all: $(ROM)

$(ROM): $(CSOURCES) src/*.h
	mkdir -p build
	$(LCC) $(LCCFLAGS) -o $@ $(CSOURCES)
```

`all` is the default target (`make` = `make all`). The ROM depends on every source and header: any change rebuilds. `$@` = the target name (`build/sudoku.gb`). One `lcc` invocation compiles *and* links all nine files in one go (simple and fast enough here).

```make
test-host: tests/test_host.c src/board.c src/puzzles.c src/puzzles_gen.c
	gcc -Wall -Wextra -Isrc -o /tmp/sudoku_test tests/test_host.c src/board.c src/puzzles.c src/puzzles_gen.c && /tmp/sudoku_test
```

Note what is **excluded**: `main.c`, `ui.c`, `input.c`, `save.c`, `tiles*.c` — everything touching hardware. Only the portable logic is tested on PC. `-Isrc` tells `gcc` where `"board.h"` lives. The trailing `&& /tmp/sudoku_test` runs the tests only if compilation succeeded.

```make
check: $(ROM)
	python3 -c "import os; ..."   # size == 32768, logo bytes, cart 0x03, SRAM 8KB
run: $(ROM)
	open -a mGBA $(ROM) ...       # build, then open in an emulator
regen-puzzles: ; python3 tools/gen_puzzles.py --seed=20260916
regen-tiles:   ; python3 tools/gen_tiles.py
setup-gbdk:    ; curl ... && tar ...   # once per fresh clone
clean:         ; rm -rf build/*.gb build/*.ihx ... /tmp/sudoku_test
```

`make check` re-verifies the cartridge header with Python: exact 32 KB size, Nintendo logo bytes at `0x104`, cart type and SRAM size nibbles. `make clean` deletes artefacts (all gitignored) so the next build starts fresh.

## 4. Compiler vs linker vs `make` responsibilities

| Tool | Reads | Writes | Fails when |
|------|-------|--------|------------|
| compiler (`gcc`/`lcc`/`sdcc` inside) | one `.c` + headers | one `.o` | syntax/type errors, warnings |
| linker (`link`/`makebin` inside `lcc`) | all `.o` + libraries | one ROM/executable | `undefined`/`multiple` symbols, size overflow |
| `make` | `Makefile` timestamps | runs the above | any step returns non-zero |

`make` itself understands nothing about C — it only re-runs commands whose inputs are newer than their outputs, and stops on the first failure. `.PHONY` marks targets that are not files (`clean`, `test-host`, …) so `make` never confuses them with a file of the same name.

## 5. GBDK in one paragraph (what `lcc` hides)

`lcc` is a driver: it calls the SDCC C compiler, the assembler and the linker, then `makebin` stamps the Nintendo logo, checksums and cartridge bytes into the ROM header. SDCC is stricter than `gcc`: no variable-length arrays, no `malloc` in this codebase, `enum`s are `int`, and some standard-library functions are missing or heavy (no float `printf`). Code that is clean under `gcc -Wall -Wextra` almost always passes SDCC; the reverse is not guaranteed — which is why the repo demands zero warnings on *both*.

## Exercises

1. Run `make -n` (dry run: print commands without executing). Match each printed command to a `Makefile` rule.
2. Touch one header (`touch src/board.h`) and run `make -n` again. Why does everything rebuild? Which dependency causes it?
3. Explain why `test-host` lists 4 files while `CSOURCES` lists 9. For each of the 5 excluded files, name the hardware reason it cannot compile with plain `gcc`.

Next: `10-memory-rom-ram-stack.md` — where every byte lives and why.
