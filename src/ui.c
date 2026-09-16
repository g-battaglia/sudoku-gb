#include <gbdk/platform.h>

#include "ui.h"
#include "board.h"
#include "puzzles.h"
#include "tiles.h"

/* ---------------------------------------------------------------------------
 * ui.c — Fullscreen grid + text menus. One screen per function.
 *
 * The game screen is only tiles: each cell is 2x2 BG tiles drawn at
 * (GRID_X + col*2, GRID_Y + row*2), plus the frame margins at columns
 * 0 and 19. The cursor is 4 sprites (a 16x16 outline) moved with
 * move_sprite: sprite coords need +8/+16 offset
 * (DEVICE_SPRITE_PX_OFFSET_X/Y).
 *
 * TEXT WITHOUT STDIO: menus never use printf/gotoxy/cls (GBDK varargs
 * plus console state caused garbled screens). Every character is
 * written as a font tile (ASCII c = tile c - 32) through the VRAM-safe
 * map_* helpers below.
 *
 * ATOMIC SCREENS (no LCD-off flash, no stale sprites): tile patterns
 * are resident (loaded once at boot, see tiles.h), and the DMG has two
 * background maps. Every full screen is drawn into the HIDDEN map
 * while the LCD keeps showing the old one, then one LCDC write swaps
 * the map (+ tile mode + OBJ enable) at the next frame start:
 * - draw_hidden = 1 routes map_* to the hidden map (GBDK set_tiles /
 *   set_vram_byte, both WAIT_STAT-guarded, so LCD-on writes are safe);
 * - draw_hidden = 0 routes map_* to the visible map (delta updates:
 *   menu markers, page rows, single cells — always LCD-on);
 * - screen_present() does vsync() (the VBlank ISR copies the prepared
 *   shadow OAM) and flips the visible map in a single LCDC write, so
 *   the new map and the new sprites appear on the same frame.
 * Protocol: begin_draw() -> draw content -> prepare shadow OAM
 * (cursor_place for game, cursor_sprites_off for menus) ->
 * screen_present(). The LCD is stopped exactly once, in ui_init().
 * -------------------------------------------------------------------------*/

/* LCD on, tiles at 0x8000, map selected by present, sprites 8x8 on,
 * background on (game screens). */
#define LCDC_GAME 0x93

/* LCD on, tiles at 0x9000 (signed: font), map selected by present,
 * sprites off, background on (menus). OBJ-off hides any stale OAM
 * even if a park were missed. */
#define LCDC_MENU 0x81

/* Blank map tile: font tile 0 (ASCII space). */
#define FONT_BLANK 0

/* The two DMG background maps (window stays off, so 0x9C00 is free). */
#define MAP_9800 ((uint8_t *)0x9800)
#define MAP_9C00 ((uint8_t *)0x9C00)

/* One map row of the grid: 9 cells x 2 tiles wide, 2 tile rows. */
static uint8_t grid_map_row[9 * 2 * 2];

/* One margin column (18 tiles, filled with one margin tile). */
static uint8_t margin_col[SCREEN_ROWS];

/* Scratch tile indices for one cell (TL, TR, BL, BR). */
static uint8_t cell_tiles[4];

/* Scratch tile indices for one line of text (max one screen row). */
static uint8_t text_tiles[SCREEN_COLS];

/* 1 = the visible map is 0x9C00 (else 0x9800). Flipped by present. */
static uint8_t shown_9c00;

/* 1 = map_* writes go to the hidden map (full redraw in progress). */
static uint8_t draw_hidden;

/* Base address of the map that is NOT shown right now. */
static uint8_t *hidden_base(void)
{
    return shown_9c00 ? MAP_9800 : MAP_9C00;
}

/* Write a tile rectangle (routes hidden/visible, always VRAM-safe). */
static void map_tiles(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                      const uint8_t *tiles)
{
    if (draw_hidden) {
        set_tiles(x, y, w, h, hidden_base(), tiles);
    } else {
        set_bkg_tiles(x, y, w, h, tiles);
    }
}

/* Write one tile (routes hidden/visible, always VRAM-safe). */
static void map_tile(uint8_t x, uint8_t y, uint8_t t)
{
    if (draw_hidden) {
        set_vram_byte(hidden_base() + (uint16_t)((uint16_t)y * 32 + x), t);
    } else {
        set_bkg_tile_xy(x, y, t);
    }
}

/* Fill the 20x18 viewport (all SCX/SCY = 0 ever shows). Only used for
 * full redraws (draw_hidden = 1). */
static void map_fill_view(uint8_t t)
{
    uint8_t x, y;

    for (y = 0; y < SCREEN_ROWS; y++) {
        for (x = 0; x < SCREEN_COLS; x++) {
            map_tile(x, y, t);
        }
    }
}

/* Start a full redraw into the hidden map (LCD keeps showing old). */
static void begin_draw(void)
{
    draw_hidden = 1;
}

/* Show the hidden map: wait for VBlank (shadow OAM is copied there),
 * then swap map + tile mode + OBJ enable in one LCDC write. The new
 * map and the new sprites land on the same frame. `mode` is LCDC_GAME
 * or LCDC_MENU (both keep the LCD bit set: it never clears again). */
static void screen_present(uint8_t mode)
{
    draw_hidden = 0;
    vsync();
    if (shown_9c00) {
        LCDC_REG = (uint8_t)(mode & (uint8_t)~LCDCF_BG9C00);
    } else {
        LCDC_REG = (uint8_t)(mode | LCDCF_BG9C00);
    }
    shown_9c00 = (uint8_t)(!shown_9c00);
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
        map_tiles(x, y, n, 1, text_tiles);
    }
}

/* Draw unsigned 0-255 with leading zeros (exactly 3 digits). Returns
 * the width (3). */
static uint8_t draw_dec3(uint8_t x, uint8_t y, uint8_t n)
{
    text_tiles[0] = (uint8_t)('0' + (uint8_t)(n / 100) - 32);
    text_tiles[1] = (uint8_t)('0' + (uint8_t)((n / 10) % 10) - 32);
    text_tiles[2] = (uint8_t)('0' + (uint8_t)(n % 10) - 32);
    map_tiles(x, y, 3, 1, text_tiles);
    return 3;
}

/* Draw unsigned 0-99 in 2 chars (leading space). Returns the width (2).
 * Fixed width keeps the PAGE line stable across page changes. */
static uint8_t draw_num2(uint8_t x, uint8_t y, uint8_t n)
{
    text_tiles[0] = (uint8_t)((n >= 10 ? '0' + (uint8_t)(n / 10) : ' ') - 32);
    text_tiles[1] = (uint8_t)('0' + (uint8_t)(n % 10) - 32);
    map_tiles(x, y, 2, 1, text_tiles);
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
    map_tiles(x, y, w, 1, text_tiles);
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
    map_tiles(3, (uint8_t)(3 + i), 14, 1, text_tiles);
}

/* Draw the PAGE line (fixed width, column 6 row 2). */
static void select_draw_page(uint8_t page)
{
    draw_text(6, 2, "PAGE ");
    draw_num2(11, 2, (uint8_t)(page + 1));
    draw_text(13, 2, "/10");
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

/* Draw the select screen content (works hidden or visible). */
static void draw_select_content(uint8_t page, uint8_t row,
                                const uint8_t *done)
{
    uint8_t i, x;

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
}

/* Draw grid row `row` (2 tile rows) into the map. */
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
    map_tiles(GRID_X, (uint8_t)(GRID_Y + row * 2), 18, 2, grid_map_row);
}

/* Draw the game screen content: frame margins + grid rows. The rows
 * cover the whole viewport, so no clear is needed. */
static void draw_game_content(void)
{
    uint8_t r, i;

    for (i = 0; i < SCREEN_ROWS; i++) {
        margin_col[i] = MARGIN_LEFT_TILE;
    }
    map_tiles(0, 0, 1, SCREEN_ROWS, margin_col);
    for (i = 0; i < SCREEN_ROWS; i++) {
        margin_col[i] = MARGIN_RIGHT_TILE;
    }
    map_tiles((uint8_t)(SCREEN_COLS - 1), 0, 1, SCREEN_ROWS, margin_col);
    for (r = 0; r < GRID_SIZE; r++) {
        draw_grid_row(r);
    }
}

/* Pause menu items + their centered columns (marker sits 2 left). */
static const char *PAUSE_ITEMS[4] = {"RESUME", "HINT", "RESTART", "TITLE"};
static const uint8_t PAUSE_ITEM_X[4] = {7, 8, 6, 7};

/* Width of a NUL-terminated string, in tiles. */
static uint8_t text_w(const char *s)
{
    uint8_t n;

    n = 0;
    while (s[n] != 0) {
        n++;
    }
    return n;
}

/* Width of 0-255 with draw_num padding (1-3 digits). */
static uint8_t num_w(uint8_t n)
{
    return (n >= 100) ? 3 : ((n >= 10) ? 2 : 1);
}

/* Draw a NUL-terminated string centered on row y. */
static void draw_centered(uint8_t y, const char *s)
{
    draw_text((uint8_t)((SCREEN_COLS - text_w(s)) / 2), y, s);
}

/* Draw the START menu content: three centered status lines, four
 * centered items, four centered help lines. Every line is symmetric
 * around the screen middle; nothing is ragged. */
static void draw_pause_content(uint8_t choice, uint8_t level)
{
    uint8_t i, x, w, e, l;

    w = (uint8_t)(6 + 3 + 1 + num_w(LEVEL_COUNT));
    x = (uint8_t)((SCREEN_COLS - w) / 2);
    draw_text(x, 1, "LEVEL ");
    x += 6;
    x += draw_dec3(x, 1, (uint8_t)(level + 1));
    draw_text(x, 1, "/");
    x++;
    draw_num(x, 1, LEVEL_COUNT);
    draw_centered(2, difficulty_name(puzzles[level].difficulty));
    e = board_errors();
    l = count_empty();
    w = (uint8_t)(3 + 1 + num_w(e) + 1 + 4 + 1 + num_w(l));
    x = (uint8_t)((SCREEN_COLS - w) / 2);
    draw_text(x, 3, "ERR ");
    x += 4;
    x += draw_num(x, 3, e);
    draw_text(x, 3, " LEFT ");
    x += 6;
    draw_num(x, 3, l);
    draw_text(1, 5, "------------------");
    for (i = 0; i < 4; i++) {
        map_tile((uint8_t)(PAUSE_ITEM_X[i] - 2), (uint8_t)(6 + i),
                 (uint8_t)((choice == i ? '>' : ' ') - 32));
        draw_text(PAUSE_ITEM_X[i], (uint8_t)(6 + i), PAUSE_ITEMS[i]);
    }
    draw_text(1, 11, "------------------");
    draw_centered(13, "A EDIT B ERASE");
    draw_centered(14, "UP DOWN PICK");
    draw_centered(15, "A OK B BACKS OUT");
}

/* Draw the win screen content. */
static void draw_win_content(uint8_t level, uint8_t is_last)
{
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

/* Init all video state once. Stops the LCD, loads every tile pattern
 * (tiles_load_resident stops it a second time: GBDK font_load
 * re-enables it on exit), parks all sprites and copies that to
 * hardware OAM. The LCD stays off: the first screen (ui_select from
 * main) presents it. Call once at startup. */
void ui_init(void)
{
    uint8_t i;

    display_off();
    LCDC_REG = LCDC_OFF_MODE;
    SCX_REG = 0;
    SCY_REG = 0;
    tiles_load_resident();
    for (i = 0; i < 4; i++) {
        set_sprite_tile((uint8_t)(CURSOR_SPRITE_ID + i),
                        (uint8_t)(CURSOR_SPRITE_TILE + i));
    }
    for (i = 0; i < MAX_HARDWARE_SPRITES; i++) {
        move_sprite(i, 0, 0);
    }
    refresh_OAM();
}

/* Level select: 10 levels per page + completion marks. */
void ui_select(uint8_t page, uint8_t row, const uint8_t *done)
{
    begin_draw();
    map_fill_view(FONT_BLANK);
    draw_select_content(page, row, done);
    cursor_sprites_off();
    screen_present(LCDC_MENU);
}

/* Select navigation: move the `>` marker (LCD stays on, no reload). */
void ui_select_cursor(uint8_t old_row, uint8_t new_row)
{
    map_tile(3, (uint8_t)(3 + old_row), (uint8_t)(' ' - 32));
    map_tile(3, (uint8_t)(3 + new_row), (uint8_t)('>' - 32));
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

/* Game screen: grid + margins, cursor placed by us (no caller can show
 * a game frame before its OAM is ready). No text at all. */
void ui_game_full(uint8_t row, uint8_t col)
{
    begin_draw();
    draw_game_content();
    cursor_place(row, col);
    screen_present(LCDC_GAME);
}

/* START menu: status + RESUME/HINT/RESTART/TITLE + help. */
void ui_pause(uint8_t choice, uint8_t level)
{
    begin_draw();
    map_fill_view(FONT_BLANK);
    draw_pause_content(choice, level);
    cursor_sprites_off();
    screen_present(LCDC_MENU);
}

/* Pause navigation: move the `>` marker (LCD stays on, no reload). */
void ui_pause_cursor(uint8_t old_choice, uint8_t new_choice)
{
    map_tile((uint8_t)(PAUSE_ITEM_X[old_choice] - 2),
             (uint8_t)(6 + old_choice), (uint8_t)(' ' - 32));
    map_tile((uint8_t)(PAUSE_ITEM_X[new_choice] - 2),
             (uint8_t)(6 + new_choice), (uint8_t)('>' - 32));
}

/* Win screen: level clear + mistake tally (no passwords anymore). */
void ui_win(uint8_t level, uint8_t is_last)
{
    begin_draw();
    map_fill_view(FONT_BLANK);
    draw_win_content(level, is_last);
    cursor_sprites_off();
    screen_present(LCDC_MENU);
}

/* Redraw one cell (2x2 tiles) from the board state.
 * Gray unless it is an original clue (hints render gray too). */
void ui_cell(uint8_t row, uint8_t col)
{
    uint8_t idx;

    idx = (uint8_t)(row * GRID_SIZE + col);
    grid_cell_tiles(board_get(idx), !board_is_original(idx), row, col,
                    cell_tiles);
    map_tiles((uint8_t)(GRID_X + col * 2), (uint8_t)(GRID_Y + row * 2),
              2, 2, cell_tiles);
}

/* Draw (`show` = 1, gray user digit) or erase (`show` = 0) the picked
 * digit. Erase redraws the cell from the board (untouched by preview). */
void ui_preview(uint8_t row, uint8_t col, uint8_t value, uint8_t show)
{
    if (show) {
        grid_cell_tiles(value, 1, row, col, cell_tiles);
        map_tiles((uint8_t)(GRID_X + col * 2), (uint8_t)(GRID_Y + row * 2),
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
