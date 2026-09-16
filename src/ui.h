#ifndef UI_H
#define UI_H

/* ---------------------------------------------------------------------------
 * ui.h — All screen drawing.
 *
 * Two layers:
 * - GAME (custom tiles from tiles.h): a fullscreen 9x9 grid of 16x16 px
 *   cells at tile (GRID_X, GRID_Y) = (1, 0), i.e. 144x144 px = the whole
 *   screen height, centered with a 1-tile margin left and right.
 *   No header, no footer, no messages: the board IS the screen.
 *   Everything else (level, mistakes, help) lives in the START menu.
 * - MENUS (GBDK font): title, password entry, START menu, win screen.
 *
 * Feedback without text: the proposed digit blinks inside the cursor
 * cell (ui_preview); a rejected digit blinks the cursor off briefly
 * (main.c hides it for a few frames); mistakes are only tallied and
 * shown in the START menu — play never ends.
 * -------------------------------------------------------------------------*/

#include "types.h"

/* Grid origin on the background map (in tiles). */
#define GRID_X 1
#define GRID_Y 0

/* Init font + grid tiles + display. Call once at startup. */
void ui_init(void);

/* --- Screens (each hides the cursor and draws everything) --- */

/* Level select: all 12 levels, freely playable. `pos` = cursor (0-11),
 * `done[i]` = 1 shows level i+1 as complete (`*`). Session-only state:
 * a password for level N marks levels 1..N-1 complete and jumps there. */
void ui_select(uint8_t pos, const uint8_t *done);

/* Password entry. `digits[4]`, `pos` = edited slot, `bad` = show error. */
void ui_password(const uint8_t *digits, uint8_t pos, uint8_t bad);

/* Game screen: fullscreen grid + cursor sprite. No text at all. */
void ui_game_full(void);

/* START menu. `choice` 0 = RESUME, 1 = HINT, 2 = RESTART, 3 = TITLE.
 * Shows level, difficulty, mistake count and empty cells left. */
void ui_pause(uint8_t choice, uint8_t level);

/* Win screen. Shows password for next level, or completion text if last. */
void ui_win(uint8_t level, uint16_t next_password, uint8_t is_last);

/* --- Game screen updates (no full clear, called every frame) --- */

/* Redraw one cell (2x2 tiles) from the board state. */
void ui_cell(uint8_t row, uint8_t col);

/* Draw (`show` = 1) or erase (`show` = 0) the blinking proposed digit
 * on cell (row, col). The board is untouched: erase redraws the cell. */
void ui_preview(uint8_t row, uint8_t col, uint8_t value, uint8_t show);

/* Move the 4-sprite cursor outline to cell (row, col). */
void ui_cursor(uint8_t row, uint8_t col);

/* Hide the cursor sprites (menus, win, mistake blink). */
void ui_cursor_hide(void);

#endif /* UI_H */
