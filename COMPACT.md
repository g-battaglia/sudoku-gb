# COMPACT.md — Sudoku GB: state of work and next steps

> Snapshot for resuming work. 300 levels (100 EASY + 100 MEDIUM + 100
> HARD, selected at boot), packed in ROM, double-buffered atomic
> screens, battery save (SRAM). Build is green AND headless-verified
> with PyBoy (`make test-emulator`: every transition frame-checked,
> including save → power-cycle → LOAD). All text/code in English.

---

## 1. Current state in one paragraph

`/Users/giacomo/dev/sudoku-gb` is a complete Sudoku for Game Boy Classic
(DMG) in C with GBDK-2020 4.5.0. The ROM builds clean (`make`, zero
warnings) into `build/sudoku.gb` (MBC1+RAM+BATTERY `0x03`, 8KB SRAM,
Nintendo logo OK, ~28KB used of 32KB) and passes PC host tests plus
a headless PyBoy smoke test. Game screen = **fullscreen 9x9 grid**
(16x16 px cells, chunky 2x digits, black box/frame lines, dark-gray
inner lines and player digits). Dynamics: D-Pad moves the cursor
(solid 2px ring), **A = digit-pick mode** (Up/Down and Left/Right
pick the blinking digit, A confirm, B back), B erases, START menu
has centered status + RESUME / HINT / SAVE / PLAY AGAIN / MENU +
3-line help. **No game over** (mistakes tallied only). **A+B+START+
SELECT = soft reset** (jump to 0x0100: RAM cleared like a power
cycle, battery SRAM untouched). Boot =
**mode select** (EASY / MEDIUM / HARD + LOAD when a valid battery
save exists) then **free level select** (100 levels of that mode,
10 pages of 10, `*` = beaten, `<` marks the cursor row, `B` returns
to the mode). SAVE stores the game in progress; every win stores
the marks. Original clues are black; player digits and HINT reveals
are dark gray (hints stay locked).

---

## 2. Toolchain and build

- macOS ARM. `make`, `gcc`, PyBoy (headless verify), mGBA (user visual).
- `sdcc 4.6.0` via Homebrew. GBDK-2020 4.5.0 in `tools/gbdk/`
  (gitignored; `make setup-gbdk` downloads `gbdk-macos-arm64.tar.gz`).
- Flags: `-msm83:gb -Wm-yn"SUDOKU" -Wl-yt0x03 -Wl-ya1` → cart type
  `0x03` (MBC1 + RAM + battery), 8KB SRAM, ROM still 32KB (2 banks).

```bash
make setup-gbdk   # once per fresh clone
make              # build build/sudoku.gb
make run          # open in mGBA
make check        # size 32KB + logo + cart 0x03 + 8KB SRAM
make test-host    # gcc tests (logic, solutions, marks, restore)
make test-emulator # PyBoy smoke test (needs: pip install pyboy pillow)
make regen-puzzles # regenerate src/puzzles_gen.c (seed 20260916)
make regen-tiles   # regenerate src/tiles_gen.c
make clean
```

Last verified: zero warnings; `check` 32768 bytes / logo OK / cart 0x03
/ SRAM 8KB; `test-host` ALL PASSED; `test-emulator` SMOKE PASSED
(171 checks, incl. save → power-cycle → LOAD, win-marks and the
A+B+START+SELECT soft reset);
real usage ~28KB (~4KB slack). Both generators deterministic
(double run = identical md5).

---

## 3. Repository map

```text
README.md      build, controls, rules, LCD safety, file list
DEVELOPMENT.md codebase guide for C beginners (start there if new)
PLAN.md        full plan
Makefile       GBDK build + setup-gbdk + test-emulator

src/
  types.h        constants: 9x9, screen 20x18
  puzzles.h      Puzzle { solution[41], givens_mask[11] } (52B, packed)
                 + puzzle_solution/given + difficulty_name; 300 levels
  puzzles_gen.c  GENERATED: 300 packed levels (do not edit)
  puzzles.c      difficulty_name()
  board.h/.c     cells[81] + origin[81] + mistakes (tally only)
  input.h/.c     joypad edge detection + D-Pad auto-repeat
  tiles.h/.c     resident VRAM layout + one-time copy (art in tiles_gen.c)
  tiles_gen.c    GENERATED: 230 grid + 4 cursor tiles (do not edit)
  ui.h/.c        hidden-map draws + atomic present + delta updates
  main.c         SELECT -> GAME <-> PAUSE -> WIN
tools/gen_puzzles.py  full grid + dig with uniqueness check (cap 2)
tools/gen_tiles.py    precomputed 16x16 artwork (19 contents x variants)
tools/smoke_pyboy.py  per-frame transition checks (no display needed)
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

### 4.2 LCD safety + atomic screens (real DMG hardware)

- Clearing LCDC.7 outside VBlank can damage the LCD (Pan Docs). The
  ONLY `display_off()` is in `ui_init()` (waits for VBlank, boot init).
  After that the LCD bit is never cleared: every screen is drawn into
  the HIDDEN bg map (`set_tiles`/`set_vram_byte`, both WAIT_STAT
  guarded per GBDK source, so LCD-on writes are safe), then one LCDC
  write swaps map + tile mode + OBJ enable right after `vsync()`.
- Shadow OAM is always prepared BEFORE presenting (cursor placed for
  game, parked + OBJ-off for menus), so map and sprites land on the
  same frame: no white flash, no mixed frame, no stale cursor.
- Static audit: LCDC written only in init (0x13, off) and in present
  (0x81/0x89 menu, 0x93/0x9B game — bit 7 always set); no custom
  ISRs; VBlank always enabled; raw VRAM copies only with LCD off.

### 4.3 Delta updates (no arrow flash)

- Full screens draw once into the hidden map. Up/Down only move the
  `>` marker (`ui_select_cursor` / `ui_pause_cursor`: 2 tile writes to
  the visible map). Page change redraws page line + 10 rows only.
- Game cursor is sprite-only; the background map never changes on move.
- Edit-mode digit blink is intentional (tiles only, board untouched).

### 4.4 Cell origins (hint shading)

- One `origin[81]` array: PLAYER (gray, editable), GIVEN (black,
  locked), HINT (gray, locked). Saves 81 WRAM bytes vs two arrays.
- `board_is_original()` = GIVEN; `board_is_locked()` = not PLAYER.
- Erase only clears unlocked player digits.

### 4.5 Levels, modes, select, hint, win

- 300 levels = 100 EASY + 100 MEDIUM + 100 HARD (EASY starts with 10
  intro puzzles at 48 givens). Unique solutions, generator-verified
  (cap-2 solver), seed 20260916. Packed 52 bytes/level: solution
  nibbles (41) + givens mask (11) = 15.6KB total; the old char format
  (165B/level) could never fit 300 levels in 32KB.
- Difficulty = index range (level/100); host tests assert solution
  validity + intro givens.
- Select: mode screen first (Up/Down + A; LOAD row when a valid
  battery save exists), then 10 pages x 10 of that mode (Up/Down row,
  Left/Right page, A play, B back to mode). `marks[38]` bitmap is the
  single source of truth (`*` = beaten, `<` = cursor row, `<` on the
  number means selected); `DONE x/100` counts the current mode.
- HINT: cursor cell if empty+editable else first such cell; writes the
  solution digit (puzzle_solution), locks it, redraws; skips the grid
  redraw on a win.
- Win = full grid (unique puzzles + rejected conflicts ⇒ full is
  valid). Shows the level number WITHIN the mode (`LEVEL 001/100`);
  `is_last` = mode completed; A continues to the next level.

### 4.6 Battery save (SRAM)

- Cart `0x03` MBC1+RAM+BATTERY, 8KB SRAM (`-Wl-yt0x03 -Wl-ya1`).
- One slot at `0xA000`: magic `SUDK` + version + game_active + level
  + values[81] + origins[81] + mistakes + marks[38] + 8-bit checksum
  (0xD2 bytes). `save.c` opens/closes the MBC latch around every
  access (ENABLE_RAM / DISABLE_RAM).
- SAVE (START menu item) writes the slot with game_active=1 and shows
  the GAME SAVED screen; A/B returns to the game.
- Every win rewrites the slot with game_active=0 (marks persist even
  without an explicit save).
- Boot: `save_read` validates; marks restored into RAM and the boot
  menu gains LOAD. LOAD re-reads SRAM: game_active=1 → exact board
  restored (`board_restore`, values + origins + mistakes); else →
  select screen of that mode with marks/stars.
- `marks_*` helpers live in `board.c` (PC-testable, same layout as
  SRAM: zero conversion). Host tests cover set/get/count boundaries
  and restore round trip.

---

## 5. Verification checklist (run after any change)

```bash
make clean && make          # expect: exit 0, zero warnings
make check                  # expect: Size OK, logo OK, cart 0x03, SRAM 8KB
make test-host              # expect: ALL HOST TESTS PASSED
make test-emulator          # expect: SMOKE PASSED (90+ checks)
make run                    # visual check by the user (has display)
```

Smoke test covers: boot/select text + menu mode + parked OAM,
select+pause arrows (no blank, LCDC value stable), every full-screen
transition frame-by-frame (LCDC.7 always set, old-or-new only, output
stable, OAM + tile/OBJ mode match the new screen), sprite-only cursor,
blink show/erase, gray locked hint, readable win, advance.

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
