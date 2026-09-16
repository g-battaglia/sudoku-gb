#ifndef BOARD_H
#define BOARD_H

/* ---------------------------------------------------------------------------
 * board.h — Game state and Sudoku rules.
 *
 * Holds in RAM: the working grid (clues + player digits), the cell
 * origins and the mistake counter. The cursor lives in main.c
 * (navigation, not a rule).
 *
 * Cell origins (rendering + protection):
 * - original clue: black digit, never editable.
 * - player digit: gray digit, editable (erase / overwrite).
 * - hint reveal: gray digit like a player digit, but LOCKED like
 *   a clue (the revealed digit stays).
 *
 * Rules:
 * - Locked cells (clues, hints) cannot be changed.
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
#include "puzzles.h"  /* LEVEL_COUNT for the marks bitmap */

/* Cell origin codes: rendering shade + edit lock. They are stored in
 * the battery save, so the numbering is part of the save format. */
#define ORIGIN_PLAYER 0 /* gray, editable */
#define ORIGIN_GIVEN 1 /* black, locked clue */
#define ORIGIN_HINT 2 /* gray, locked reveal */

/* Load level `level` (0-based, 0-299): reset grid and mistakes. */
void board_load(uint16_t level);

/* Value of cell `idx` (0-80, row by row): 0 = empty, 1-9 = digit. */
uint8_t board_get(uint8_t idx);

/* Return 1 if cell `idx` is an original clue (renders black). */
uint8_t board_is_original(uint8_t idx);

/* Return 1 if cell `idx` is not editable (clue or hint reveal). */
uint8_t board_is_locked(uint8_t idx);

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

/* Lock cell `idx` as a hint reveal (used by HINT: the revealed digit
 * renders gray but stays locked). */
void board_reveal(uint8_t idx);

/* Origin of cell `idx` (ORIGIN_* above) — used by the save code to
 * snapshot which digits are clues, hints or player entries. */
uint8_t board_origin(uint8_t idx);

/* Restore a full game in one shot (used by LOAD): copy values and
 * origins (81 bytes each, ORIGIN_* codes) and the mistake count.
 * Level bookkeeping stays in main.c. */
void board_restore(const uint8_t *values, const uint8_t *origins,
                   uint8_t mistakes);

/* --- Level completion marks (battery-saved) -----------------------------
 * One bit per level, packed LSB-first: MARKS_BYTES bytes cover all
 * LEVEL_COUNT levels. The same layout is stored in SRAM, so saving
 * and loading are plain copies. */
#define MARKS_BYTES ((LEVEL_COUNT + 7) / 8) /* 38 for 300 levels */

void marks_clear(uint8_t *bm);
void marks_set(uint8_t *bm, uint16_t level);
uint8_t marks_get(const uint8_t *bm, uint16_t level);

/* Number of set marks among levels first..first+n-1 (DONE x/100). */
uint8_t marks_count(const uint8_t *bm, uint16_t first, uint16_t n);

#endif /* BOARD_H */
