#ifndef TILES_H
#define TILES_H

/* Tile summary: each cell is 16x16 px = 2x2 BG tiles. 19 contents:
 * empty + givens 1-9 (black) + user digits 1-9 (dark gray, subtle shade).
 * Box/frame lines are black 2px, inner cell lines dark gray 2px
 * (uniform width: no thin/bold wobble possible).
 * Left/right outer frame lives in the side margin columns (dedicated
 * tiles), so quadrants only encode top/bottom/right flags:
 * TL tiles 0-37, TR 38-113, BL 114-151, BR 152-227, margins 228-229.
 * The game screen owns ALL BG tiles: menus reload the font on entry.
 * Cursor = 4 sprites (8x8 corner tiles 240-243). Full docs: tiles.c. */

#include "types.h"

/* Quadrant tile bases and total count in VRAM (game screen). */
#define TL_BASE 0
#define TR_BASE 38
#define BL_BASE 114
#define BR_BASE 152
#define TILE_COUNT 230

/* Margin tiles: 2px vertical frame lines for columns 0 and 19. */
#define MARGIN_LEFT_TILE 228
#define MARGIN_RIGHT_TILE 229

/* First sprite id and first sprite VRAM tile of the 16x16 cursor
 * (uses 4 consecutive sprite ids and 4 consecutive sprite tiles). */
#define CURSOR_SPRITE_ID 0
#define CURSOR_SPRITE_TILE 240

/* Prebuilt tile data (src/tiles_gen.c, GENERATED): 230 grid tiles +
 * 4 cursor tiles, 2bpp bytes ready for set_bkg/sprite_data. */
extern const uint8_t GRID_TILES[230 * 16];
extern const uint8_t CURSOR_TILE_DATA[4 * 16];

/* Resident VRAM layout (loaded once by ui_init, never reloaded):
 * 0x8000-0x8E5F  grid tiles 0-229 (game screens, unsigned BG addressing)
 * 0x8F00-0x8F3F  cursor sprite tiles 240-243 (sprites always unsigned)
 * 0x9000-0x95FF  font tiles 0-95 (menus, signed BG addressing),
 *                  placed there by GBDK font_load in signed text mode.
 * Only the grid + cursor are copied by us (to 0x8000); the font is
 * never touched after loading. */
#define FONT_TILE_COUNT 96

/* Boot/init LCDC mode: unsigned tile data (so font_load lands the font
 * at 0x8000 tiles 0-95), map 0x9800, sprites on, background on, LCDC.7
 * clear. Written while the LCD is stopped. */
#define LCDC_OFF_MODE 0x13

/* Load every tile pattern once. Call with the display off (ui_init). */
void tiles_load_resident(void);

/* Fill `out[4]` with the TL/TR/BL/BR tile indices for a cell value
 * (0 = empty, 1-9 = digit; `is_user` picks the gray shade) at grid
 * (row, col), borders included. */
void grid_cell_tiles(uint8_t value, uint8_t is_user, uint8_t row,
                     uint8_t col, uint8_t *out);

#endif /* TILES_H */
