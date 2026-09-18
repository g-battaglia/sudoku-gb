#ifndef UI_H
#define UI_H

/* ---------------------------------------------------------------------------
 * ui.h — All screen drawing.
 *
 * Two resident layers (tile patterns loaded once at boot, see tiles.h):
 * - GAME: a fullscreen 9x9 grid of 16x16 px cells at tile (GRID_X,
 *   GRID_Y) = (1, 0), i.e. 144x144 px = the whole screen height, with
 *   the outer frame in the side margins. No header, no footer, no
 *   messages: the board IS the screen. Everything else (level,
 *   mistakes, help) lives in the START menu.
 * - MENUS: GBDK font at 0x9000: difficulty select, level select
 *   (100 levels per difficulty, 10 per page), START menu, win screen.
 *
 * Atomic screens: the DMG has two background maps, so every full
 * screen is drawn into the HIDDEN map while the LCD keeps showing the
 * old one, then one LCDC write swaps map + tile mode + OBJ enable at
 * the next frame start (after the VBlank ISR copied the prepared
 * shadow OAM). No white flash, no half-drawn frame, no stale sprites.
 * The LCD is stopped exactly once, in ui_init() via GBDK display_off()
 * (VBlank-safe: clearing LCDC.7 outside VBlank can damage a real DMG).
 * Plain navigation (cursor markers, game cursor) only touches the
 * visible map or shadow OAM.
 *
 * Menus use no stdio at all: text is written as font tiles through
 * the VRAM-safe map helpers (ASCII c = tile c - 32).
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

/* Levels per select page (10 pages of 10 per difficulty). */
#define LEVELS_PER_PAGE 10

/* Select pages (DIFF_LEVELS / LEVELS_PER_PAGE). */
#define SELECT_PAGE_COUNT (DIFF_LEVELS / LEVELS_PER_PAGE)

/* START menu items (single source of truth: main.c and ui.c share it).
 * Order: RESUME / HINT / SAVE / PLAY AGAIN / MENU. */
#define UI_PAUSE_COUNT 5

/* Init all video state once (tiles, maps, sprites). The LCD stays off:
 * the first screen (ui_select from main) presents it. */
void ui_init(void);

/* --- Screens (each hides the cursor and draws everything) --- */

/* Difficulty select: EASY / MEDIUM / HARD (100 levels each) plus a
 * LOAD row when `has_load` is set (valid battery save found).
 * `choice` may be DIFF_COUNT to point at LOAD. */
void ui_diff(uint8_t choice, uint8_t has_load);

/* Difficulty navigation (call after ui_diff, LCD stays on, no reload).
 * Index DIFF_COUNT addresses the LOAD row. */
void ui_diff_cursor(uint8_t old_choice, uint8_t new_choice);

/* Level select: 100 levels of one difficulty, 10 per page. `page` 0-9,
 * `row` 0-9, `marks` is the battery-saved completion bitmap
 * (marks_get(marks, level) shows level+1 as complete `*`). */
void ui_select(uint8_t page, uint8_t row, const uint8_t *marks,
               uint8_t diff);

/* Select navigation (call after ui_select, LCD stays on, no reload):
 * move the `>` status char, or redraw the page rows on page change. */
void ui_select_cursor(uint8_t page, uint8_t old_row, uint8_t new_row,
                       const uint8_t *marks, uint8_t diff);
void ui_select_page(uint8_t page, uint8_t row, const uint8_t *marks,
                    uint8_t diff);

/* Game screen: fullscreen grid + margins, cursor placed by us (no caller
 * can show a game frame before its OAM is ready). No text at all. */
void ui_game_full(uint8_t row, uint8_t col);

/* START menu. `choice` 0 = RESUME, 1 = HINT, 2 = SAVE, 3 = PLAY AGAIN,
 * 4 = MENU (see UI_PAUSE_COUNT). Shows the level (number within the
 * difficulty, 0-99), difficulty and mistake count (passed in: ui draws
 * only, it never reads the board for status lines). */
void ui_pause(uint8_t choice, uint8_t lid, uint8_t diff, uint8_t mistakes);

/* Pause navigation (call after ui_pause, LCD stays on, no reload). */
void ui_pause_cursor(uint8_t old_choice, uint8_t new_choice);

/* Save confirmation screen: any button returns to the game. */
void ui_saved(void);

/* Win screen (`num` = level number within the difficulty, 0-99;
 * `is_last` = difficulty completed; `mistakes` = final tally shown
 * unless is_last. Completion marks are stored in the battery save
 * by main.c, not here). */
void ui_win(uint8_t num, uint8_t is_last, uint8_t mistakes);

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
