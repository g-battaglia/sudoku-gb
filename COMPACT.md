# COMPACT.md — Sudoku GB: state of work and next steps

> Snapshot for resuming work. Written after the fullscreen redesign
> (16x16 grid, edit-mode dynamics, level select, hint, no game over).
> Build is green; **visual confirmation on a real emulator is still
> pending** (this environment has no display). All text/code in English.

---

## 1. Current state in one paragraph

`/Users/giacomo/dev/sudoku-gb` is a complete Sudoku for Game Boy Classic
(DMG) in C with GBDK-2020. The ROM builds clean (`make`, zero warnings)
into `build/sudoku.gb` (ROM ONLY, Nintendo logo OK, ~11.8KB used of
32KB) and passes the PC host tests. Game screen = **fullscreen 9x9 grid**
(16x16 px cells, chunky 2x digits, baked 1px/2px lines, no text).
Dynamics: D-Pad moves, **A = digit-pick mode** (Up/Down pick blinking
digit, A confirm, B back), B erases, START menu has status + RESUME /
HINT / RESTART / TITLE. **No game over** (mistakes tallied only).
Boot = **free level select** (all 12, `*` = beaten); passwords only
restore the `*` marks and jump. Everything is committed except the
work described here as pending — check `git status`.

---

## 2. Toolchain and build

- macOS ARM. `make`, `gcc`, `mGBA 0.10.5`, `SameBoy.app`.
- `sdcc 4.6.0` via Homebrew. GBDK-2020 4.5.0 vendored in `tools/gbdk/`
  (gitignored, local build tooling only).
- Flags: `-msm83:gb -Wm-yn"SUDOKU"` → cart type `0x00` (ROM ONLY, 32KB).

```bash
make                # build build/sudoku.gb
make run            # open in mGBA (falls back to SameBoy / mgba CLI)
make check          # size <= 32KB + Nintendo logo + cart type
make test-host      # gcc tests for the hardware-free logic
make passwords      # print the 12 level passwords (calls test-host)
make regen-puzzles  # regenerate src/puzzles_gen.c (seed 20260916)
make clean
```

Last verified: `make` zero warnings; `check` 32768 bytes / logo OK /
cart 0x00; `test-host` ALL PASSED (validity, rules, mistakes saturate,
hint lock, password roundtrip); real usage ~11.8KB (21KB slack).

---

## 3. Repository map

```text
PLAN.md        full plan (current: fullscreen, select, hint, no game over)
README.md      build, controls, rules, select/password meaning, file list
Makefile       GBDK build + check + host tests + puzzle regen

src/
  types.h        constants: 9x9, 12 levels, passwords, screen 20x18
  puzzles.h      Puzzle { Difficulty, givens[82], solution[82] } + table
  puzzles_gen.c  GENERATED: 12 givens + 12 solutions (do not edit)
  puzzles.c      difficulty_name()
  passwords.h/.c formula (((level+1)*7919 + 104729) ^ 0xBEEF) % 10000
  board.h/.c     cells[81] + locked[81] + mistakes (tally only) + reveal
  input.h/.c     joypad edge detection + D-Pad auto-repeat
  tiles.h/.c     procedural 16x16 cell tiles + 4-sprite cursor tiles
  ui.h/.c        fullscreen grid + text menus (select/pwd/pause/win)
  main.c         SELECT -> PASSWORD? -> GAME <-> PAUSE -> WIN

tools/gen_puzzles.py  random full grid + dig with uniqueness check (cap 2)
tests/test_host.c     gcc tests (no GBDK includes) for logic modules
```

Clean-code invariants (keep them):

- `board`, `passwords`, `puzzles` **never include `<gb/gb.h>`**.
- One responsibility per module; one handler per state in `main.c`;
  one screen per function in `ui.c`.
- Short comment on every file/function; no dead code.

---

## 4. Key design decisions

### 4.1 Fullscreen tile grid

- Cell = 16x16 px = 2x2 BG tiles at map (1 + col*2, row*2) → 144x144 px,
  full screen height, 1-tile margins left/right. No header/footer/text.
- Digit: 3x5 bitmaps scaled 2x → 6x10 chunky glyph, cols 5-10 / rows 3-12,
  all black (newspaper style). Borders baked per cell: 2px outer frame
  (row/col 0), 2px box gaps (after rows/cols 2,5,8), 1px thin lines.
  Each line drawn exactly once from one side → no thin/bold wobble.
- VRAM: TL 96-135, TR 136-175, BL 176-215, BR 216-255 = 10 contents x
  4 variants per quadrant = 160 tiles (all free BG tiles; font keeps 0-95).
  `grid_cell_tiles(value, row, col, out[4])` computes the 4 indices.
- Verified with a Python re-render of `paint_cell` (`/tmp/grid_check.png`
  pattern): frame/box/thin lines uniform, digits legible.

### 4.2 Sprites, preview, feedback (no text in game)

- Cursor = 4 sprites (8x8 corners, tiles 240-243, ids 0-3), 16x16 outline.
- Digit-pick mode: picked digit blinks in the cell every 32 frames via
  `ui_preview` (tiles only, board untouched; `preview_update()` in main).
- Rejected digit: restore old value, mistake+1, cursor hidden 24 frames.
- Locked cell (A edit / B erase on given): cursor hidden 20 frames.
- Mistake count + empties left shown in START menu only.

### 4.3 Select, passwords, hint, win

- Boot = SELECT LEVEL: 12 rows (`> 03 MEDIUM  *`), Up/Down + A plays any;
  SELECT button opens password entry; footer `A PLAY  SELECT PWD`.
- `completed[12]` in main.c (session-only, zero at boot). Win sets it.
- Password for level N (`found`): marks 0..N-1 complete, cursor jumps to N,
  back to select. Unlocks nothing — every level always playable.
- HINT (START menu): cursor cell if editable, else first empty editable
  cell; writes solution digit + `board_reveal()` (locked); checks win.
- Win = full grid (unique puzzles + rejected conflicts ⇒ full is valid).
  Win screen shows next password (to restore `*` later) or completion.

---

## 5. Verification checklist (run after any change)

```bash
make clean && make          # expect: exit 0, zero warnings
make check                  # expect: 32768 bytes, Size OK, logo OK, cart 0x00
make test-host              # expect: ALL HOST TESTS PASSED
make run                    # visual check on a real emulator (not possible here)
```

Manual playthrough to re-confirm:

1. Boot → SELECT LEVEL lists 12 levels with difficulties, no `*`.
2. A on level 01 → fullscreen grid, thick frame + box lines, thin cells.
3. D-Pad moves + wraps; cursor outline sits exactly on the cell.
4. A on empty → digit blinks; Up/Down changes it; B cancels cleanly.
5. A + Up/Down + A places digit; wrong digit rejected, play continues.
6. B erases player digit; B/A on given blinks cursor, board unchanged.
7. START → menu shows LEVEL/MISTAKES/LEFT + 4 items + help; HINT fills
   and locks a cell; RESUME/B redraws the grid intact.
8. Many mistakes → still playing (no game over).
9. Solve level 01 → `LEVEL 01 CLEAR!` + password; select shows `01 *`.
10. SELECT + password for level 05 → levels 01-04 `*`, cursor on 05.
11. ROM sanity: DMG mode, 32KB, no `.sav` needed.

---

## 6. Ideas only if requested (P1+, each has a cost)

- Blinking cursor / error tile variants (no free BG tiles left: 96-255
  full — would need a second tileset load per screen).
- Given vs player shade distinction (same tile-budget problem).
- Pencil marks (needs mini-digit tiles + WRAM notes bitmask).
- Timer / best time (runtime state only, cheap).
- More levels (82→ solutions are 162 bytes/level now; ROM has room).
- Audio (needs a sound engine; GBDK has no sfx API).
- Real save (MBC + SRAM + battery; explicitly rejected before).
- SGB/CGB extras (user asked DMG no-color).

---

## 7. Session facts worth remembering

- Everything in English (docs, code, comments, UI strings).
- Clean code, YAGNI, explicit well-commented code; no dead code.
- Toolchain vendored but gitignored: fresh clone must re-download
  `gbdk-macos-arm64.tar.gz` 4.5.0 into `tools/gbdk/` before `make`.
- `lcc` needs full platform syntax `-msm83:gb`.
- SDCC `warning 158`: avoid `BASE + N` arithmetic on tile defines —
  use plain literals / computed uint8 values.
- ROM ONLY pads to 32KB with 0xFF: check real usage by last non-0xFF
  byte, not file size. Solutions (+972B) fit with ~21KB slack.
- Emulator automation impossible here (no display): tile logic was
  validated by re-rendering `paint_cell` in Python and viewing the PNG;
  all gameplay/visual claims need `make run` by the user.
