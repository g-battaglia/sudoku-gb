# PLAN.md — Sudoku for Game Boy Classic (DMG, .gb)

> Target: **Game Boy Classic DMG, `.gb` ROM, no color, 32KB ROM,
> MBC1 + battery SRAM (save).**
> Toolchain: **GBDK-2020 4.5.0 + SDCC 4.6.0** (vendored in `tools/gbdk/`,
> gitignored; `make setup-gbdk` re-downloads it on a fresh clone).
> Principles: **clean code, YAGNI, explicit and well-commented code.**
> User constraints: **single shot (no milestones), no audio (for now),
> free level select, no passwords, battery save (added 2026-09-17).**
> Language: **everything in English (docs, code, comments, UI strings).**
>
> NOTE (2026-09-16): the password system was removed entirely. There are
> now 300 free levels (100 EASY incl. 10 intro at 48 givens + 100 MEDIUM
> + 100 HARD), precomputed grid tiles, gray player/hint digits and
> double-buffered atomic screen swaps (LCD stopped once at boot).

---

## 1. Goal

A complete, playable Sudoku game for real DMG hardware and emulators
(primary: mGBA):

- 300 free levels: 100 EASY (10 introductory at 48 givens + 90 at 42)
  + 100 MEDIUM (34) + 100 HARD (29), all playable from boot.
- 9x9 fullscreen grid, D-Pad navigation, A = digit-pick mode, erase.
- No game over: mistakes are only tallied, play goes on forever.
- HINT (START menu) reveals a cell digit, renders it gray like a player
  digit, and locks it.
- Boot screen: `SELECT MODE` (EASY / MEDIUM / HARD), then 100 levels
  per mode, 10 per page, `*` = beaten.
- Custom precomputed tiles for the board (no external graphic assets).
- LCD-safe on real hardware: the LCD stops exactly once (boot init via
  GBDK display_off). Every screen is drawn into the hidden BG map, then
  one LCDC write swaps map + tile mode + sprites at frame start.

Explicitly OUT of scope (YAGNI):

- Audio/SFX/music. Pencil marks, timer, on-device generator.
- SGB/CGB support (the game runs in DMG mode, also on CGB).
- Multiple save slots (one battery slot is enough).

---

## 2. Context and toolchain

- Repo `/Users/giacomo/dev/sudoku-gb` (was empty, now a skeleton).
- macOS ARM (Darwin 25.5.0, arm64).
- Available: `make`, `gcc`, `mGBA 0.10.5`, `RGBDS 1.0.3` (not used).
- Installed: `sdcc 4.6.0` via brew; `gbdk-2020 4.5.0 macos-arm64`
  from GitHub releases, unpacked into `tools/gbdk/` (vendored, so the
  build is reproducible with no global GBDKDIR). `lcc` verified.

### Why GBDK-2020 in C (not RGBDS assembly)

Sudoku logic and text UI are far more readable in C;
explicit user request (clean code + YAGNI + comments).
Acceptable cost: the game fits in <32KB with no banking.

### DMG hardware constraints (respected)

LR35902 CPU @ ~4.19MHz, 8KB WRAM, 8KB VRAM, 160x144 screen.
32x32-tile background, 20x18 viewport of 8x8 tiles, 4 grays.
Polled input with debounce at 60fps (`vsync()`), GBDK `J_*` joypad.
Valid ROM header from `lcc`/`makebin` (Nintendo logo, checksums).
MBC1+RAM+BATTERY cartridge (0x03), 8KB battery SRAM: one save slot.
ROM stays 32KB (2 banks, no banking needed).
## 3. Architecture

### 3.1 File map (actual state)

```text
PLAN.md                  <- this file
Makefile                 <- GBDK build (+ setup-gbdk, test-emulator)
src/
  types.h                [DONE] constants (9x9, screen 20x18)
  puzzles.h / puzzles.c  [DONE] Puzzle type + difficulty_name()
  puzzles_gen.c          [GENERATED] 300 puzzles + solutions (gen_puzzles.py)
  board.h / board.c      [DONE] state + rules (origins, hint locks, marks)
  input.h / input.c      [DONE] joypad debounce (pressed + repeat)
  save.h / save.c        [DONE] battery save slot (SRAM I/O)
  save_format.h/.c       [DONE] save layout + checksum + validation (no HW)
  tiles.h / tiles.c      [DONE] precomputed 16x16 grid + cursor tiles
  tiles_gen.c            [GENERATED] 230 grid + 4 cursor tiles (gen_tiles.py)
  ui.h / ui.c            [DONE] grid screens + tile-drawn text menus
  main.c                 [DONE] state loop + edit-mode + save flow
tools/
  gbdk/                  [DONE] vendored toolchain (gitignored, setup-gbdk)
  gen_puzzles.py         [DONE] generator, 300 unique verified puzzles
  gen_tiles.py           [DONE] precomputed grid artwork generator
  smoke_pyboy.py         [DONE] headless emulator smoke test
build/                   .gb/.ihx/.map output (gitignored)
```

### 3.2 Modules: one job each (no god-object)

| Module | Job | HW? |
|---|---|---|
| `types.h` | Global `#define` only. | No |
| `puzzles` | Level data + difficulty names. | No (ROM) |
| `board` | `cells[81]`, `cell_origin[81]`, load/get/locked/original/conflicts/is_solved/errors/reveal + marks bitmap. No game over: mistakes tallied only. | No |
| `input` | Reads `joypad()`, exposes edge `pressed` + D-Pad auto-repeat (16-bit timer). | Yes (GBDK) |
| `save_format` | Image layout + checksum + field validation. | No |
| `save` | SRAM I/O through the MBC latch (uses `save_format`). | Yes (GBDK) |
| `ui` | All drawing: tile text helpers, grid, screens; atomic LCD-safe transitions; delta menu redraws. | Yes (GBDK) |
| `main` | State machine + flow, no direct drawing (calls ui_*). | Via ui/input |

Clean-code rule: `board/puzzles/save_format` **never include `<gb/gb.h>`**,
so they compile and run on PC with `gcc` (`make test-host`).

### 3.3 Puzzle format

```c
typedef struct {
    uint8_t solution[41];    // 81 digits 1-9, 4 bits each (nibbles)
    uint8_t givens_mask[11]; // bit i = cell i is a given
} Puzzle;  // 52 bytes/level, difficulty = index range
```

Givens + solution in ROM (81 bytes/level extra, ~12KB total code+data
in a 32KB ROM: plenty of slack). Win still means "full and valid",
which equals the stored solution. Uniqueness is generator-guaranteed
and `board.c`
rejects any conflicting digit, so "full" implies "valid".
`tools/gen_puzzles.py --seed=20260916`: random full grid + digging with
a backtracking+MRV solver that checks uniqueness (cap 2) after each dig.
Givens targets: 42/34/29. Regenerate with `python3 tools/gen_puzzles.py`.
Generated file: NEVER edit by hand.

### 3.4 Passwords — REMOVED

The password system was deleted (git history has it): all levels are
freely selectable, progress marks are battery-saved.
## 4. Rendering: fullscreen tile grid + 4-sprite cursor (no assets)

The game screen is ONLY the board: 9x9 cells of 16x16 px (2x2 BG tiles)
at tile (1, 0) = 144x144 px, full screen height, 1-tile margins. No
header, footer or messages: level, mistakes, help live in START menu.

```text
fullscreen: 9x9 cells x 16px     <- chunky 2x digits (6x10)
     borders baked per cell: 2px black frame/box lines,
     2px dark-gray inner lines (each line drawn once, no wobble)
```

Cursor = 4 sprites (8x8 corners, tiles 240-243) forming a 16x16 outline.
Feedback without text: picked digit blinks in the cell (tiles only,
board untouched); rejected digit hides the cursor ~24 frames; locked
cells blink the cursor. `ui_cell(r,c)` redraws one 2x2 cell,
`ui_preview(r,c,v,show)` draws/erases the blink, `ui_cursor(r,c)` moves.
ui_* screens: difficulty select (EASY/MEDIUM/HARD), select (100 levels
per mode + `*` marks), START menu (status +
RESUME/HINT/RESTART/TITLE + help), win (tally or completion).
Screens are double-buffered (hidden map + one atomic LCDC swap, tile
patterns resident); text is font tiles, never stdio.

## 5. Final controls (help is in the START menu)

| Context | Input | Action |
|---|---|---|
| Diff | Up/Down + A | Choose EASY / MEDIUM / HARD (100 levels each) |
| Diff | LOAD + A | Resume the battery save (shown only if valid) |
| Select | Up/Down + Left/Right + A, B | Play any level of the mode (`*` = beaten); B back to the mode |
| Game | D-Pad | Cursor (wraps at edges, auto-repeat) |
| Game | A on cell | Digit-pick mode (locked cells blink) |
| Pick | Up/Right, Down/Left | Next / previous digit (wraps 9-1, blinks in the cell) |
| Pick | A / B | Confirm (conflict -> mistake, keep picking) / back |
| Game | B | Erase player digit (locked cells blink) |
| Game | START | Menu: RESUME / HINT / SAVE / PLAY AGAIN / MENU |
| Any | A+B+START+SELECT | Soft reset to the boot menu (save kept) |
| Menus/Win | D-Pad + A (+B back) | Navigate, confirm, cancel |

## 6. States (main.c)

```text
ST_DIFF -> ST_SELECT -> ST_GAME <-> ST_PAUSE
ST_GAME -> ST_WIN (marks `*`, saves slot) -> [A] next level, or select if last
ST_PAUSE -> ST_SAVED (SAVE item) -> [A/B] back to ST_GAME
ST_DIFF + LOAD -> ST_GAME (resumed) or ST_SELECT (marks only)
```

No game over, no retry screen: mistakes tallied forever. `board_load(level)`
on every level entry; win = `board_is_solved()` after each confirmed A
(or HINT); last level = completion screen back to select.
## 7. Remaining work (single shot)

### 7.1 `src/input.{h,c}` (~60 lines, simple)

- `void input_poll_init(void)` — clear state.
- `void input_poll(void)` — call `joypad()`, compute
  `pressed = now & ~prev` (edge), plus D-Pad auto-repeat
  after 18 frames every 6 frames (16-bit hold timer, never stalls).
- `uint8_t input_pressed(uint8_t mask)` — edge for A/B/START/SELECT.
- `uint8_t input_dir(uint8_t mask)` — repeat-aware, for cursor movement.
- Why: no double-count while held; smooth held movement.

### 7.2 `src/ui.{h,c}` + `src/tiles.{h,c}` [DONE] (fullscreen grid)

- `ui_init()` — LCD off once, resident tiles, parked sprites (LCD stays
  off until the first screen presents it).
- `ui_diff(choice, has_load)` (EASY/MEDIUM/HARD + LOAD),
  `ui_select(page, row, marks, diff)` (100 levels of one mode, 10/page,
  `*`), `ui_game_full(row, col)` (grid + placed cursor, no text),
  `ui_cell(r,c)` (one 2x2 cell), `ui_preview(r,c,v,show)` (blink without
  touching the board), `ui_cursor(r,c)` (4-sprite outline),
  `ui_pause(choice, lid, diff, mistakes)` (status + 5 items + help),
  `ui_win(num, is_last, mistakes)`. Delta helpers for marker/cell
  updates (LCD stays on).
- `tiles.c`: 16x16 cells (2x scaled digits, uniform 2px borders),
  230 grid tiles at 0-229 + font at 0x9000 (resident, never reloaded);
  cursor corners at 240-243.

### 7.3 `src/main.c` [DONE] (states + edit-mode flow)

- `void main(void)`: init, read battery save, start at `ST_DIFF`,
  one `switch(state)` loop with `vsync()`.
- Small handlers: `diff_update()`, `select_update()`, `game_update()`
  (split: `game_update_nav()` vs `game_update_editing()`),
  `pause_update()` (RESUME/HINT/SAVE/PLAY AGAIN/MENU switch),
  `win_update()`, `saved_update()` — one main switch.
- State: `state, level/sel_diff, Cursor{row,col,entry,editing},
  marks[38], slot/has_save, menu_choice, sel_page/row,
  Pending{need_win/win_level/win_last/need_saved}, frame (16-bit),
  flash, Preview{row,col,val,shown}`.
- Mistakes via `board_errors()/board_add_mistake()` (counted only).
- HINT via `board_reveal()` + `puzzles[level].solution` (locks the cell).

### 7.4 `Makefile` (GBDK, 32KB ROM + battery SRAM)

Keep it minimal and explicit:

```make
LCCFLAGS = -msm83:gb -Wm-yn"SUDOKU" -Wl-yt0x03 -Wl-ya1  # MBC1+RAM+BATT, 8KB SRAM
all / run / check (size+logo+cart+sram) / test-host (gcc asserts) /
test-emulator (PyBoy frame checks + save power-cycle) /
regen-puzzles / regen-tiles / setup-gbdk (fresh-clone bootstrap) / clean
```
## 8. Build / Run / Test (macOS)

```bash
make                 # build/sudoku.gb (32KB ROM, MBC1 + battery SRAM)
make run             # open in mGBA
make check           # ihxcheck + size check + header check
make test-host       # gcc tests on PC: board/marks/restore/save_format
make test-emulator   # PyBoy: per-frame transition checks (no flash)
make regen-puzzles   # regenerate src/puzzles_gen.c (only if needed)
make clean
```

Final check: boot in mGBA with no errors, header/Nintendo-logo OK,
level select (300 free, `*` after win), edit-mode playthrough of level
0 to win, HINT locks, no game over after many mistakes, cursor wrap,
fullscreen grid with clean 2px lines, ROM <= 32768 bytes.

## 9. Risks

| Risk | Fix |
|---|---|
| GBDK font tiles / slow `printf` | Delta redraw only (cell/cursor), never cls per frame |
| Float `printf` pulls big lib | Only `%s/%d/%c`, no float |
| Strict SDCC (no VLA, int enums) | Fixed arrays, `uint8_t`, no malloc |
| Puzzles too easy/hard | 48/42/34/29 givens, unique-verified; tune by regenerating |
| `tools/gbdk.tar.gz` committed | `.gitignore` excludes it, only extracted `tools/gbdk/` |

---

## 10. Single-shot checklist (execution order)

1. [x] Toolchain (brew sdcc + vendored gbdk) + `gen_puzzles.py` + 12 puzzles.
2. [x] `types/puzzles/board`, simple + commented (EN).
3. [x] `input.h/c` (debounce + repeat).
4. [x] `ui.h/c` + `tiles.h/c` (tile grid screens + sprite cursor).
5. [x] `main.c` (states + flow).
6. [x] `Makefile` + `.gitignore` + `README.md` (commands + table + controls).
7. [x] Green `make test-host` + ROM <= 32KB + tile PNG check.
8. [ ] `make run` playthrough on a real emulator + final commit.

No intermediate milestones by user choice: one final commit/validation.
