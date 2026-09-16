#ifndef TILES_H
#define TILES_H

/* Tile summary: each cell is 16x16 px = 2x2 BG tiles. 10 contents
 * (empty + digits 1-9, all black) x 4 border variants per quadrant:
 * TL tiles 96-135, TR 136-175, BL 176-215, BR 216-255 (all free VRAM).
 * Borders: 2px outer frame + 2px box gaps, 1px thin cell lines.
 * Cursor = 4 sprites (8x8 corner tiles 240-243). Full docs: tiles.c. */

#include "types.h"

/* First sprite id and first sprite VRAM tile of the 16x16 cursor
 * (uses 4 consecutive sprite ids and 4 consecutive sprite tiles). */
#define CURSOR_SPRITE_ID 0
#define CURSOR_SPRITE_TILE 240

/* Generate all grid + cursor tiles and load them into VRAM. */
void tiles_load(void);

/* Fill `out[4]` with the TL/TR/BL/BR tile indices for a cell value
 * (0 = empty, 1-9 = digit) at grid (row, col), borders included. */
void grid_cell_tiles(uint8_t value, uint8_t row, uint8_t col, uint8_t *out);

#endif /* TILES_H */
