#ifndef UI_H
#define UI_H

/* ---------------------------------------------------------------------------
 * ui.h — All screen drawing.
 *
 * Two layers, switched explicitly (the game owns ALL BG tiles):
 * - GAME (custom tiles from tiles.h): a fullscreen 9x9 grid of 16x16 px
 *   cells at tile (GRID_X, GRID_Y) = (1, 0), i.e. 144x144 px = the whole
 *   screen height, with the outer frame in the side margins.
 *   No header, no footer, no messages: the board IS the screen.
 *   Everything else (level, mistakes, help) lives in the START menu.
 * - MENUS (GBDK font, reloaded on every menu entry): level select
 *   (100 levels, 10 per page), START menu, win screen.
 *
 * Two emulator-proofing rules (learned from garbled-screenshot bugs):
 * - ui_init() sets LCDC_REG explicitly: never depend on boot state.
 * - Numbers are printed with putchar helpers: GBDK printf mishandles
 *   %c mixed with later specifiers and ignores/fumbles flags, so every
 *   printf call uses at most one plain %s and no flags at all.
 *
 * Feedback without text: the picked digit blinks gray in the cursor cell
 * (ui_preview); a rejected digit blinks the cursor off briefly (main.c
 * hides it a few frames); mistakes are only tallied and shown in the
 * START menu — play never ends.
 * -------------------------------------------------------------------------*/

#include "types.h"

/* Grid origin on the background map (in tiles). */
#define GRID_X 1
#define GRID_Y 0

/* Levels per select page (10 pages of 10). */
#define LEVELS_PER_PAGE 10

/* Init font + LCDC + cursor sprites. Call once at startup. */
void ui_init(void);

/* --- Screens (each hides the cursor and draws everything) --- */

/* Level select: 100 free levels, 10 per page. `page` 0-9, `row` 0-9,
 * `done[i]` = 1 shows level i+1 as complete (`*`). Session-only. */
void ui_select(uint8_t page, uint8_t row, const uint8_t *done);

/* Game screen: fullscreen grid + margins. No text at all. */
void ui_game_full(void);

/* START menu. `choice` 0 = RESUME, 1 = HINT, 2 = RESTART, 3 = TITLE.
 * Shows level, mistake count and empty cells left. */
void ui_pause(uint8_t choice, uint8_t level);

/* Win screen (`is_last` = game completed). No passwords: all levels
 * are always playable, progress marks are session-only. */
void ui_win(uint8_t level, uint8_t is_last);

/* --- Game screen updates (no full clear, called every frame) --- */

/* Redraw one cell (2x2 tiles) from the board state (gray if user). */
void ui_cell(uint8_t row, uint8_t col);

/* Draw (`show` = 1, gray user digit) or erase (`show` = 0) the blinking
 * picked digit on cell (row, col). The board is untouched by this. */
void ui_preview(uint8_t row, uint8_t col, uint8_t value, uint8_t show);

/* Move the 4-sprite cursor outline to cell (row, col). */
void ui_cursor(uint8_t row, uint8_t col);

/* Hide the cursor sprites (menus, win, mistake blink). */
void ui_cursor_hide(void);

#endif /* UI_H */
