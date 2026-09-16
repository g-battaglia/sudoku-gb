#ifndef SAVE_H
#define SAVE_H

/* ---------------------------------------------------------------------------
 * save.h — Battery save (SRAM) slot.
 *
 * One slot holds everything worth persisting: the game in progress
 * (level, grid, origins, mistakes) plus the completion marks of all
 * 300 levels. The slot is mirrored at 0xA000 in battery SRAM guarded
 * by a magic string, a version byte and a checksum, so garbage SRAM
 * (first boot, dead battery) is detected and ignored.
 *
 * save.c does the hardware access (MBC registers); this header and
 * SaveSlot stay plain C for readability.
 * -------------------------------------------------------------------------*/

#include "types.h"
#include "board.h" /* MARKS_BYTES, ORIGIN_* codes */

typedef struct {
    uint8_t game_active;    /* 1 = a game in progress can be resumed */
    uint16_t level;         /* 0-299, level of the saved game */
    uint8_t values[CELL_COUNT];  /* 0-9, row by row */
    uint8_t origins[CELL_COUNT]; /* ORIGIN_* code per cell */
    uint8_t mistakes;       /* mistake count of the saved game */
    uint8_t marks[MARKS_BYTES];  /* level completion bitmap */
} SaveSlot;

/* Return 1 if SRAM holds a valid save (magic + version + checksum). */
uint8_t save_present(void);

/* Read the slot from SRAM into *slot. Returns 1 on success, 0 if the
 * save is invalid (caller must not use *slot then). */
uint8_t save_read(SaveSlot *slot);

/* Write *slot to SRAM (adds magic, version and checksum). */
void save_write(const SaveSlot *slot);

#endif /* SAVE_H */
