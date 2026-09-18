#!/usr/bin/env python3
"""Tile generator for sudoku-gb: precomputes the 230 grid tiles
(including the 2 frame margins) + 4 cursor sprite tiles.

Layout (must match src/tiles.c indexing exactly):
  TL 0-37, TR 38-113, BL 114-151, BR 152-227, margins 228-229.
Contents: 0 = empty, 1-9 = given (black), 10-18 = user (dark gray).
All lines 2px: black box/frame, dark gray inner lines.

Usage:
    python3 tools/gen_tiles.py
Writes src/tiles_gen.c (GENERATED, do not edit by hand).
"""

from pathlib import Path

# DMG shades: 0 = white, 2 = dark gray, 3 = black.
W, G, B = 0, 2, 3

# 3x5 digit bitmaps, top row first. Bit 2 = left pixel.
# Source of truth for the glyphs: the ROM carries only the baked 2bpp
# output (tiles.c never rebuilds digits at runtime).
DIGITS: dict[int, list[int]] = {
    1: [0x2, 0x6, 0x2, 0x2, 0x7],
    2: [0x7, 0x1, 0x7, 0x4, 0x7],
    3: [0x7, 0x1, 0x7, 0x1, 0x7],
    4: [0x5, 0x5, 0x7, 0x1, 0x1],
    5: [0x7, 0x4, 0x7, 0x1, 0x7],
    6: [0x7, 0x4, 0x7, 0x5, 0x7],
    7: [0x7, 0x1, 0x2, 0x2, 0x2],
    8: [0x7, 0x5, 0x7, 0x5, 0x7],
    9: [0x7, 0x5, 0x7, 0x1, 0x7],
}

VARS = [2, 4, 2, 4]  # variants per quadrant: TL, TR, BL, BR
QUAD_OFF = [(0, 0), (8, 0), (0, 8), (8, 8)]

# Repo-rooted output (works from any cwd).
OUT_PATH = Path(__file__).resolve().parent.parent / "src" / "tiles_gen.c"


def paint(value: int, is_user: bool, row: int, col: int) -> list[list[int]]:
    """16x16 cell pixels. Baked into src/tiles_gen.c.

    Line priority (painter's order): the outer frame and the black box
    lines always win at crossings, so they stay solid: the right edge
    keeps out of the top frame rows, and the bottom edge keeps out of
    the right (box/frame) columns. Both skips key on the variant flags
    (top row / box column), so every real cell maps to its tile."""
    px = [[W] * 16 for _ in range(16)]
    if 1 <= value <= 9:
        sh = G if is_user else B
        for r in range(5):
            for p in range(3):
                if DIGITS[value][r] & (1 << (2 - p)):
                    for dy in range(2):
                        for dx in range(2):
                            px[3 + r * 2 + dy][5 + p * 2 + dx] = sh
    if row == 0:  # top outer frame, black 2px, full width
        for x in range(16):
            px[0][x] = px[1][x] = B
    ln = B if col in (2, 5, 8) else G  # right edge
    r0 = 2 if row == 0 else 0  # stop below the top frame
    for y in range(r0, 16):
        px[y][14] = px[y][15] = ln
    ln = B if row in (2, 5, 8) else G  # bottom edge
    # Stop before a black right line (box gap or outer frame): it owns
    # the corner, so box/frame verticals stay unbroken. Elsewhere the
    # bottom line runs full width (drawn last, it wins inner corners).
    c1 = 14 if col in (2, 5, 8) else 16
    for x in range(0, c1):
        px[14][x] = px[15][x] = ln
    return px


def pack_quad(cell: list[list[int]], ox: int, oy: int) -> list[int]:
    """8x8 block -> 16 bytes 2bpp. Mirrors the VRAM tile format."""
    out = []
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            v = cell[oy + y][ox + x]
            if v & 1:
                lo |= 0x80 >> x
            if v & 2:
                hi |= 0x80 >> x
        out += [lo, hi]
    return out


def pack_margin(left: bool) -> list[int]:
    """2px vertical frame line -> 16 bytes 2bpp."""
    out = []
    for _ in range(8):
        lo = hi = 0
        for x in range(8):
            if (left and x >= 6) or (not left and x <= 1):
                lo |= 0x80 >> x
                hi |= 0x80 >> x
        out += [lo, hi]
    return out


def build() -> list[list[int]]:
    """Build all 230 grid tiles in tiles.h order (then 2 margins)."""
    tiles: list[list[int]] = []
    for q in range(4):
        ox, oy = QUAD_OFF[q]
        for content in range(19):
            value = 0 if content == 0 else ((content - 1) % 9) + 1
            is_user = content >= 10
            for v in range(VARS[q]):
                if q == 0:
                    row = 0 if v else 1
                    col = 1
                elif q == 1:
                    row = 0 if (v & 2) else 1
                    col = 2 if (v & 1) else 1
                elif q == 2:
                    row = 2 if v else 1
                    col = 1
                else:
                    row = 2 if (v & 2) else 1
                    col = 2 if (v & 1) else 1
                cell = paint(value, is_user, row, col)
                tiles.append(pack_quad(cell, ox, oy))
    assert len(tiles) == 228, len(tiles)
    tiles.append(pack_margin(True))
    tiles.append(pack_margin(False))
    return tiles


# 2bpp rows: 0xFF = 8 black pixels; 0xC0/0x03 = 2 black pixels
# (left/right). Plain 2px ring: same weight as the grid lines.
CURSOR = [
    # TL: full top rows + left columns.
    [0xFF, 0xFF, 0xFF, 0xFF] + [0xC0, 0xC0] * 6,
    # TR: full top rows + right columns.
    [0xFF, 0xFF, 0xFF, 0xFF] + [0x03, 0x03] * 6,
    # BL: left columns + full bottom rows.
    [0xC0, 0xC0] * 6 + [0xFF, 0xFF, 0xFF, 0xFF],
    # BR: right columns + full bottom rows.
    [0x03, 0x03] * 6 + [0xFF, 0xFF, 0xFF, 0xFF],
]


def main() -> None:
    tiles = build()
    assert len(tiles) == 230
    assert all(len(t) == 16 for t in tiles)
    with open(OUT_PATH, "w") as f:
        f.write("/* GENERATED by tools/gen_tiles.py -- DO NOT edit by hand. */\n")
        f.write("/* 230 grid tiles (19 contents x quadrant variants + 2 frame\n")
        f.write(" * margins) + 4 cursor sprite tiles, 2bpp. See tiles.c. */\n")
        f.write('#include "tiles.h"\n\n')
        f.write("const uint8_t GRID_TILES[230 * 16] = {\n")
        for t in tiles:
            f.write("    " + ", ".join(f"0x{b:02X}" for b in t) + ",\n")
        f.write("};\n\n")
        f.write("const uint8_t CURSOR_TILE_DATA[4 * 16] = {\n")
        for t in CURSOR:
            f.write("    " + ", ".join(f"0x{b:02X}" for b in t) + ",\n")
        f.write("};\n")
    print(f"Wrote {len(tiles)} grid tiles + 4 cursor tiles to {OUT_PATH}")


if __name__ == "__main__":
    main()
