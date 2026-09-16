#ifndef PUZZLES_H
#define PUZZLES_H

/* ---------------------------------------------------------------------------
 * puzzles.h — Level definitions.
 *
 * Each level is a string of 81 characters ('0' = empty cell,
 * '1'-'9' = fixed digit). We store ONLY the givens, not the solution:
 * every puzzle has a unique solution (guaranteed by tools/gen_puzzles.py),
 * so "full and valid grid" is equivalent to "solved level".
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

/* A playable level. */
typedef struct {
    Difficulty difficulty;       /* Level difficulty. */
    char givens[CELL_COUNT + 1]; /* 81 chars + terminator. */
} Puzzle;

/* Level table (in ROM). Defined in puzzles_gen.c. */
extern const Puzzle puzzles[LEVEL_COUNT];

/* Printable difficulty name ("EASY", "MEDIUM", "HARD"). */
const char *difficulty_name(Difficulty diff);

#endif /* PUZZLES_H */
