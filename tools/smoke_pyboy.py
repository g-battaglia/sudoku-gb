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

p = PyBoy(ROM, window="null")

FAILURES = []


def check(name, cond):
    print(("PASS " if cond else "FAIL ") + name)
    if not cond:
        FAILURES.append(name)


def hold(btn, frames):
    p.button(btn, frames)
    p.tick(frames)


def idle(frames):
    p.tick(frames)


def lcd():
    return p.memory[0xFF40]


def lcd_on():
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


def oam():
    return bytes(p.memory[a] for a in range(0xFE00, 0xFEA0))


def cursor_placed(o):
    """Sprites 0-3 carry tiles 240-243 and sprite 0 is on-screen."""
    return (o[2], o[6], o[10], o[14]) == (240, 241, 242, 243) and o[0] != 0


def cursor_parked(o):
    return o[0] == 0 and o[4] == 0 and o[8] == 0 and o[12] == 0


def swap(action, kind, want_oam, want_mode, timeout=150):
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


def cursor_cell():
    """Cursor grid cell from real OAM sprite 0."""
    oy, ox = p.memory[0xFE00], p.memory[0xFE01]
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


def has_ink(path, need=300):
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
lcd0 = lcd()
blank = False
hold("down", 4)
for _ in range(10):
    p.tick(2)
    if map_kind(vis_map()) != "text":
        blank = True
hold("up", 4)
for _ in range(10):
    p.tick(2)
    if map_kind(vis_map()) != "text":
        blank = True
check("diff nav never blanks", not blank)
check("diff nav keeps LCDC", lcd() == lcd0)
p.screen.image.save("/tmp/smoke_diff.png")
check("diff pixels drawn", has_ink("/tmp/smoke_diff.png"))

# 1c. A -> select of that difficulty (still a menu swap, parked OAM).
hold("down", 4)  # MEDIUM
idle(8)
swap(lambda: hold("a", 4), "text", "parked", "menu")
check("select title is MEDIUM", vis_map()[1 * 32:1 * 32 + 20] !=
      b"\x00" * 20)

# 2. Select arrows: same LCDC value throughout (no map flip, no reload).
lcd0 = lcd()
blank = False
hold("down", 4)
for _ in range(10):
    p.tick(2)
    if map_kind(vis_map()) != "text":
        blank = True
hold("down", 4)
for _ in range(10):
    p.tick(2)
    if map_kind(vis_map()) != "text":
        blank = True
hold("right", 4)
for _ in range(10):
    p.tick(2)
    if map_kind(vis_map()) != "text":
        blank = True
check("select nav never blanks", not blank)
check("select nav keeps LCDC", lcd() == lcd0)
check("page 2 text intact", map_kind(vis_map()) == "text")
p.screen.image.save("/tmp/smoke_select.png")
check("select pixels drawn", has_ink("/tmp/smoke_select.png"))

# 2b. B returns to the difficulty screen.
swap(lambda: hold("b", 4), "text", "parked", "menu")
swap(lambda: hold("a", 4), "text", "parked", "menu")  # re-enter MEDIUM

# 3. A -> game: atomic swap to grid + cursor + game mode.
ok = swap(lambda: hold("a", 4), "grid", "cursor", "game")
m = vis_map()
check("margin tiles present", 228 in m and 229 in m)
check("grid tiles in range", all(t <= 229 for t in m if t != 0))
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
p.screen.image.save("/tmp/smoke_pause.png")
check("pause pixels drawn", has_ink("/tmp/smoke_pause.png"))
lcd0 = lcd()
blank = False
hold("down", 4)
for _ in range(10):
    p.tick(2)
    if map_kind(vis_map()) != "text":
        blank = True
check("pause nav never blanks", not blank)
check("pause nav keeps LCDC", lcd() == lcd0)
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
p.screen.image.save("/tmp/smoke_hint.png")
check("hint pixels drawn", has_ink("/tmp/smoke_hint.png"))


# --- Battery save ---------------------------------------------------------


def sram_gate(on):
    """Enable/disable SRAM through the MBC1 latch (like the game does)."""
    p.memory[0x0000] = 0x0A if on else 0x00


def sram_read():
    sram_gate(True)
    data = bytes(p.memory[0xA000:0xA000 + 0xD2])
    sram_gate(False)
    return data


def sram_write(data):
    sram_gate(True)
    for i, b in enumerate(data):
        p.memory[0xA000 + i] = b
    sram_gate(False)


def text_row(y):
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
p.screen.image.save("/tmp/smoke_win.png")
check("win pixels drawn", has_ink("/tmp/smoke_win.png"))
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
p.screen.image.save("/tmp/smoke_load.png")

p.stop()
print("SMOKE " + ("PASSED" if not FAILURES else f"FAILED: {FAILURES}"))
sys.exit(1 if FAILURES else 0)
