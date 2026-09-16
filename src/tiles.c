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

/* Box gap (black 2px) after columns 2/5/8, else inner line (gray 2px). */
static uint8_t right_is_box(uint8_t col)
{
    return (col == 2 || col == 5 || col == 8) ? 1 : 0;
}

/* Box gap (black 2px) after rows 2/5/8, else inner line (gray 2px). */
static uint8_t bottom_is_box(uint8_t row)
{
    return (row == 2 || row == 5 || row == 8) ? 1 : 0;
}

/* Content index: 0 = empty, 1-9 = given digit, 10-18 = user digit. */
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

/* Load the prebuilt grid + cursor tiles into VRAM (fast copy).
 * NOTE: hand-rolled copy, not set_bkg_data: GBDK's routine silently
 * drops the low tile range on big loads (tiles 0-114 came out zero
 * no matter the split or order), while a plain loop just works.
 * Call with the display off (ui_game_full does that). */
void tiles_load_grid(void)
{
    uint8_t *dst;
    const uint8_t *src;
    uint16_t left;

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

/* Reload the GBDK font as the BG tileset (for text menus). */
void tiles_load_font(void)
{
    font_t ibm_font;

    font_init();
    ibm_font = font_load(font_ibm);
    font_set(ibm_font);
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
