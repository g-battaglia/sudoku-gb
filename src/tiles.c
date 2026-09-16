#include <gbdk/platform.h>

#include "tiles.h"

/* ---------------------------------------------------------------------------
 * tiles.c — Procedural grid tiles. No art files, just bit math.
 * -------------------------------------------------------------------------*/

/* DMG pixel values: 0 = white, 2 = dark gray, 3 = black. */
#define PX_WHITE 0
#define PX_USER 2
#define PX_BLACK 3

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

/* Cursor sprite: 8x8 outline box (middle left transparent). */
static const uint8_t CURSOR_TILE[16] = {
    0xFF, 0xFF, /* row 0: full line */
    0x81, 0x81, /* rows 1-6: side pixels only */
    0x81, 0x81,
    0x81, 0x81,
    0x81, 0x81,
    0x81, 0x81,
    0x81, 0x81,
    0xFF, 0xFF  /* row 7: full line */
};

/* Build one 8x8 cell tile (16 bytes, 2bpp) into `out`.
 * `digit` 0 = empty. `color` = digit shade. Thick edges are 2px. */
static void make_cell_tile(uint8_t *out, uint8_t digit, uint8_t color,
                           uint8_t right_thick, uint8_t bottom_thick)
{
    /* Pixel buffer, white by default. Digit lives at cols 2-4, rows 1-5. */
    uint8_t px[8][8];
    uint8_t x, y, r, p, v, lo, hi;

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            px[y][x] = PX_WHITE;
        }
    }
    if (digit >= 1 && digit <= 9) {
        for (r = 0; r < 5; r++) {
            for (p = 0; p < 3; p++) {
                if (DIGITS[digit][r] & (uint8_t)(1 << (2 - p))) {
                    px[1 + r][2 + p] = color;
                }
            }
        }
    }
    /* Right + bottom borders (top/left come from neighbours or frame). */
    for (y = 0; y < 8; y++) {
        px[y][7] = PX_BLACK;
        if (right_thick) {
            px[y][6] = PX_BLACK;
        }
    }
    for (x = 0; x < 8; x++) {
        px[7][x] = PX_BLACK;
        if (bottom_thick) {
            px[6][x] = PX_BLACK;
        }
    }
    /* Pack to Game Boy 2bpp (low byte then high byte per row). */
    for (y = 0; y < 8; y++) {
        lo = 0;
        hi = 0;
        for (x = 0; x < 8; x++) {
            v = px[y][x];
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
/* Build the whole tileset into `tiles` (in GRID_TILE_BASE order) and
 * load it into VRAM with set_bkg_data + set_sprite_data. */
static void build_tileset(uint8_t *tiles)
{
    uint8_t *p;
    uint8_t d, r_thick, b_thick, v;

    p = tiles;
    /* Given digits (black): 9 digits x 4 border variants. */
    for (d = 1; d <= 9; d++) {
        for (v = 0; v < 4; v++) {
            r_thick = v & 1;
            b_thick = (v >> 1) & 1;
            make_cell_tile(p, d, PX_BLACK, r_thick, b_thick);
            p += 16;
        }
    }
    /* Player digits (dark gray): 9 digits x 4 border variants. */
    for (d = 1; d <= 9; d++) {
        for (v = 0; v < 4; v++) {
            r_thick = v & 1;
            b_thick = (v >> 1) & 1;
            make_cell_tile(p, d, PX_USER, r_thick, b_thick);
            p += 16;
        }
    }
    /* Empty cells: 4 border variants. */
    for (v = 0; v < 4; v++) {
        r_thick = v & 1;
        b_thick = (v >> 1) & 1;
        make_cell_tile(p, 0, PX_WHITE, r_thick, b_thick);
        p += 16;
    }
    /* Frame: top thin, top thick, left thin, left thick, corner.
     * Each is blank except the border pixel line(s). */
    for (v = 0; v < 5; v++) {
        uint8_t x, y;
        uint8_t px[8][8];
        uint8_t lo, hi, vv;

        for (y = 0; y < 8; y++) {
            for (x = 0; x < 8; x++) {
                px[y][x] = PX_WHITE;
            }
        }
        if (v == 0 || v == 1) {
            /* Top frame: bottom line (+1 for thick). */
            for (x = 0; x < 8; x++) {
                px[7][x] = PX_BLACK;
                if (v == 1) {
                    px[6][x] = PX_BLACK;
                }
            }
        } else if (v == 2 || v == 3) {
            /* Left frame: right line (+1 for thick). */
            for (y = 0; y < 8; y++) {
                px[y][7] = PX_BLACK;
                if (v == 3) {
                    px[y][6] = PX_BLACK;
                }
            }
        } else {
            /* Corner: bottom + right lines, thick. */
            for (x = 0; x < 8; x++) {
                px[7][x] = PX_BLACK;
                px[6][x] = PX_BLACK;
            }
            for (y = 0; y < 8; y++) {
                px[y][7] = PX_BLACK;
                px[y][6] = PX_BLACK;
            }
        }
        for (y = 0; y < 8; y++) {
            lo = 0;
            hi = 0;
            for (x = 0; x < 8; x++) {
                vv = px[y][x];
                if (vv & 1) {
                    lo |= (uint8_t)(0x80 >> x);
                }
                if (vv & 2) {
                    hi |= (uint8_t)(0x80 >> x);
                }
            }
            p[y * 2] = lo;
            p[y * 2 + 1] = hi;
        }
        p += 16;
    }
}

/* Generate all grid/frame/sprite tiles and load them into VRAM. */
void tiles_load(void)
{
    /* 81 tiles x 16 bytes. Static: too big for the GB stack. */
    static uint8_t tiles[81 * 16];

    build_tileset(tiles);
    set_bkg_data(GRID_TILE_BASE, 81, tiles);
    set_sprite_data(CURSOR_SPRITE_TILE, 1, CURSOR_TILE);
}

/* Border variant index: thick right edge after cols 2/5/8, thick
 * bottom edge after rows 2/5/8 (box gaps + outer border). */
static uint8_t border_variant(uint8_t row, uint8_t col)
{
    uint8_t v;

    v = 0;
    if (col == 2 || col == 5 || col == 8) {
        v |= 1;
    }
    if (row == 2 || row == 5 || row == 8) {
        v |= 2;
    }
    return v;
}

/* Tile index for a cell value (0 = empty, 1-9 = digit). */
uint8_t grid_tile(uint8_t value, uint8_t is_user, uint8_t row, uint8_t col)
{
    uint8_t v;

    v = border_variant(row, col);
    if (value == 0) {
        return (uint8_t)(168 + v);
    }
    if (is_user) {
        return (uint8_t)(132 + (value - 1) * 4 + v);
    }
    return (uint8_t)(96 + (value - 1) * 4 + v);
}

/* Frame tile above grid column `col` (thick where a box gap is). */
uint8_t frame_tile_top(uint8_t col)
{
    /* Top frame mirrors the column's own right edge: thick on box gaps. */
    if (col == 2 || col == 5 || col == 8) {
        return 173;
    }
    return 172;
}

/* Frame tile left of grid row `row` (thick where a box gap is). */
uint8_t frame_tile_left(uint8_t row)
{
    if (row == 2 || row == 5 || row == 8) {
        return 175;
    }
    return 174;
}
