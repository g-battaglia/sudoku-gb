# PLAN.md — Sudoku for Game Boy Classic (DMG, .gb)

> Target: **Game Boy Classic DMG, `.gb` ROM, no color, 32KB ROM ONLY.**
> Toolchain: **GBDK-2020 4.5.0 + SDCC 4.6.0** (vendored in `tools/gbdk/`).
> Principles: **clean code, YAGNI, explicit and well-commented code.**
> User constraints: **single shot (no milestones), no save system,
> no audio (for now), level unlock via passwords.**
> Language: **everything in English (docs, code, comments, UI strings).**

---

## 1. Goal

A complete, playable Sudoku game for real DMG hardware and emulators
(primary: mGBA):

- 12 sequential levels: 4 EASY + 4 MEDIUM + 4 HARD.
- 9x9 grid, D-Pad navigation, digit entry 1-9, erase.
- 3 mistakes = game over. Full and valid grid = win.
- Beating level N shows the **4-digit password** for level N+1.
- Title screen: `NEW GAME` (from level 0) or `PASSWORD`
  (jump straight to a level with a code).
- Custom procedural tiles for the board (no external graphic assets).

Explicitly OUT of scope (YAGNI):

- Cartridge save (no MBC/SRAM/battery: ROM ONLY 32KB).
- Audio/SFX/music. Pencil marks, hints, timer, on-device generator.
- SGB/CGB support (the game runs in DMG mode, also on CGB).

---

## 2. Context and toolchain

- Repo `/Users/giacomo/dev/sudoku-gb` (was empty, now a skeleton).
- macOS ARM (Darwin 25.5.0, arm64).
- Available: `make`, `gcc`, `mGBA 0.10.5`, `RGBDS 1.0.3` (not used).
- Installed: `sdcc 4.6.0` via brew; `gbdk-2020 4.5.0 macos-arm64`
  from GitHub releases, unpacked into `tools/gbdk/` (vendored, so the
  build is reproducible with no global GBDKDIR). `lcc` verified.

### Why GBDK-2020 in C (not RGBDS assembly)

Sudoku logic, passwords and text UI are far more readable in C;
explicit user request (clean code + YAGNI + comments).
Acceptable cost: the game fits in <32KB with no banking.

### DMG hardware constraints (respected)

LR35902 CPU @ ~4.19MHz, 8KB WRAM, 8KB VRAM, 160x144 screen.
32x32-tile background, 20x18 viewport of 8x8 tiles, 4 grays.
Polled input with debounce at 60fps (`vsync()`), GBDK `J_*` joypad.
Valid ROM header from `lcc`/`makebin` (Nintendo logo, checksums).
`ROM ONLY` cartridge (0x00), no RAM: no save possible by design.
## 3. Architecture

### 3.1 File map (actual state)

```text
PLAN.md                  <- this file
Makefile                 <- GBDK build (TODO, see section 7)
src/
  types.h                [DONE] constants (9x9, 12 levels, max mistakes)
  puzzles.h / puzzles.c  [DONE] Puzzle type + difficulty_name()
  puzzles_gen.c          [GENERATED] 12 puzzles (tools/gen_puzzles.py)
  passwords.h/.c         [DONE] password formula, match, find
  board.h / board.c      [DONE] state + rules
  input.h / input.c      [TODO] joypad debounce (pressed + repeat)
  ui.h / ui.c            [TODO] text rendering + screens
  main.c                 [TODO] state loop + game flow
tools/
  gbdk/                  [DONE] vendored toolchain (gbdk.tar.gz gitignored)
  gen_puzzles.py         [DONE] generator, 12 unique verified puzzles
build/                   .gb/.ihx/.map output (gitignored)
```

### 3.2 Modules: one job each (no god-object)

| Module | Job | HW? |
|---|---|---|
| `types.h` | Global `#define` only. | No |
| `puzzles` | Level data + difficulty names. | No (ROM) |
| `passwords` | `password_for_level/matches/find_level`. uint32 math only. | No |
| `board` | `cells[81]`, load/get/set/conflicts/is_solved/errors. 3-mistake rule here. | No |
| `input` | Reads `joypad()`, exposes edge `pressed` + D-Pad auto-repeat. | Yes (GBDK) |
| `ui` | All drawing: font_init, cls, gotoxy/printf, grid, screens. | Yes (GBDK) |
| `main` | State machine + flow, no direct drawing (calls ui_*). | Via ui/input |

Clean-code rule: `board/passwords/puzzles` **never include `<gb/gb.h>`**,
so they compile and run on PC with `gcc` (`make test-host`).

### 3.3 Puzzle format

```c
typedef struct {
    Difficulty difficulty;        // EASY / MEDIUM / HARD
    char givens[CELL_COUNT + 1];  // 81 chars '0'-'9' + '\0', '0' = empty
} Puzzle;
```

Givens only in ROM, solution NOT stored (saves ROM + YAGNI):
every puzzle has a unique solution (generator-guaranteed) and `board.c`
rejects any conflicting digit, so "full" implies "valid".
`tools/gen_puzzles.py --seed=20260916`: random full grid + digging with
a backtracking+MRV solver that checks uniqueness (cap 2) after each dig.
Givens targets: 42/34/29. Regenerate with `python3 tools/gen_puzzles.py`.
Generated file: NEVER edit by hand.

### 3.4 Passwords (instead of a save system)

```c
code = ((((level + 1) * 7919 + 104729) ^ 0xBEEF) % 10000)  // 0..9999
```

`level` is 0-based (0..11). Explicit `uint32_t`: identical on SDCC/gcc.
`password_matches(l, code)`, `password_find_level(code)` (-1 if invalid).
Single source of truth = `password_for_level()` in C; the printable
table comes from `make passwords` (host test printing the codes).
Honest NOTE in comments: not security, just anti-spoiler (formula is in ROM).
## 4. Rendering: tile grid + sprite cursor (custom tiles, no assets)

Text (menus, header, footer) uses the GBDK built-in font. The board is
a custom tile grid (`src/tiles.*`, procedural 8x8 tiles, no PNG/assets):

```text
r0:  SUDOKU L01 EASY                   <- title + level + difficulty
r1:  ERRORS X X .                      <- 3 mistake slots (X = used)
r2:  (blank separator row)
r3:  grid top frame (corner + top tiles)
r4-12: 9 grid rows x 9 cells           <- 1 tile per cell, right+bottom
     borders in-tile: 1px inside a 3x3 box, 2px between boxes/edges
     (top/left borders come from neighbours or frame tiles)
r13: ENTER: 5                           <- proposed digit (Up/Down, A confirms)
r14-15: context help                    <- "UP/DN NUM A:OK" / "B:DEL START:MENU"
r16: messages                           <- "MISTAKE!", "LOCKED CELL", ...
```

Cursor = sprite 0 (8x8 outline, VRAM tile 240), moved with `move_sprite`;
it overlays the cell so grid lines are never erased (no repaint needed).
Given digits are black, player digits dark gray; `ui_cell(r,c)` redraws
one cell, `ui_cursor(r,c)` just moves the sprite.
ui_* screens: title (2 items), password entry (4 slots), pause
(RESUME/RESTART/TITLE), win (password N+1 or "YOU WIN!"),
game over (RETRY/TITLE). Grid via `set_bkg_tiles`, text via gotoxy/printf.

## 5. Final controls (also shown in-game as help)

| Context | Input | Action |
|---|---|---|
| Title | Up/Down + A | NEW GAME (level 0) / PASSWORD |
| Game | D-Pad | Cursor (wraps at edges, auto-repeat) |
| Game | Up/Down on editable | Proposed digit 1-9 (in ENTER row) |
| Game | A | Confirm (conflict -> mistake+1, rejected) |
| Game | B | Erase player digit (givens protected) |
| Game | START | Pause |
| Password | Up/Down+Left/Right, A/B | 4 digits, confirm / back |
| Menus/Win/Lose | D-Pad + A (+B back) | Navigate, confirm, cancel |

YAGNI: no separate number submenu, the digit cycles in place.

## 6. States (main.c)

```text
ST_TITLE -> ST_PASSWORD_ENTRY -> ST_GAME <-> ST_PAUSE
ST_GAME -> ST_WIN (shows pwd N+1) -> [A] next level, or title if last
ST_GAME -> ST_GAMEOVER (3 mistakes) -> RETRY (reload) / TITLE
```

`board_load(level)` on every level entry; win = `board_is_solved()`
after each confirmed A; last level = completion screen.
## 7. Remaining work (single shot)

### 7.1 `src/input.{h,c}` (~60 lines, simple)

- `void input_init(void)` — clear state.
- `void input_poll(void)` — call `joypad()`, compute
  `pressed = now & ~prev` (edge), plus D-Pad auto-repeat
  after ~20 frames at ~6 frames rate.
- `uint8_t input_pressed(uint8_t mask)` — edge for A/B/START/SELECT.
- `uint8_t input_dir(uint8_t mask)` — repeat-aware, for cursor movement.
- Why: no double-count while held; smooth held movement.

### 7.2 `src/ui.{h,c}` + `src/tiles.{h,c}` [DONE] (tile grid + sprite cursor)

- `ui_init()` — `font_init()`, `tiles_load()`, sprite tile, `DISPLAY_ON`.
- `ui_draw_title(sel)`, `ui_draw_password(digits, pos, bad)`,
  `ui_draw_board_frame()` (header + tile grid + footer, drawn once),
  `ui_draw_cell(r,c)` (single cell tile: digit or empty, keeps borders),
  `ui_draw_cursor(r,c)` (move sprite 0 outline),
  `ui_draw_status()` (mistakes + level),
  `ui_draw_entry(value)`, `ui_message(msg)` (row 16),
  `ui_draw_pause(sel)`, `ui_draw_win(level, next_pwd, is_last)`,
  `ui_draw_gameover(sel)`.
- `tiles.c`: procedural 8x8 tiles (3x5 digits + thin/thick borders),
  loaded with `set_bkg_data` (BG tiles 96-176, font uses 0-95).

### 7.3 `src/main.c` [DONE] (states + flow)

- `void main(void)`: init, start at `ST_TITLE`,
  one `switch(state)` loop with `vsync()`.
- Small handlers: `title_update()`, `password_update()`,
  `game_update()`, `pause_update()`, `win_update()`,
  `gameover_update()` — one main switch, no scattered machine.
- State vars: `state, level, cursor_r/c, entry_value, pwd_digits[4]`.
- Mistakes via `board_errors()/board_register_error()`.

### 7.4 `Makefile` (GBDK, ROM ONLY 32KB)

Keep it minimal and explicit:

```make
GBDK = tools/gbdk
LCC = $(GBDK)/bin/lcc
PROJECT = sudoku
CSOURCES = $(wildcard src/*.c)
LCCFLAGS = -msm83 -Wl-yt0x00 -Wm-yn"SUDOKU"
all: build/$(PROJECT).gb
build/$(PROJECT).gb: $(CSOURCES)
	mkdir -p build && $(LCC) $(LCCFLAGS) -o $@ $(CSOURCES)
run: all         # open in mGBA
check: all       # ihxcheck + size <= 32KB + header check
test-host:       # gcc -Wall -Wextra board/passwords/puzzles + asserts
passwords:       # print the 12 passwords from the C code (source of truth)
clean / regen-puzzles
```
## 8. Build / Run / Test (macOS)

```bash
make                 # build/sudoku.gb (ROM ONLY 32KB)
make run             # open in mGBA
make check           # ihxcheck + size check + header check
make test-host       # gcc tests on PC: board rules + password formula
make passwords       # print the 12 passwords (for the manual)
make regen-puzzles   # regenerate src/puzzles_gen.c (only if needed)
make clean
```

Final check: boot in mGBA with no errors, header/Nintendo-logo OK,
full playthrough of level 0 to win (password shown) + password
entry for level 5, game over after 3 mistakes, cursor wrap,
B on a given ignored, ROM <= 32768 bytes.

## 9. Risks

| Risk | Fix |
|---|---|
| GBDK font tiles / slow `printf` | Delta redraw only (cell/cursor), never cls per frame |
| Float `printf` pulls big lib | Only `%s/%d/%c`, no float |
| Strict SDCC (no VLA, int enums) | Fixed arrays, `uint8_t`, no malloc |
| Puzzles too easy/hard | 42/34/29 givens, unique-verified; tune by regenerating |
| Passwords reversible | Accepted and documented (anti-spoiler, not security) |
| `tools/gbdk.tar.gz` committed | `.gitignore` excludes it, only extracted `tools/gbdk/` |

---

## 10. Single-shot checklist (execution order)

1. [x] Toolchain (brew sdcc + vendored gbdk) + `gen_puzzles.py` + 12 puzzles.
2. [x] `types/puzzles/passwords/board`, simple + commented (EN).
3. [x] `input.h/c` (debounce + repeat).
4. [x] `ui.h/c` + `tiles.h/c` (tile grid screens + sprite cursor).
5. [x] `main.c` (states + flow).
6. [x] `Makefile` + `.gitignore` + `README.md` (commands + table + controls).
7. [ ] Green `make test-host` + ROM <= 32KB + full `make run` playthrough.
8. [ ] Final commit.

No intermediate milestones by user choice: one final commit/validation.
