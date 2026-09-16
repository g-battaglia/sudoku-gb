#include "puzzles.h"

/* ---------------------------------------------------------------------------
 * puzzles.c — Packed level helpers. No hardware dependencies.
 * -------------------------------------------------------------------------*/

/* Solution digit 1-9 of cell `idx` in level `level`. Even cells live in
 * the high nibble of byte idx/2, odd cells in the low nibble. */
uint8_t puzzle_solution(uint16_t level, uint8_t idx)
{
    uint8_t b;

    b = puzzles[level].solution[idx >> 1];
    if ((idx & 1) == 0) {
        return (uint8_t)(b >> 4);
    }
    return (uint8_t)(b & 0x0F);
}

/* Givens digit of cell `idx`: the solution digit if the mask keeps it,
 * else 0 (empty). Bit idx lives in byte idx/8, bit idx%8. */
uint8_t puzzle_given(uint16_t level, uint8_t idx)
{
    if (puzzles[level].givens_mask[idx >> 3] & (uint8_t)(1 << (idx & 7))) {
        return puzzle_solution(level, idx);
    }
    return 0;
}

/* Return the printable difficulty name. */
const char *difficulty_name(uint8_t diff)
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
