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

/* Load the prebuilt grid + cursor tiles into VRAM (fast copy).
 * Call on every game screen entry (menus overwrite them with the font). */
void tiles_load_grid(void);

/* Reload the GBDK font as the BG tileset. Call on every menu entry. */
void tiles_load_font(void);

/* Fill `out[4]` with the TL/TR/BL/BR tile indices for a cell value
 * (0 = empty, 1-9 = digit; `is_user` picks the gray shade) at grid
 * (row, col), borders included. */
void grid_cell_tiles(uint8_t value, uint8_t is_user, uint8_t row,
                     uint8_t col, uint8_t *out);

#endif /* TILES_H */
