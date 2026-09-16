#ifndef PUZZLES_H
#define PUZZLES_H

/* ---------------------------------------------------------------------------
 * puzzles.h — Level definitions (packed for ROM ONLY 32KB).
 *
 * 300 levels: 100 EASY + 100 MEDIUM + 100 HARD, in that order. The
 * difficulty is the index range (level / DIFF_LEVELS), not stored data.
 * Each level keeps the full solution (powers HINT) plus the givens mask:
 * - solution[41]: 81 digits 1-9, 4 bits each (cell i: byte i/2, even i
 *   = high nibble, odd i = low nibble) = 40.5 bytes, rounded up.
 * - givens_mask[11]: bit i = cell i is a given (81 bits, 11 bytes).
 * Total 52 bytes/level (15.6KB for 300): the old char format cost
 * ~165 bytes/level and could never fit 300 levels in 32KB.
 * Win still means "full and valid grid", which equals the solution.
 *
 * The actual data lives in src/puzzles_gen.c (generated file, do not edit).
 * This module has no hardware dependencies: it is testable on PC with gcc.
 * -------------------------------------------------------------------------*/

#include "types.h"

/* Difficulty of a level block. The order of the values is significant. */
typedef enum {
    DIFF_EASY = 0,
    DIFF_MEDIUM = 1,
    DIFF_HARD = 2
} Difficulty;

/* Levels per difficulty, difficulty count, total levels. */
#define DIFF_LEVELS 100
#define DIFF_COUNT 3
#define LEVEL_COUNT (DIFF_COUNT * DIFF_LEVELS)

/* Bytes per level: 41 solution + 11 mask. */
#define SOLUTION_BYTES 41
#define GIVENS_MASK_BYTES 11

/* A playable level. Givens = solution cells kept by the mask. */
typedef struct {
    uint8_t solution[SOLUTION_BYTES];   /* 81 digits 1-9, 4 bits each. */
    uint8_t givens_mask[GIVENS_MASK_BYTES]; /* Bit i = cell i is given. */
} Puzzle;

/* Level table (in ROM, EASY block first). Defined in puzzles_gen.c. */
extern const Puzzle puzzles[LEVEL_COUNT];

/* Solution digit 1-9 of cell `idx` (0-80) in level `level` (0-299:
 * uint16_t, a uint8_t cannot address 300 levels). */
uint8_t puzzle_solution(uint16_t level, uint8_t idx);

/* Givens digit of cell `idx` in level `level` (0 = empty cell). */
uint8_t puzzle_given(uint16_t level, uint8_t idx);

/* Printable difficulty name ("EASY", "MEDIUM", "HARD"). */
const char *difficulty_name(uint8_t diff);

#endif /* PUZZLES_H */
