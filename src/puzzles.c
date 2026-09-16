#include "puzzles.h"

/* ---------------------------------------------------------------------------
 * puzzles.c — Level helpers. No hardware dependencies.
 * -------------------------------------------------------------------------*/

/* Return the printable difficulty name. */
const char *difficulty_name(Difficulty diff)
{
    switch (diff) {
    case DIFF_EASY:
        return "EASY";
    case DIFF_MEDIUM:
        return "MEDIUM";
    case DIFF_HARD:
        return "HARD";
    default:
        return "?????";
    }
}
