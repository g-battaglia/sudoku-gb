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

/* Draw a NUL-terminated string centered on row y. NOTE: on a 20-tile
 * screen only EVEN-width strings center perfectly; every line below
 * uses fixed even-width fields so the layout stays symmetric. */
static void draw_centered(uint8_t y, const char *s)
{
    draw_text((uint8_t)((SCREEN_COLS - text_w(s)) / 2), y, s);
}

/* Draw one select row as a fixed 4-wide centered block (cols 8-11):
 * number + status char ('<' selected, pointing at the number, '*'
 * done, '-' else). Every row shows the same width, so every row is
 * perfectly symmetric. `marks` is the battery-saved completion
 * bitmap (one bit per level). */
static void select_draw_row(uint8_t diff, uint8_t page, uint8_t i,
                            uint8_t row, const uint8_t *marks)
{
    uint8_t r, n;

    r = (uint8_t)(page * LEVELS_PER_PAGE + i); /* 0-99, shown */
    n = (uint8_t)(diff * DIFF_LEVELS + r); /* absolute, bitmap index */
    text_tiles[0] = (uint8_t)('0' + (uint8_t)((r + 1) / 100) - 32);
    text_tiles[1] = (uint8_t)('0' + (uint8_t)(((r + 1) / 10) % 10) - 32);
    text_tiles[2] = (uint8_t)('0' + (uint8_t)((r + 1) % 10) - 32);
    if (i == row) {
        text_tiles[3] = (uint8_t)('<' - 32);
    } else {
        text_tiles[3] = (uint8_t)((marks_get(marks, n) ? '*' : '-') - 32);
    }
    map_tiles(8, (uint8_t)(3 + i), 4, 1, text_tiles);
}

/* Status char of a select row (to redraw it when the marker moves). */
static uint8_t select_status(uint8_t n, const uint8_t *marks)
{
    return (uint8_t)((marks_get(marks, n) ? '*' : '-') - 32);
}

/* Draw the PAGE line: fixed 10-wide block (cols 5-14), symmetric. */
static void select_draw_page(uint8_t page)
{
    draw_text(5, 2, "PAGE ");
    draw_num2(10, 2, (uint8_t)(page + 1));
    draw_text(12, 2, "/10");
}

/* Draw the select screen content (works hidden or visible). Title is
 * the difficulty name (always even width -> perfectly centered). */
static void draw_select_content(uint8_t page, uint8_t row,
                                const uint8_t *marks, uint8_t diff)
{
    uint8_t i;

    draw_centered(1, difficulty_name(diff));
    select_draw_page(page);
    for (i = 0; i < LEVELS_PER_PAGE; i++) {
        select_draw_row(diff, page, i, row, marks);
    }
    draw_centered(14, "A PLAY LR PAGE");
    draw_centered(15, "B MODE");
    /* DONE line: fixed 12-wide block (cols 4-15): DONE ddd/100. */
    draw_text(4, 16, "DONE ");
    draw_dec3(9, 16, marks_count(marks,
                                  (uint16_t)(diff * DIFF_LEVELS),
                                  DIFF_LEVELS));
    draw_text(12, 16, "/");
    draw_text(13, 16, "100");
}

/* Draw the difficulty screen content (works hidden or visible).
 * Item names are all even width and individually centered; selection
 * shows a `>` and `<` pair flanking the name (symmetric marker).
 * When `has_load` is set a fourth item LOAD (row 10) resumes the
 * battery save. */
static void draw_diff_content(uint8_t choice, uint8_t has_load)
{
    uint8_t i, x, w;

    draw_centered(3, "DIFFICULTY");
    for (i = 0; i < DIFF_COUNT; i++) {
        w = text_w(difficulty_name(i));
        x = (uint8_t)((SCREEN_COLS - w) / 2);
        map_tile((uint8_t)(x - 2), (uint8_t)(7 + i),
                 (uint8_t)((choice == i ? '>' : ' ') - 32));
        map_tile((uint8_t)(x + w + 1), (uint8_t)(7 + i),
                 (uint8_t)((choice == i ? '<' : ' ') - 32));
        draw_text(x, (uint8_t)(7 + i), difficulty_name(i));
    }
    if (has_load) {
        map_tile(6, 10, (uint8_t)((choice == DIFF_COUNT ? '>' : ' ') - 32));
        map_tile(13, 10, (uint8_t)((choice == DIFF_COUNT ? '<' : ' ') - 32));
        draw_text(8, 10, "LOAD");
    }
    draw_centered(13, "100 LEVELS");
    draw_centered(14, "A CHOOSE");
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

/* Pause menu items: all even width, individually centered; selection
 * shows a `>` and `<` pair flanking the item (symmetric marker).
 * SAVE writes the battery save slot. */
static const char *PAUSE_ITEMS[5] = {
    "RESUME", "HINT", "SAVE", "PLAY AGAIN", "MENU"
};
static uint8_t pause_item_x(uint8_t i)
{
    return (uint8_t)((SCREEN_COLS - text_w(PAUSE_ITEMS[i])) / 2);
}

/* Draw the START menu content. Every line is a fixed even-width block
 * centered on the 20-tile screen: perfect symmetry at all times. */
static void draw_pause_content(uint8_t choice, uint8_t lid, uint8_t diff)
{
    uint8_t i, x;

    /* LEVEL ddd OF 100 = 16 wide, cols 2-17. */
    draw_text(2, 1, "LEVEL ");
    draw_dec3(8, 1, (uint8_t)(lid + 1));
    draw_text(11, 1, " OF ");
    draw_text(15, 1, "100");
    draw_centered(2, difficulty_name(diff));
    /* ERRORS ddd = 10 wide, cols 5-14. */
    draw_text(5, 3, "ERRORS ");
    draw_dec3(12, 3, board_errors());
    draw_text(1, 5, "------------------");
    for (i = 0; i < 5; i++) {
        x = pause_item_x(i);
        map_tile((uint8_t)(x - 2), (uint8_t)(6 + i),
                 (uint8_t)((choice == i ? '>' : ' ') - 32));
        map_tile((uint8_t)(x + text_w(PAUSE_ITEMS[i]) + 1), (uint8_t)(6 + i),
                 (uint8_t)((choice == i ? '<' : ' ') - 32));
        draw_text(x, (uint8_t)(6 + i), PAUSE_ITEMS[i]);
    }
    draw_text(1, 12, "------------------");
    draw_centered(14, "A EDIT B ERASE");
    draw_centered(15, "UP DOWN PICK");
    draw_centered(16, "A OK B BACKS OUT");
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
        /* MISTAKES ddd = 12 wide, cols 4-15. */
        draw_text(4, 7, "MISTAKES ");
        draw_dec3(13, 7, board_errors());
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

/* Difficulty select: EASY / MEDIUM / HARD (100 levels each), plus a
 * LOAD item when `has_load` is set (valid battery save found). */
void ui_diff(uint8_t choice, uint8_t has_load)
{
    begin_draw();
    map_fill_view(FONT_BLANK);
    draw_diff_content(choice, has_load);
    cursor_sprites_off();
    screen_present(LCDC_MENU);
}

/* Difficulty navigation: move the marker pair (LCD stays on). Index
 * DIFF_COUNT addresses the LOAD row (shown only when a save exists). */
void ui_diff_cursor(uint8_t old_choice, uint8_t new_choice)
{
    uint8_t w, x;

    if (old_choice == DIFF_COUNT) {
        map_tile(6, 10, (uint8_t)(' ' - 32));
        map_tile(13, 10, (uint8_t)(' ' - 32));
    } else {
        w = text_w(difficulty_name(old_choice));
        x = (uint8_t)((SCREEN_COLS - w) / 2);
        map_tile((uint8_t)(x - 2), (uint8_t)(7 + old_choice), (uint8_t)(' ' - 32));
        map_tile((uint8_t)(x + w + 1), (uint8_t)(7 + old_choice), (uint8_t)(' ' - 32));
    }
    if (new_choice == DIFF_COUNT) {
        map_tile(6, 10, (uint8_t)('>' - 32));
        map_tile(13, 10, (uint8_t)('<' - 32));
    } else {
        w = text_w(difficulty_name(new_choice));
        x = (uint8_t)((SCREEN_COLS - w) / 2);
        map_tile((uint8_t)(x - 2), (uint8_t)(7 + new_choice), (uint8_t)('>' - 32));
        map_tile((uint8_t)(x + w + 1), (uint8_t)(7 + new_choice), (uint8_t)('<' - 32));
    }
}

/* Level select: 100 levels of one difficulty, 10 per page. `marks`
 * is the battery-saved completion bitmap (one bit per level). */
void ui_select(uint8_t page, uint8_t row, const uint8_t *marks,
               uint8_t diff)
{
    begin_draw();
    map_fill_view(FONT_BLANK);
    draw_select_content(page, row, marks, diff);
    cursor_sprites_off();
    screen_present(LCDC_MENU);
}

/* Level-select navigation: move the `<' status char between rows
 * (redraws the old row's real status), LCD stays on. */
void ui_select_cursor(uint8_t page, uint8_t old_row, uint8_t new_row,
                       const uint8_t *marks, uint8_t diff)
{
    map_tile(11, (uint8_t)(3 + old_row),
             select_status((uint8_t)(diff * DIFF_LEVELS +
                                     page * LEVELS_PER_PAGE + old_row), marks));
    map_tile(11, (uint8_t)(3 + new_row), (uint8_t)('<' - 32));
}

/* Select page change: page line + rows only (LCD stays on, no reload). */
void ui_select_page(uint8_t page, uint8_t row, const uint8_t *marks,
                    uint8_t diff)
{
    uint8_t i;

    select_draw_page(page);
    for (i = 0; i < LEVELS_PER_PAGE; i++) {
        select_draw_row(diff, page, i, row, marks);
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

/* START menu: status + RESUME/HINT/SAVE/PLAY AGAIN/MENU + help.
 * `lid` is the level number within the difficulty (0-99), `diff` the
 * difficulty. */
void ui_pause(uint8_t choice, uint8_t lid, uint8_t diff)
{
    begin_draw();
    map_fill_view(FONT_BLANK);
    draw_pause_content(choice, lid, diff);
    cursor_sprites_off();
    screen_present(LCDC_MENU);
}

/* Pause navigation: move the `>` marker (LCD stays on, no reload). */
void ui_pause_cursor(uint8_t old_choice, uint8_t new_choice)
{
    uint8_t w, x;

    w = text_w(PAUSE_ITEMS[old_choice]);
    x = pause_item_x(old_choice);
    map_tile((uint8_t)(x - 2), (uint8_t)(6 + old_choice), (uint8_t)(' ' - 32));
    map_tile((uint8_t)(x + w + 1), (uint8_t)(6 + old_choice), (uint8_t)(' ' - 32));
    x = pause_item_x(new_choice);
    map_tile((uint8_t)(x - 2), (uint8_t)(6 + new_choice), (uint8_t)('>' - 32));
    map_tile((uint8_t)(x + text_w(PAUSE_ITEMS[new_choice]) + 1),
             (uint8_t)(6 + new_choice), (uint8_t)('<' - 32));
}

/* Win screen: level clear + mistake tally. `num` is the level number
 * within the difficulty (0-99). */
void ui_win(uint8_t num, uint8_t is_last)
{
    begin_draw();
    map_fill_view(FONT_BLANK);
    draw_win_content(num, is_last);
    cursor_sprites_off();
    screen_present(LCDC_MENU);
}

/* Save confirmation screen: shown after SAVE in the START menu.
 * Any button returns to the game (handled by main). */
void ui_saved(void)
{
    begin_draw();
    map_fill_view(FONT_BLANK);
    draw_text(2, 6, "------------------");
    draw_centered(8, "GAME SAVED");
    draw_centered(10, "A BACK");
    draw_text(2, 12, "------------------");
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
