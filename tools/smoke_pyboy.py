#!/usr/bin/env python3
"""Headless smoke test for sudoku-gb (needs: pip install pyboy pillow).

Screens are double-buffered: every full screen is drawn into the hidden
BG map, then one LCDC write swaps map + tile mode + OBJ enable at frame
start (after the VBlank ISR copied shadow OAM). So every visible frame
must be a COMPLETE old screen or a COMPLETE new screen: never blank,
never mixed, LCDC.7 never clears, and real OAM must match the screen
(cursor sprites only in game, parked on menus).

Usage:
    python3 tools/smoke_pyboy.py [build/sudoku.gb]
Exits non-zero on the first failure.
"""

import sys

ROM = sys.argv[1] if len(sys.argv) > 1 else "build/sudoku.gb"

from pyboy import PyBoy  # noqa: E402
from PIL import Image  # noqa: E402

# --- Named constants (was magic numbers inline) ---
LCDC_ADDR = 0xFF40
LCDC_TILE_BIT = 0x10  # 0 = menu/font mode, 1 = game/grid mode (checked as 0x12)
LCDC_OBJ_BIT = 0x02
OAM_BASE = 0xFE00
OAM_SPRITE0_Y = 0xFE00
OAM_SPRITE0_X = 0xFE01
CURSOR_TILES = (240, 241, 242, 243)
GRID_TILE_MAX = 229
BOOT_TRACE_TICKS = 300
SWAP_TIMEOUT = 150
DIFF_PNG = "/tmp/smoke_diff.png"
SELECT_PNG = "/tmp/smoke_select.png"
GAME_PNG = "/tmp/smoke_game.png"
PAUSE_PNG = "/tmp/smoke_pause.png"
HINT_PNG = "/tmp/smoke_hint.png"
WIN_PNG = "/tmp/smoke_win.png"
LOAD_PNG = "/tmp/smoke_load.png"
INK_THRESHOLD = 300  # dark pixels proving tiles are drawn, not blank

p = PyBoy(ROM, window="null")

FAILURES: list[str] = []


def check(name: str, cond: bool) -> None:
    """Record one named assertion (prints PASS/FAIL, collects failures)."""
    print(("PASS " if cond else "FAIL ") + name)
    if not cond:
        FAILURES.append(name)


def hold(btn: str, frames: int) -> None:
    """Hold emulator button `btn` for `frames` ticks."""
    p.button(btn, frames)
    p.tick(frames)


def idle(frames: int) -> None:
    """Advance the emulator without input."""
    p.tick(frames)


def lcd() -> int:
    """Current LCDC register value."""
    return p.memory[LCDC_ADDR]


def lcd_on() -> bool:
    """True while LCDC.7 is set (must never clear after boot init)."""
    return bool(lcd() & 0x80)


def vis_base():
    """Base of the currently visible BG map (follows LCDC bit 3)."""
    return 0x9C00 if lcd() & 0x08 else 0x9800


def vis_map():
    b = vis_base()
    return bytes(p.memory[a] for a in range(b, b + 1024))


def map_kind(m):
    if any(b >= 96 for b in m):
        return "grid"
    if sum(1 for b in m if b != 0) > 30:
        return "text"
    return "blank"


def oam() -> bytes:
    """Full 160-byte hardware OAM."""
    return bytes(p.memory[a] for a in range(OAM_BASE, OAM_BASE + 0xA0))


def cursor_placed(o: bytes) -> bool:
    """Sprites 0-3 carry tiles 240-243 and sprite 0 is on-screen."""
    return (o[2], o[6], o[10], o[14]) == CURSOR_TILES and o[0] != 0


def cursor_parked(o: bytes) -> bool:
    """Sprites 0-3 parked at y=0 (menus, win, save screens)."""
    return o[0] == 0 and o[4] == 0 and o[8] == 0 and o[12] == 0


def nav_stable(presses: list[tuple[str, int]], samples: int = 10,
               gap: int = 2) -> tuple[bool, bool]:
    """Press buttons, then sample the visible map.

    Returns (never_blank, lcdc_unchanged): the screen must stay the same
    kind (no blank/mixed frame) and LCDC must not flip. Shared by the
    diff/select/pause navigation checks (were 3 copy-pasted loops)."""
    lcd0 = lcd()
    blank = False
    for btn, frames in presses:
        hold(btn, frames)
        for _ in range(samples):
            p.tick(gap)
            if map_kind(vis_map()) != "text":
                blank = True
    return (not blank, lcd() == lcd0)


def swap(action, kind, want_oam, want_mode, timeout=SWAP_TIMEOUT):
    """Run action (a full-screen transition) and assert atomicity.

    Every sampled frame must keep LCDC.7 set and show either the whole
    old screen or the whole new screen. On arrival, OAM and the LCDC
    tile/OBJ mode bits must match the new screen.
    want_oam: 'cursor' or 'parked'. want_mode: 'game' or 'menu'.
    """
    before = vis_map()
    action()
    after = None
    clean = True
    for _ in range(timeout):
        p.tick(1)
        if not lcd_on():
            clean = False
        m = vis_map()
        if m != before:
            if map_kind(m) == "blank":
                clean = False
            else:
                after = m
                break
    check("swap arrives as " + kind, after is not None and
          map_kind(after) == kind)
    check("swap never blanks/mixes", clean and after is not None)
    if after is None:
        return False
    for _ in range(5):
        p.tick(1)
        if vis_map() != after or not lcd_on():
            clean = False
    check("swap output stable", clean)
    o = oam()
    if want_oam == "cursor":
        check("swap OAM shows cursor", cursor_placed(o))
    else:
        check("swap OAM parked", cursor_parked(o))
    if want_mode == "game":
        check("swap LCDC game mode", lcd() & 0x12 == 0x12)
    else:
        check("swap LCDC menu mode", lcd() & 0x12 == 0x00)
    return True


def cursor_cell() -> tuple[int, int]:
    """Cursor grid cell from real OAM sprite 0."""
    oy, ox = p.memory[OAM_SPRITE0_Y], p.memory[OAM_SPRITE0_X]
    return (oy - 16) // 16, (ox - 16) // 16


def cell_content(r, c):
    """Decode a grid cell: (value, is_user) from its TL tile."""
    tl = p.memory[vis_base() + (r * 2) * 32 + (1 + c * 2)]
    content = (tl - (1 if r == 0 else 0)) // 2
    if content == 0:
        return 0, 0
    return ((content - 1) % 9) + 1, 1 if content >= 10 else 0


def cell_tiles(r, c):
    base = vis_base() + (r * 2) * 32 + (1 + c * 2)
    return bytes(p.memory[base + o] for o in (0, 1, 32, 33))


def press_retry(btn, hold_frames, idle_frames, want, tries=6):
    """Press until want() is true (input during transitions is missed)."""
    for _ in range(tries):
        hold(btn, hold_frames)
        idle(idle_frames)
        if want():
            return True
    return False


def has_ink(path: str, need: int = INK_THRESHOLD) -> bool:
    """Screenshot must contain dark pixels (catches blank tile data:
    map indices alone cannot tell a zeroed tileset apart)."""
    img = Image.open(path).convert("L")
    return sum(1 for v in img.getdata() if v < 128) > need


def vram_sum(base, n):
    return sum(p.memory[a] for a in range(base, base + n))


# 1. Boot trace: the LCD goes off once for init (one contiguous window)
# and never clears again. Catches mid-init re-enables (e.g. GBDK
# font_load forcing LCDC back on) and any later LCDC.7 clear.
p.tick(1)
lcd_trace = []
for _ in range(300):
    p.tick(1)
    lcd_trace.append(lcd())
offs = [i for i, v in enumerate(lcd_trace) if not v & 0x80]
check("boot LCD init window", len(offs) >= 5)
check("boot LCD never re-clears",
      bool(offs) and all(v & 0x80 for v in lcd_trace[offs[-1] + 1:]))
check("boot diff screen text", map_kind(vis_map()) == "text")
check("boot LCD on", lcd_on())
check("boot LCDC menu mode", lcd() & 0x12 == 0x00)
check("boot OAM parked", cursor_parked(oam()))
check("font resident at 0x9000", vram_sum(0x9000, 96 * 16) > 1000)
check("grid resident at 0x8000", vram_sum(0x8000, 230 * 16) > 1000)

# 1b. Difficulty screen: marker moves without blanking or LCDC flip.
no_blank, same_lcdc = nav_stable([("down", 4), ("up", 4)])
check("diff nav never blanks", no_blank)
check("diff nav keeps LCDC", same_lcdc)
p.screen.image.save(DIFF_PNG)
check("diff pixels drawn", has_ink(DIFF_PNG))

# 1c. A -> select of that difficulty (still a menu swap, parked OAM).
hold("down", 4)  # MEDIUM
idle(8)
swap(lambda: hold("a", 4), "text", "parked", "menu")
check("select title is MEDIUM", vis_map()[1 * 32:1 * 32 + 20] !=
      b"\x00" * 20)

# 2. Select arrows: same LCDC value throughout (no map flip, no reload).
no_blank, same_lcdc = nav_stable([("down", 4), ("down", 4), ("right", 4)])
check("select nav never blanks", no_blank)
check("select nav keeps LCDC", same_lcdc)
check("page 2 text intact", map_kind(vis_map()) == "text")
p.screen.image.save(SELECT_PNG)
check("select pixels drawn", has_ink(SELECT_PNG))

# 2b. B returns to the difficulty screen.
swap(lambda: hold("b", 4), "text", "parked", "menu")
swap(lambda: hold("a", 4), "text", "parked", "menu")  # re-enter MEDIUM

# 3. A -> game: atomic swap to grid + cursor + game mode.
ok = swap(lambda: hold("a", 4), "grid", "cursor", "game")
m = vis_map()
check("margin tiles present", 228 in m and 229 in m)
check("grid tiles in range", all(t <= GRID_TILE_MAX for t in m if t != 0))
p.screen.image.save("/tmp/smoke_game.png")
check("game pixels drawn", has_ink("/tmp/smoke_game.png"))

# 4. Cursor movement is sprite-only (map identical, OAM moved).
oy0, ox0 = p.memory[0xFE00], p.memory[0xFE01]
before = vis_map()
hold("right", 4)
idle(10)
hold("down", 4)
idle(10)
check("cursor move keeps map", vis_map() == before)
check("cursor sprite moved",
      (p.memory[0xFE00], p.memory[0xFE01]) != (oy0, ox0))

# 5. Edit blink: cell tiles toggle, then B restores the empty cell.
# Cursor starts on the first editable (empty) cell of level 1.
cr, cc = cursor_cell()
val, _ = cell_content(cr, cc)
check("cursor on empty cell", val == 0)
empty_tiles = cell_tiles(cr, cc)
hold("a", 4)
idle(10)
seen = {cell_tiles(cr, cc)}
for _ in range(6):
    idle(15)
    seen.add(cell_tiles(cr, cc))
check("preview blinks", len(seen) > 1)
check("blink differs from empty", empty_tiles in seen and
      any(t != empty_tiles for t in seen))
hold("b", 4)
idle(10)
check("cancel restores cell", cell_tiles(cr, cc) == empty_tiles)

# 6. START -> pause text; DOWN moves marker without blanking or flip.
swap(lambda: hold("start", 4), "text", "parked", "menu")
p.screen.image.save(PAUSE_PNG)
check("pause pixels drawn", has_ink(PAUSE_PNG))
no_blank, same_lcdc = nav_stable([("down", 4)])
check("pause nav never blanks", no_blank)
check("pause nav keeps LCDC", same_lcdc)
check("pause still text", map_kind(vis_map()) == "text")
swap(lambda: hold("b", 4), "grid", "cursor", "game")

# 7. HINT: gray digit + locked (B cannot erase it).
swap(lambda: hold("start", 4), "text", "parked", "menu")
hold("down", 4)
idle(8)
swap(lambda: hold("a", 4), "grid", "cursor", "game")
cr, cc = cursor_cell()
val, is_user = cell_content(cr, cc)
check("hint filled cursor cell", val != 0)
check("hint renders gray", is_user == 1)
hinted = cell_tiles(cr, cc)
hold("b", 4)
idle(10)
check("hint locked vs erase", cell_tiles(cr, cc) == hinted)
p.screen.image.save(HINT_PNG)
check("hint pixels drawn", has_ink(HINT_PNG))


# --- Battery save ---------------------------------------------------------


def sram_gate(on: bool) -> None:
    """Enable/disable SRAM through the MBC1 latch (like the game does)."""
    p.memory[0x0000] = 0x0A if on else 0x00


def sram_read() -> bytes:
    """Read the 0xD2-byte save image (SAVE_IMAGE_SIZE in save_format.h)."""
    sram_gate(True)
    data = bytes(p.memory[0xA000:0xA000 + 0xD2])
    sram_gate(False)
    return data


def sram_write(data: bytes) -> None:
    """Inject a save image (battery kept across the power-cycle below)."""
    sram_gate(True)
    for i, b in enumerate(data):
        p.memory[0xA000 + i] = b
    sram_gate(False)


def text_row(y: int) -> str:
    """Visible text of map row y (font tile c = ASCII c - 32)."""
    m = vis_map()
    return "".join(chr(m[y * 32 + x] + 32) if m[y * 32 + x] < 96 else "#"
                    for x in range(20))


# 7s. SAVE from the START menu (RESUME/HINT/SAVE/...: down, down, A).
swap(lambda: hold("start", 4), "text", "parked", "menu")
hold("down", 4)
idle(8)
hold("down", 4)
idle(8)
swap(lambda: hold("a", 4), "text", "parked", "menu")
check("save screen text", "GAME SAVED" in text_row(8))
swap(lambda: hold("a", 4), "grid", "cursor", "game")

# 7t. The slot in SRAM: magic, active game, level 101 (MEDIUM 001 =
# index 100), valid checksum.
sram = sram_read()
check("save magic", sram[0:4] == b"SUDK")
check("save game active", sram[5] == 1)
check("save level", sram[6] | (sram[7] << 8) == 100)
check("save checksum", (sum(sram[0:0xD1]) & 0xFF) == sram[0xD1])

# 7u. Power-cycle: fresh emulator + injected SRAM = battery kept.
p.stop()
p = PyBoy(ROM, window="null")
sram_write(sram)
idle(300)
check("boot shows LOAD row", text_row(10).strip() == "LOAD")

# 7v. LOAD resumes the exact board: the hinted cell is back.
hold("down", 4)
idle(6)
hold("down", 4)
idle(6)
hold("down", 4)
idle(6)
swap(lambda: hold("a", 4), "grid", "cursor", "game")
check("load restores hint cell", cell_tiles(cr, cc) == hinted)

# 8. Repeated hints -> readable win screen, A advances.
# Every leg is frame-checked: LCD on, old-or-new only, coherent OAM.
won = False
for _ in range(85):
    before = vis_map()
    hold("start", 4)
    got_pause = False
    for _ in range(60):
        p.tick(1)
        if not lcd_on() or map_kind(vis_map()) == "blank":
            break
        if vis_map() != before and map_kind(vis_map()) == "text":
            got_pause = True
            break
    if not got_pause:
        continue
    check("hint cycle OAM parked", cursor_parked(oam()))
    hold("down", 4)
    idle(8)
    before = vis_map()
    hold("a", 4)
    for _ in range(150):
        p.tick(1)
        if not lcd_on() or map_kind(vis_map()) == "blank":
            break
        m = vis_map()
        if m != before and map_kind(m) in ("grid", "text"):
            if map_kind(m) == "text":
                won = True
            break
    if won:
        break
check("win screen readable", won)
check("win OAM parked", cursor_parked(oam()))
check("win LCDC menu mode", lcd() & 0x12 == 0x00)
p.screen.image.save(WIN_PNG)
check("win pixels drawn", has_ink(WIN_PNG))
if won:
    swap(lambda: hold("a", 4), "grid", "cursor", "game")

# 8b. Power-cycle after the win: marks survive (star + DONE 001/100)
# and the slot has no active game, so LOAD lands on the select screen.
sram = sram_read()
check("win save marks bit", (sram[0xAB + (100 >> 3)] >> (100 & 7)) & 1 == 1)
check("win save inactive", sram[5] == 0)
p.stop()
p = PyBoy(ROM, window="null")
sram_write(sram)
idle(300)
hold("down", 4)
idle(6)
hold("down", 4)
idle(6)
hold("down", 4)
idle(6)
swap(lambda: hold("a", 4), "text", "parked", "menu")
check("load after win -> select", text_row(1).strip() == "MEDIUM")
check("won level row selected", text_row(3).strip() == "001<")
hold("down", 4)
idle(8)
check("completed star shown", text_row(3).strip() == "001*")
check("next row selected", text_row(4).strip() == "002<")
check("done count shown", text_row(16).strip() == "DONE 001/100")
p.screen.image.save(LOAD_PNG)

# 9. A+B+START+SELECT: soft reset to the boot menu; battery SRAM is
# untouched, so LOAD is still offered (and the reset re-boots cleanly:
# LCD on, menu mode, sprites parked).
COMBO = ("a", "b", "start", "select")
for b in COMBO:
    p.button_press(b)
p.tick(12)
for b in COMBO:
    p.button_release(b)
idle(280)
check("reset returns to boot menu", text_row(3).strip() == "DIFFICULTY")
check("reset keeps battery save", text_row(10).strip() == "LOAD")
check("reset LCD on + menu mode", lcd_on() and lcd() & 0x12 == 0x00)
check("reset OAM parked", cursor_parked(oam()))

p.stop()
print("SMOKE " + ("PASSED" if not FAILURES else f"FAILED: {FAILURES}"))
sys.exit(1 if FAILURES else 0)
