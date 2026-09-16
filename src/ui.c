#include <gbdk/platform.h>
#include <gbdk/font.h>

#include "ui.h"
#include "board.h"
#include "puzzles.h"
#include "tiles.h"

/* ---------------------------------------------------------------------------
 * ui.c — Fullscreen grid + text menus. One screen per function.
 *
 * The game screen is only tiles: each cell is 2x2 BG tiles drawn with
 * set_bkg_tiles at (GRID_X + col*2, GRID_Y + row*2), plus the frame
 * margins at columns 0 and 19. The cursor is 4 sprites (a 16x16
 * outline) moved with move_sprite: sprite coords need +8/+16 offset
 * (DEVICE_SPRITE_PX_OFFSET_X/Y).
 *
 * TEXT WITHOUT STDIO: menus never use printf/gotoxy/cls (GBDK varargs
 * plus console state caused garbled screens). Every character is
 * written as a font tile (ASCII c = tile c - 32) through the VRAM-safe
 * set_bkg_* family, which works with the LCD on or off.
 *
 * LCD SAFETY (real DMG hardware): clearing LCDC bit 7 outside VBlank
 * can damage the LCD, so every GAME<->MENU switch goes through
 * screen_begin()/screen_end(): GBDK display_off() (waits for VBlank)
 * first, full tile + map redraw while off, DISPLAY_ON last. Plain
 * navigation never touches the LCD: menus redraw only the cells that
 * changed (ui_select_cursor/ui_select_page/ui_pause_cursor) and the
 * game cursor is sprite-only.
 * -------------------------------------------------------------------------*/

/* LCDC mode we always run in: LCD on, tiles at 0x8000, map at 0x9800,
 * sprites 8x8 on, background on. Written once at boot; transitions
 * only toggle the LCD bit through display_off()/DISPLAY_ON. */
#define LCDC_MODE 0x93

/* Same mode with the LCD bit clear (used while a transition redraws). */
#define LCDC_OFF_MODE 0x13

/* Blank map tile: font tile 0 (ASCII space). */
#define FONT_BLANK 0

/* One map row of the grid: 9 cells x 2 tiles wide, 2 tile rows. */
static uint8_t grid_map_row[9 * 2 * 2];

/* One margin column (18 tiles, filled with one margin tile). */
static uint8_t margin_col[SCREEN_ROWS];

/* Scratch tile indices for one cell (TL, TR, BL, BR). */
static uint8_t cell_tiles[4];

/* Scratch tile indices for one line of text (max one screen row). */
static uint8_t text_tiles[SCREEN_COLS];

/* Begin an atomic screen transition (GAME<->MENU): stop the LCD the
 * ONLY hardware-safe way, then set the mode bits with the LCD bit
 * kept clear. Never call nested or from an ISR; the VBlank interrupt
 * must stay enabled (the main loop never disables it). */
static void screen_begin(void)
{
    if (LCDC_REG & LCDCF_ON) {
        display_off();
    }
    LCDC_REG = LCDC_OFF_MODE;
}

/* End a transition: the new screen is fully drawn, turn the LCD on.
 * Re-enabling is safe at any time (the first frame stays blank). */
static void screen_end(void)
{
    DISPLAY_ON;
}

/* Draw a NUL-terminated string at map (x, y). Clipped to the row end.
 * Safe with the LCD on or off. */
static void draw_text(uint8_t x, uint8_t y, const char *s)
{
    uint8_t n;

    n = 0;
    while (s[n] != 0 && (uint8_t)(x + n) < SCREEN_COLS) {
        text_tiles[n] = (uint8_t)(s[n] - 32);
        n++;
    }
    if (n > 0) {
        set_bkg_tiles(x, y, n, 1, text_tiles);
    }
}

/* Draw unsigned 0-255 with leading zeros (exactly 3 digits). Returns
 * the width (3). */
static uint8_t draw_dec3(uint8_t x, uint8_t y, uint8_t n)
{
    text_tiles[0] = (uint8_t)('0' + (uint8_t)(n / 100) - 32);
    text_tiles[1] = (uint8_t)('0' + (uint8_t)((n / 10) % 10) - 32);
    text_tiles[2] = (uint8_t)('0' + (uint8_t)(n % 10) - 32);
    set_bkg_tiles(x, y, 3, 1, text_tiles);
    return 3;
}

/* Draw unsigned 0-99 in 2 chars (leading space). Returns the width (2).
 * Fixed width keeps the PAGE line stable across page changes. */
static uint8_t draw_num2(uint8_t x, uint8_t y, uint8_t n)
{
    text_tiles[0] = (uint8_t)((n >= 10 ? '0' + (uint8_t)(n / 10) : ' ') - 32);
    text_tiles[1] = (uint8_t)('0' + (uint8_t)(n % 10) - 32);
    set_bkg_tiles(x, y, 2, 1, text_tiles);
    return 2;
}

/* Draw unsigned 0-255 without padding (1-3 digits). Returns the width. */
static uint8_t draw_num(uint8_t x, uint8_t y, uint8_t n)
{
    uint8_t w;

    w = 0;
    if (n >= 100) {
        text_tiles[w] = (uint8_t)('0' + (uint8_t)(n / 100) - 32);
        w++;
    }
    if (n >= 10) {
        text_tiles[w] = (uint8_t)('0' + (uint8_t)((n / 10) % 10) - 32);
        w++;
    }
    text_tiles[w] = (uint8_t)('0' + (uint8_t)(n % 10) - 32);
    w++;
    set_bkg_tiles(x, y, w, 1, text_tiles);
    return w;
}

/* Draw one select row (fixed 14 tiles at column 3): marker, number,
 * difficulty (padded to 6), completion mark. */
static void select_draw_row(uint8_t page, uint8_t i, uint8_t row,
                            const uint8_t *done)
{
    uint8_t n, k;
    const char *name;

    n = (uint8_t)(page * LEVELS_PER_PAGE + i);
    text_tiles[0] = (uint8_t)((i == row ? '>' : ' ') - 32);
    text_tiles[1] = (uint8_t)(' ' - 32);
    text_tiles[2] = (uint8_t)('0' + (uint8_t)((n + 1) / 100) - 32);
    text_tiles[3] = (uint8_t)('0' + (uint8_t)(((n + 1) / 10) % 10) - 32);
    text_tiles[4] = (uint8_t)('0' + (uint8_t)((n + 1) % 10) - 32);
    text_tiles[5] = (uint8_t)(' ' - 32);
    name = difficulty_name(puzzles[n].difficulty);
    k = 0;
    while (k < 6 && name[k] != 0) {
        text_tiles[6 + k] = (uint8_t)(name[k] - 32);
        k++;
    }
    while (k < 6) {
        text_tiles[6 + k] = (uint8_t)(' ' - 32);
        k++;
    }
    text_tiles[12] = (uint8_t)(' ' - 32);
    text_tiles[13] = (uint8_t)((done[n] ? '*' : ' ') - 32);
    set_bkg_tiles(3, (uint8_t)(3 + i), 14, 1, text_tiles);
}

/* Draw the PAGE line (fixed width, column 6 row 2). */
static void select_draw_page(uint8_t page)
{
    draw_text(6, 2, "PAGE ");
    draw_num2(11, 2, (uint8_t)(page + 1));
    draw_text(13, 2, "/10");
}

/* Draw grid row `row` (2 tile rows) into the background map. */
static void draw_grid_row(uint8_t row)
{
    uint8_t c, idx;

    for (c = 0; c < GRID_SIZE; c++) {
        idx = (uint8_t)(row * GRID_SIZE + c);
        grid_cell_tiles(board_get(idx), !board_is_original(idx), row, c,
                        cell_tiles);
        grid_map_row[c * 2] = cell_tiles[0];
        grid_map_row[c * 2 + 1] = cell_tiles[1];
        grid_map_row[18 + c * 2] = cell_tiles[2];
        grid_map_row[18 + c * 2 + 1] = cell_tiles[3];
    }
    set_bkg_tiles(GRID_X, (uint8_t)(GRID_Y + row * 2), 18, 2, grid_map_row);
}

/* Move the 4 cursor sprites over grid cell (row, col). Sprite-only:
 * safe at any time (shadow OAM is copied during VBlank). */
static void cursor_place(uint8_t row, uint8_t col)
{
    uint8_t x, y;

    x = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + (GRID_X + col * 2) * 8);
    y = (uint8_t)(DEVICE_SPRITE_PX_OFFSET_Y + (GRID_Y + row * 2) * 8);
    move_sprite((uint8_t)(CURSOR_SPRITE_ID + 0), x, y);
    move_sprite((uint8_t)(CURSOR_SPRITE_ID + 1), (uint8_t)(x + 8), y);
    move_sprite((uint8_t)(CURSOR_SPRITE_ID + 2), x, (uint8_t)(y + 8));
    move_sprite((uint8_t)(CURSOR_SPRITE_ID + 3), (uint8_t)(x + 8), (uint8_t)(y + 8));
}

/* Park the 4 cursor sprites off-screen (shadow OAM only, always safe). */
static void cursor_sprites_off(void)
{
    uint8_t i;

    for (i = 0; i < 4; i++) {
        move_sprite((uint8_t)(CURSOR_SPRITE_ID + i), 0, 0);
    }
}

/* Full menu entry: font tiles + blank screen, LCD stays off until the
 * caller finishes drawing and calls screen_end(). */
static void menu_begin(void)
{
    screen_begin();
    tiles_load_font();
    fill_bkg_rect(0, 0, SCREEN_COLS, SCREEN_ROWS, FONT_BLANK);
    cursor_sprites_off();
}

/* Count empty cells (for the START menu "LEFT" line). */
static uint8_t count_empty(void)
{
    uint8_t i, n;

    n = 0;
    for (i = 0; i < CELL_COUNT; i++) {
        if (board_get(i) == 0) {
            n++;
        }
    }
    return n;
}

/* Count beaten levels (for the select screen "DONE" line). */
static uint8_t count_done(const uint8_t *done)
{
    uint8_t i, n;

    n = 0;
    for (i = 0; i < LEVEL_COUNT; i++) {
        if (done[i]) {
            n++;
        }
    }
    return n;
}

/* Init font + LCDC + cursor sprites. Call once at startup. */
void ui_init(void)
{
    uint8_t i;

    screen_begin();
    tiles_load_font();
    fill_bkg_rect(0, 0, SCREEN_COLS, SCREEN_ROWS, FONT_BLANK);
    for (i = 0; i < 4; i++) {
        set_sprite_tile((uint8_t)(CURSOR_SPRITE_ID + i),
                        (uint8_t)(CURSOR_SPRITE_TILE + i));
    }
    cursor_sprites_off();
    /* Sprite tiles never change again: menus only use BG tiles, the
     * grid loader refreshes sprite tiles on every game entry. */
}

/* Level select: 10 levels per page + completion marks. */
void ui_select(uint8_t page, uint8_t row, const uint8_t *done)
{
    uint8_t i, x;

    menu_begin();
    draw_text(4, 1, "SELECT LEVEL");
    select_draw_page(page);
    for (i = 0; i < LEVELS_PER_PAGE; i++) {
        select_draw_row(page, i, row, done);
    }
    draw_text(2, 14, "A PLAY  LR PAGE");
    draw_text(4, 16, "DONE ");
    x = 9;
    x += draw_num(x, 16, count_done(done));
    draw_text(x, 16, "/");
    x++;
    draw_num(x, 16, LEVEL_COUNT);
    screen_end();
}

/* Select navigation: move the `>` marker (LCD stays on, no reload). */
void ui_select_cursor(uint8_t old_row, uint8_t new_row)
{
    set_bkg_tile_xy(3, (uint8_t)(3 + old_row), (uint8_t)(' ' - 32));
    set_bkg_tile_xy(3, (uint8_t)(3 + new_row), (uint8_t)('>' - 32));
}

/* Select page change: page line + rows only (LCD stays on, no reload). */
void ui_select_page(uint8_t page, uint8_t row, const uint8_t *done)
{
    uint8_t i;

    select_draw_page(page);
    for (i = 0; i < LEVELS_PER_PAGE; i++) {
        select_draw_row(page, i, row, done);
    }
}

/* Game screen: grid tiles + frame margins. No text at all. The rows
 * cover the whole viewport, so no clear is needed. */
void ui_game_full(void)
{
    uint8_t r, i;

    screen_begin();
    tiles_load_grid();
    for (i = 0; i < SCREEN_ROWS; i++) {
        margin_col[i] = MARGIN_LEFT_TILE;
    }
    set_bkg_tiles(0, 0, 1, SCREEN_ROWS, margin_col);
    for (i = 0; i < SCREEN_ROWS; i++) {
        margin_col[i] = MARGIN_RIGHT_TILE;
    }
    set_bkg_tiles((uint8_t)(SCREEN_COLS - 1), 0, 1, SCREEN_ROWS, margin_col);
    for (r = 0; r < GRID_SIZE; r++) {
        draw_grid_row(r);
    }
    screen_end();
}

/* START menu: status + RESUME/HINT/RESTART/TITLE + help. */
void ui_pause(uint8_t choice, uint8_t level)
{
    uint8_t i, x;
    static const char *ITEMS[4] = {"RESUME", "HINT", "RESTART", "TITLE"};

    menu_begin();
    draw_text(1, 1, "L");
    x = 2;
    x += draw_dec3(x, 1, (uint8_t)(level + 1));
    draw_text(x, 1, "/");
    x++;
    x += draw_num(x, 1, LEVEL_COUNT);
    draw_text(x, 1, " ");
    x++;
    draw_text(x, 1, difficulty_name(puzzles[level].difficulty));
    draw_text(1, 2, "ERR ");
    x = 5;
    x += draw_num(x, 2, board_errors());
    draw_text(x, 2, " LEFT ");
    x += 6;
    draw_num(x, 2, count_empty());
    draw_text(2, 4, "------------------");
    for (i = 0; i < 4; i++) {
        set_bkg_tile_xy(5, (uint8_t)(6 + i),
                        (uint8_t)((choice == i ? '>' : ' ') - 32));
        draw_text(7, (uint8_t)(6 + i), ITEMS[i]);
    }
    draw_text(2, 11, "------------------");
    draw_text(2, 13, "A:EDIT B:ERASE");
    draw_text(0, 14, "UD PICK A OK B BACK");
    draw_text(1, 16, "HINT LOCKS THE CELL");
    screen_end();
}

/* Pause navigation: move the `>` marker (LCD stays on, no reload). */
void ui_pause_cursor(uint8_t old_choice, uint8_t new_choice)
{
    set_bkg_tile_xy(5, (uint8_t)(6 + old_choice), (uint8_t)(' ' - 32));
    set_bkg_tile_xy(5, (uint8_t)(6 + new_choice), (uint8_t)('>' - 32));
}

/* Win screen: level clear + mistake tally (no passwords anymore). */
void ui_win(uint8_t level, uint8_t is_last)
{
    menu_begin();
    draw_text(2, 2, "LEVEL ");
    draw_dec3(8, 2, (uint8_t)(level + 1));
    draw_text(11, 2, " CLEAR!");
    draw_text(2, 4, "------------------");
    if (is_last) {
        draw_text(1, 7, "YOU BEAT THE GAME!");
        draw_text(3, 9, "THANKS 4 PLAY!");
    } else {
        draw_text(2, 7, "MISTAKES: ");
        draw_num(12, 7, board_errors());
    }
    draw_text(2, 15, "------------------");
    draw_text(4, 16, "A CONTINUE");
    screen_end();
}

/* Redraw one cell (2x2 tiles) from the board state.
 * Gray unless it is an original clue (hints render gray too). */
void ui_cell(uint8_t row, uint8_t col)
{
    uint8_t idx;

    idx = (uint8_t)(row * GRID_SIZE + col);
    grid_cell_tiles(board_get(idx), !board_is_original(idx), row, col,
                    cell_tiles);
    set_bkg_tiles((uint8_t)(GRID_X + col * 2), (uint8_t)(GRID_Y + row * 2),
                  2, 2, cell_tiles);
}

/* Draw (`show` = 1, gray user digit) or erase (`show` = 0) the picked
 * digit. Erase redraws the cell from the board (untouched by preview). */
void ui_preview(uint8_t row, uint8_t col, uint8_t value, uint8_t show)
{
    if (show) {
        grid_cell_tiles(value, 1, row, col, cell_tiles);
        set_bkg_tiles((uint8_t)(GRID_X + col * 2), (uint8_t)(GRID_Y + row * 2),
                      2, 2, cell_tiles);
    } else {
        ui_cell(row, col);
    }
}

/* Move the 4-sprite cursor outline to cell (row, col). */
void ui_cursor(uint8_t row, uint8_t col)
{
    cursor_place(row, col);
}

/* Hide the cursor sprites (menus, win, mistake blink). */
void ui_cursor_hide(void)
{
    cursor_sprites_off();
}
