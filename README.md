# Sudoku GB

Sudoku for Game Boy Classic (DMG). 12 levels, passwords, hints, no save,
no audio, no game over (infinite play, mistakes only tallied).
Toolchain: GBDK-2020 (vendored in `tools/gbdk/`). Everything in English.

## Screen

Fullscreen 9x9 grid (16x16 px cells, chunky 2x digits, 2px box lines).
No header/footer in game: level, mistakes and help live in the START menu.

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

- Select screen: Up/Down choose any of the 12 levels, A plays,
  SELECT opens password entry.
- D-Pad: move cursor (wraps at edges, auto-repeat when held)
- A on a cell: digit-pick mode (picked digit blinks in the cell)
  - Up/Down: pick digit 1-9, A: confirm, B: back (no change)
  - Wrong digit = rejected + 1 mistake, keep picking
- B: erase player digit (locked cells blink the cursor)
- START: menu (RESUME / HINT / RESTART / TITLE) + status + help

## Rules

- No game over: play forever, mistakes are only counted (START menu).
- HINT reveals the true digit of a cell and locks it (free, unlimited).
- Full grid = win (conflicts are always rejected, puzzles are unique).
- Win shows the 4-digit password for the next level (to restore `*`).
- Boot shows all 12 levels free: A plays any of them, `*` = beaten.
- A password never unlocks: it only marks levels 1..N-1 `*` and jumps
  to level N (progress display is session-only, there is no save).

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
- `src/tiles.*` — procedural 16x16 grid tiles + 4-sprite cursor (no assets).
- `src/main.c` — state machine. `tools/gen_puzzles.py` — generator.
- `tests/test_host.c` — gcc tests. `PLAN.md` — full plan.
