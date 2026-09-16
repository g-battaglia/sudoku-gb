#ifndef TYPES_H
#define TYPES_H

/* ---------------------------------------------------------------------------
 * types.h — Global game types and constants.
 *
 * Target: Game Boy Classic (DMG, no color).
 * The background is a 32x32 tile map; the screen shows 20x18 8x8-pixel
 * tiles. Text is drawn as font tiles (no stdio), the grid from
 * precomputed tiles: no external graphic assets are needed.
 * -------------------------------------------------------------------------*/

#include <stdint.h>

/* --- Sudoku ---------------------------------------------------------------- */
#define GRID_SIZE 9    /* 9x9 grid. */
#define CELL_COUNT 81  /* 9*9 cells. */
#define BOX_SIZE 3     /* 3x3 boxes. */

/* --- Levels ----------------------------------------------------------------- */
/* LEVEL_COUNT lives in puzzles.h: it depends on DIFF_COUNT/DIFF_LEVELS
 * (3 difficulties x 100 levels each). */

/* --- Screen ----------------------------------------------------------------- */
#define SCREEN_COLS 20 /* GBDK font is 8x8: 160/8 = 20 columns. */
#define SCREEN_ROWS 18 /* 144/8 = 18 rows. */

#endif /* TYPES_H */
