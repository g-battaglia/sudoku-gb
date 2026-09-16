#include "passwords.h"

/* ---------------------------------------------------------------------------
 * passwords.c — Password formula. No hardware dependencies.
 *
 * Formula:  code = (((level + 1) * MULT + ADD) XOR XORV) mod 10000
 *
 * - Explicit uint32_t arithmetic: no ambiguous overflow, same result on
 *   Game Boy (SDCC) and PC (gcc) — and reproducible by hand in Python.
 * - The constants are arbitrary (chosen to spread codes over the whole
 *   0000-9999 range); changing them invalidates the printed passwords.
 * -------------------------------------------------------------------------*/

#define PWD_MULT 7919UL
#define PWD_ADD 104729UL
#define PWD_XOR 0xBEEFUL

/* Compute the password (0-9999) for `level` (0-based). */
uint16_t password_for_level(uint8_t level)
{
    uint32_t x;

    x = ((uint32_t)level + 1UL) * PWD_MULT + PWD_ADD;
    x = x ^ PWD_XOR;
    return (uint16_t)(x % 10000UL);
}

/* Return 1 if `code` is the password for `level`, 0 otherwise. */
uint8_t password_matches(uint8_t level, uint16_t code)
{
    if (level >= LEVEL_COUNT) {
        return 0;
    }
    return password_for_level(level) == code;
}

/* Find which level `code` belongs to. Return -1 if invalid. */
int8_t password_find_level(uint16_t code)
{
    uint8_t level;

    for (level = 0; level < LEVEL_COUNT; level++) {
        if (password_for_level(level) == code) {
            return (int8_t)level;
        }
    }
    return -1;
}
