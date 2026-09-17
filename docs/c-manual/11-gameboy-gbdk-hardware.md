# 11 — Game Boy hardware and GBDK in 10 ideas

You do not need to memorise hardware manuals to read `ui.c`. Ten ideas explain everything, each with the mechanism, the numbers, and the exact repo spot that uses it. Read this chapter with `src/ui.h`'s header comment open — it is the one-page summary; this is the ten-page version.

## 0. Numbers first (the whole machine in one paragraph)

LR35902 CPU @ ~4.19 MHz (≈ 70 000 cycles per 60 Hz frame), 8 KB WRAM, 8 KB VRAM, 160 B OAM, screen 160×144 pixels, 4 grays, 60 frames/second. Background = 8×8-pixel tiles on a 32×32 grid; the screen shows a 20×18 window, never scrolled here. 40 sprites (movable 8×8 tiles) on top. Polled input (`joypad()`), debounced in software at 60 fps. Cartridge: MBC1 + RAM + battery (`0x03`), 8 KB SRAM at `0xA000`, 32 KB ROM with no banking (2 banks, mapper mostly idle). Valid ROM header (Nintendo logo at `0x104`, checksums) — the boot ROM verifies the logo before running anything.

## 1. Screen: 160×144, 4 grays, 60 fps

The picture is redrawn 60 times a second, top to bottom, like an old TV. `vsync()` in the main loop waits for the next refresh — that wait *is* the frame clock: game logic runs once per frame, animations tick in frames (blink every 32 frames, mistake flash 24 frames, locked feedback 20 frames — all constants in `main.c`, all multiples of the frame). Holding a button for 60 frames means 60 handler runs (hence edge detection, §9). Four grays (white/light/dark/black, 2 bits per pixel) are the entire palette: "gray player digits" vs "black clues" is shade selection within those four, baked into tile patterns by `gen_tiles.py`.

## 2. Tiles: everything is 8×8 pixels, 16 bytes, 2 bits per pixel

Text, digits, grid lines, margins — all are 8×8-pixel tile *patterns*. Each tile is 16 bytes: 8 rows × 2 bytes, where each pixel's 2-bit shade comes from combining one bit from each byte (bit-plane format):

```text
row bytes:  low  = b0 b1 b2 b3 b4 b5 b6 b7
            high = B0 B1 B2 B3 B4 B5 B6 B7
pixel i shade = (Bi << 1) | bi   (0=white .. 3=black)
```

A chunky game digit is a 2×-scaled 3×5 bitmap (6×10 pixels) centred in a 16×16 cell = 2×2 tiles, with 2-pixel borders baked per quadrant. `tools/gen_tiles.py` paints every combination offline: 19 contents (empty + 9 black givens + 9 gray player digits) × 12 quadrant/border variants = 228 tiles + 2 margin tiles = 230 at VRAM 0–229, plus 4 cursor corners at 240–243. The C side never draws a pixel — it only copies bytes once (`tiles_load_grid`) and computes indices (`src/tiles.h` documents the quadrant/variant numbering the generator must match exactly; drift = scrambled grid, chapter 12 §6 regression 3).

> **Python vs C vs hardware:** Python would `pygame.draw.line(...)` per frame. Here Python (`gen_tiles.py`) draws once at build time into bytes, and C copies the bytes once at boot. Runtime drawing is reduced to tile-number writes (§3–§4) — the frame budget (§4 chapter 04: ~70k cycles) is spent on logic, never rasterisation.

## 3. Tile maps: layouts of tile *numbers*

VRAM holds patterns ("what pixels", §2) separately from *maps* ("which tile goes where"): a 32×32 grid of tile numbers per map, of which the 20×18 window at scroll (0,0) is visible. Writing `5` at map position (3,2) shows a copy of pattern 5 there — cheap (one byte), instant, tear-free for small writes. Two vocabularies share the word "tile": *pattern* (pixels) vs *map entry* (number). Menus write font-tile numbers (`ASCII c = tile c − 32`, GBDK `font_ibm` = tiles 0–95); the grid writes precomputed cell-quadrant numbers from `tiles.c`'s index math. `GRID_X = 1, GRID_Y = 0` (`ui.h:38`) puts the 18-wide grid (9 cells × 2 tiles) at map column 1: 144 pixels wide + 1-tile margins = 160. The arithmetic of the layout *is* the centring.

## 4. Two maps: the anti-flicker trick (the whole design in one idea)

The DMG has two background maps (`0x9800`, `0x9C00`); one LCDC bit selects the visible one. The game draws each full screen into the **hidden** map while the LCD keeps showing the old one, then flips with a single LCDC write (`screen_present()`, called right after `vsync()` so the VBlank ISR has copied the prepared shadow OAM first): old screen one frame, new screen the next, nothing in between. No white flash, no half-drawn frame, no stale sprites — the three symptoms the headless tests (`smoke_pyboy.py`) check frame-by-frame (old-or-new only, output stable, OAM matches the new screen).

The protocol, every full screen (`ui_pause` as the example):

```text
begin_draw()          route subsequent writes to the HIDDEN map
clear + draw content  font text / grid cells, all off-screen
prepare shadow OAM    cursor placed (game) or parked + OBJ-off (menus) — BEFORE present
screen_present(mode)  vsync(); one LCDC write: map + tile area + OBJ enable
```

Small updates skip the protocol and write straight to the visible map — they are too small to tear: menu `>` markers (`ui_select_cursor`/`ui_pause_cursor`: 2 tile writes), one cell (`ui_cell`: 2×2 tiles from board state), cursor moves (sprites only, §6 — the background never changes). Full redraws are rare (screen transitions); deltas are per-frame. That frequency split is why the game feels instant at 4 MHz.

## 5. Tile data areas: `0x8000` vs `0x9000`, and the LCDC byte

Patterns live at `0x8000` (tile numbers 0–255 address patterns as-is) and `0x9000` (numbers 0–127 address as-is; 128–255 go negative — signed addressing). One LCDC bit selects the area the map numbers refer to. Layout here: grid owns `0x8000` (230 tiles + cursor at 240+), GBDK font at `0x9000`. Switching screens therefore swaps map + tile-mode + sprite-enable **together** in one LCDC value:

```text
bit 7  LCD enable      always 1 after boot (never cleared — §10)
bit 5  window enable   0 (no window layer used)
bit 4  tile area       0 = 0x8800/0x9000 mode (menus/font), 1 = 0x8000 mode (game grid)
bit 3  spare           -
bit 2  OBJ size        0 = 8x8 sprites
bit 1  OBJ enable      1 on game screens, 0 on menus (no stale cursor)
bit 0  BG/map select   which of 0x9800/0x9C00 is visible (the flip bit)

0x81/0x89  menus  (area 0x9000, OBJ off/on variants)
0x93/0x9B  game   (area 0x8000, OBJ on; map bit differs)
0x13       init   (LCD OFF — the only off value, boot only)
```

GBDK gotcha, documented in `COMPACT.md` §4.1: `set_bkg_data` silently drops tiles 0–114 on big loads, so `tiles_load_grid()` hand-copies instead (raw copy needs LCD off — another reason init stops the LCD twice around the font load, chapter 12 §2). When a library helper misbehaves at scale, a commented hand-roll with the reason beats a clever workaround — and the comment must name the failure, not just the fix.

## 6. Sprites (OBJ): the 4-piece cursor

40 hardware sprites (8×8 pixels each, 4 grays with transparent colour 0) float above the background. The cursor is sprites 0–3: four 8×8 corner tiles (patterns 240–243) forming a 16×16 outline around the selected cell. Moving the cursor never touches the background map — `ui_cursor(r,c)` only repositions sprites (4 OAM writes via shadow, §7). Menus park all 40 sprites off-screen and disable OBJ entirely (LCDC bit 1 = 0): no stale cursor over text, no sprite budget spent where unneeded. Blinks are sprite visibility games: `ui_cursor_hide()` + a frame counter (`flash = 24/20`) instead of redrawing anything — feedback at the cost of 4 bytes, not 4 tiles.

Sprite limits you do not hit but should know: 10 sprites per scanline max, 40 total — the design uses 4, always 8×8, never near limits. Constraints respected by margin, not by tuning.

## 7. OAM and shadow OAM: why order matters

Sprite positions live in OAM (`0xFE00–0xFE9F`: 40 × 4 bytes: y, x, tile, flags), touchable by the CPU only during VBlank/HBlank. So `move_sprite()` writes to a normal-RAM copy — **shadow OAM** — and the system's VBlank ISR DMAs it to hardware each refresh. Consequence chain:

1. Prepare sprite positions *before* presenting the screen that needs them (`ui_game_full` places the cursor, *then* presents).
2. Touching sprites after presenting shows them one frame late — the classic "cursor lags / ghost cursor" bug, first item in chapter 12's sprite regression.
3. Parking must also precede the present that hides them (menus park + OBJ-off in the same atomic write).

The rule compresses to: **OAM is staged, maps are written; both land together at present**. Any new screen that shows sprites must follow begin → draw → *stage OAM* → present, in that order, and chapter 12's safe-change recipe for new screens repeats it verbatim.

## 8. VBlank, HBlank, STAT and `vsync()`

The LCD draws top-to-bottom (~144 lines), then rests: **VBlank** (~10 lines' time) — the only long safe window, when the ISR copies shadow OAM and `vsync()` returns. Between lines, tiny **HBlank** gaps allow GBDK's `WAIT_STAT`-guarded helpers (`set_bkg_tiles`, `set_vram_byte`) to sneak single writes safely with the LCD on — drawing "takes a little longer but never corrupts" (`DEVELOPMENT.md` §4.9). Raw bulk copies have no such guard and run only with the LCD off (boot init). Frame order in `main.c` falls out of this physics: poll input → run handler (logic + queued draw calls) → `frame++` → `vsync()` (commit point). Drawing functions *queue* into maps/OAM; the visible commit happens at the next safe moment. Thinking "draw = immediate" is the PC habit to unlearn; on hardware, draw = stage, present = commit.

## 9. Input: no events, only "held right now" (edge + repeat from bits)

`joypad()` returns a bitmask of currently held buttons — no press/release events, no queue, no timestamps. `src/input.c` (93 lines) builds the entire event layer in software:

```c
now = joypad();
just_pressed = (uint8_t)(now & (uint8_t)~prev_state);  /* edge: new this frame (ch.07 C) */
combo_fire = (uint8_t)((now & COMBO_MASK) == COMBO_MASK &&
                       (prev_state & COMBO_MASK) != COMBO_MASK);  /* chord became complete */
if ((now & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) == 0) repeat_count = 0;      /* released */
else if (just_pressed & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) repeat_count = 0; /* new press */
else if (repeat_count < 255) repeat_count++;                                /* held: saturate */
```

`input_pressed(mask)` = `just_pressed & mask`: single actions (A confirm, B erase/cancel, START, SELECT). `input_dir(mask)` = first press immediately, then repeat every 6 frames after an 18-frame delay (`REPEAT_DELAY`/`REPEAT_RATE` — 0.3 s pause, 10 steps/s, the feel of every held cursor). `input_reset_combo()` fires once when A+B+START+SELECT *become* jointly held (edge on the chord, not the level — holding it does not reset twice).

Contract, violated at your peril: call `input_poll()` **once per frame, first** — every handler assumes fresh `just_pressed`. Zero calls = dead input; two calls = second call sees no edges (everything already consumed into `prev_state`). `input_poll_init()` zeroes the state at boot so the first frame has no phantom edges from power-on garbage.

## 10. LCD on/off: stop it once, never again (hardware safety)

Clearing LCDC bit 7 outside VBlank can *damage real DMG hardware* (Pan Docs — this is physical, not stylistic). So `ui_init()` calls GBDK `display_off()` (which waits for VBlank first) at boot, loads every resident pattern, parks sprites — and the **first presented screen turns the LCD on for good**. After that the LCD bit is never cleared. Audit with one grep: LCDC writes exist only in init (`0x13`, off) and present (`0x81/0x89/0x93/0x9B`, bit 7 always set). Static audit beats code review here — the invariant is enumerable: five values, one with bit 7 clear, and it runs once at boot. If you add a screen, use `begin_draw()` + `screen_present()` — never `display_off()`. The developer who adds "just one more" display_off for safety introduces the only hardware-damage vector in the program.

## GBDK vocabulary (the library this repo uses)

GBDK-2020 4.5.0 (vendored in `tools/gbdk/`, gitignored, fetched by `make setup-gbdk`) provides: `joypad()` + `J_A/J_B/J_UP/…` bit constants, `vsync()`, `display_off()`, `set_bkg_data` / `set_bkg_tiles` / `set_vram_byte`, `move_sprite()`, `ENABLE_RAM`/`DISABLE_RAM`, `font_ibm` (tiles 0–95, `c - 32` mapping), and the boot crt0 up to `main()` (the SGB-PAL `vsync()` warmup loop in `main.c` exists because real Super Game Boy PAL hardware needs settled frames before LCD work). You call GBDK; you never reimplement the PPU, the ISR, or the startup code. SDCC strictness (§5 chapter 09) rides along: what GBDK headers declare, SDCC's dialect must accept.

Next: `12-real-project-traces-debug.md` — follow the code through boot, input, editing, HINT and saves.
