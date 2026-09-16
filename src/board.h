#ifndef BOARD_H
#define BOARD_H

/* ---------------------------------------------------------------------------
 * board.h — Game state and Sudoku rules.
 *
 * Holds in RAM: the working grid (givens + player digits), the lock
 * flags and the mistake counter. The cursor lives in main.c
 * (navigation, not a rule).
 *
 * Rules:
 * - Given (fixed) cells cannot be changed.
 * - A digit that conflicts (same number in row/column/box) is REJECTED
 *   and counts as one mistake. There is no game over: mistakes are
 *   only tallied (visible in the START menu) and play goes on forever.
 * - A HINT reveals the true digit of a cell and locks it like a given.
 * - Win: grid full. That is enough, because every puzzle has a unique
 *   solution and conflicting digits are always rejected, so a full
 *   grid is necessarily valid.
 *
 * No hardware dependencies: testable on PC with gcc.
 * -------------------------------------------------------------------------*/

#include "types.h"

/* Load level `level` (0-based): reset grid and mistakes. */
void board_load(uint8_t level);

/* Value of cell `idx` (0-80, row by row): 0 = empty, 1-9 = digit. */
uint8_t board_get(uint8_t idx);

/* Return 1 if cell `idx` is locked (given or hint), 0 if editable. */
uint8_t board_is_given(uint8_t idx);

/* Write `value` (0-9) into cell `idx`. No checks here: the caller must
 * call board_conflicts() BEFORE accepting the move. */
void board_set(uint8_t idx, uint8_t value);

/* Return 1 if the value in cell `idx` breaks the rules
 * (duplicate in row, column or 3x3 box), 0 if legal. */
uint8_t board_conflicts(uint8_t idx);

/* Return 1 if the grid is full and valid (level solved). */
uint8_t board_is_solved(void);

/* Mistakes made so far (0-255, saturates). */
uint8_t board_errors(void);

/* Record one mistake. Play never ends: the count is only shown
 * in the START menu. */
void board_add_mistake(void);

/* Lock cell `idx` as a given (used by HINT: the revealed digit stays). */
void board_reveal(uint8_t idx);

#endif /* BOARD_H */
