#include <gbdk/platform.h>
#include <gbdk/font.h>
#include <gbdk/console.h>
#include <stdio.h>

#include "ui.h"
#include "board.h"
#include "puzzles.h"
#include "passwords.h"

/* ---------------------------------------------------------------------------
 * ui.c — Text drawing. Simple helpers, one screen per function.
 *
 * Grid geometry: each cell takes 2 columns (digit + space), with one
 * extra space after column 2 and 5 (3x3 box gaps). Row r starts at
 * screen row GRID_TOP, column x = r-th cell * 2 + box gap.
 * -------------------------------------------------------------------------*/

/* First screen row of the grid. */
#define GRID_TOP 3

/* Screen column of grid column c: 2 chars per cell + box gaps. */
static uint8_t cell_x(uint8_t col)
{
    uint8_t x;

    x = (uint8_t)(col * 2);
    if (col >= 3) {
        x++;
    }
    if (col >= 6) {
        x++;
    }
    return x;
}

/* Screen row of grid row r. */
static uint8_t cell_y(uint8_t row)
{
    return (uint8_t)(GRID_TOP + row);
}

/* Print one cell char (digit or '.') at its grid position. */
static void print_cell_value(uint8_t row, uint8_t col)
{
    uint8_t value;

    value = board_get((uint8_t)(row * GRID_SIZE + col));
    gotoxy(cell_x(col), cell_y(row));
    if (value == 0) {
        printf(".");
    } else {
        printf("%d", value);
    }
}

/* Init font + display. Call once at startup. */
void ui_init(void)
{
    font_t ibm_font;

    font_init();
    ibm_font = font_load(font_ibm);
    font_set(ibm_font);
    DISPLAY_ON;
    SHOW_BKG;
}

/* Title screen. `choice` 0 = NEW GAME, 1 = PASSWORD. */
void ui_title(uint8_t choice)
{
    cls();
    gotoxy(5, 2);
    printf("SUDOKU GB");
    gotoxy(3, 4);
    printf("12 LEVELS - 3 LIVES");
    gotoxy(6, 7);
    printf("%c NEW GAME", choice == 0 ? '>' : ' ');
    gotoxy(6, 8);
    printf("%c PASSWORD", choice == 1 ? '>' : ' ');
    gotoxy(1, 12);
    printf("UP/DOWN+A SELECT");
    gotoxy(1, 14);
    printf("BEAT A LEVEL TO GET");
    gotoxy(1, 15);
    printf("THE NEXT PASSWORD");
}

/* Password entry. `digits[4]`, `pos` = edited slot, `bad` = show error. */
void ui_password(const uint8_t *digits, uint8_t pos, uint8_t bad)
{
    uint8_t i;

    cls();
    gotoxy(4, 2);
    printf("PASSWORD");
    gotoxy(5, 5);
    for (i = 0; i < PASSWORD_DIGITS; i++) {
        if (i == pos) {
            printf("[%d]", digits[i]);
        } else {
            printf(" %d ", digits[i]);
        }
    }
    gotoxy(1, 9);
    printf("UP/DOWN DIGIT");
    gotoxy(1, 10);
    printf("LEFT/RIGHT SLOT");
    gotoxy(1, 11);
    printf("A OK  B BACK");
    if (bad) {
        gotoxy(3, 14);
        printf("WRONG PASSWORD");
    }
}

/* Game screen frame: header, full grid, footer. Redraws everything. */
void ui_game_full(uint8_t level)
{
    uint8_t r, c;

    cls();
    /* Header: title + level + difficulty. */
    gotoxy(0, 0);
    printf("SUDOKU L%02d %s", level + 1, difficulty_name(puzzles[level].difficulty));
    ui_mistakes();
    /* Grid: 9 rows. */
    for (r = 0; r < GRID_SIZE; r++) {
        for (c = 0; c < GRID_SIZE; c++) {
            print_cell_value(r, c);
        }
    }
    /* Footer: proposed digit + help. */
    ui_entry(1);
    gotoxy(0, 13);
    printf("+-UP/DOWN NUM");
    gotoxy(0, 14);
    printf("A:OK B:DEL ST:MENU");
}

/* Pause menu. `choice` 0 = RESUME, 1 = RESTART, 2 = TITLE. */
void ui_pause(uint8_t choice)
{
    cls();
    gotoxy(6, 2);
    printf("PAUSED");
    gotoxy(6, 5);
    printf("%c RESUME", choice == 0 ? '>' : ' ');
    gotoxy(6, 6);
    printf("%c RESTART", choice == 1 ? '>' : ' ');
    gotoxy(6, 7);
    printf("%c TITLE", choice == 2 ? '>' : ' ');
}

/* Win screen. Shows password for next level, or completion text if last. */
void ui_win(uint8_t level, uint16_t next_password, uint8_t is_last)
{
    cls();
    gotoxy(3, 2);
    printf("LEVEL %02d CLEAR", level + 1);
    if (is_last) {
        gotoxy(2, 6);
        printf("YOU BEAT THE GAME!");
        gotoxy(4, 8);
        printf("THANKS 4 PLAY");
    } else {
        gotoxy(1, 6);
        printf("NEXT PASSWORD:");
        gotoxy(7, 8);
        printf("%04d", next_password);
        gotoxy(1, 11);
        printf("WRITE IT DOWN!");
    }
    gotoxy(3, 14);
    printf("A CONTINUE");
}

/* Game over screen. `choice` 0 = RETRY, 1 = TITLE. */
void ui_gameover(uint8_t choice)
{
    cls();
    gotoxy(5, 2);
    printf("GAME OVER");
    gotoxy(2, 5);
    printf("3 MISTAKES MADE");
    gotoxy(6, 8);
    printf("%c RETRY", choice == 0 ? '>' : ' ');
    gotoxy(6, 9);
    printf("%c TITLE", choice == 1 ? '>' : ' ');
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

/* Redraw the proposed digit on row 12. */
void ui_entry(uint8_t value)
{
    gotoxy(0, 12);
    printf("ENTER: %d        ", value);
}

/* Show a message on row 15 (empty string clears). */
void ui_message(const char *text)
{
    gotoxy(0, 15);
    printf("%-20s", text);
}

/* Redraw one cell without cursor brackets (digit or '.'). */
void ui_cell(uint8_t row, uint8_t col)
{
    print_cell_value(row, col);
    /* Erase leftover bracket chars around this cell. */
    gotoxy((uint8_t)(cell_x(col) - 1), cell_y(row));
    printf(" ");
    gotoxy((uint8_t)(cell_x(col) + 1), cell_y(row));
    printf(" ");
}

/* Move the cursor brackets from (old) to (new) cell. */
void ui_cursor(uint8_t old_row, uint8_t old_col, uint8_t new_row, uint8_t new_col)
{
    /* Erase old brackets by redrawing the plain cell. */
    ui_cell(old_row, old_col);
    /* Draw new brackets around the value. */
    gotoxy((uint8_t)(cell_x(new_col) - 1), cell_y(new_row));
    printf("[");
    print_cell_value(new_row, new_col);
    gotoxy((uint8_t)(cell_x(new_col) + 1), cell_y(new_row));
    printf("]");
}
