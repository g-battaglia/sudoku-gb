# 09 — Separate compilation, linking and the Makefile

A real C program is many `.c` files compiled separately and stitched together by a linker, driven by `make`. This chapter explains the pipeline, the two classic link errors, `make` syntax from zero, and this repo's `Makefile` flag by flag — including the Game Boy cartridge flags.

## 1. Why many files?

Python splits code into modules for readability; C additionally splits for **compile time, portability boundaries, and information hiding**:

- Change `ui.c` → only `ui.c` recompiles; `board.c`'s object is reused. (This `Makefile` recompiles everything each time for simplicity — 9 small files take seconds — but the *model* is separate, and §6 shows the per-file pattern for when projects outgrow one-shot builds.)
- `board.c` + `puzzles.c` compile with desktop `gcc` for tests; `ui.c` + `input.c` need GBDK headers and only build for the Game Boy. File boundaries *are* portability boundaries (§3 `test-host` excludes exactly the hardware files).
- Each header publishes the minimum: callers of `board.h` cannot see `cells[]` or `cell_index` (both `static`), so they cannot depend on them. The compiler enforces the architecture.

Each `.c` compiles **alone** (a *translation unit* = one `.c` plus all its pasted headers): it sees only its `#include`s. `board.c` knows nothing about `main.c` — the header promises were enough. The linker introduces them afterwards. This isolation is what makes separate testing possible at all.

## 2. Objects, symbols and the two classic link errors

Compiling `board.c` produces `board.o`: machine code plus two lists — *symbols I define* (`board_load`, `board_get`, `marks_set`, …) and *symbols I need* (`puzzle_given`, `puzzle_solution` from `puzzles.c`). Linking merges all `.o` files and resolves every need to exactly one definition. Inspect on PC: `nm board.o` shows `T _board_load` (defined, text section), `U _puzzle_given` (undefined, must come from elsewhere); `nm` on the final binary shows no `U` left except system calls resolved from libraries.

Error 1 — `undefined reference` (a promise with no body):

```bash
gcc -Wall -Wextra -Isrc -o /tmp/t tests/test_host.c src/puzzles.c src/puzzles_gen.c
# undefined reference to `board_load'  <- test_host calls it, nobody defines it
# (fix: add src/board.c to the command)
```

Missing file, misspelled function name, forgotten `CSOURCES` entry, or a `static` that should have been public — all surface as this one message naming the symbol. Read the *symbol*, find who should define it, add the file.

Error 2 — `multiple definition` (two bodies for one promise): defining a non-`static` global or function in two `.c` files, or (equivalently) defining a variable in a header included by two files. Fix: declare with `extern` in the header, define once in one `.c` (chapter 08 §8). The `static`-vs-public decision (§1) prevents this class structurally: shared-by-accident becomes impossible when only headers publish names.

A third linker failure belongs to embedded work: **size overflow** — code + data exceeding ROM. Here `make check` asserts exactly 32768 bytes; an oversized link fails in `makebin` or produces a ROM the hardware cannot map. Every `%f` avoided, every nibble packed, every `malloc` refused (chapters 03/07/10) is headroom for this limit.

> **Try it.** Run the failing command above (it cannot damage anything; it only fails to link), read the `undefined reference` lines naming every missing `board_*`/`marks_*` symbol, then run the real `make test-host` and watch it pass. The missing-symbol list *is* `board.c`'s public API — compare it with `board.h`.

## 3. `make` syntax primer (60 seconds, then the real file)

```make
target: prerequisite1 prerequisite2   # "to build target, need these first"
<TAB>command to run                   # MUST start with a Tab, not spaces
```

- If any prerequisite is newer than the target (or the target is missing), `make` runs the recipe; otherwise it says "up to date" and does nothing. Timestamps, not content — `touch` alone forces rebuilds.
- `VAR = value` defines a variable; `$(VAR)` expands it. `CC`, `CFLAGS` are conventional names (this file uses `LCC`/`LCCFLAGS` for the GBDK driver).
- `$@` = the target name, `$<` = the first prerequisite, `$^` = all prerequisites. They keep recipes generic.
- `.PHONY: clean test-host` marks targets that are *actions*, not files — otherwise a file named `clean` would make `make clean` silently do nothing.
- Each recipe line runs in its own shell (use `&&` or `\` continuations to chain); a failing command (non-zero exit) stops the build unless prefixed with `-`.

With that, the repo's `Makefile`:

```make
GBDK = tools/gbdk
GBDK_VERSION = 4.5.0
GBDK_URL = https://github.com/gbdk-2020/gbdk-2020/releases/download/$(GBDK_VERSION)/gbdk-macos-arm64.tar.gz
LCC = $(GBDK)/bin/lcc

PROJECT = sudoku
ROM = build/$(PROJECT).gb
CSOURCES = src/board.c src/input.c src/main.c src/puzzles.c src/puzzles_gen.c src/save.c src/tiles.c src/tiles_gen.c src/ui.c
```

- `GBDK`/`LCC`: the vendored Game Boy toolchain lives *inside the repo* (`tools/gbdk/`, gitignored, fetched once with `make setup-gbdk`). No global install, reproducible builds — `GBDK_URL` pins the exact tarball so fresh clones get the identical compiler.
- `CSOURCES`: the complete file list. **Only these files are compiled.** Add a new `.c` and forget it here → its functions become `undefined reference` (§2 error 1). Generated files are listed like hand-written ones — the compiler cannot tell the difference, and must not need to.

```make
# -msm83:gb = Game Boy target. -Wm-yn = ROM title.
# -Wl-yt0x03 = MBC1+RAM+BATTERY (battery save), -Wl-ya1 = 8KB SRAM.
LCCFLAGS = -msm83:gb -Wm-yn"SUDOKU" -Wl-yt0x03 -Wl-ya1
```

| Flag | Layer | Meaning |
|------|-------|---------|
| `-msm83:gb` | compiler | Target the LR35902/Game Boy (full platform syntax; bare `-mgb` does not work with this `lcc`) |
| `-Wm-yn"SUDOKU"` | makebin (`-Wm-`) | ROM title in the cartridge header (shows in emulator menus; `make check` does not assert it, humans read it) |
| `-Wl-yt0x03` | linker (`-Wl-`) | Cartridge type `0x03` = MBC1 + RAM + BATTERY (the hardware feature the whole save system needs) |
| `-Wl-ya1` | linker | 8 KB SRAM size nibble |

`-Wm-` forwards to `makebin` (ROM header tool), `-Wl-` to the linker — `lcc` is a driver (chapter 02 §8, §5 below), and these prefixes choose which backstage tool receives the option.

```make
all: $(ROM)

$(ROM): $(CSOURCES) src/*.h
	mkdir -p build
	$(LCC) $(LCCFLAGS) -o $@ $(CSOURCES)
```

`all` is the default target (`make` = `make all`). The ROM depends on every source and header: any change rebuilds everything (coarse but correct for 9 files; §6 refines it). `$@` = `build/sudoku.gb`. One `lcc` invocation compiles *and* links all nine files in one go.

```make
test-host: tests/test_host.c src/board.c src/puzzles.c src/puzzles_gen.c
	gcc -Wall -Wextra -Isrc -o /tmp/sudoku_test tests/test_host.c src/board.c src/puzzles.c src/puzzles_gen.c && /tmp/sudoku_test
```

Note what is **excluded**: `main.c` (needs GBDK `main` signature/flow), `ui.c`/`tiles*.c` (VRAM/GBDK calls), `input.c` (`joypad()`), `save.c` (`ENABLE_RAM`, SRAM address) — everything touching hardware. Only the portable logic is tested on PC, which is precisely the file set with zero `<gb/…>` includes. `-Isrc` tells `gcc` where `"board.h"` lives when compiling from `tests/`. The trailing `&& /tmp/sudoku_test` runs the tests only if compilation succeeded — `;` there would run (stale or missing) binaries after failures.

```make
check: $(ROM)
	python3 -c "import os; s=os.path.getsize('$(ROM)'); ..."   # size == 32768
	python3 -c "d=open('$(ROM)','rb').read(); ..."             # logo, cart 0x03, banks, SRAM

test-emulator: $(ROM)
	python3 tools/smoke_pyboy.py $(ROM)

regen-puzzles:
	python3 tools/gen_puzzles.py --seed=20260916

regen-tiles:
	python3 tools/gen_tiles.py

setup-gbdk:
	mkdir -p tools
	curl -L -o /tmp/gbdk-macos-arm64.tar.gz $(GBDK_URL)
	tar -xzf /tmp/gbdk-macos-arm64.tar.gz -C tools

clean:
	rm -rf build/*.gb build/*.ihx build/*.cdb build/*.map build/*.noi build/*.sym /tmp/sudoku_test

.PHONY: all run check test-host test-emulator regen-puzzles regen-tiles setup-gbdk clean
```

`make check` re-verifies the cartridge header with Python: exact 32 KB size, Nintendo logo bytes at `0x104` (48 bytes the boot ROM verifies — a wrong logo locks real hardware), cart type and SRAM size nibbles. `setup-gbdk` runs once per fresh clone (network + extract into gitignored `tools/gbdk/`). `make clean` deletes artefacts (all gitignored, including linker by-products `.ihx/.map/.sym` — the map file lists every symbol's address and is worth a look after linking: it shows exactly how the 32 KB filled up).

## 4. Compiler vs linker vs `make` responsibilities

| Tool | Reads | Writes | Fails when |
|------|-------|--------|------------|
| compiler (`gcc` / SDCC inside `lcc`) | one `.c` + headers | one `.o` (or direct ROM with one-shot builds) | syntax/type errors, warnings |
| assembler | `.s` assembly | `.o` object | invalid mnemonics (rare by hand) |
| linker (+ `makebin` inside `lcc`) | all `.o` + libraries, header flags | one ROM/executable | `undefined`/`multiple` symbols, size overflow |
| `make` | `Makefile` timestamps + exit codes | runs the above in order | any step returns non-zero |

`make` itself understands nothing about C — it only re-runs commands whose inputs are newer than their outputs, and stops on the first failure. That ignorance is its strength: the same engine drives generation (`regen-*`), verification (`check`, `test-*`), and setup.

## 5. GBDK in one paragraph expanded (what `lcc` hides)

`lcc` is a driver: preprocessor → SDCC C compiler → assembler → linker → `makebin` (stamps Nintendo logo, header/global checksums, cartridge bytes). SDCC is stricter than `gcc`: no variable-length arrays, minimal heap support, `enum`s are `int`, some standard-library functions missing or ROM-heavy (no float `printf`, limited `<string.h>`), declarations-before-statements historically required (hence the house style from chapter 05 §2). Code clean under `gcc -Wall -Wextra` almost always passes SDCC; the reverse is not guaranteed — which is why the repo demands zero warnings on *both*, and why `test-host` (gcc) and `make` (SDCC) are complementary rather than redundant gates.

## 6. Scaling pattern: per-file rules (recognise, not needed here)

When projects outgrow one-shot builds, each `.c` gets its own rule so only changed files recompile:

```make
# Pattern (illustrative — this repo intentionally stays one-shot):
build/%.o: src/%.c src/*.h
	mkdir -p build
	$(LCC) $(LCCFLAGS) -c $< -o $@

$(ROM): $(CSOURCES:src/%.c=build/%.o)
	$(LCC) $(LCCFLAGS) -o $@ $^
```

`%.o: %.c` = "any object from its source"; `-c` = compile only (no link); `$<`/`$^` = first/all prerequisites. Nine files build in seconds one-shot, so the added complexity buys nothing here — YAGNI applied to the build itself. Know the pattern for your tenth-thousand-line project; appreciate its absence in this one.

Next: `10-memory-rom-ram-stack.md` — where every byte lives and why.
