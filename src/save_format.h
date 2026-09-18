#ifndef SAVE_FORMAT_H
#define SAVE_FORMAT_H

/* ---------------------------------------------------------------------------
 * save_format.h — Battery save format + hardware-free helpers.
 *
 * Single source of truth for the on-wire SRAM layout, shared by the
 * Game Boy I/O (save.c) and the PC host tests (tests/test_host.c).
 * No hardware access here: safe to compile with gcc.
 *
 * Layout at 0xA000 (fixed offsets, part of the save format):
 *   0x00 'S''U''D''K'  magic          0x59 origins[81]
 *   0x04 0x01          version        0xAA mistakes
 *   0x05 game_active                  0xAB marks[38]
 *   0x06 level (u16, low first)       0xD1 checksum
 *   0x08 values[81]                   0xD2 end
 * -------------------------------------------------------------------------*/

#include "types.h"
#include "board.h" /* MARKS_BYTES, ORIGIN_HINT, CELL_COUNT via types */

/* SRAM image size in bytes (checksum is the last one). */
#define SAVE_IMAGE_SIZE 0xD2

/* Slot version (bump to invalidate old saves on format change). */
#define SAVE_VERSION 1

/* Field offsets inside the image. */
#define SAVE_OFF_MAGIC 0x00    /* 4 bytes: 'S','U','D','K' */
#define SAVE_OFF_VERSION 0x04  /* 1 byte */
#define SAVE_OFF_ACTIVE 0x05   /* 1 byte: 0/1 */
#define SAVE_OFF_LEVEL_LO 0x06 /* u16, low byte first */
#define SAVE_OFF_LEVEL_HI 0x07
#define SAVE_OFF_VALUES 0x08    /* 81 bytes, 0-9 */
#define SAVE_OFF_ORIGINS 0x59   /* 81 bytes, ORIGIN_* codes */
#define SAVE_OFF_MISTAKES 0xAA  /* 1 byte */
#define SAVE_OFF_MARKS 0xAB     /* MARKS_BYTES bytes */
#define SAVE_OFF_CHECKSUM 0xD1  /* 1 byte: 8-bit sum of all prior bytes */

/* 8-bit sum of bytes[0..n). Not crypto: just detects dead battery /
 * wrong cartridge. Return value is the checksum byte to store. */
uint8_t save_checksum(const uint8_t *bytes, uint16_t n);

/* Return 1 if decoded slot fields are in range, else 0.
 * Checks: game_active 0/1, level < LEVEL_COUNT, values 0-9,
 * origins <= ORIGIN_HINT. Marks need no check (any bitmap is valid). */
uint8_t save_fields_valid(uint16_t level, uint8_t game_active,
                           const uint8_t *values, const uint8_t *origins);

#endif /* SAVE_FORMAT_H */
