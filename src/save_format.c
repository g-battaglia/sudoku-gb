#include "save_format.h"

/* ---------------------------------------------------------------------------
 * save_format.c — Hardware-free save helpers (GB + PC host tests).
 * -------------------------------------------------------------------------*/

/* 8-bit sum of bytes[0..n). */
uint8_t save_checksum(const uint8_t *bytes, uint16_t n)
{
    uint16_t i;
    uint8_t sum;

    sum = 0;
    for (i = 0; i < n; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum;
}

/* Return 1 if decoded slot fields are in range, else 0. */
uint8_t save_fields_valid(uint16_t level, uint8_t game_active,
                           const uint8_t *values, const uint8_t *origins)
{
    uint8_t i;

    if (game_active > 1) {
        return 0;
    }
    if (level >= LEVEL_COUNT) {
        return 0;
    }
    for (i = 0; i < CELL_COUNT; i++) {
        if (values[i] > 9 || origins[i] > ORIGIN_HINT) {
            return 0;
        }
    }
    return 1;
}
