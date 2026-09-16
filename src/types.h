#ifndef TYPES_H
#define TYPES_H

/* ---------------------------------------------------------------------------
 * types.h — Global game types and constants.
 *
 * Target: Game Boy Classic (DMG, no color).
 * The background is a 32x32 tile map; the screen shows 20x18 8x8-pixel
 * tiles. We only use the GBDK built-in font (font_init / printf / gotoxy),
 * so no external graphic assets are needed.
 * -------------------------------------------------------------------------*/

#include <stdint.h>

/* --- Sudoku ---------------------------------------------------------------- */
#define GRID_SIZE 9    /* 9x9 grid. */
#define CELL_COUNT 81  /* 9*9 cells. */
#define BOX_SIZE 3     /* 3x3 boxes. */

/* --- Levels ----------------------------------------------------------------- */
#define LEVEL_COUNT 12 /* 4 EASY + 4 MEDIUM + 4 HARD. */
#define MAX_ERRORS 3   /* 3 mistakes = game over. */

/* --- Passwords (4 decimal digits) ------------------------------------------- */
#define PASSWORD_DIGITS 4

/* --- Screen ----------------------------------------------------------------- */
#define SCREEN_COLS 20 /* GBDK font is 8x8: 160/8 = 20 columns. */
#define SCREEN_ROWS 18 /* 144/8 = 18 rows. */

#endif /* TYPES_H */
