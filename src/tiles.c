#include <gbdk/platform.h>
#include <gbdk/font.h>

#include "tiles.h"

/* ---------------------------------------------------------------------------
 * tiles.c — Grid tile indexing + loading. The artwork itself is
 * PRECOMPUTED offline (tools/gen_tiles.py -> src/tiles_gen.c): computing
 * 230 tiles at runtime on the LR35902 takes ~10 seconds, so the ROM
 * carries the finished 2bpp bytes (3680 + 64) and only copies them.
 *
 * Fullscreen board: each of the 9x9 cells is 16x16 px (2x2 BG tiles),
 * so the grid is 144x144 px = the whole screen height, centered with a
 * 1-tile margin left and right (which carries the outer frame lines).
 * The game screen owns every BG tile; text menus reload the font.
 *
 * Cell layout (16x16 px):
 * - Digit: 3x5 bitmaps scaled 2x -> chunky 6x10 px glyph at columns
 *   5-10, rows 3-12. Givens are black, user digits dark gray (subtle).
 * - Borders, ALL 2px wide (uniform: thin/bold mismatch is impossible):
 *   top outer frame, right/bottom box gaps (after cols/rows 2, 5, 8)
 *   are black; inner cell lines are dark gray. Left/right frame comes
 *   from dedicated margin tiles (columns 0 and 19). Top/left inner
 *   lines come from the neighbour's right/bottom edge, so every line
 *   is drawn exactly once, always from the same side.
 *
 * VRAM (game screen):
 *    0-37   TL quadrant: 19 contents x 2 variants (top frame?)
 *   38-113  TR quadrant: 19 contents x 4 variants (right box? top?)
 *  114-151  BL quadrant: 19 contents x 2 variants (bottom box?)
 *  152-227  BR quadrant: 19 contents x 4 variants (right? bottom?)
 *  228-229  margin frame tiles (left, right)
 * Contents: 0 = empty, 1-9 = given digit, 10-18 = user digit (value+9).
 *
 * Cursor: 4 sprites (8x8 corner pieces) forming a 2px 16x16 outline.
 * -------------------------------------------------------------------------*/

/* Box gap (black 2px) after columns 2/5/8, else inner line (gray 2px).
 * In: col < GRID_SIZE. Out: 1 = box gap, 0 = inner line. */
static uint8_t right_is_box(uint8_t col)
{
    return (col == 2 || col == 5 || col == 8) ? 1 : 0;
}

/* Box gap (black 2px) after rows 2/5/8, else inner line (gray 2px).
 * In: row < GRID_SIZE. Out: 1 = box gap, 0 = inner line. */
static uint8_t bottom_is_box(uint8_t row)
{
    return (row == 2 || row == 5 || row == 8) ? 1 : 0;
}

/* Content index: 0 = empty, 1-9 = given digit, 10-18 = user digit.
 * In: value 0-9, is_user 0/1. Out: 0-18. */
static uint8_t content_of(uint8_t value, uint8_t is_user)
{
    if (value == 0) {
        return 0;
    }
    if (is_user) {
        return (uint8_t)(9 + value);
    }
    return value;
}

/* Load every tile pattern once (see tiles.h for the layout).
 * Boot-only: ~3.7KB byte copy is fine here (not per-frame).
 * Hand-rolled loop instead of memcpy/set_bkg_data: GBDK's block loader
 * silently dropped the low tile range on big loads (tiles 0-114 came
 * out zero), while a plain loop just works. No extra ROM cost worth
 * chasing: this runs once at boot.
 * 1. GBDK font_init/font_load decompress the font to 0x9000 tiles 0-95
 *    (font_init forces signed text mode, so the loader's LCDC-bit-4
 *    check picks the 0x9000 base; first font claims tiles from 0).
 *    Leave it there: menus read it in signed mode. Never copy it.
 * 2. Copy the prebuilt grid to 0x8000 + the cursor sprite tiles.
 * NOTE: hand-rolled copies, not set_bkg_data: GBDK's routine silently
 * drops the low tile range on big loads (tiles 0-114 came out zero
 * no matter the split or order), while a plain loop just works.
 * WARNING: font_load() re-enables the LCD on exit (GBDK font.s forces
 * LCDC on). The raw copies below have no WAIT_STAT, so a dropped byte
 * in PPU mode 3 would corrupt tiles on strict timing (real DMG, mGBA)
 * while passing on lenient emulators. Stop the LCD a second time
 * first: after this point it stays off until ui_init ends. Call with
 * the display off (ui_init does that). */
void tiles_load_resident(void)
{
    font_t ibm_font;
    uint8_t *dst;
    const uint8_t *src;
    uint16_t left;

    font_init();
    ibm_font = font_load(font_ibm);
    font_set(ibm_font);
    display_off();
    LCDC_REG = LCDC_OFF_MODE;
    dst = (uint8_t *)0x8000;
    src = GRID_TILES;
    left = (uint16_t)(TILE_COUNT * 16);
    while (left) {
        *dst = *src;
        dst++;
        src++;
        left--;
    }
    set_sprite_data(CURSOR_SPRITE_TILE, 4, CURSOR_TILE_DATA);
}

/* Fill `out[4]` with the TL/TR/BL/BR tile indices for a cell value
 * (0 = empty, 1-9 = digit; `is_user` picks the gray shade) at grid
 * (row, col), borders included. */
void grid_cell_tiles(uint8_t value, uint8_t is_user, uint8_t row,
                     uint8_t col, uint8_t *out)
{
    uint8_t content, top, right, bottom;

    content = content_of(value, is_user);
    top = (row == 0) ? 1 : 0;
    right = right_is_box(col);
    bottom = bottom_is_box(row);
    out[0] = (uint8_t)(TL_BASE + content * 2 + top);
    out[1] = (uint8_t)(TR_BASE + content * 4 + right + top * 2);
    out[2] = (uint8_t)(BL_BASE + content * 2 + bottom);
    out[3] = (uint8_t)(BR_BASE + content * 4 + right + bottom * 2);
}
