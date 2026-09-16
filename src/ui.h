#ifndef UI_H
#define UI_H

/* ---------------------------------------------------------------------------
 * ui.h — All screen drawing. Text only, GBDK built-in font.
 *
 * Screen is 20x18 chars. Layout of the game screen:
 *
 *   row  0: "SUDOKU L01 EASY"      title + level + difficulty
 *   row  1: "ERRORS X X ."         mistake slots (X = used)
 *   row  2: blank
 *   rows 3-11: 9 grid rows         '.' = empty, digits otherwise.
 *               3x3 boxes split with extra spaces (no '|': the
 *               built-in font may lack it, spaces always work).
 *   row 12: "ENTER: 5"             proposed digit for the cursor cell
 *   row 13: "+-UP/DOWN NUM"        help line 1
 *   row 14: "A:OK B:DEL ST:MENU"   help line 2
 *   rows 15-17: messages           transient text ("MISTAKE!", ...)
 *
 * The cursor is shown by overwriting the 3 chars of the pointed cell
 * with "[x]". Only that cell is redrawn on move: no flicker.
 *
 * Hardware: uses font_init/gotoxy/printf/cls from GBDK.
 * -------------------------------------------------------------------------*/

#include "types.h"

/* Init font + display. Call once at startup. */
void ui_init(void);

/* --- Screens (each clears the screen and draws everything) --- */

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

/* Redraw the proposed digit on row 12. */
void ui_entry(uint8_t value);

/* Show a message on rows 15-17 (empty string clears). */
void ui_message(const char *text);

/* Redraw one cell without cursor brackets (digit or '.'). */
void ui_cell(uint8_t row, uint8_t col);

/* Move the cursor brackets from (old) to (new) cell. */
void ui_cursor(uint8_t old_row, uint8_t old_col, uint8_t new_row, uint8_t new_col);

#endif /* UI_H */
