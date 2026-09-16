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
 * ui.c — Text + tile drawing. One screen per function.
 *
 * Text uses the GBDK font (tiles 0-95). The grid uses custom tiles
 * (GRID_TILE_BASE+) drawn with set_bkg_tiles at (GRID_X, GRID_Y).
 * The cursor is sprite 0 (an 8x8 outline), moved with move_sprite:
 * sprite coords need +8/+16 offset (DEVICE_SPRITE_PX_OFFSET_X/Y).
 * -------------------------------------------------------------------------*/

/* Static map buffer: one full grid row (9 cells) of tile indices. */
static uint8_t grid_row_tiles[GRID_SIZE];

/* Write the tile map of grid row `row` (cells + frame handled by caller). */
static void draw_grid_row(uint8_t row)
{
    uint8_t c, idx;

    for (c = 0; c < GRID_SIZE; c++) {
        idx = (uint8_t)(row * GRID_SIZE + c);
        grid_row_tiles[c] = grid_tile(board_get(idx), !board_is_given(idx), row, c);
    }
    set_bkg_tiles((uint8_t)(GRID_X + 1), (uint8_t)(GRID_Y + 1 + row),
                  GRID_SIZE, 1, grid_row_tiles);
}

/* Draw the whole grid: top frame row, left frame column, 9 cell rows. */
static void draw_grid(void)
{
    uint8_t c, r, frame_row[GRID_SIZE + 1];

    /* Top frame: corner + one tile per column. */
    frame_row[0] = FRAME_CORNER_TILE;
    for (c = 0; c < GRID_SIZE; c++) {
        frame_row[c + 1] = frame_tile_top(c);
    }
    set_bkg_tiles(GRID_X, GRID_Y, (uint8_t)(GRID_SIZE + 1), 1, frame_row);
    /* Cell rows: left frame tile + cells. */
    for (r = 0; r < GRID_SIZE; r++) {
        frame_row[0] = frame_tile_left(r);
        set_bkg_tiles(GRID_X, (uint8_t)(GRID_Y + 1 + r), 1, 1, frame_row);
        draw_grid_row(r);
    }
}

/* Move the cursor sprite over grid cell (row, col). */
static void cursor_place(uint8_t row, uint8_t col)
{
    move_sprite(CURSOR_SPRITE_ID,
                (uint8_t)(DEVICE_SPRITE_PX_OFFSET_X + (GRID_X + 1 + col) * 8),
                (uint8_t)(DEVICE_SPRITE_PX_OFFSET_Y + (GRID_Y + 1 + row) * 8));
}

/* Hide sprites while a full-screen text menu is shown. */
static void show_menu_text(void)
{
    HIDE_SPRITES;
    cls();
}

/* Init font + grid tiles + display. Call once at startup. */
void ui_init(void)
{
    font_t ibm_font;

    font_init();
    ibm_font = font_load(font_ibm);
    font_set(ibm_font);
    tiles_load();
    set_sprite_tile(CURSOR_SPRITE_ID, CURSOR_SPRITE_TILE);
    SHOW_SPRITES;
    DISPLAY_ON;
    SHOW_BKG;
}
/* Title screen. `choice` 0 = NEW GAME, 1 = PASSWORD. */
void ui_title(uint8_t choice)
{
    show_menu_text();
    gotoxy(5, 1);
    printf("SUDOKU GB");
    gotoxy(2, 3);
    printf("------------------");
    gotoxy(3, 5);
    printf("12 LEVELS-3 LIVES");
    gotoxy(5, 8);
    printf("%c NEW GAME", choice == 0 ? '>' : ' ');
    gotoxy(5, 9);
    printf("%c PASSWORD", choice == 1 ? '>' : ' ');
    gotoxy(2, 11);
    printf("------------------");
    gotoxy(2, 13);
    printf("UP/DOWN+A SELECT");
    gotoxy(1, 15);
    printf("BEAT A LEVEL TO GET");
    gotoxy(1, 16);
    printf("THE NEXT PASSWORD");
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

/* Game screen frame: header, full grid, footer. Redraws everything. */
void ui_game_full(uint8_t level)
{
    SHOW_SPRITES;
    cls();
    /* Header: title + level + difficulty. */
    gotoxy(0, 0);
    printf("SUDOKU L%02d %s", level + 1, difficulty_name(puzzles[level].difficulty));
    ui_mistakes();
    /* Grid with frame + lines. */
    draw_grid();
    /* Footer: proposed digit + help (below the grid rows 3-12). */
    ui_entry(1);
    gotoxy(0, 14);
    printf("UP/DN NUM  A:OK");
    gotoxy(0, 15);
    printf("B:DEL START:MENU");
}

/* Pause menu. `choice` 0 = RESUME, 1 = RESTART, 2 = TITLE. */
void ui_pause(uint8_t choice)
{
    show_menu_text();
    gotoxy(6, 2);
    printf("PAUSED");
    gotoxy(2, 4);
    printf("------------------");
    gotoxy(5, 6);
    printf("%c RESUME", choice == 0 ? '>' : ' ');
    gotoxy(5, 7);
    printf("%c RESTART", choice == 1 ? '>' : ' ');
    gotoxy(5, 8);
    printf("%c TITLE", choice == 2 ? '>' : ' ');
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

/* Game over screen. `choice` 0 = RETRY, 1 = TITLE. */
void ui_gameover(uint8_t choice)
{
    show_menu_text();
    gotoxy(5, 2);
    printf("GAME OVER");
    gotoxy(2, 4);
    printf("------------------");
    gotoxy(2, 6);
    printf("3 MISTAKES MADE");
    gotoxy(5, 9);
    printf("%c RETRY", choice == 0 ? '>' : ' ');
    gotoxy(5, 10);
    printf("%c TITLE", choice == 1 ? '>' : ' ');
    gotoxy(2, 13);
    printf("------------------");
}
/* Redraw the mistake slots on row 1. */
void ui_mistakes(void)
{
    uint8_t i, errors;

    errors = board_errors();
    gotoxy(0, 1);
    printf("ERRORS ");
    for (i = 0; i < MAX_ERRORS; i++) {
        printf("%c ", i < errors ? 'X' : '.');
    }
}

/* Redraw the proposed digit on row 13 (below the grid). */
void ui_entry(uint8_t value)
{
    gotoxy(0, 13);
    printf("ENTER: %d", value);
}

/* Show a message on row 16 (empty string clears the line). */
void ui_message(const char *text)
{
    gotoxy(0, 16);
    printf("%-20s", text);
}

/* Redraw one cell tile (digit or empty, keeps the borders). */
void ui_cell(uint8_t row, uint8_t col)
{
    uint8_t idx, tile;

    idx = (uint8_t)(row * GRID_SIZE + col);
    tile = grid_tile(board_get(idx), !board_is_given(idx), row, col);
    set_bkg_tiles((uint8_t)(GRID_X + 1 + col), (uint8_t)(GRID_Y + 1 + row),
                  1, 1, &tile);
}

/* Move the cursor outline to cell (row, col). Sprite overlay: nothing
 * to erase, just move it. */
void ui_cursor(uint8_t row, uint8_t col)
{
    cursor_place(row, col);
}

/* Hide the cursor sprite (menus, win, game over). */
void ui_cursor_hide(void)
{
    move_sprite(CURSOR_SPRITE_ID, 0, 0);
}
