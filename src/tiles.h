#ifndef TILES_H
#define TILES_H

/* Tile layout summary (full docs in tiles.c):
 * BG tiles 96-176 = grid digits/empty x4 border variants + frame tiles
 * (font uses 0-95); sprite tile 240 = cursor outline. Thick 2px border
 * every 3rd row/col, thin 1px elsewhere. Only constants + API live here. */

#include "types.h"

/* VRAM index of the first grid tile (must follow the loaded font). */
#define GRID_TILE_BASE 96

/* Corner tile of the outer frame (top-left of the grid). */
#define FRAME_CORNER_TILE 176

/* Sprite slot and VRAM tile used for the cursor outline. */
#define CURSOR_SPRITE_ID 0
#define CURSOR_SPRITE_TILE 240

/* Generate all grid/frame/sprite tiles and load them into VRAM. */
void tiles_load(void);

/* Tile index for a cell value (0 = empty, 1-9 = digit).
 * `is_user` picks the gray player shade (ignored when value is 0).
 * `row`/`col` select the border variant (thick every 3rd line). */
uint8_t grid_tile(uint8_t value, uint8_t is_user, uint8_t row, uint8_t col);

/* Frame tile above grid column `col` (thick where a box gap is). */
uint8_t frame_tile_top(uint8_t col);

/* Frame tile left of grid row `row` (thick where a box gap is). */
uint8_t frame_tile_left(uint8_t row);

#endif /* TILES_H */
