#include <gbdk/platform.h>

#include "tiles.h"

/* ---------------------------------------------------------------------------
 * tiles.c — Procedural grid tiles. No art files, just bit math.
 *
 * Fullscreen board: each of the 9x9 cells is 16x16 px (2x2 BG tiles),
 * so the grid is 144x144 px = the whole screen height, centered with a
 * 1-tile margin left and right. All digits are black (newspaper style).
 *
 * Cell layout (16x16 px):
 * - Digit: the 3x5 bitmaps below scaled 2x -> chunky 6x10 px glyph
 *   at columns 5-10, rows 3-12. Big enough to read on a real DMG.
 * - Borders (black, baked into the cell tiles so box lines always
 *   land on the same pixels — no thin/bold wobble):
 *   top/left outer frame ... 2px (row 0 / column 0 cells only)
 *   right/bottom box gaps .. 2px (after columns/rows 2, 5, 8)
 *   right/bottom cell lines  1px (everywhere else)
 *   Top/left inner lines come from the neighbour's right/bottom edge,
 *   so every line is drawn exactly once, always from the same side.
 *
 * VRAM (BG tiles 0-95 are the GBDK font, used by the menus):
 *   96-135  TL quadrant: 10 contents x 4 variants (left? top?)
 *   136-175 TR quadrant: 10 contents x 4 variants (right thick? top?)
 *   176-215 BL quadrant: 10 contents x 4 variants (left? bottom thick?)
 *   216-255 BR quadrant: 10 contents x 4 variants (right? bottom thick?)
 * That is 160 tiles = every free BG tile. The game screen shows no
 * text, so nothing else is needed there.
 *
 * Cursor: 4 sprites (8x8 corner pieces) forming a 2px 16x16 outline.
 * -------------------------------------------------------------------------*/

/* DMG pixel values: 0 = white, 3 = black. */
#define PX_WHITE 0
#define PX_BLACK 3

/* Quadrant tile bases in VRAM. */
#define TL_BASE 96
#define TR_BASE 136
#define BL_BASE 176
#define BR_BASE 216
#define TILE_COUNT 160

/* 3x5 digit bitmaps, top row first. Bit 2 = left pixel. */
static const uint8_t DIGITS[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7}, /* 0 (unused, kept for completeness) */
    {0x2, 0x6, 0x2, 0x2, 0x7}, /* 1 */
    {0x7, 0x1, 0x7, 0x4, 0x7}, /* 2 */
    {0x7, 0x1, 0x7, 0x1, 0x7}, /* 3 */
    {0x5, 0x5, 0x7, 0x1, 0x1}, /* 4 */
    {0x7, 0x4, 0x7, 0x1, 0x7}, /* 5 */
    {0x7, 0x4, 0x7, 0x5, 0x7}, /* 6 */
    {0x7, 0x1, 0x2, 0x2, 0x2}, /* 7 */
    {0x7, 0x5, 0x7, 0x5, 0x7}, /* 8 */
    {0x7, 0x5, 0x7, 0x1, 0x7}  /* 9 */
};

/* Cursor sprite corners: 2px outline pieces (black = 0xFF/0xC0/0x03). */
static const uint8_t CURSOR_TILES[4][16] = {
    /* TL: full top rows + left columns. */
    {0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0xC0, 0xC0, 0xC0,
     0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0},
    /* TR: full top rows + right columns. */
    {0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0x03, 0x03, 0x03,
     0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03},
    /* BL: left columns + full bottom rows. */
    {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,
     0xC0, 0xC0, 0xC0, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF},
    /* BR: right columns + full bottom rows. */
    {0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
     0x03, 0x03, 0x03, 0x03, 0xFF, 0xFF, 0xFF, 0xFF}
};

/* 16x16 scratch pixels for one cell. Static: too big for the GB stack. */
static uint8_t cell_px[16][16];

/* 160 tiles x 16 bytes. Static: too big for the GB stack. */
static uint8_t tileset[TILE_COUNT * 16];

/* Thick (2px) right edge after columns 2/5/8, else thin (1px). */
static uint8_t right_thick(uint8_t col)
{
    return (col == 2 || col == 5 || col == 8) ? 1 : 0;
}

/* Thick (2px) bottom edge after rows 2/5/8, else thin (1px). */
static uint8_t bottom_thick(uint8_t row)
{
    return (row == 2 || row == 5 || row == 8) ? 1 : 0;
}

/* Paint one 16x16 cell into cell_px: digit (0 = empty) + borders. */
static void paint_cell(uint8_t value, uint8_t row, uint8_t col)
{
    uint8_t x, y, r, p, dx, dy;

    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            cell_px[y][x] = PX_WHITE;
        }
    }
    /* Digit, 2x scaled: 6x10 px at columns 5-10, rows 3-12. */
    if (value >= 1 && value <= 9) {
        for (r = 0; r < 5; r++) {
            for (p = 0; p < 3; p++) {
                if (DIGITS[value][r] & (uint8_t)(1 << (2 - p))) {
                    for (dy = 0; dy < 2; dy++) {
                        for (dx = 0; dx < 2; dx++) {
                            cell_px[3 + r * 2 + dy][5 + p * 2 + dx] = PX_BLACK;
                        }
                    }
                }
            }
        }
    }
    /* Outer frame: 2px top (first row) and left (first column). */
    if (row == 0) {
        for (x = 0; x < 16; x++) {
            cell_px[0][x] = PX_BLACK;
            cell_px[1][x] = PX_BLACK;
        }
    }
    if (col == 0) {
        for (y = 0; y < 16; y++) {
            cell_px[y][0] = PX_BLACK;
            cell_px[y][1] = PX_BLACK;
        }
    }
    /* Right edge: 1px, or 2px on box gaps. */
    for (y = 0; y < 16; y++) {
        cell_px[y][15] = PX_BLACK;
        if (right_thick(col)) {
            cell_px[y][14] = PX_BLACK;
        }
    }
    /* Bottom edge: 1px, or 2px on box gaps. */
    for (x = 0; x < 16; x++) {
        cell_px[15][x] = PX_BLACK;
        if (bottom_thick(row)) {
            cell_px[14][x] = PX_BLACK;
        }
    }
}

/* Pack an 8x8 block of cell_px at (ox, oy) into 2bpp tile bytes at `out`. */
static void pack_quadrant(uint8_t *out, uint8_t ox, uint8_t oy)
{
    uint8_t x, y, v, lo, hi;

    for (y = 0; y < 8; y++) {
        lo = 0;
        hi = 0;
        for (x = 0; x < 8; x++) {
            v = cell_px[oy + y][ox + x];
            if (v & 1) {
                lo |= (uint8_t)(0x80 >> x);
            }
            if (v & 2) {
                hi |= (uint8_t)(0x80 >> x);
            }
        }
        out[y * 2] = lo;
        out[y * 2 + 1] = hi;
    }
}

/* Build the whole tileset into `tileset` in VRAM order:
 * 40 TL, then 40 TR, 40 BL, 40 BR (10 contents x 4 variants each).
 * Each tile is painted from a representative (row, col) that carries
 * the same border flags as the real cells using that variant. */
static void build_tileset(void)
{
    uint8_t *p;
    uint8_t value, v, q, row, col, ox, oy;

    static const uint8_t QUAD_OX[4] = {0, 8, 0, 8};
    static const uint8_t QUAD_OY[4] = {0, 0, 8, 8};

    p = tileset;
    for (q = 0; q < 4; q++) {
        ox = QUAD_OX[q];
        oy = QUAD_OY[q];
        for (value = 0; value <= 9; value++) {
            for (v = 0; v < 4; v++) {
                /* Variant bit 0: left/right flag; bit 1: top/bottom flag. */
                if (q == 0) {          /* TL: left + top */
                    row = (v & 2) ? 0 : 1;
                    col = (v & 1) ? 0 : 1;
                } else if (q == 1) {   /* TR: right thick + top */
                    row = (v & 2) ? 0 : 1;
                    col = (v & 1) ? 2 : 1;
                } else if (q == 2) {   /* BL: left + bottom thick */
                    row = (v & 2) ? 2 : 1;
                    col = (v & 1) ? 0 : 1;
                } else {               /* BR: right thick + bottom thick */
                    row = (v & 2) ? 2 : 1;
                    col = (v & 1) ? 2 : 1;
                }
                paint_cell(value, row, col);
                pack_quadrant(p, ox, oy);
                p += 16;
            }
        }
    }
}

/* Generate all grid + cursor tiles and load them into VRAM. */
void tiles_load(void)
{
    build_tileset();
    set_bkg_data(TL_BASE, TILE_COUNT, tileset);
    set_sprite_data(CURSOR_SPRITE_TILE, 4, (uint8_t *)CURSOR_TILES);
}

/* Fill `out[4]` with the TL/TR/BL/BR tile indices for a cell value
 * (0 = empty, 1-9 = digit) at grid (row, col), borders included. */
void grid_cell_tiles(uint8_t value, uint8_t row, uint8_t col, uint8_t *out)
{
    uint8_t left, top, right, bottom;

    left = (col == 0) ? 1 : 0;
    top = (row == 0) ? 1 : 0;
    right = right_thick(col);
    bottom = bottom_thick(row);
    out[0] = (uint8_t)(TL_BASE + value * 4 + left + top * 2);
    out[1] = (uint8_t)(TR_BASE + value * 4 + right + top * 2);
    out[2] = (uint8_t)(BL_BASE + value * 4 + left + bottom * 2);
    out[3] = (uint8_t)(BR_BASE + value * 4 + right + bottom * 2);
}
