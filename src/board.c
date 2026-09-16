#include "board.h"
#include "puzzles.h"

/* ---------------------------------------------------------------------------
 * board.c — State + rules. Plain C only, no hardware access.
 * -------------------------------------------------------------------------*/

/* Working grid: 81 values 0-9, index = row * 9 + column. */
static uint8_t cells[CELL_COUNT];

/* One origin per cell (single source of truth for shade + lock):
 * codes are shared through board.h (part of the save format). */
static uint8_t origin[CELL_COUNT];

/* Mistakes made in the current game. */
static uint8_t error_count;

/* Convert (row, column) to a linear index 0-80. */
static uint8_t cell_index(uint8_t row, uint8_t col)
{
    return (uint8_t)(row * GRID_SIZE + col);
}

/* Load level `level` (0-based): reset grid, origins and mistakes. */
void board_load(uint16_t level)
{
    uint8_t i, g;

    for (i = 0; i < CELL_COUNT; i++) {
        g = puzzle_given(level, i);
        cells[i] = g;
        origin[i] = (g != 0) ? ORIGIN_GIVEN : ORIGIN_PLAYER;
    }
    error_count = 0;
}

/* Value of cell `idx`: 0 = empty, 1-9 = digit. */
uint8_t board_get(uint8_t idx)
{
    return cells[idx];
}

/* Return 1 if cell `idx` is an original clue (black, never editable). */
uint8_t board_is_original(uint8_t idx)
{
    return (uint8_t)(origin[idx] == ORIGIN_GIVEN);
}

/* Return 1 if cell `idx` is not editable (clue or hint), 0 if the
 * player may change it. */
uint8_t board_is_locked(uint8_t idx)
{
    return (uint8_t)(origin[idx] != ORIGIN_PLAYER);
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

/* --- Mistakes: tallied only, play never ends. ---------------------------- */

/* Mistakes made so far (0-255, saturates). */
uint8_t board_errors(void)
{
    return error_count;
}

/* Record one mistake. */
void board_add_mistake(void)
{
    if (error_count < 255) {
        error_count++;
    }
}

/* Lock cell `idx` as a hint reveal (used by HINT). The digit renders
 * gray like a player digit, but the cell stays locked like a clue. */
void board_reveal(uint8_t idx)
{
    origin[idx] = ORIGIN_HINT;
}

/* Origin of cell `idx` (used by the save code). */
uint8_t board_origin(uint8_t idx)
{
    return origin[idx];
}

/* Restore a full game in one shot (used by LOAD). */
void board_restore(const uint8_t *values, const uint8_t *origins,
                   uint8_t mistakes)
{
    uint8_t i;

    for (i = 0; i < CELL_COUNT; i++) {
        cells[i] = values[i];
        origin[i] = origins[i];
    }
    error_count = mistakes;
}

/* --- Level completion marks (battery-saved) ---------------------------- */

void marks_clear(uint8_t *bm)
{
    uint8_t i;

    for (i = 0; i < MARKS_BYTES; i++) {
        bm[i] = 0;
    }
}

void marks_set(uint8_t *bm, uint16_t level)
{
    bm[level >> 3] |= (uint8_t)(1u << (level & 7));
}

uint8_t marks_get(const uint8_t *bm, uint16_t level)
{
    return (uint8_t)((bm[level >> 3] >> (level & 7)) & 1);
}

uint8_t marks_count(const uint8_t *bm, uint16_t first, uint16_t n)
{
    uint16_t level, count;

    count = 0;
    for (level = first; level < (uint16_t)(first + n); level++) {
        count += marks_get(bm, level);
    }
    return (uint8_t)count;
}
