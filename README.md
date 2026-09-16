# Sudoku GB

Sudoku for Game Boy Classic (DMG). 100 levels, hints, no save,
no audio, no game over (infinite play, mistakes only tallied).
Toolchain: GBDK-2020 4.5.0 (vendored in `tools/gbdk/`, gitignored;
run `make setup-gbdk` once on a fresh clone). Everything in English.

## Screen

Fullscreen 9x9 grid (16x16 px cells, chunky 2x digits, uniform 2px
lines: black box/frame lines, dark-gray inner lines). No header/footer
in game: level, mistakes and help live in the START menu.
Original clues are black; player digits and HINT reveals are dark gray.

## Build

```bash
make setup-gbdk   # once: download GBDK 4.5.0 into tools/gbdk/
make              # build/sudoku.gb (32KB ROM ONLY)
make run          # open in mGBA
make check        # header + size checks
make test-host    # PC logic tests (gcc)
make test-emulator # headless smoke test (needs: pip install pyboy pillow)
make regen-puzzles # regenerate puzzles (seed 20260916, deterministic)
make regen-tiles   # regenerate grid tiles (deterministic)
make clean
```

## Controls

- Select screen: Up/Down choose a row, Left/Right change page
  (10 pages of 10 levels), A plays. `*` = beaten this session.
- D-Pad: move cursor (wraps at edges, auto-repeat when held)
- A on a cell: digit-pick mode (picked digit blinks in the cell)
  - Up/Down: pick digit 1-9, A: confirm, B: back (no change)
  - Wrong digit = rejected + 1 mistake, keep picking
- B: erase player digit (locked cells blink the cursor)
- START: menu (RESUME / HINT / RESTART / TITLE) + status + help

## Rules

- 100 free levels from boot: 10 introductory EASY (48 givens),
  then 24 EASY (42) + 33 MEDIUM (34) + 33 HARD (29).
- Every puzzle has a unique solution (generator-verified).
- No game over: play forever, mistakes are only counted (START menu).
- HINT reveals the true digit of a cell and locks it (free, unlimited).
  Hinted cells render gray like player digits but stay locked.
- Full grid = win (conflicts are always rejected, puzzles are unique).
- Progress marks are session-only: there is no save (ROM ONLY).

## LCD safety (real hardware)

The DMG LCD can be damaged by stopping it outside VBlank, so the LCD
is stopped exactly once (boot init). Tile patterns are resident
(grid at `0x8000`, font at `0x9000`, cursor sprites), and every full
screen is drawn into the hidden background map while the LCD keeps
showing the old one; one LCDC write then swaps map + tile mode +
sprites at frame start. No white flash, no half-drawn frame, no stale
sprites. Small updates (menu marker, cells, cursor) write straight to
the visible screen. Menus use no stdio: text is written as font tiles.

## Files

- `src/types.h` — constants. `src/board.*` — rules + cell origins.
- `src/puzzles.*` + `src/puzzles_gen.c` (generated, do not edit).
- `src/input.*` — joypad debounce. `src/ui.*` — screens + tile grid.
- `src/tiles.*` + `src/tiles_gen.c` (generated) — precomputed grid art.
- `src/main.c` — state machine. `tools/gen_puzzles.py`,
  `tools/gen_tiles.py` — deterministic generators.
- `tools/smoke_pyboy.py` — headless emulator smoke test.
- `tests/test_host.c` — gcc tests. `PLAN.md` — full plan.
- `DEVELOPMENT.md` — codebase guide (start here if C is new to you).
