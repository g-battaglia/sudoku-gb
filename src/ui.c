#include <gbdk/platform.h>
#include <gbdk/font.h>
#include <gbdk/console.h>
#include <stdio.h>

#include "ui.h"
#include "board.h"
#include "puzzles.h"
#include "passwords.h"
#include "tiles.h"

/* ---------------------------------------------------------------------------
 * ui.c — Fullscreen grid + text menus. One screen per function.
 *
 * The game screen is only tiles: each cell is 2x2 BG tiles drawn with
 * set_bkg_tiles at (GRID_X + col*2, GRID_Y + row*2). The cursor is 4
 * sprites (a 16x16 outline) moved with move_sprite: sprite coords need
 * +8/+16 offset (DEVICE_SPRITE_PX_OFFSET_X/Y).
 * -------------------------------------------------------------------------*/

/* One map row of the grid: 9 cells x 2 tiles wide, 2 tile rows. */
static uint8_t grid_map_row[9 * 2 * 2];

/* Scratch tile indices for one cell (TL, TR, BL, BR). */
static uint8_t cell_tiles[4];

/* Draw grid row `row` (2 tile rows) into the background map. */
static void draw_grid_row(uint8_t row)
{
    uint8_t c;

    for (c = 0; c < GRID_SIZE; c++) {
        grid_cell_tiles(board_get((uint8_t)(row * GRID_SIZE + c)), row, c,
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

/* Hide sprites while a full-screen text menu is shown. */
static void show_menu_text(void)
{
    HIDE_SPRITES;
    cls();
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

/* Init font + grid tiles + display. Call once at startup. */
void ui_init(void)
{
    uint8_t i;
    font_t ibm_font;

    font_init();
    ibm_font = font_load(font_ibm);
    font_set(ibm_font);
    tiles_load();
    for (i = 0; i < 4; i++) {
        set_sprite_tile((uint8_t)(CURSOR_SPRITE_ID + i),
                        (uint8_t)(CURSOR_SPRITE_TILE + i));
    }
    SHOW_SPRITES;
    DISPLAY_ON;
    SHOW_BKG;
}

/* Level select: 12 free levels + completion marks + password hint. */
void ui_select(uint8_t pos, const uint8_t *done)
{
    uint8_t i;

    show_menu_text();
    gotoxy(4, 1);
    printf("SELECT LEVEL");
    for (i = 0; i < LEVEL_COUNT; i++) {
        gotoxy(4, (uint8_t)(3 + i));
        printf("%c %02d %-6s %c", i == pos ? '>' : ' ', i + 1,
               difficulty_name(puzzles[i].difficulty), done[i] ? '*' : ' ');
    }
    gotoxy(1, 16);
    printf("A PLAY  SELECT PWD");
}

/* Password entry. `digits[4]`, `pos` = edited slot, `bad` = show error. */
void ui_password(const uint8_t *digits, uint8_t pos, uint8_t bad)
{
    uint8_t i;

    show_menu_text();
    gotoxy(5, 1);
    printf("PASSWORD");
    gotoxy(2, 3);
    printf("------------------");
    gotoxy(4, 6);
    for (i = 0; i < PASSWORD_DIGITS; i++) {
        if (i == pos) {
            printf("[%d]", digits[i]);
        } else {
            printf(" %d ", digits[i]);
        }
    }
    gotoxy(2, 8);
    printf("------------------");
    gotoxy(3, 10);
    printf("UP/DOWN DIGIT");
    gotoxy(2, 11);
    printf("LEFT/RIGHT SLOT");
    gotoxy(4, 12);
    printf("A OK  B BACK");
    if (bad) {
        gotoxy(3, 15);
        printf("WRONG PASSWORD");
    }
}

/* Game screen: fullscreen grid + cursor sprite. No text at all. */
void ui_game_full(void)
{
    uint8_t r;

    SHOW_SPRITES;
    cls();
    for (r = 0; r < GRID_SIZE; r++) {
        draw_grid_row(r);
    }
}

/* START menu: status + RESUME/HINT/RESTART/TITLE + help. */
void ui_pause(uint8_t choice, uint8_t level)
{
    show_menu_text();
    gotoxy(1, 1);
    printf("LEVEL %02d/%02d %s", level + 1, LEVEL_COUNT,
           difficulty_name(puzzles[level].difficulty));
    gotoxy(1, 2);
    printf("MISTAKES %d LEFT %d", board_errors(), count_empty());
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

/* Win screen. Shows password for next level, or completion text if last. */
void ui_win(uint8_t level, uint16_t next_password, uint8_t is_last)
{
    show_menu_text();
    gotoxy(2, 2);
    printf("LEVEL %02d CLEAR!", level + 1);
    gotoxy(2, 4);
    printf("------------------");
    if (is_last) {
        gotoxy(1, 7);
        printf("YOU BEAT THE GAME!");
        gotoxy(3, 9);
        printf("THANKS 4 PLAY!");
    } else {
        gotoxy(2, 7);
        printf("NEXT PASSWORD:");
        gotoxy(0, 9);
        printf("      %04d", next_password);
        gotoxy(2, 12);
        printf("WRITE IT DOWN!");
    }
    gotoxy(2, 15);
    printf("------------------");
    gotoxy(4, 16);
    printf("A CONTINUE");
}

/* Redraw one cell (2x2 tiles) from the board state. */
void ui_cell(uint8_t row, uint8_t col)
{
    grid_cell_tiles(board_get((uint8_t)(row * GRID_SIZE + col)), row, col,
                    cell_tiles);
    set_bkg_tiles((uint8_t)(GRID_X + col * 2), (uint8_t)(GRID_Y + row * 2),
                  2, 2, cell_tiles);
}

/* Draw (`show` = 1) or erase (`show` = 0) the proposed digit.
 * Erase redraws the cell from the board (untouched by the preview). */
void ui_preview(uint8_t row, uint8_t col, uint8_t value, uint8_t show)
{
    if (show) {
        grid_cell_tiles(value, row, col, cell_tiles);
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
    uint8_t i;

    for (i = 0; i < 4; i++) {
        move_sprite((uint8_t)(CURSOR_SPRITE_ID + i), 0, 0);
    }
}
