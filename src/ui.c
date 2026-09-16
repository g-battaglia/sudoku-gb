#include <gbdk/platform.h>
#include <gbdk/font.h>
#include <gbdk/console.h>
#include <stdio.h>

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
 * PRINTF RULE: GBDK printf garbles output when %c is followed by more
 * specifiers (args shift) and prints unknown flags literally, so every
 * printf below holds at most one plain %s and zero flags. Numbers go
 * through print_num()/print_dec3() (putchar loops), single characters
 * through putchar(). Literals and lone %s are proven safe.
 * -------------------------------------------------------------------------*/

/* LCDC mode we always run in: LCD on, tiles at 0x8000, map at 0x9800,
 * sprites 8x8 on, background on. Set explicitly: boot state differs
 * per emulator/bios and OR-ing bits can leave the wrong tile area. */
#define LCDC_MODE 0x93

/* One map row of the grid: 9 cells x 2 tiles wide, 2 tile rows. */
static uint8_t grid_map_row[9 * 2 * 2];

/* One margin column (18 tiles, filled with one margin tile). */
static uint8_t margin_col[SCREEN_ROWS];

/* Which tileset is in VRAM: 1 = font (menus), 0 = grid (game). Menus
 * used to reload the font on EVERY redraw (select/pause navigation),
 * and each reload whites the screen for ~14 frames (font_init clears
 * the map, font_load decompresses slowly with LCD on). Cache it. */
static uint8_t tiles_font_loaded;

/* Scratch tile indices for one cell (TL, TR, BL, BR). */
static uint8_t cell_tiles[4];

/* Print unsigned 0-255 with leading zeros (exactly 3 digits). */
static void print_dec3(uint8_t n)
{
    putchar((char)('0' + n / 100));
    putchar((char)('0' + (n / 10) % 10));
    putchar((char)('0' + n % 10));
}

/* Print unsigned 0-255 without padding (1-3 digits). */
static void print_num(uint8_t n)
{
    if (n >= 100) {
        putchar((char)('0' + n / 100));
    }
    if (n >= 10) {
        putchar((char)('0' + (n / 10) % 10));
    }
    putchar((char)('0' + n % 10));
}

/* Hide the 4 cursor sprites (real hiding: sprites off-screen). */
static void cursor_sprites_off(void)
{
    uint8_t i;

    for (i = 0; i < 4; i++) {
        move_sprite((uint8_t)(CURSOR_SPRITE_ID + i), 0, 0);
    }
}

/* Draw grid row `row` (2 tile rows) into the background map. */
static void draw_grid_row(uint8_t row)
{
    uint8_t c, idx;

    for (c = 0; c < GRID_SIZE; c++) {
        idx = (uint8_t)(row * GRID_SIZE + c);
        grid_cell_tiles(board_get(idx), !board_is_given(idx), row, c,
                        cell_tiles);
        grid_map_row[c * 2] = cell_tiles[0];
        grid_map_row[c * 2 + 1] = cell_tiles[1];
        grid_map_row[18 + c * 2] = cell_tiles[2];
        grid_map_row[18 + c * 2 + 1] = cell_tiles[3];
    }
    set_bkg_tiles(GRID_X, (uint8_t)(GRID_Y + row * 2), 18, 2, grid_map_row);
}

/* Move the 4 cursor sprites over grid cell (row, col). */
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

/* Full-screen text menu entry: font tiles (cached) + sprites off.
 * Display off during the switch: no white flash, no tearing. */
static void show_menu_text(void)
{
    uint8_t i;

    display_off();
    if (!tiles_font_loaded) {
        tiles_load_font();
        tiles_font_loaded = 1;
    }
    HIDE_SPRITES;
    cursor_sprites_off();
    for (i = 0; i < 4; i++) {
        set_sprite_tile((uint8_t)(CURSOR_SPRITE_ID + i),
                        (uint8_t)(CURSOR_SPRITE_TILE + i));
    }
    cls();
    LCDC_REG = LCDC_MODE;
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

    tiles_load_font();
    tiles_font_loaded = 1;
    for (i = 0; i < 4; i++) {
        set_sprite_tile((uint8_t)(CURSOR_SPRITE_ID + i),
                        (uint8_t)(CURSOR_SPRITE_TILE + i));
    }
    cursor_sprites_off();
    LCDC_REG = LCDC_MODE;
    SHOW_SPRITES;
    DISPLAY_ON;
    SHOW_BKG;
}

/* Level select: 10 levels per page + completion marks. */
void ui_select(uint8_t page, uint8_t row, const uint8_t *done)
{
    uint8_t i, n;

    show_menu_text();
    gotoxy(4, 1);
    printf("SELECT LEVEL");
    gotoxy(6, 2);
    printf("PAGE ");
    print_num((uint8_t)(page + 1));
    printf("/");
    print_num((uint8_t)(LEVEL_COUNT / LEVELS_PER_PAGE));
    for (i = 0; i < LEVELS_PER_PAGE; i++) {
        n = (uint8_t)(page * LEVELS_PER_PAGE + i);
        gotoxy(3, (uint8_t)(3 + i));
        putchar(i == row ? '>' : ' ');
        putchar(' ');
        print_dec3((uint8_t)(n + 1));
        putchar(' ');
        printf("%s", difficulty_name(puzzles[n].difficulty));
        putchar(' ');
        putchar(done[n] ? '*' : ' ');
    }
    gotoxy(2, 14);
    printf("A PLAY  LR PAGE");
    gotoxy(4, 16);
    printf("DONE ");
    print_num(count_done(done));
    printf("/");
    print_num(LEVEL_COUNT);
}

/* Game screen: grid tiles + frame margins. No text at all.
 * Display off during the VRAM copy (fast, no tearing), on after. */
void ui_game_full(void)
{
    uint8_t r, i;

    display_off();
    SHOW_SPRITES;
    if (tiles_font_loaded) {
        tiles_load_grid();
        tiles_font_loaded = 0;
    }
    cls();
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
    LCDC_REG = LCDC_MODE;
}

/* START menu: status + RESUME/HINT/RESTART/TITLE + help. */
void ui_pause(uint8_t choice, uint8_t level)
{
    show_menu_text();
    gotoxy(1, 1);
    printf("L");
    print_dec3((uint8_t)(level + 1));
    printf("/");
    print_num(LEVEL_COUNT);
    printf(" ");
    printf("%s", difficulty_name(puzzles[level].difficulty));
    gotoxy(1, 2);
    printf("ERR ");
    print_num(board_errors());
    printf(" LEFT ");
    print_num(count_empty());
    gotoxy(2, 4);
    printf("------------------");
    gotoxy(5, 6);
    printf("%c RESUME", choice == 0 ? '>' : ' ');
    gotoxy(5, 7);
    printf("%c HINT", choice == 1 ? '>' : ' ');
    gotoxy(5, 8);
    printf("%c RESTART", choice == 2 ? '>' : ' ');
    gotoxy(5, 9);
    printf("%c TITLE", choice == 3 ? '>' : ' ');
    gotoxy(2, 11);
    printf("------------------");
    gotoxy(2, 13);
    printf("A:EDIT B:ERASE");
    gotoxy(0, 14);
    printf("UD PICK A OK B BACK");
    gotoxy(1, 16);
    printf("HINT LOCKS THE CELL");
}

/* Win screen: level clear + mistake tally (no passwords anymore). */
void ui_win(uint8_t level, uint8_t is_last)
{
    show_menu_text();
    gotoxy(2, 2);
    printf("LEVEL ");
    print_dec3((uint8_t)(level + 1));
    printf(" CLEAR!");
    gotoxy(2, 4);
    printf("------------------");
    if (is_last) {
        gotoxy(1, 7);
        printf("YOU BEAT THE GAME!");
        gotoxy(3, 9);
        printf("THANKS 4 PLAY!");
    } else {
        gotoxy(2, 7);
        printf("MISTAKES: ");
        print_num(board_errors());
    }
    gotoxy(2, 15);
    printf("------------------");
    gotoxy(4, 16);
    printf("A CONTINUE");
}

/* Redraw one cell (2x2 tiles) from the board state (gray if user). */
void ui_cell(uint8_t row, uint8_t col)
{
    uint8_t idx;

    idx = (uint8_t)(row * GRID_SIZE + col);
    grid_cell_tiles(board_get(idx), !board_is_given(idx), row, col,
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
