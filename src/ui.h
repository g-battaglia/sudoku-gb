#ifndef UI_H
#define UI_H

/* ---------------------------------------------------------------------------
 * ui.h — All screen drawing.
 *
 * Two layers:
 * - TEXT (GBDK font): header, footer, menus, messages.
 * - GRID (custom tiles from tiles.h): the 9x9 board starting at
 *   tile (GRID_X, GRID_Y) = (5, 3), one tile per cell, plus a 1-tile
 *   frame on the top and left. Thin 1px borders inside a 3x3 box,
 *   thick 2px borders between boxes and around the grid.
 *
 * Game screen layout (20x18 tile rows):
 *   row  0: "SUDOKU L01 EASY"   title + level + difficulty
 *   row  1: "ERRORS X X ."      mistake slots (X = used)
 *   rows 3-12: grid + top frame (frame row 3, cells rows 4-12)
 *   row 13: "ENTER: 5"          proposed digit for the cursor cell
 *   row 14: "UP/DN NUM A:OK"    help line 1
 *   row 15: "B:DEL START:MENU"  help line 2
 *   row  16: messages        transient text ("MISTAKE!", ...)
 *
 * Cells with a digit use black (givens) or dark gray (player) tiles;
 * empty cells are blank tiles. The cursor is a sprite outline over
 * the cell: it never erases grid lines.
 * -------------------------------------------------------------------------*/

#include "types.h"

/* Grid origin on the background map (in tiles). */
#define GRID_X 5
#define GRID_Y 3

/* Init font + grid tiles + display. Call once at startup. */
void ui_init(void);

/* --- Screens (each hides the cursor and draws everything) --- */

/* Title screen. `choice` 0 = NEW GAME, 1 = PASSWORD. */
void ui_title(uint8_t choice);

/* Password entry. `digits[4]`, `pos` = edited slot, `bad` = show error. */
void ui_password(const uint8_t *digits, uint8_t pos, uint8_t bad);

/* Game screen frame: header, full grid, footer. Redraws everything. */
void ui_game_full(uint8_t level);

/* Pause menu. `choice` 0 = RESUME, 1 = RESTART, 2 = TITLE. */
void ui_pause(uint8_t choice);

/* Win screen. Shows password for next level, or completion text if last. */
void ui_win(uint8_t level, uint16_t next_password, uint8_t is_last);

/* Game over screen. `choice` 0 = RETRY, 1 = TITLE. */
void ui_gameover(uint8_t choice);

/* --- Game screen updates (no full clear, called every frame) --- */

/* Redraw the mistake slots on row 1. */
void ui_mistakes(void);

/* Redraw the proposed digit on row 13. */
void ui_entry(uint8_t value);

/* Show a message on row 16 (empty string clears the line). */
void ui_message(const char *text);

/* Redraw one cell tile (digit or empty, keeps the borders). */
void ui_cell(uint8_t row, uint8_t col);

/* Move the cursor outline to cell (row, col). */
void ui_cursor(uint8_t row, uint8_t col);

/* Hide the cursor sprite (menus, win, game over). */
void ui_cursor_hide(void);

#endif /* UI_H */
