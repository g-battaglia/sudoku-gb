# 11 — Game Boy hardware and GBDK in 10 ideas

You do not need to memorise hardware manuals to read `ui.c`. Ten ideas explain everything. Each ends with the exact repo spot that uses it.

## 0. Numbers first

LR35902 CPU @ ~4.19 MHz, 8 KB WRAM, 8 KB VRAM, screen 160×144 pixels, 4 grays, 60 frames/second. Background = 8×8-pixel tiles on a 32×32 grid; the screen shows a 20×18 window, never scrolled here. 40 sprites (movable tiles) on top. Polled input (`joypad()`), debounced in software. Cartridge: MBC1 + RAM + battery (`0x03`), 8 KB SRAM, 32 KB ROM, no banking.

## 1. Screen: 160×144, 4 grays, 60 fps

The picture is redrawn 60 times a second. `vsync()` in the main loop waits for the next refresh — that wait *is* the frame clock. Game logic runs once per frame; holding a button for 60 frames means 60 handler runs (hence edge detection, idea 9).

## 2. Tiles: everything is 8×8 pixels

Text, digits, grid lines — all are 8×8-pixel tile *patterns*, 16 bytes each (2 bits per pixel → 4 grays). A chunky game digit is a 2×-scaled 3×5 bitmap filling a 16×16-cell quadrant set. `tools/gen_tiles.py` bakes every cell variant offline; the C side only copies bytes and computes indices (`src/tiles.h` documents the quadrant/variant numbering the generator must match exactly).

## 3. Tile maps: layouts of tile *numbers*

VRAM holds patterns ("what pixels") separately from *maps* ("which tile goes where"): a 32×32 grid of tile numbers, of which 20×18 is visible at scroll (0,0). Writing `5` at map position (3,2) shows a copy of pattern 5 there. Menus write font-tile numbers (`ASCII c = tile c − 32`); the grid writes precomputed cell-quadrant numbers.

## 4. Two maps: the anti-flicker trick

The DMG has two background maps (`0x9800`, `0x9C00`). The game draws each full screen into the **hidden** one while the LCD shows the old one, then flips with a single LCDC write (`screen_present()` after `vsync()`): old screen one frame, new screen the next, nothing in between. No white flash, no half-drawn frame. Small updates (menu `>` marker, one cell, cursor) skip the protocol and write straight to the visible map — they are too small to tear.

Repo link: `ui_pause()` does begin-draw → clear + content → prepare shadow OAM → present. `ui_select_cursor()` / `ui_pause_cursor()` do 2 tile writes to the visible map. `ui_cell(r,c)` redraws one 2×2 cell in place.

## 5. Tile data areas: `0x8000` vs `0x9000`

Patterns live at `0x8000` (numbers 0–255 as-is) and `0x9000` (numbers 0–127 as-is); an LCDC bit selects which area the map numbers refer to. Layout here: grid owns `0x8000` (230 tiles + cursor at 240+), GBDK font at `0x9000`. Switching screens therefore swaps map + tile-mode + sprite-enable **together** in one LCDC value (`0x81/0x89` menus, `0x93/0x9B` game — bit 7 always set = LCD on).

GBDK gotcha, documented in `COMPACT.md`: `set_bkg_data` silently drops tiles 0–114 on big loads, so `tiles_load_grid()` hand-copies instead. When a library helper misbehaves at scale, a commented hand-roll with the reason beats a clever workaround.

## 6. Sprites (OBJ): the cursor

40 hardware sprites float above the background. The cursor is sprites 0–3 (four 8×8 corners forming a 16×16 outline, tiles 240–243). Moving the cursor never touches the background map — `ui_cursor(r,c)` only repositions sprites. Menus park all sprites and disable OBJ entirely (no stale cursor over text).

## 7. OAM and shadow OAM

Sprite positions live in OAM (`0xFE00`), touchable by the CPU only at safe moments. So `move_sprite()` writes to a normal-RAM copy — **shadow OAM** — and the system DMAs it to hardware during VBlank. Consequence: prepare sprite positions *before* presenting the screen that needs them (`ui_game_full` places the cursor, *then* presents). Touching sprites after presenting shows them one frame late — the classic "cursor lags" bug and the first thing to check in §12's debugging list.

## 8. VBlank and `vsync()`

VBlank is the short pause between refreshes when the picture chip is idle. The system copies shadow OAM then; `vsync()` waits for it. Frame order in `main.c`: poll input → run handler (logic + draw calls) → `frame++` → `vsync()`. Drawing functions only *queue* map/sprite writes (GBDK helpers wait internally); the visible flip happens at the next safe moment.

## 9. Input: no events, only "held right now"

`joypad()` returns a bitmask of currently held buttons — no press/release events, no queue. `src/input.c` builds events in software:

```c
now = joypad();
just_pressed = (uint8_t)(now & (uint8_t)~prev_state);  /* edge: new this frame */
/* D-Pad auto-repeat: immediate on press, then every 6 frames after 18 */
```

`input_pressed(mask)` = single actions (A confirm, B erase, START). `input_dir(mask)` = movement (first press + held repeat). `input_reset_combo()` fires once when A+B+START+SELECT *become* jointly held. Call `input_poll()` once per frame, first — every handler assumes fresh `just_pressed`. Forgetting the single call, or calling it twice, double-counts or drops presses.

## 10. LCD on/off: stop it once, never again

Clearing LCDC bit 7 outside VBlank can damage real DMG hardware (Pan Docs). So `ui_init()` calls GBDK `display_off()` (VBlank-safe) at boot, loads every resident pattern, parks sprites — and the **first presented screen turns the LCD on for good**. After that the LCD bit is never cleared: audit with `grep LCDC`: writes exist only in init (`0x13`, off) and present (`0x81/0x89/0x93/0x9B`, always on). Raw VRAM copies run only with LCD off; LCD-on writes go through `WAIT_STAT`-guarded helpers. If you add a screen, use `begin_draw()` + `screen_present()` — never `display_off()`.

## GBDK vocabulary (the library this repo uses)

GBDK-2020 4.5.0 (vendored in `tools/gbdk/`) provides: `joypad()` + `J_A/J_B/J_START/...`, `vsync()`, `display_off()`, `set_bkg_data/tiles`, `move_sprite()`, `ENABLE_RAM/DISABLE_RAM`, `font_ibm` (tiles 0–95). It also owns the boot sequence up to `main()` (the SGB-PAL `vsync()` warmup loop in `main.c` exists for real hardware). You call GBDK; you never reimplement the PPU.

## Exercises

1. Explain why `ui_game_full` must position sprites *before* `screen_present`, using shadow OAM timing. What would the player see on the first frame if the order were reversed?
2. A new developer adds `display_off()` before each screen "to be safe". Explain in two sentences why this is wrong (hardware risk + visual result) and what to use instead.
3. `input_dir` repeats held directions; `input_pressed` does not. Find one call site of each in `game_update()` and justify the choice (what breaks if digit-confirm repeated while held? what breaks if cursor movement did not repeat?).

Next: `12-real-project-traces-debug.md` — follow the code through boot, input, editing, HINT and saves.
