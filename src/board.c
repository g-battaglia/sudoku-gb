#include "board.h"
#include "puzzles.h"

/* ---------------------------------------------------------------------------
 * board.c — State + rules. Plain C only, no hardware access.
 * -------------------------------------------------------------------------*/

/* Working grid: 81 values 0-9, index = row * 9 + column. */
static uint8_t cells[CELL_COUNT];

/* Givens of the loaded level (81-char string in ROM). */
static const char *givens_ref;

/* Mistakes made in the current game. */
static uint8_t error_count;

/* Convert (row, column) to a linear index 0-80. */
static uint8_t cell_index(uint8_t row, uint8_t col)
{
    return (uint8_t)(row * GRID_SIZE + col);
}

/* Load level `level` (0-based): reset grid and mistakes. */
void board_load(uint8_t level)
{
    uint8_t i;

    givens_ref = puzzles[level].givens;
    for (i = 0; i < CELL_COUNT; i++) {
        cells[i] = (uint8_t)(givens_ref[i] - '0');
    }
    error_count = 0;
}

/* Value of cell `idx`: 0 = empty, 1-9 = digit. */
uint8_t board_get(uint8_t idx)
{
    return cells[idx];
}

/* Return 1 if cell `idx` is a given, 0 if editable. */
uint8_t board_is_given(uint8_t idx)
{
    return givens_ref[idx] != '0';
}

/* Write `value` (0-9) into cell `idx`, unchecked. */
void board_set(uint8_t idx, uint8_t value)
{
    cells[idx] = value;
}

/* Return 1 if the value in `idx` is duplicated in row/column/box. */
uint8_t board_conflicts(uint8_t idx)
{
    uint8_t row, col, r, c, value;

    value = cells[idx];
    if (value == 0) {
        return 0; /* Empty cell: no conflict possible. */
    }
    row = (uint8_t)(idx / GRID_SIZE);
    col = (uint8_t)(idx % GRID_SIZE);

    /* Same row (skip the cell itself). */
    for (c = 0; c < GRID_SIZE; c++) {
        if (c != col && cells[cell_index(row, c)] == value) {
            return 1;
        }
    }
    /* Same column. */
    for (r = 0; r < GRID_SIZE; r++) {
        if (r != row && cells[cell_index(r, col)] == value) {
            return 1;
        }
    }
    /* Same 3x3 box. */
    for (r = (uint8_t)((row / BOX_SIZE) * BOX_SIZE);
         r < (uint8_t)((row / BOX_SIZE) * BOX_SIZE + BOX_SIZE); r++) {
        for (c = (uint8_t)((col / BOX_SIZE) * BOX_SIZE);
             c < (uint8_t)((col / BOX_SIZE) * BOX_SIZE + BOX_SIZE); c++) {
            if ((r != row || c != col) && cells[cell_index(r, c)] == value) {
                return 1;
            }
        }
    }
    return 0;
}

/* Return 1 if the grid is full and valid (level solved). */
uint8_t board_is_solved(void)
{
    uint8_t i;

    for (i = 0; i < CELL_COUNT; i++) {
        if (cells[i] == 0 || board_conflicts(i)) {
            return 0;
        }
    }
    return 1;
}

/* --- Mistake counter: lives here because "max 3" is a game rule. -------- */

/* Mistakes made so far (0-MAX_ERRORS). */
uint8_t board_errors(void)
{
    return error_count;
}

/* Record one mistake. Return 1 if the game is lost (too many mistakes). */
uint8_t board_register_error(void)
{
    if (error_count < 255) {
        error_count++;
    }
    return error_count >= MAX_ERRORS;
}
