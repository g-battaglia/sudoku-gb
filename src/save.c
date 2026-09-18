#include "save.h"
#include "save_format.h"
#include <gb/gb.h>

/* ---------------------------------------------------------------------------
 * save.c — Battery save I/O (Game Boy side).
 *
 * SRAM lives at 0xA000-0xBFFF behind the MBC1 latch: it must be
 * enabled (write 0x0A to the RAM-enable register) before every access
 * and disabled afterwards, so a stray write during a crash can not
 * corrupt the save. GBDK's ENABLE_RAM / DISABLE_RAM macros do the
 * register writes. Every public function balances ENABLE/DISABLE with
 * no early return in between.
 *
 * Layout, version, checksum and field validation live in save_format.*
 * (hardware-free, host-tested). This file only moves bytes.
 * -------------------------------------------------------------------------*/

#define SRAM ((uint8_t *)0xA000)

static const uint8_t MAGIC[4] = {'S', 'U', 'D', 'K'};

/* Read one field of `n` bytes from SRAM into `dst`.
 * In: off + n <= SAVE_IMAGE_SIZE. SRAM must be enabled. */
static void sram_read(uint8_t *dst, uint16_t off, uint8_t n)
{
    uint8_t i;

    for (i = 0; i < n; i++) {
        dst[i] = SRAM[(uint16_t)(off + i)];
    }
}

/* Write one field of `n` bytes from `src` into SRAM.
 * In: off + n <= SAVE_IMAGE_SIZE. SRAM must be enabled. */
static void sram_write(const uint8_t *src, uint16_t off, uint8_t n)
{
    uint8_t i;

    for (i = 0; i < n; i++) {
        SRAM[(uint16_t)(off + i)] = src[i];
    }
}

/* Return 1 if magic, version and checksum match, else 0.
 * In: SRAM must be enabled. Pure header check, no field ranges. */
static uint8_t sram_valid(void)
{
    uint8_t i;

    for (i = 0; i < 4; i++) {
        if (SRAM[(uint16_t)(SAVE_OFF_MAGIC + i)] != MAGIC[i]) {
            return 0;
        }
    }
    if (SRAM[SAVE_OFF_VERSION] != SAVE_VERSION) {
        return 0;
    }
    return (uint8_t)(save_checksum(SRAM, SAVE_OFF_CHECKSUM) ==
                     SRAM[SAVE_OFF_CHECKSUM]);
}

/* Return 1 if SRAM holds a plausible save header, else 0. */
uint8_t save_present(void)
{
    uint8_t ok;

    ENABLE_RAM;
    ok = sram_valid();
    DISABLE_RAM;
    return ok;
}

/* Read the slot from SRAM into *slot.
 * Return 1 on success (header + field ranges valid), 0 if the save
 * is invalid (caller must not use *slot then). */
uint8_t save_read(SaveSlot *slot)
{
    uint8_t ok;
    uint16_t level;

    ENABLE_RAM;
    ok = sram_valid();
    if (ok) {
        slot->game_active = SRAM[SAVE_OFF_ACTIVE];
        slot->level = (uint16_t)(SRAM[SAVE_OFF_LEVEL_LO] |
                                 (SRAM[SAVE_OFF_LEVEL_HI] << 8));
        sram_read(slot->values, SAVE_OFF_VALUES, CELL_COUNT);
        sram_read(slot->origins, SAVE_OFF_ORIGINS, CELL_COUNT);
        slot->mistakes = SRAM[SAVE_OFF_MISTAKES];
        sram_read(slot->marks, SAVE_OFF_MARKS, MARKS_BYTES);
        /* A checksum-passing slot can still hold out-of-range fields
         * (e.g. level 600 from a foreign cart): reject those too so
         * LOAD never jumps out of the level table. */
        level = slot->level;
        ok = save_fields_valid(level, slot->game_active, slot->values,
                               slot->origins);
    }
    DISABLE_RAM;
    return ok;
}

/* Write *slot to SRAM (adds magic, version and checksum).
 * In: slot fields in range (level < LEVEL_COUNT, values 0-9,
 * origins ORIGIN_*). Checksum is computed by re-reading what landed
 * in SRAM, so it covers the actual stored bytes. */
void save_write(const SaveSlot *slot)
{
    uint8_t lo, hi;

    ENABLE_RAM;
    sram_write(MAGIC, SAVE_OFF_MAGIC, 4);
    SRAM[SAVE_OFF_VERSION] = SAVE_VERSION;
    SRAM[SAVE_OFF_ACTIVE] = slot->game_active;
    lo = (uint8_t)(slot->level & 0xFF);
    hi = (uint8_t)(slot->level >> 8);
    SRAM[SAVE_OFF_LEVEL_LO] = lo;
    SRAM[SAVE_OFF_LEVEL_HI] = hi;
    sram_write(slot->values, SAVE_OFF_VALUES, CELL_COUNT);
    sram_write(slot->origins, SAVE_OFF_ORIGINS, CELL_COUNT);
    SRAM[SAVE_OFF_MISTAKES] = slot->mistakes;
    sram_write(slot->marks, SAVE_OFF_MARKS, MARKS_BYTES);

    /* Checksum last, over everything written so far. */
    SRAM[SAVE_OFF_CHECKSUM] = save_checksum(SRAM, SAVE_OFF_CHECKSUM);
    DISABLE_RAM;
}
