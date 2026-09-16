# PLAN.md — Sudoku for Game Boy Classic (DMG, .gb)

> Target: **Game Boy Classic DMG, `.gb` ROM, no color, 32KB ROM ONLY.**
> Toolchain: **GBDK-2020 4.5.0 + SDCC 4.6.0** (vendored in `tools/gbdk/`).
> Principles: **clean code, YAGNI, explicit and well-commented code.**
> User constraints: **single shot (no milestones), no save system,
> no audio (for now), free level select, passwords only as proof.**
> Language: **everything in English (docs, code, comments, UI strings).**

---

## 1. Goal

A complete, playable Sudoku game for real DMG hardware and emulators
(primary: mGBA):

- 12 free levels: 4 EASY + 4 MEDIUM + 4 HARD, all playable from boot.
- 9x9 fullscreen grid, D-Pad navigation, A = digit-pick mode, erase.
- No game over: mistakes are only tallied, play goes on forever.
- HINT (START menu) reveals a cell digit and locks it.
- Beating level N shows the **4-digit password** for level N+1.
- Boot screen: `SELECT LEVEL` (all 12 free, `*` = beaten this session).
- A password never unlocks: it only marks levels 1..N-1 `*` and jumps
  to level N (restores the progress display, no save exists).
- Custom procedural tiles for the board (no external graphic assets).

Explicitly OUT of scope (YAGNI):

- Cartridge save (no MBC/SRAM/battery: ROM ONLY 32KB).
- Audio/SFX/music. Pencil marks, timer, on-device generator.
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
Makefile                 <- GBDK build
src/
  types.h                [DONE] constants (9x9, 12 levels, screen)
  puzzles.h / puzzles.c  [DONE] Puzzle type + difficulty_name()
  puzzles_gen.c          [GENERATED] 12 puzzles + solutions (gen_puzzles.py)
  passwords.h/.c         [DONE] password formula, match, find
  board.h / board.c      [DONE] state + rules (no game over, hint locks)
  input.h / input.c      [DONE] joypad debounce (pressed + repeat)
  tiles.h / tiles.c      [DONE] fullscreen 16x16 grid + cursor tiles
  ui.h / ui.c            [DONE] grid screens + text menus
  main.c                 [DONE] state loop + edit-mode flow
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
| `board` | `cells[81]`, `locked[81]`, load/get/set/conflicts/is_solved/errors/reveal. No game over: mistakes tallied only. | No |
| `input` | Reads `joypad()`, exposes edge `pressed` + D-Pad auto-repeat. | Yes (GBDK) |
| `ui` | All drawing: font_init, cls, gotoxy/printf, grid, screens. | Yes (GBDK) |
| `main` | State machine + flow, no direct drawing (calls ui_*). | Via ui/input |

Clean-code rule: `board/passwords/puzzles` **never include `<gb/gb.h>`**,
so they compile and run on PC with `gcc` (`make test-host`).

### 3.3 Puzzle format

```c
typedef struct {
    Difficulty difficulty;         // EASY / MEDIUM / HARD
    char givens[CELL_COUNT + 1];   // 81 chars '0'-'9' + '\0', '0' = empty
    char solution[CELL_COUNT + 1]; // unique full grid (powers HINT)
} Puzzle;
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

### 3.4 Passwords (instead of a save system)

```c
code = ((((level + 1) * 7919 + 104729) ^ 0xBEEF) % 10000)  // 0..9999
```

`level` is 0-based (0..11). Explicit `uint32_t`: identical on SDCC/gcc.
`password_matches(l, code)`, `password_find_level(code)` (-1 if invalid).
A password for level N (0-based `found`) proves levels 1..N-1 beaten:
the game marks them `*` and jumps to level N. It unlocks nothing —
every level is always playable; the marks are session-only.
Single source of truth = `password_for_level()` in C; the printable
table comes from `make passwords` (host test printing the codes).
Honest NOTE in comments: not security, just anti-spoiler (formula is in ROM).
## 4. Rendering: fullscreen tile grid + 4-sprite cursor (no assets)

The game screen is ONLY the board: 9x9 cells of 16x16 px (2x2 BG tiles)
at tile (1, 0) = 144x144 px, full screen height, 1-tile margins. No
header, footer or messages: level, mistakes, help live in START menu.

```text
fullscreen: 9x9 cells x 16px     <- chunky 2x digits (6x10), all black
     borders baked per cell: 2px outer frame + 2px box gaps,
     1px thin cell lines (each line drawn once, no wobble)
```

Cursor = 4 sprites (8x8 corners, tiles 240-243) forming a 16x16 outline.
Feedback without text: picked digit blinks in the cell (tiles only,
board untouched); rejected digit hides the cursor ~24 frames; locked
cells blink the cursor. `ui_cell(r,c)` redraws one 2x2 cell,
`ui_preview(r,c,v,show)` draws/erases the blink, `ui_cursor(r,c)` moves.
ui_* screens: select (12 levels + `*` marks), password entry (4 slots),
START menu (status + RESUME/HINT/RESTART/TITLE + help), win (password
N+1 or completion). Grid via `set_bkg_tiles`, menus via gotoxy/printf.

## 5. Final controls (help is in the START menu)

| Context | Input | Action |
|---|---|---|
| Select | Up/Down + A | Play any of the 12 levels (`*` = beaten) |
| Select | SELECT | Password entry (restores `*` marks, jumps) |
| Game | D-Pad | Cursor (wraps at edges, auto-repeat) |
| Game | A on cell | Digit-pick mode (locked cells blink) |
| Pick | Up/Down | Pick digit 1-9 (blinks in the cell) |
| Pick | A / B | Confirm (conflict -> mistake, keep picking) / back |
| Game | B | Erase player digit (locked cells blink) |
| Game | START | Menu: RESUME / HINT / RESTART / TITLE |
| Password | Up/Down+Left/Right, A/B | 4 digits, confirm / back |
| Menus/Win | D-Pad + A (+B back) | Navigate, confirm, cancel |

## 6. States (main.c)

```text
ST_TITLE (select) -> ST_PASSWORD -> ST_GAME <-> ST_PAUSE
ST_GAME -> ST_WIN (shows pwd N+1, marks `*`) -> [A] next level, or select if last
```

No game over, no retry screen: mistakes tallied forever. `board_load(level)`
on every level entry; win = `board_is_solved()` after each confirmed A
(or HINT); last level = completion screen back to select.
## 7. Remaining work (single shot)

### 7.1 `src/input.{h,c}` (~60 lines, simple)

- `void input_init(void)` — clear state.
- `void input_poll(void)` — call `joypad()`, compute
  `pressed = now & ~prev` (edge), plus D-Pad auto-repeat
  after ~20 frames at ~6 frames rate.
- `uint8_t input_pressed(uint8_t mask)` — edge for A/B/START/SELECT.
- `uint8_t input_dir(uint8_t mask)` — repeat-aware, for cursor movement.
- Why: no double-count while held; smooth held movement.

### 7.2 `src/ui.{h,c}` + `src/tiles.{h,c}` [DONE] (fullscreen grid)

- `ui_init()` — `font_init()`, `tiles_load()`, 4 cursor sprites, `DISPLAY_ON`.
- `ui_select(pos, done)` (12 free levels + `*`), `ui_password(digits,pos,bad)`,
  `ui_game_full()` (grid only, no text), `ui_cell(r,c)` (one 2x2 cell),
  `ui_preview(r,c,v,show)` (blink without touching the board),
  `ui_cursor(r,c)` (4-sprite outline), `ui_pause(choice, level)`
  (status + RESUME/HINT/RESTART/TITLE + help), `ui_win(...)`.
- `tiles.c`: 16x16 cells (2x scaled digits, baked 1px/2px borders),
  160 tiles at 96-255 (font keeps 0-95); cursor corners at 240-243.

### 7.3 `src/main.c` [DONE] (states + edit-mode flow)

- `void main(void)`: init, start at `ST_TITLE`,
  one `switch(state)` loop with `vsync()`.
- Small handlers: `select_update()`, `password_update()`,
  `game_update()` (navigation vs digit-pick mode), `pause_update()`
  (RESUME/HINT/RESTART/TITLE), `win_update()` — one main switch.
- State vars: `state, level, cursor_r/c, entry_value, editing, completed[12],
  pwd_digits[4], frame, flash, preview tracker`.
- Mistakes via `board_errors()/board_add_mistake()` (counted only).
- HINT via `board_reveal()` + `puzzles[level].solution` (locks the cell).

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
level select (all 12 free, `*` after win), password restores `*`,
edit-mode playthrough of level 0 to win (password shown), HINT locks,
no game over after many mistakes, cursor wrap, fullscreen grid with
clean 1px/2px lines, ROM <= 32768 bytes.

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
7. [x] Green `make test-host` + ROM <= 32KB + tile PNG check.
8. [ ] `make run` playthrough on a real emulator + final commit.

No intermediate milestones by user choice: one final commit/validation.
