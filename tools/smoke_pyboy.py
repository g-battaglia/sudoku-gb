#!/usr/bin/env python3
"""Headless smoke test for sudoku-gb (needs: pip install pyboy pillow).

Runs the built ROM without a display and asserts the user-visible
behaviour that regressed before:
  - select/pause arrows never blank the screen and never touch LCDC.7;
  - page changes keep valid font tiles;
  - game entry draws valid grid + cursor tiles;
  - cursor movement is sprite-only (background map untouched);
  - edit-mode blink shows/erases without touching other cells;
  - HINT renders gray (user shade) and locks the cell (B cannot erase);
  - repeated hints reach a readable win screen, A advances a level.

Usage:
    python3 tools/smoke_pyboy.py [build/sudoku.gb]
Exits non-zero on the first failure.
"""

import sys

ROM = sys.argv[1] if len(sys.argv) > 1 else "build/sudoku.gb"

from pyboy import PyBoy  # noqa: E402

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


def full_map():
    return bytes(p.memory[a] for a in range(0x9800, 0x9C00))


def map_nonzero():
    return sum(1 for a in range(0x9800, 0x9C00) if p.memory[a] != 0)


def has_grid():
    return any(p.memory[a] >= 96 for a in range(0x9800, 0x9C00))


def has_text():
    return map_nonzero() > 30 and not has_grid()


def lcd_on():
    return bool(p.memory[0xFF40] & 0x80)


def cursor_cell():
    """Cursor grid cell from sprite 0 (OAM)."""
    oy, ox = p.memory[0xFE00], p.memory[0xFE01]
    return (oy - 16) // 16, (ox - 16) // 16


def cell_content(r, c):
    """Decode a grid cell: (value, is_user) from its TL tile."""
    tl = p.memory[0x9800 + (r * 2) * 32 + (1 + c * 2)]
    content = (tl - (1 if r == 0 else 0)) // 2
    if content == 0:
        return 0, 0
    return ((content - 1) % 9) + 1, 1 if content >= 10 else 0


def cell_tiles(r, c):
    base = 0x9800 + (r * 2) * 32 + (1 + c * 2)
    return bytes(p.memory[base + o] for o in (0, 1, 32, 33))


def press_retry(btn, hold_frames, idle_frames, want, tries=6):
    """Press until want() is true (input during transitions is missed)."""
    for _ in range(tries):
        hold(btn, hold_frames)
        idle(idle_frames)
        if want():
            return True
    return False


# 1. Boot -> select shows text, LCD on.
p.tick(300)
check("boot select text", has_text())
check("boot LCD on", lcd_on())

# 2. Select arrows: screen never blanks, LCDC.7 never toggles.
blank = False
lcd_toggled = False
hold("down", 4)
for _ in range(10):
    p.tick(2)
    if map_nonzero() < 30:
        blank = True
    if not lcd_on():
        lcd_toggled = True
hold("down", 4)
for _ in range(10):
    p.tick(2)
    if map_nonzero() < 30:
        blank = True
    if not lcd_on():
        lcd_toggled = True
hold("right", 4)
for _ in range(10):
    p.tick(2)
    if map_nonzero() < 30:
        blank = True
    if not lcd_on():
        lcd_toggled = True
check("select nav never blanks", not blank)
check("select nav keeps LCD on", not lcd_toggled)
check("page 2 text intact", has_text())
p.screen.image.save("/tmp/smoke_select.png")

# 3. A -> game: valid grid tiles (margins 228/229 + cell range).
ok = press_retry("a", 4, 40, has_grid)
check("game entry grid", ok)
m = full_map()
check("margin tiles present", 228 in m and 229 in m)
tiles = [b for b in m if b != 0]
check("grid tiles in range", all(t <= 229 for t in tiles))
check("game LCD on", lcd_on())
p.screen.image.save("/tmp/smoke_game.png")

# 4. Cursor movement is sprite-only (map identical, OAM moved).
oy0, ox0 = p.memory[0xFE00], p.memory[0xFE01]
before = full_map()
hold("right", 4)
idle(10)
hold("down", 4)
idle(10)
check("cursor move keeps map", full_map() == before)
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

# 6. START -> pause text; DOWN moves marker without blanking.
ok = press_retry("start", 4, 40, has_text)
check("pause text", ok)
p.screen.image.save("/tmp/smoke_pause.png")
blank = False
hold("down", 4)
for _ in range(10):
    p.tick(2)
    if map_nonzero() < 30:
        blank = True
check("pause nav never blanks", not blank)
check("pause still text", has_text())
hold("b", 4)
idle(40)
check("resume grid intact", has_grid())

# 7. HINT: gray digit + locked (B cannot erase it).
ok = press_retry("start", 4, 40, has_text)
check("pause reopen", ok)
hold("down", 4)
idle(8)
hold("a", 4)
idle(40)
check("hint back in game", has_grid())
cr, cc = cursor_cell()
val, is_user = cell_content(cr, cc)
check("hint filled cursor cell", val != 0)
check("hint renders gray", is_user == 1)
hinted = cell_tiles(cr, cc)
hold("b", 4)
idle(10)
check("hint locked vs erase", cell_tiles(cr, cc) == hinted)
p.screen.image.save("/tmp/smoke_hint.png")

# 8. Repeated hints -> readable win screen, A advances.
# NOTE: after the winning hint, poll for the text: the menu entry
# reloads the font (~15 frames of CPU with the LCD off), so a fixed
# short wait can sample the blank transition instead of the screen.
won = False
for _ in range(45):
    hold("start", 4)
    idle(30)
    if not has_text():
        continue
    hold("down", 4)
    idle(8)
    hold("a", 4)
    for _ in range(75):
        idle(2)
        if has_text():
            won = True
            break
    if won:
        break
check("win screen readable", won)
p.screen.image.save("/tmp/smoke_win.png")
if won:
    hold("a", 4)
    idle(40)
    check("advance to next level", has_grid())

p.stop()
print("SMOKE " + ("PASSED" if not FAILURES else f"FAILED: {FAILURES}"))
sys.exit(1 if FAILURES else 0)
