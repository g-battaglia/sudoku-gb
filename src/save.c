#include "save.h"
#include <gb/gb.h>

/* ---------------------------------------------------------------------------
 * save.c — Battery save I/O.
 *
 * SRAM lives at 0xA000-0xBFFF behind the MBC1 latch: it must be
 * enabled (write 0x0A to the RAM-enable register) before every access
 * and disabled afterwards, so a stray write during a crash can not
 * corrupt the save. GBDK's ENABLE_RAM / DISABLE_RAM macros do the
 * register writes.
 *
 * On-wire layout at 0xA000 (fixed offsets, part of the save format):
 *   0x00 'S''U''D''K'  magic          0x59 origins[81]
 *   0x04 0x01          version        0xAA mistakes
 *   0x05 game_active                  0xAB marks[38]
 *   0x06 level (u16, low first)       0xD1 checksum
 *   0x08 values[81]                   0xD2 end
 *
 * The checksum is the 8-bit sum of every byte before it. Not crypto,
 * just enough to notice a dead battery or a wrong cartridge.
 * -------------------------------------------------------------------------*/

#define SRAM ((uint8_t *)0xA000)

/* Offsets inside the SRAM slot. */
#define OFF_MAGIC      0x00 /* 4 bytes */
#define OFF_VERSION    0x04
#define OFF_ACTIVE     0x05
#define OFF_LEVEL_LO   0x06
#define OFF_LEVEL_HI   0x07
#define OFF_VALUES     0x08  /* 81 bytes */
#define OFF_ORIGINS    0x59  /* 81 bytes */
#define OFF_MISTAKES   0xAA
#define OFF_MARKS      0xAB  /* MARKS_BYTES bytes */
#define OFF_CHECKSUM   0xD1

#define SAVE_VERSION 1

static const uint8_t MAGIC[4] = {'S', 'U', 'D', 'K'};

/* Read one field of `n` bytes from SRAM into `dst`. */
static void sram_read(uint8_t *dst, uint16_t off, uint8_t n)
{
    uint8_t i;

    for (i = 0; i < n; i++) {
        dst[i] = SRAM[(uint16_t)(off + i)];
    }
}

/* Write one field of `n` bytes from `src` into SRAM. */
static void sram_write(const uint8_t *src, uint16_t off, uint8_t n)
{
    uint8_t i;

    for (i = 0; i < n; i++) {
        SRAM[(uint16_t)(off + i)] = src[i];
    }
}

/* Return 1 if magic, version and checksum match (SRAM must be enabled). */
static uint8_t sram_valid(void)
{
    uint16_t off;
    uint8_t sum, i;

    for (i = 0; i < 4; i++) {
        if (SRAM[(uint16_t)(OFF_MAGIC + i)] != MAGIC[i]) {
            return 0;
        }
    }
    if (SRAM[OFF_VERSION] != SAVE_VERSION) {
        return 0;
    }
    sum = 0;
    for (off = 0; off < OFF_CHECKSUM; off++) {
        sum = (uint8_t)(sum + SRAM[off]);
    }
    return (uint8_t)(sum == SRAM[OFF_CHECKSUM]);
}

uint8_t save_present(void)
{
    uint8_t ok;

    ENABLE_RAM;
    ok = sram_valid();
    DISABLE_RAM;
    return ok;
}

uint8_t save_read(SaveSlot *slot)
{
    uint8_t ok;

    ENABLE_RAM;
    ok = sram_valid();
    if (ok) {
        slot->game_active = SRAM[OFF_ACTIVE];
        slot->level = (uint16_t)(SRAM[OFF_LEVEL_LO]
                                 | (SRAM[OFF_LEVEL_HI] << 8));
        sram_read(slot->values, OFF_VALUES, CELL_COUNT);
        sram_read(slot->origins, OFF_ORIGINS, CELL_COUNT);
        slot->mistakes = SRAM[OFF_MISTAKES];
        sram_read(slot->marks, OFF_MARKS, MARKS_BYTES);
    }
    DISABLE_RAM;
    return ok;
}

void save_write(const SaveSlot *slot)
{
    uint16_t off;
    uint8_t sum, lo, hi;

    ENABLE_RAM;
    sram_write(MAGIC, OFF_MAGIC, 4);
    SRAM[OFF_VERSION] = SAVE_VERSION;
    SRAM[OFF_ACTIVE] = slot->game_active;
    lo = (uint8_t)(slot->level & 0xFF);
    hi = (uint8_t)(slot->level >> 8);
    SRAM[OFF_LEVEL_LO] = lo;
    SRAM[OFF_LEVEL_HI] = hi;
    sram_write(slot->values, OFF_VALUES, CELL_COUNT);
    sram_write(slot->origins, OFF_ORIGINS, CELL_COUNT);
    SRAM[OFF_MISTAKES] = slot->mistakes;
    sram_write(slot->marks, OFF_MARKS, MARKS_BYTES);

    /* Checksum last, over everything written so far. */
    sum = 0;
    for (off = 0; off < OFF_CHECKSUM; off++) {
        sum = (uint8_t)(sum + SRAM[off]);
    }
    SRAM[OFF_CHECKSUM] = sum;
    DISABLE_RAM;
}
