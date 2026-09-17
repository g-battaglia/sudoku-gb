# 12 — Reading the real project: traces, debugging, glossary

You know C now. This chapter teaches you to *navigate* a real codebase: where to start, how to follow one frame through four modules, and how to fix the five regressions that actually happen here.

## 1. Reading order (do not start at `main.c`)

1. `README.md` — what the game is, commands, controls.
2. `src/types.h` — all global constants (28 lines; start here for real).
3. `src/puzzles.h` + `src/puzzles.c` — packed format + nibble/mask helpers (skip `puzzles_gen.c`: generated).
4. `src/board.h`, then `src/board.c` — rules. Header comment first (origins, conflicts, no-game-over tally). Pure logic, zero hardware.
5. `tests/test_host.c` — executable documentation. Each `test_*` plays with the board and `assert`s a rule. If you change `board.c`, run it.
6. `src/save.h`, then `src/save.c` — the slot struct, latch discipline, offsets, checksum.
7. `src/input.h` + `src/input.c` — edge + repeat + combo (93 lines, one idea per function).
8. `src/main.c` — the conductor: one `switch`, one handler per state. Owns cursor, edit mode, preview bookkeeping, save flow; draws nothing directly.
9. `src/ui.h`, then `src/ui.c` — all drawing. Header comment first (double-buffer design), then one screen function at a time.
10. `src/tiles.h` + `src/tiles.c` + `tools/gen_tiles.py` — artwork pipeline and VRAM layout.
11. `tools/gen_puzzles.py`, `Makefile` — generation + build targets.

## 2. Trace A — boot (`main.c:575`)

```c
for (i = 4; i != 0; i--) vsync();   /* SGB PAL warmup, harmless elsewhere */
ui_init();            /* LCD off once, load all resident tiles, park sprites */
input_poll_init();    /* clear prev/just_pressed/repeat/combo */
sel_diff = sel_page = sel_row = menu_choice = 0;
preview_forget();     /* pv_row = 0xFF = "no preview tracked" */
has_save = save_read(&slot);        /* validate magic+version+checksum */
if (has_save) marks[:] = slot.marks[:];  /* restore completions */
ui_diff(sel_diff, has_save);        /* FIRST present: LCD on for good */
state = ST_DIFF;
while (1) { input_poll(); …; switch (state) {…} frame++; vsync(); }
```

Note the LCD flicker subtlety from `DEVELOPMENT.md`: GBDK's font loader turns the LCD back on mid-init, so `ui_init` stops it a second time before the raw grid copy (raw copies have no PPU wait — LCD must be off). Every later screen uses hidden-map + present. Boot is the only place with an excuse to touch the LCD bit.

## 3. Trace B — one input frame (`DEVELOPMENT.md` §3.2)

1. `input_poll()` snapshots `joypad()` into `just_pressed` (+ repeat timer + combo edge).
2. Exactly one handler runs: `diff_update` / `select_update` / `game_update` / `pause_update` / `win_update` / `saved_update`.
3. The handler reads `input_pressed` (single actions) or `input_dir` (movement), mutates state first (`board_set`, cursor, `entry_value`), then calls `ui_*` drawing second.
4. `frame++`, `vsync()`. The visible flip lands at the next safe moment.

Game logic first, drawing second, wait last. Any trace that violates this order is suspicious.

## 4. Trace C — editing a digit (`main.c:253`)

On a free cell, `A` calls `enter_editing()`: locked ⇒ `locked_feedback()` (cursor blinks off 20 frames); free ⇒ `entry_value = current ? current : 1`, `editing = 1`. From now on Up/Right and Down/Left change `entry_value` instead of moving the cursor (`entry_value % 9 + 1` wraps 9→1; `(entry_value + 7) % 9 + 1` wraps 1→9).

Every frame, `preview_update()` blinks the picked digit with `ui_preview(row, col, value, show)` — **the board is never touched by the blink**, only tiles. `A` (`confirm_editing`) writes tentatively, checks `board_conflicts`: illegal ⇒ restore + `ui_cell` redraw + `preview_forget` (the redraw killed any visible preview) + `board_add_mistake` + cursor-hide 24 frames, *keep picking*; legal ⇒ `editing = 0`, `ui_cell`, check `board_is_solved` ⇒ `win_now`. `B` (`cancel_editing`) erases the preview; the board is untouched. `B` outside editing (`erase_cell`) clears unlocked player digits; locked cells blink.

Follow the three preview primitives and the mode is obvious: `preview_forget` (caller already redrew), `preview_erase` (redraw tracked cell), `preview_update` (blink state machine with `pv_row/pv_col/pv_val/pv_shown`).

## 5. Trace D — START menu, HINT, SAVE, screen swap

`START` in game: `preview_erase`, `menu_choice = 0`, `ui_pause(...)`, `state = ST_PAUSE`. `pause_update`: Up/Down move through RESUME/HINT/SAVE/PLAY AGAIN/MENU (`ui_pause_cursor` = 2 tile writes), B/START resumes, A dispatches.

`do_hint()` (`main.c:471`): cursor cell if free + editable, else first such cell (linear scan, `0xFF` sentinel = full ⇒ `resume_game`); `value = puzzle_solution(level, idx)` (the ROM always knew the answer); `board_set` + `board_reveal` (lock as gray `ORIGIN_HINT`); solved ⇒ `win_now`, else `resume_game` (full atomic redraw shows the digit).

`save_store(active)` (`main.c:208`): snapshot level + `board_get`/`board_origin` × 81 + mistakes + marks into `slot`, `save_write(&slot)`, `has_save = 1`. SAVE ⇒ `active = 1` + `ST_SAVED` confirmation; every win (`win_now`) ⇒ `active = 0` + `marks_set` so completions persist without explicit save.

Screen swap (`ui_pause` as example): `begin_draw()` (route to hidden map) → clear + draw content → `cursor_sprites_off()` (shadow OAM prepared *before*) → `screen_present(LCDC_MENU)` (wait VBlank, one LCDC write flips map + tile mode + OBJ). Old screen one frame, new screen the next — nothing between.

## 6. Debugging checklist (in order, after any change)

```bash
make clean && make          # expect: exit 0, ZERO warnings
make check                  # expect: 32768 bytes, logo OK, cart 0x03, SRAM 8KB
make test-host              # expect: ALL HOST TESTS PASSED
make test-emulator          # expect: SMOKE PASSED (optional, needs pyboy+pillow)
make run                    # human eyeball pass in mGBA
```

The five regressions that actually happen (and where to look):

1. **Drawing to the wrong map** — new content appears on the wrong screen or never flips. Check `draw_hidden` routing: full screens draw hidden, deltas draw visible.
2. **Sprites touched after present** — cursor/marker one frame late or stale over menus. Prepare shadow OAM *before* `screen_present`; menus must park + disable OBJ.
3. **Generator indexing drift** — grid art scrambles after touching `gen_tiles.py`. The quadrant/variant numbering in `src/tiles.h` must match the generator exactly; regen twice must give identical md5.
4. **NUL overrun in menu text** — garbage chars from neighbouring ROM literals (the `'M' of MEDIUM` bug). Stop at `\0` when padding rows to 20 columns.
5. **Hardware include in portable modules** — `#include <gb/gb.h>` sneaks into `board`/`puzzles` and `make test-host` fails on PC. Portability is a feature; keep the boundary.

PyBoy note (`COMPACT.md` §6): headless presses landing inside synchronous transitions are missed — tests must retry/poll (especially after menu entries: font load ~15 frames). `memory[0xFF40]` reads mid-render prove nothing about tile areas — trust pixels.

## 7. Safe-change recipes

- **Help text / menu layout**: edit `draw_*_content()` in `ui.c`. Keep lines ≤ 20 columns; numbers via `draw_num*`.
- **Rules** (what counts as a mistake): edit `board.c` + `tests/test_host.c`, run `make test-host`. Never include Game Boy headers there.
- **Controls**: edit `*_update()` in `main.c`. Single actions → `input_pressed`; movement → `input_dir`.
- **Visuals**: edit `tools/gen_tiles.py`, `make regen-tiles`, rebuild. Respect `tiles.h` indexing.
- **Levels**: edit `tools/gen_puzzles.py`, `make regen-puzzles`. Every puzzle must keep a unique solution (script asserts; `make test-host` re-verifies).
- **New screen**: `draw_*_content()` + public `ui_*` following begin → draw → OAM → present; add the state/branch in `main.c`; extend `tools/smoke_pyboy.py`.

## 8. Glossary

- **BG / map** — background layer / tile-number layout in VRAM.
- **DMG** — original 1989 Game Boy (Dot Matrix Game).
- **GBDK** — C toolkit used here (vendored in `tools/gbdk/`).
- **LCDC** — LCD control register (one byte, one bit per feature).
- **MBC1 (`0x03`)** — cartridge mapper: bank switching + battery SRAM at `0xA000–0xBFFF`.
- **OAM** — sprite attribute memory (positions/tiles); shadow OAM = RAM staging copy.
- **SRAM** — battery-kept cartridge RAM (the save).
- **VBlank / vsync** — between-frames pause / waiting for it.
- **VRAM** — video memory (patterns + maps).

## 9. Final exercises (capstone)

1. Add a fourth origin code `ORIGIN_BONUS` (rendered gray, editable): which functions must change (`board_is_locked`? `board_is_original`? `save`? `ui_cell`? tests?). List every touch point before writing code — the answer measures whether you see the single-source-of-truth design.
2. Change `REPEAT_DELAY` from 18 to 6 in `input.c`. Predict the feel (cursor hyperactive), then explain which `input_dir` call sites are affected and which are not.
3. With `make -n`, trace what `make test-host` compiles and links. Explain why `save.c` is excluded even though saves are core to the game (what would including it require?).

You can now read, modify and verify this codebase. Start small, keep warnings at zero, and let `make test-host` + `make check` guard every change.
