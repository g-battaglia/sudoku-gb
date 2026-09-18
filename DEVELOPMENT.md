# DEVELOPMENT.md — how this Game Boy game is built

A guide for someone who knows little or no C. It explains how to read
this codebase, how the pieces fit, and how the Game Boy draws the
screen. For build commands and controls, see `README.md`.

---

## 1. C crash course (only what this project uses)

### 1.1 `.h` files declare, `.c` files define

- A **header** (`.h`) says *what exists*: function names, constants,
  types. It contains no logic, only promises. Example `src/board.h`:
  `uint8_t board_get(uint8_t idx);` means "there is a function called
  `board_get` that takes a small number and returns a small number".
- A **source** (`.c`) says *how it works*: the actual logic.
  Example `src/board.c` contains the body of `board_get`.
- `#include "board.h"` at the top of a file is copy-paste: it lets that
  file *call* the promised functions. The real code is linked in later
  (see 1.2). Rule of thumb: to understand *what a module offers*, read
  its `.h`; to understand *how*, read its `.c`.

### 1.2 Compile, then link

`make` runs these steps (see `Makefile`):

1. **Compile**: each `.c` file is translated separately into machine
   code for the Game Boy CPU (LR35902). `board.c` knows nothing about
   `main.c` at this stage — the `#include`d headers were enough.
2. **Link**: all compiled pieces plus the GBDK library are stitched
   into one file, `build/sudoku.gb` (32 KB max). Linking fails if a
   promised function has no body anywhere ("undefined reference").
3. **Check**: `make check` verifies the ROM header (Nintendo logo,
   cartridge type) and size.

Only the files listed in `CSOURCES` in the `Makefile` are compiled.
Generated files (`puzzles_gen.c`, `tiles_gen.c`) are normal `.c` files
to the compiler — they just happen to be written by Python scripts.

### 1.3 The vocabulary of the code

- `uint8_t` — an unsigned number 0–255 (one byte). Almost everything
  here is a `uint8_t`: cell values, rows, tile indices. `int8_t` is the
  signed version (-128–127), used for cursor steps like `-1`.
- `#define GRID_SIZE 9` — a named constant. The preprocessor replaces
  the name with the value before compiling. No memory is used.
- Arrays — `static uint8_t cells[81]` is 81 bytes in a row. `cells[i]`
  is element `i` (counting from 0). The grid is stored row by row, so
  cell `(row, col)` is `cells[row * 9 + col]`.
- Pointers — an address in memory. `const uint8_t *marks` means "the
  address of some bytes I will only read". `bytes + n` moves forward
  `n` bytes. You rarely need to think harder than that here.
- `const` — "read-only": `const Puzzle puzzles[300]` lives in ROM and
  can never change. `const char *s` means "I promise not to modify
  your text".
- `static` (on a function or variable) — "private to this file".
  `static void draw_text(...)` in `ui.c` can only be called from
  `ui.c`. Non-static functions (like `ui_select`) are the module's
  public API, listed in its header.
- `enum` — a small set of named states. `ST_SELECT, ST_GAME, ...`
  reads better than `0, 1, ...` and the compiler treats them as such.
- `struct` — several values grouped under one name. `SaveSlot`
  bundles the saved game: level, values, origins, mistakes, marks.
- `0x9800` — the `0x` prefix means hexadecimal (base 16). Memory
  addresses are written in hex by convention.

### 1.4 Generated C data

`src/puzzles_gen.c` and `src/tiles_gen.c` are **written by Python**,
not by hand (see `tools/`). They contain big tables (`const ... = {
... };`) that end up in ROM. Never edit them: change the generator
and re-run `make regen-puzzles` / `make regen-tiles`. Both generators
are deterministic — running twice produces byte-identical files.

---

## 2. How to read this codebase (in this order)

1. **`README.md`** — what the game is and how to build it.
2. **`src/types.h`** — every global constant (grid 9x9, screen 20x18
   tiles). Short; start here.
3. **`src/puzzles.h` + `src/puzzles.c`** — the packed `Puzzle` struct
   (52 bytes: solution nibbles + givens mask) and the
   `puzzle_solution`/`puzzle_given`/`difficulty_name` helpers. Skip
   `puzzles_gen.c` (300 generated tables).
4. **`src/board.h`, then `src/board.c`** — the rules. Read the header
   comment first: it explains the three cell origins (player digit,
   original clue, hint reveal), conflict rejection, and the
   no-game-over mistake tally. The `.c` is plain logic with **zero**
   Game Boy code, which is why `make test-host` can compile it on
   your PC with `gcc`.
5. **`tests/test_host.c`** — executable documentation: it plays with
   the board on PC and asserts the rules (`assert(...)` crashes the
   test if a rule breaks). If you change `board.c`, run it.
6. **`src/save.h`, then `src/save.c`** — the battery save slot:
   `SaveSlot` bundles one game in progress + the completion marks;
   `save_read`/`save_write` do the SRAM access (§3.5). The on-wire
   layout, checksum and field validation live in the hardware-free
   `src/save_format.{h,c}`, shared with the PC host tests.
7. **`src/input.h` + `src/input.c`** — the Game Boy has no key
   *events*, only "buttons held right now". This module remembers last
   frame's buttons and reports each physical press once (`pressed`),
   plus auto-repeat for held directions (`dir`), and fires once when
   A+B+START+SELECT are all held (`input_reset_combo`) — `main.c`
   then jumps back to `0x0100`, the boot entry, which clears RAM and
   restarts with the battery save intact.
8. **`src/main.c`** — the conductor. One `switch (state)` loop, one
   small handler per state (`select_update`, `game_update`,
   `pause_update`, `win_update`, `saved_update`). It owns the cursor,
   the edit mode, the preview blink bookkeeping and the save/restore
   flow — but draws nothing itself: every visual change is a `ui_*`
   call.
9. **`src/ui.h`, then `src/ui.c`** — all drawing. Read the header
   comment for the double-buffer design (§4), then one screen
   function at a time. Text is written as font tiles, never `printf`.
10. **`src/tiles.h` + `src/tiles.c` + `tools/gen_tiles.py`** — the
    precomputed grid artwork and the VRAM layout. The Python script
    paints each 16x16 cell; the C side only copies bytes and computes
    tile indices.
11. **`tools/gen_puzzles.py`, `Makefile`** — puzzle generation
    (random full grid, dig holes while the solution stays unique;
    100 per difficulty, packed) and the build/test targets.

---

## 3. Guided traces (follow the code)

### 3.1 Boot

`main()` → a few `vsync()`s → `ui_init()`: the LCD is stopped,
every tile pattern is loaded (GBDK puts its font at `0x9000`; the grid
and cursor sprites are copied to `0x8000`), all 40 sprites are parked
and copied to hardware OAM. Note the LCD flickers during init: GBDK's
font loader turns it back on, so `ui_init` stops it a second time
before the raw grid copy (raw copies have no PPU wait — they must run
with the LCD off). Then the battery save is read (`save_read`: if a
valid slot exists, the completion marks are restored and the boot
menu gains a LOAD item) and `ui_diff(0, has_save)` presents the
difficulty screen — that single LCDC write also turns the LCD on for
good (A then opens the level select of that mode).

### 3.2 One input frame

The main loop repeats: `input_poll()` snapshots the joypad →
`switch (state)` runs one handler → `frame++` → `vsync()` waits for
the TV-like refresh (60 fps). A handler reads `input_pressed(...)`
("was it just pushed?") or `input_dir(...)` ("push or held repeat?")
and calls `ui_*` functions. Game logic first, drawing second, wait
last — always in that order.

### 3.3 Editing a digit

On a free cell, `A` calls `enter_editing()`: from now on Up/Down
and Left/Right
change `cursor.entry` instead of moving the cursor. Every frame,
`preview_update()` draws or erases the blinking digit with
`ui_preview()` — the **board is never touched** by the blink. `A`
(`confirm_editing()`) writes the digit and checks conflicts: illegal
→ restore + mistake + keep picking; legal → draw the cell and check
for a win. `B` (`cancel_editing()`) erases the preview.

### 3.4 START menu and HINT

`START` in game draws the pause screen (level, mistakes, five items:
RESUME / HINT / SAVE / PLAY AGAIN / MENU) and switches state.
`HINT` (`do_hint()`) picks the cursor cell if it is free, else the
first free cell; writes the **solution** digit from ROM; locks the
cell as a hint (gray like a player digit, but not editable); redraws
the whole grid atomically — or records a win if the grid is full.
`SAVE` snapshots the whole game into the battery save (`save_store`)
and shows a confirmation screen.

### 3.5 The battery save

The cartridge is an **MBC1 + RAM + battery** (`0x03`): a small SRAM
at `0xA000-0xBFFF` that the battery keeps alive with the power off.
The game cannot just write there — the MBC gates it behind a latch:
`ENABLE_RAM` opens it, `DISABLE_RAM` closes it (`src/save.c` does
exactly that around every access, so a crash mid-write cannot
silently corrupt the slot).

The slot holds one game in progress (level, 81 values, 81 origins,
mistakes) plus the completion marks of all 300 levels (one bit each,
38 bytes). A 4-byte magic, a version byte and a checksum guard it:
first boot the SRAM is garbage, the checksum fails, and the boot
menu simply hides `LOAD`. Reading is `save_read()` (restore marks +
resume), writing is `save_write()` — on SAVE and on every win (so
completions survive even without an explicit save). `board.c` owns
the marks bitmap (`marks_*`), keeping the SRAM layout testable on
PC with gcc.

### 3.6 A screen swap (double buffer)

`ui_pause()` (for example) does four steps: `begin_draw()` (draw into
the hidden map) → clear + draw content → `cursor_sprites_off()`
(prepare shadow OAM) → `screen_present(LCDC_MENU)`, which waits for
VBlank (hardware OAM updated by the system) and flips map + tile mode
+ sprite enable in **one** LCDC write. Old screen one frame, new
screen the next — nothing in between. Small updates (menu `>` marker,
single cells, cursor moves) skip all this and write straight to the
visible map or shadow OAM.

---

## 4. Game Boy graphics primer (the 10 ideas that explain `ui.c`)

1. **Screen**: 160x144 pixels, 4 grays, redrawn 60 times a second.
2. **Tiles**: everything is 8x8-pixel tiles. The screen shows 20x18
   of them. A tile pattern is 16 bytes (2 bits per pixel).
3. **Tile maps**: the layout ("which tile goes where") is a 32x32
   grid of tile *numbers* in memory. Only the 20x18 window at scroll
   (0, 0) is visible. This game never scrolls.
4. **Two maps**: the DMG has two tile maps, at `0x9800` and `0x9C00`.
   This game draws each new screen into the hidden one and flips —
   that is the entire anti-flicker trick (§3.5).
5. **Tile data areas**: tile *patterns* live at `0x8000` (numbers
   0–255 as-is) and `0x9000` (numbers 0–127 as-is). The grid owns
   `0x8000` (230 tiles + cursor); the font lives at `0x9000`.
   An LCDC bit selects which area the map numbers refer to.
6. **Sprites (OBJ)**: 40 small movable tiles on top of the
   background. The 4-piece cursor is sprites 0–3. Sprites 8x8,
   enabled only on game screens (menus switch them off entirely).
7. **OAM and shadow OAM**: sprite positions live in a special memory
   (OAM, `0xFE00`) that the CPU can only touch at safe moments.
   `move_sprite()` therefore writes to a normal-RAM copy — the
   **shadow OAM** — and the system copies it to hardware during
   VBlank. Consequence: always prepare sprite positions *before*
   presenting the screen that needs them.
8. **VBlank**: the short pause between two screen refreshes. The
   system uses it to copy sprites; `vsync()` in the main loop waits
   for it (that is what "60 fps" means here).
9. **VRAM windows**: outside VBlank the picture chip may be busy;
   GBDK's write helpers wait for a safe microsecond automatically
   (`WAIT_STAT`), so drawing takes a little longer but never
   corrupts — even with the LCD on.
10. **LCD on/off**: switching the LCD off blanks the screen and, done
    at the wrong moment, can *damage real hardware*. This program
    stops it exactly once (boot init, via GBDK `display_off()`, which
    waits for VBlank) and never again. If you add screens, use
    `begin_draw()` + `screen_present()` — never `display_off()`.

---

## 5. Safe changes (recipes)

- **Change help text / menu layout**: edit `draw_*_content()` in
  `src/ui.c`. Keep lines within 20 columns; numbers via `draw_dec3`
  (ERRORS/LEVEL) and `draw_num2` (PAGE). Marker pairs go through
  `marker_pair_set()`.
- **Change rules** (e.g. what counts as a mistake): edit `src/board.c`
  (+ `tests/test_host.c`), run `make test-host`. Never `#include`
  Game Boy headers there — PC testability is a feature.
- **Change controls**: edit the `*_update()` handlers in `src/main.c`.
  Single actions → `input_pressed`; movement → `input_dir`.
- **Change visuals**: edit `tools/gen_tiles.py`, run
  `make regen-tiles`, rebuild. Respect the quadrant/variant indexing
  documented in `src/tiles.h` (the generator must match it exactly).
- **Change/add levels**: edit `tools/gen_puzzles.py`, run
  `make regen-puzzles`. Every puzzle must keep a unique solution
  (the script asserts it; `make test-host` re-verifies).
- **Add a screen**: write `draw_*_content()` + a public `ui_*`
  following the begin → draw → OAM → present protocol, add the state
  or menu branch in `main.c`, extend `tools/smoke_pyboy.py`.

## 6. Debugging checklist

1. `make clean && make` — must build with **zero warnings**.
2. `make check` — size 32 KB, Nintendo logo OK, cart `0x03`
   (MBC1 + RAM + battery), 8 KB SRAM.
3. `make test-host` — `ALL HOST TESTS PASSED`.
4. `make test-emulator` — `SMOKE PASSED` (checks every transition
   frame-by-frame: no blank/mixed frame, OAM matches the screen).
5. `make run` — human eyeball pass in mGBA.
6. Regressions usually come from: drawing to the wrong map (check
   `use_hidden_map` routing), touching sprites after presenting (prepare
   OAM *before* `screen_present`), or changing generator indexing
   without updating `tiles.h`.

## 7. Glossary

- **BG / map** — background layer / tile-number layout in VRAM.
- **DMG** — the original 1989 Game Boy (Dot Matrix Game).
- **GBDK** — the C toolkit used here (vendored in `tools/gbdk/`).
- **LCDC** — the LCD control register (one byte, one bit per feature).
- **OAM** — sprite attribute memory (positions/tiles).
- **MBC1 (`0x03`)** — the cartridge mapper: bank switching plus a
  battery-kept SRAM at `0xA000-0xBFFF` (our save slot).
- **SRAM** — cartridge RAM kept alive by the battery (the save).
- **VBlank / vsync** — the between-frames pause / waiting for it.
- **VRAM** — video memory (tile patterns + maps).
