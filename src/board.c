#include "board.h"
#include "puzzles.h"

/* ---------------------------------------------------------------------------
 * board.c — State + rules. Plain C only, no hardware access.
 *
 * Conventions: idx is 0-80 row-major (row * 9 + col). value is 0-9
 * (0 = empty). Predicates return 1 = true, 0 = false. Callers guarantee
 * idx < CELL_COUNT and level < LEVEL_COUNT (no runtime checks: this is
 * inner-loop Game Boy code, ROM and CPU are tight).
 * -------------------------------------------------------------------------*/

/* Working grid: 81 values 0-9, index = row * 9 + column. */
static uint8_t cells[CELL_COUNT];

/* One origin per cell (single source of truth for shade + lock):
 * codes are shared through board.h (part of the save format). */
static uint8_t cell_origin[CELL_COUNT];

/* Mistakes made in the current game. */
static uint8_t error_count;

/* Convert (row, column) to a linear index 0-80.
 * In: row, col < GRID_SIZE. Out: row-major index. */
static uint8_t cell_index(uint8_t row, uint8_t col)
{
    return (uint8_t)(row * GRID_SIZE + col);
}

/* Load level `level` (0-based, < LEVEL_COUNT): reset grid, origins
 * and mistakes. */
void board_load(uint16_t level)
{
    uint8_t i, g;

    for (i = 0; i < CELL_COUNT; i++) {
        g = puzzle_given(level, i);
        cells[i] = g;
        cell_origin[i] = (g != 0) ? ORIGIN_GIVEN : ORIGIN_PLAYER;
    }
    error_count = 0;
}

/* Value of cell `idx` (0-80): 0 = empty, 1-9 = digit. */
uint8_t board_get(uint8_t idx)
{
    return cells[idx];
}

/* Return 1 if cell `idx` is an original clue (black, never editable). */
uint8_t board_is_original(uint8_t idx)
{
    return (uint8_t)(cell_origin[idx] == ORIGIN_GIVEN);
}

/* Return 1 if cell `idx` is not editable (clue or hint), 0 if the
 * player may change it. */
uint8_t board_is_locked(uint8_t idx)
{
    return (uint8_t)(cell_origin[idx] != ORIGIN_PLAYER);
}

/* Write `value` (0-9) into cell `idx`, unchecked.
 * Caller must call board_conflicts() BEFORE accepting the move. */
void board_set(uint8_t idx, uint8_t value)
{
    cells[idx] = value;
}

/* Return 1 if the value in `idx` is duplicated in row/column/box.
 * In: idx < CELL_COUNT, cells[idx] already written. Out: 1 = illegal. */
uint8_t board_conflicts(uint8_t idx)
{
    uint8_t row, col, r, c, value;
    uint8_t box_row0, box_col0;

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
    /* Same 3x3 box: precompute the top-left corner once (division is
     * expensive on the LR35902, never put it in a loop guard). */
    box_row0 = (uint8_t)((row / BOX_SIZE) * BOX_SIZE);
    box_col0 = (uint8_t)((col / BOX_SIZE) * BOX_SIZE);
    for (r = box_row0; r < (uint8_t)(box_row0 + BOX_SIZE); r++) {
        for (c = box_col0; c < (uint8_t)(box_col0 + BOX_SIZE); c++) {
            if ((r != row || c != col) && cells[cell_index(r, c)] == value) {
                return 1;
            }
        }
    }
    return 0;
}

/* Return 1 if the grid is full and valid (level solved), else 0.
 * Full implies valid here: puzzles are unique and conflicts are
 * rejected on entry, so no separate solution compare is needed. */
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

/* Mistakes made so far (0-255, saturates, never wraps). */
uint8_t board_errors(void)
{
    return error_count;
}

/* Record one mistake (saturates at 255). */
void board_add_mistake(void)
{
    if (error_count < 255) {
        error_count++;
    }
}

/* Lock cell `idx` as a hint reveal (used by HINT). The digit renders
 * gray like a player digit, but the cell stays locked like a clue.
 * In: idx < CELL_COUNT, cells[idx] already holds the solution digit. */
void board_reveal(uint8_t idx)
{
    cell_origin[idx] = ORIGIN_HINT;
}

/* Origin of cell `idx` (ORIGIN_* code, used by the save code). */
uint8_t board_origin(uint8_t idx)
{
    return cell_origin[idx];
}

/* Restore a full game in one shot (used by LOAD).
 * In: values 0-9, origins ORIGIN_* codes, mistakes count.
 * Level bookkeeping stays in main.c. */
void board_restore(const uint8_t *values, const uint8_t *origins,
                   uint8_t mistakes)
{
    uint8_t i;

    for (i = 0; i < CELL_COUNT; i++) {
        cells[i] = values[i];
        cell_origin[i] = origins[i];
    }
    error_count = mistakes;
}

/* --- Level completion marks (battery-saved) ---------------------------- */
/* One bit per level, LSB-first. Same layout as SRAM: save/load are
 * plain copies, no conversion. bm must hold MARKS_BYTES bytes. */

void marks_clear(uint8_t *bm)
{
    uint8_t i;

    for (i = 0; i < MARKS_BYTES; i++) {
        bm[i] = 0;
    }
}

/* Mark `level` (< LEVEL_COUNT) as beaten. */
void marks_set(uint8_t *bm, uint16_t level)
{
    bm[level >> 3] |= (uint8_t)(1u << (level & 7));
}

/* Return 1 if `level` is marked beaten, else 0. */
uint8_t marks_get(const uint8_t *bm, uint16_t level)
{
    return (uint8_t)((bm[level >> 3] >> (level & 7)) & 1);
}

/* Count set marks in [first, first+n). Fits in uint8_t (n <= 100). */
uint8_t marks_count(const uint8_t *bm, uint16_t first, uint16_t n)
{
    uint16_t level, count;

    count = 0;
    for (level = first; level < (uint16_t)(first + n); level++) {
        count += marks_get(bm, level);
    }
    return (uint8_t)count;
}
