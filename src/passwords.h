#ifndef PASSWORDS_H
#define PASSWORDS_H

/* ---------------------------------------------------------------------------
 * passwords.h — Level unlock passwords (instead of a save system).
 *
 * Rule: beating level N shows the password for level N+1. Entering a
 * valid password from the title screen jumps straight to that level.
 * No cartridge save (no MBC/battery needed: a 32KB ROM ONLY is enough).
 *
 * The password is deterministically derived from the level number via
 * password_for_level(): the formula is the SINGLE source of truth
 * (the printable table comes from `make passwords`, which runs the host
 * test and prints the codes computed by the C code itself).
 *
 * NOTE: this is not security, just anti-spoiler — the formula is in ROM.
 * -------------------------------------------------------------------------*/

#include "types.h"

/* Compute the password (0-9999) for `level` (0-based). */
uint16_t password_for_level(uint8_t level);

/* Return 1 if `code` is the password for `level`, 0 otherwise. */
uint8_t password_matches(uint8_t level, uint16_t code);

/* Find which level `code` belongs to. Return -1 if invalid. */
int8_t password_find_level(uint16_t code);

#endif /* PASSWORDS_H */
