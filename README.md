# Sudoku GB

Sudoku for Game Boy Classic (DMG). 12 levels, passwords, no save, no audio.
Toolchain: GBDK-2020 (vendored in `tools/gbdk/`). Everything in English.

## Build

```bash
make              # build/sudoku.gb (32KB ROM ONLY)
make run          # open in mGBA
make check        # header + size checks
make test-host    # PC logic tests (gcc)
make passwords    # print the 12 level passwords
make regen-puzzles # regenerate puzzles (seed 20260916)
make clean
```

## Controls

- D-Pad: move cursor (wraps at edges, auto-repeat when held)
- Up/Down on editable cell: change proposed digit (ENTER row)
- Up/Down/Left/Right on given cell: move cursor
- A: confirm digit / menu OK (wrong digit = 1 mistake, move rejected)
- B: erase player digit / back
- START: pause (RESUME / RESTART / TITLE)

## Rules

- 3 mistakes = game over (RETRY / TITLE).
- Full grid = win (conflicts are always rejected, puzzles are unique).
- Win shows the 4-digit password for the next level.
- Title: NEW GAME (level 1) or PASSWORD (jump to any level).

## Passwords

| Level | Difficulty | Password |
|------:|------------|----------|
| 01 | EASY | 7303 |
| 02 | EASY | 2184 |
| 03 | EASY | 4745 |
| 04 | EASY | 4650 |
| 05 | MEDIUM | 7211 |
| 06 | MEDIUM | 1580 |
| 07 | MEDIUM | 4141 |
| 08 | MEDIUM | 2974 |
| 09 | HARD | 5535 |
| 10 | HARD | 9872 |
| 11 | HARD | 2497 |
| 12 | HARD | 2338 |

## Files

- `src/types.h` — constants. `src/board.*` — rules. `src/passwords.*` — codes.
- `src/puzzles.*` + `src/puzzles_gen.c` (generated, do not edit).
- `src/input.*` — joypad debounce. `src/ui.*` — screens + tile grid + sprite cursor.
- `src/tiles.*` — procedural 8x8 grid/frame/cursor tiles (no assets).
- `src/main.c` — state machine. `tools/gen_puzzles.py` — generator.
- `tests/test_host.c` — gcc tests. `PLAN.md` — full plan.
