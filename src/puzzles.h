#ifndef PUZZLES_H
#define PUZZLES_H

/* ---------------------------------------------------------------------------
 * puzzles.h — Level definitions.
 *
 * Each level is a string of 81 characters ('0' = empty cell,
 * '1'-'9' = fixed digit). Givens are the start position; the solution
 * is the unique full grid (guaranteed by tools/gen_puzzles.py) and
 * powers the HINT command. Win still means "full and valid grid",
 * which equals the stored solution.
 *
 * The actual data lives in src/puzzles_gen.c (generated file, do not edit).
 * This module has no hardware dependencies: it is testable on PC with gcc.
 * -------------------------------------------------------------------------*/

#include "types.h"

/* Difficulty of a level. The order of the values is significant. */
typedef enum {
    DIFF_EASY = 0,
    DIFF_MEDIUM = 1,
    DIFF_HARD = 2
} Difficulty;

/* A playable level. Givens are the starting cells; the solution is the
 * unique full grid (powers the HINT command, 81 bytes per level). */
typedef struct {
    Difficulty difficulty;         /* Level difficulty. */
    char givens[CELL_COUNT + 1];   /* 81 chars + terminator. */
    char solution[CELL_COUNT + 1]; /* 81 chars + terminator. */
} Puzzle;

/* Level table (in ROM). Defined in puzzles_gen.c. */
extern const Puzzle puzzles[LEVEL_COUNT];

/* Printable difficulty name ("EASY", "MEDIUM", "HARD"). */
const char *difficulty_name(Difficulty diff);

#endif /* PUZZLES_H */
