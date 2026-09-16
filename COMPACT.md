# COMPACT.md — Sudoku GB: state of work and next steps

> Snapshot for resuming work. 100 levels, no passwords, precomputed
> tiles, LCD-safe transitions. Build is green AND headless-verified
> with PyBoy (`make test-emulator`: 26 checks pass). All text/code in
> English.

---

## 1. Current state in one paragraph

`/Users/giacomo/dev/sudoku-gb` is a complete Sudoku for Game Boy Classic
(DMG) in C with GBDK-2020 4.5.0. The ROM builds clean (`make`, zero
warnings) into `build/sudoku.gb` (ROM ONLY `0x00`, Nintendo logo OK,
~27.8KB used of 32KB) and passes PC host tests plus a headless PyBoy
smoke test. Game screen = **fullscreen 9x9 grid** (16x16 px cells,
chunky 2x digits, black box/frame lines, dark-gray inner lines and
player digits). Dynamics: D-Pad moves, **A = digit-pick mode** (Up/Down
pick blinking digit, A confirm, B back), B erases, START menu has status
+ RESUME / HINT / RESTART / TITLE. **No game over** (mistakes tallied
only). Boot = **free level select** (100 levels, 10 pages of 10,
`*` = beaten this session). Original clues are black; player digits and
HINT reveals are dark gray (hints stay locked).

---

## 2. Toolchain and build

- macOS ARM. `make`, `gcc`, PyBoy (headless verify), mGBA (user visual).
- `sdcc 4.6.0` via Homebrew. GBDK-2020 4.5.0 in `tools/gbdk/`
  (gitignored; `make setup-gbdk` downloads `gbdk-macos-arm64.tar.gz`).
- Flags: `-msm83:gb -Wm-yn"SUDOKU"` → cart type `0x00` (ROM ONLY, 32KB).

```bash
make setup-gbdk   # once per fresh clone
make              # build build/sudoku.gb
make run          # open in mGBA
make check        # size <= 32KB + Nintendo logo + cart type
make test-host    # gcc tests (logic, solutions, intro levels)
make test-emulator # PyBoy smoke test (needs: pip install pyboy pillow)
make regen-puzzles # regenerate src/puzzles_gen.c (seed 20260916)
make regen-tiles   # regenerate src/tiles_gen.c
make clean
```

Last verified: zero warnings; `check` 32768 bytes / logo OK / cart 0x00;
`test-host` ALL PASSED; `test-emulator` SMOKE PASSED (26 checks);
real usage ~27.8KB (~4.9KB slack). Both generators deterministic
(double run = identical md5).

---

## 3. Repository map

```text
README.md      build, controls, rules, LCD safety, file list
PLAN.md        full plan (password sections marked [HISTORIC])
Makefile       GBDK build + setup-gbdk + test-emulator

src/
  types.h        constants: 9x9, 100 levels, screen 20x18
  puzzles.h      Puzzle { Difficulty, givens[82], solution[82] } + table
  puzzles_gen.c  GENERATED: 100 givens + solutions (do not edit)
  puzzles.c      difficulty_name()
  board.h/.c     cells[81] + given[81] + hinted[81] + mistakes (tally only)
  input.h/.c     joypad edge detection + D-Pad auto-repeat
  tiles.h/.c     grid indexing + VRAM copy (artwork in tiles_gen.c)
  tiles_gen.c    GENERATED: 230 grid + 4 cursor tiles (do not edit)
  ui.h/.c        tile-drawn text menus + grid screens + transitions
  main.c         SELECT -> GAME <-> PAUSE -> WIN
tools/gen_puzzles.py  full grid + dig with uniqueness check (cap 2)
tools/gen_tiles.py    precomputed 16x16 artwork (19 contents x variants)
tools/smoke_pyboy.py  headless checks (no display needed)
tests/test_host.c     gcc tests (no GBDK includes) for logic modules
```

Clean-code invariants (keep them):

- `board`, `puzzles` **never include `<gb/gb.h>`**.
- One responsibility per module; one handler per state in `main.c`;
  one screen per function in `ui.c` (+ small delta helpers).
- No stdio/console in `ui.c`: text = font tiles via `set_bkg_*`.
- Short comment on every file/function; no dead code.

---

## 4. Key design decisions

### 4.1 Fullscreen tile grid (precomputed)

- Cell = 16x16 px = 2x2 BG tiles at map (1 + col*2, row*2) → 144x144 px,
  full screen height, 1-tile margins carrying the outer frame.
- Digit: 3x5 bitmaps scaled 2x → 6x10 chunky glyph. All lines 2px:
  black box gaps/frame, dark-gray inner lines (no thin/bold wobble).
- 19 contents (empty + 9 black givens + 9 gray user) x quadrant
  variants = 228 tiles + 2 margin tiles at VRAM 0-229 (game screen owns
  ALL BG tiles; menus reload the font). Cursor = 4 sprites, tiles 240+.
- `tools/gen_tiles.py` bakes the art; `tiles_load_grid()` hand-copies
  it (GBDK `set_bkg_data` silently drops tiles 0-114 on big loads).
- Deterministic: regen twice = identical file.

### 4.2 LCD safety (real DMG hardware)

- Clearing LCDC.7 outside VBlank can damage the LCD (Pan Docs). The
  ONLY LCD-off path is `screen_begin()`: GBDK `display_off()` (waits
  for VBlank) + mode bits with LCD bit kept clear. `screen_end()` does
  `DISPLAY_ON` once (re-enabling is always safe).
- Static audit: one `LCDC_REG` write (0x13, LCD already off), no direct
  1->0 clear, no SHOW_/HIDE_ toggling, no custom ISRs, VBlank always
  enabled, raw VRAM copy only with LCD off. LCD-on updates use GBDK
  STAT-safe calls (`set_bkg_*`, `fill_bkg_rect`) + shadow OAM.
- Menu entry costs ~15 frames (font decompress, LCD off = invisible);
  navigation is instant (see 4.3).

### 4.3 Delta menu redraws (no arrow flash)

- `ui_select` / `ui_pause` draw fully once (LCD off). Up/Down only move
  the `>` marker (`ui_select_cursor` / `ui_pause_cursor`: 2 tile
  writes, LCD on). Page change redraws page line + 10 rows only.
- Game cursor is sprite-only; the background map never changes on move.
- Edit-mode digit blink is intentional (tiles only, board untouched).

### 4.4 Cell origins (hint shading)

- `board_is_original()` = clue (black, never editable).
- `board_is_locked()` = clue OR hint (not editable: no edit/erase).
- Player digits and hints render gray (`!board_is_original`);
  erase only clears unlocked player digits.

### 4.5 Levels, select, hint, win

- 100 levels: 10 intro EASY (48 givens) + 24 EASY (42) + 33 MEDIUM (34)
  + 33 HARD (29). Unique solutions, generator-verified (cap-2 solver),
  seed 20260916. Host tests assert solution validity + intro givens.
- Select: 10 pages x 10 (`SELECT_PAGE_COUNT`), Up/Down row, Left/Right
  page, A play. `completed[100]` session-only.
- HINT: cursor cell if empty+editable else first such cell; writes the
  solution digit, locks it, redraws; skips the grid redraw on a win.
- Win = full grid (unique puzzles + rejected conflicts ⇒ full is valid).
  Win screen is drawn on ST_WIN entry (deferred `need_win_draw`), shows
  `LEVEL NNN CLEAR!` + mistakes (or completion), A continues.

---

## 5. Verification checklist (run after any change)

```bash
make clean && make          # expect: exit 0, zero warnings
make check                  # expect: Size OK, logo OK, cart 0x0
make test-host              # expect: ALL HOST TESTS PASSED
make test-emulator          # expect: SMOKE PASSED (26 checks)
make run                    # visual check by the user (has display)
```

Smoke test covers: boot/select text, select+pause arrows never blank
and never toggle LCDC.7, page tiles intact, grid+margins on entry,
sprite-only cursor, blink show/erase, gray+locked hint, readable win,
advance to next level.

---

## 6. Session facts worth remembering

- Everything in English (docs, code, comments, UI strings).
- Clean code, YAGNI, explicit well-commented code; no dead code.
- `lcc` needs full platform syntax `-msm83:gb`.
- Font mapping: GBDK `font_ibm` = tiles 0-95, ASCII c = tile c-32.
- `name[k]` past a NUL reads the next ROM literal (bug caught: 'M' of
  MEDIUM after "EASY") — always stop at NUL when padding rows.
- PyBoy headless: `window='null'`, `button(btn, frames)` + `tick()`;
  presses landing inside synchronous transitions are missed — tests
  must retry/poll (esp. after menu entries: font load ~15 frames).
- PyBoy `memory[0xFF40]` reads 0x83 while rendering proves tile area
  0x8000 — register introspection there is unreliable; trust pixels.
- Emulator automation works here (pyboy+pillow installed); only human
  `make run` eyeballing needs the user.
