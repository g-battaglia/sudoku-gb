# 12 — Reading the real project: traces, debugging, glossary

You know C and the hardware now. This chapter teaches you to *navigate* a real codebase: where to start, how to follow one frame through four modules, how to debug without `printf`, the seven regressions that actually happen here, and copy-paste-safe recipes for common changes.

## 1. Reading order (do not start at `main.c`)

`main.c` is the worst first file: it references every module before you know any. Read bottom-up instead:

1. `README.md` — what the game is, commands, controls, rules, LCD safety summary.
2. `src/types.h` — all global constants (28 lines; start here for real: 9×9, 20×18).
3. `src/puzzles.h` + `src/puzzles.c` — packed format (52 bytes: nibbles + mask) and the three helpers (skip `puzzles_gen.c`: 300 generated tables, read the generator instead).
4. `src/board.h`, then `src/board.c` — rules. Header comment first (three origins, conflict rejection, no-game-over tally, win = full). Pure logic, zero hardware — the file `make test-host` proves on PC.
5. `tests/test_host.c` — executable documentation. Each `test_*` plays with the board and `assert`s a rule (validity, load, conflicts, saturation, hint lock, bitmap, restore). If you change `board.c`, this file is the specification.
6. `src/save.h`, then `src/save.c` — the slot struct, latch discipline, fixed offsets, magic + version + checksum.
7. `src/input.h` + `src/input.c` — edge + repeat + combo (93 lines, one idea per function, chapter 11 §9).
8. `src/main.c` — the conductor: one `switch`, one handler per state. Owns cursor, edit mode, preview bookkeeping (`pv_*`), save flow; draws nothing directly — every visual change is a `ui_*` call.
9. `src/ui.h`, then `src/ui.c` — all drawing. Header comment first (double-buffer design, chapter 11 §4), then one screen function at a time plus the delta helpers.
10. `src/tiles.h` + `src/tiles.c` + `tools/gen_tiles.py` — artwork pipeline and VRAM layout (chapter 11 §2).
11. `tools/gen_puzzles.py`, `tools/smoke_pyboy.py`, `Makefile` — generation (seed 20260916, uniqueness cap 2), frame-level verification, build targets (chapter 09 §3).

Each step uses only earlier steps (except `main.c`, which uses all — hence last among sources). When lost, ask "which layer am I in: data (`puzzles`), rules (`board`), persistence (`save`), events (`input`), pixels (`ui`/`tiles`), flow (`main`)?" — the filename answers, by design.

## 2. Trace A — boot (`main.c:575`)

```c
for (i = 4; i != 0; i--) vsync();   /* SGB PAL warmup, harmless elsewhere */
ui_init();            /* LCD off once, load all resident tiles, park sprites */
input_poll_init();    /* clear prev/just_pressed/repeat/combo: no phantom edges */
sel_diff = sel_page = sel_row = menu_choice = 0;   /* navigation state */
preview_forget();     /* pv_row = 0xFF = "no preview tracked" (sentinel, ch.04) */
has_save = save_read(&slot);        /* validate magic+version+checksum (ch.10) */
if (has_save) marks[:] = slot.marks[:];  /* restore completions (38-byte copy) */
ui_diff(sel_diff, has_save);        /* FIRST present: LCD on for good */
state = ST_DIFF;
while (1) { input_poll(); …; switch (state) {…} frame++; vsync(); }
```

Three details worth lingering on:

- The LCD flicker subtlety from `DEVELOPMENT.md` §3.1: GBDK's font loader turns the LCD back on mid-init, so `ui_init` stops it a second time before the raw grid copy (raw copies have no PPU wait — LCD must be off, chapter 11 §8). Every later screen uses hidden-map + present. Boot is the only place with an excuse to touch the LCD bit (chapter 11 §10 audit: `0x13` once).
- `has_save` fans out to two places: the boot menu gains LOAD (`ui_diff`'s extra row, `sel_diff == DIFF_COUNT` addressing it) and `marks` is pre-restored so select screens show `*` immediately. One read, two consumers — note the shape for your own init code.
- `preview_forget` before the loop (not `preview_erase`): nothing was ever drawn, so there is nothing to undraw — forget (drop bookkeeping) vs erase (redraw + forget) is the recurring pair of chapter 04's trace C.

## 3. Trace B — one input frame (`DEVELOPMENT.md` §3.2, expanded)

1. `input_poll()` snapshots `joypad()` into `just_pressed` (+ repeat timer + combo edge). Single call, first statement — the contract from chapter 11 §9.
2. Combo pre-empts everything: `if (input_reset_combo()) soft_reset();` runs before the state switch, so the chord works from *any* screen (diff, game, pause, win). Global keys precede local keys — check chord-then-state whenever adding a global shortcut.
3. Exactly one handler runs: `diff_update` / `select_update` / `game_update` / `pause_update` / `win_update` / `saved_update`. No handler calls another; transitions happen by setting `state` (+ drawing the next screen *before* switching, so no frame shows the old state's pixels with the new state's logic).
4. The handler reads `input_pressed` (single actions) or `input_dir` (movement), mutates state first (`board_set`, cursor, `entry_value`, `menu_choice`), then calls `ui_*` drawing second.
5. `frame++`, `vsync()`. The visible flip lands at the next safe moment (chapter 11 §8: stage now, commit at blank).

Game logic first, drawing second, wait last. Any trace that violates this order is suspicious — e.g. drawing inside `input_poll`'s caller before the handler would show stale-state pixels for a frame.

## 4. Trace C — editing a digit (the modal core, `main.c:253`)

On a free cell, `A` calls `enter_editing()`: locked ⇒ `locked_feedback()` (cursor hidden 20 frames — sprite blink, zero tile writes); free ⇒ `entry_value = current ? current : 1` (editing starts from the digit already there, or 1 if empty — resuming an edit never loses the visible value), `editing = 1`, cursor re-shown. From now on Up/Right and Down/Left change `entry_value` instead of moving the cursor (`entry_value % 9 + 1` wraps 9→1; `(entry_value + 7) % 9 + 1` wraps 1→9 — the 1-based wrap from chapter 04 §4).

Every frame, `preview_update()` blinks the picked digit with `ui_preview(row, col, value, show)` — **the board is never touched by the blink**, only tiles. The tracker (`pv_row/pv_col/pv_val/pv_shown`, `0xFF` = untracked) reconciles three disturbance sources: mode exit (`want` false → erase), cursor/value change (tracker mismatch → erase + re-track), blink phase (`frame >> 5 & 1` — bit 5 of the free-running counter, chapter 07 TEST idiom — differing from `pv_shown` → one tile write). Maximum one tile operation per frame; usually zero.

`A` (`confirm_editing`, full body in chapter 04 §5): tentative `board_set`, `board_conflicts` check — illegal ⇒ restore + `ui_cell` redraw + `preview_forget` (the redraw already killed any visible preview, so *forget*, not *erase* — the distinction from §2) + `board_add_mistake` + cursor-hide 24 frames, *keep picking* (no game over, mistakes tallied); legal ⇒ `editing = 0`, `preview_forget`, `ui_cell`, `board_is_solved` ⇒ `win_now` (which defers the win screen to state entry — see §5 why). `B` (`cancel_editing`) erases the preview; the board is untouched. `B` outside editing (`erase_cell`) clears unlocked player digits in place; locked non-empty cells blink instead of silently ignoring (every input gets feedback — a UI rule worth stealing).

## 5. Trace D — START menu, HINT, SAVE, win, screen swap

`START` in game: `editing = 0`, `preview_erase` (a preview may be visible — erase, not forget), `flash = 0`, `menu_choice = 0`, `ui_pause(...)` (status: level-within-mode, difficulty, mistakes + 5 items + 3-line help), `state = ST_PAUSE`. `pause_update`: Up/Down wrap through RESUME/HINT/SAVE/PLAY AGAIN/MENU (`ui_pause_cursor` = 2 tile writes on the visible map), B/START resumes, A dispatches. `saved_update` (confirmation screen) returns on any of A/B/START — generous exit keys on dead-end screens.

`do_hint()` (`main.c:471`): cursor cell if free + editable, else first such cell (linear scan, `0xFF` sentinel = full ⇒ `resume_game`, since a full grid already triggered the win path); `value = puzzle_solution(level, idx)` (the ROM always knew the answer — HINT is a ROM read plus a lock); `board_set` + `board_reveal` (gray like player, locked like clue — the third origin from chapter 07 §2); solved ⇒ `win_now`, else `resume_game` (full atomic redraw shows the digit; no delta needed).

`save_store(active)` (`main.c:208`): snapshot level + `board_get`/`board_origin` × 81 + mistakes + marks into `slot`, `save_write(&slot)` (latch + checksum, chapter 10 §5), `has_save = 1`. SAVE ⇒ `active = 1` + `ST_SAVED` confirmation; every win (`win_now`: `marks_set` + `save_store(0)` + hide cursor + record `win_level`/`win_last`) ⇒ `active = 0` so completions persist even without explicit save. `apply_load` re-reads SRAM (sees saves from *this* session too), restores marks, then branches: `game_active` ⇒ `board_restore` + first-editable cursor + game screen; else ⇒ select screen of the saved difficulty positioned at the saved level (`sel_page`/`sel_row` derived — the marks-only resume).

The deferred-draw trick (`need_win_draw`/`need_saved_draw`, `main.c:87`): win/save screens are drawn on *state entry* (flat in `main`'s switch), never nested inside the game/pause update — drawing a full screen mid-handler garbled menu text during development, so `win_now` only *records* (`win_level`, `win_last`, flag) and the switch *presents*. Symptoms of violating this: text from two screens interleaved. Any future full-screen transition from inside a handler must use the same deferral.

Screen swap (`ui_pause` as the canonical example): `begin_draw()` (route to hidden map) → clear + draw content → `cursor_sprites_off()` (shadow OAM staged *before* present, chapter 11 §7) → `screen_present(LCDC_MENU)` (wait VBlank, one LCDC write flips map + tile mode + OBJ). Old screen one frame, new screen the next — nothing between, verified by `smoke_pyboy.py` frame diffs.

## 6. Trace E — level select (100 levels, 10 pages, bitmap-driven)

`diff_update`: Up/Down wraps over `DIFF_COUNT + (has_save ? 1 : 0)` rows (LOAD appended conditionally — the count itself is data-dependent); A/START on LOAD ⇒ `apply_load`, else ⇒ reset `sel_page`/`sel_row` and `ui_select`. `select_update`: Up/Down = row (`ui_select_cursor`: marker delta), Left/Right = page (`ui_select_page`: page line + 10 rows), B = back to difficulty, A/START = `start_level(diff*100 + page*10 + row)` (arithmetic *is* the mapping — no lookup table). `marks_get(marks, level)` renders `*` per row; `marks_count` renders `DONE x/100` per mode. `start_level`: `board_load` + reset editing/flash/preview + first-editable cursor + `ui_game_full` + `state = ST_GAME`. The select screen is a pure view over (`marks`, `sel_diff`, `sel_page`, `sel_row`) — four small values, zero duplication.

## 7. Trace F — winning and advancing

`win_now` → `ST_WIN` → (next frame entry) `ui_win(win_level, win_last)` → `win_update`: A/START advances to `level + 1` via `start_level` (fresh board, same flow) or, on the mode's last level (`win_last`), back to select. Every win rewrote the slot already (`save_store(0)` inside `win_now`), so powering off on the win screen loses nothing — persistence precedes presentation. The level number shown is *within the mode* (`level_in_diff()`), matching the select screen's numbering; global 0–299 indices never reach the player. Two numberings (storage vs display) with one conversion function — the pattern for any indexed content.

## 8. Debugging without `printf` (the GB reality)

There is no console on hardware and no stdio in game code (chapter 03 §7: tiles, not `printf`). The substitute stack, in order of cheapness:

1. **Assert on PC** (`make test-host`): logic bugs (rules, packing, bitmaps, restore) reproduce without hardware — `board`/`puzzles` were isolated precisely for this. New rule ⇒ new `test_*` first.
2. **Tile-level reasoning**: menus are deterministic functions of state — re-derive the expected tiles by hand from (`marks`, `page`, `row`) rather than eyeballing pixels.
3. **PyBoy frame checks** (`make test-emulator`): transitions asserted old-or-new-only, LCDC.7 always set, OAM matching the new screen. A failing check names the transition and the property — read both before touching code.
4. **mGBA eyeballing** (`make run`): last, for feel (repeat rates, blink phases, layout) — never for logic.
5. **Bisect by revert**: generators deterministic, builds reproducible — `git stash` + rebuild + retest isolates whether *your* change caused it within minutes.

The five regressions that actually happen (and where to look first):

1. **Drawing to the wrong map** — content on the wrong screen or never flipping. Check `draw_hidden` routing: full screens hidden, deltas visible (chapter 11 §4).
2. **Sprites staged after present** — cursor a frame late, ghost over menus. Stage shadow OAM *before* `screen_present`; menus park + OBJ-off (chapter 11 §7).
3. **Generator indexing drift** — scrambled grid art after touching `gen_tiles.py`. Quadrant/variant numbering in `src/tiles.h` must match the generator exactly; regen twice must md5-identical.
4. **NUL overrun in menu text** — garbage chars from neighbouring ROM literals (the `'M' of MEDIUM` bug, chapter 06 §2). Stop at `\0` when padding to 20 columns.
5. **Hardware include in portable modules** — `#include <gb/gb.h>` sneaks into `board`/`puzzles`, `make test-host` fails on PC. The boundary is the feature (chapter 09 §3 exclusion list).
6. **Deferred-draw violation** — interleaved text from two screens. Full screens only on state entry via `need_*_draw` flags (§5).
7. **Stale preview tracker** — blink remnant after a redraw. Every code path that redraws a tracked cell must `preview_forget`; every path showing something new must reconcile in `preview_update` (§4).

PyBoy notes (`COMPACT.md` §6): presses landing inside synchronous transitions are missed — tests retry/poll (especially after menu entries: font load ~15 frames). `memory[0xFF40]` reads mid-render prove nothing about tile areas — trust pixels, not register peeks.

## 9. Safe-change recipes (copy the shape, keep the invariants)

- **Help text / menu layout**: edit `draw_*_content()` in `ui.c`. Keep lines ≤ 20 columns; numbers via `draw_num*`; stop strings at `\0`; full screens via begin → draw → OAM → present.
- **Rules** (what counts as a mistake, win condition): edit `board.c` + extend `tests/test_host.c`, run `make test-host`. Never include Game Boy headers there — PC testability is the invariant.
- **Controls**: edit the `*_update()` handler in `main.c`. Single actions → `input_pressed`; movement → `input_dir`; global chords before the state switch.
- **Visuals**: edit `tools/gen_tiles.py`, run `make regen-tiles`, rebuild. Respect the quadrant/variant indexing in `src/tiles.h`; verify double-regen md5 equality.
- **Levels**: edit `tools/gen_puzzles.py`, run `make regen-puzzles`. Every puzzle keeps a unique solution (script asserts with cap-2 solver; `make test-host` re-verifies validity + intro givens).
- **New screen**: `draw_*_content()` + public `ui_*` (begin → draw → OAM → present) + state/branch in `main.c` + deferred draw if triggered mid-handler + extend `tools/smoke_pyboy.py` with the transition checks.
- **Save format change**: bump `SAVE_VERSION`, update offsets + checksum range + `SaveSlot`, extend host tests with a stale-version case (old saves must be *ignored*, never misread).

## 10. Glossary (every term earned in chapters 01–11)

- **BG / map** — background layer / tile-number layout in VRAM (chapter 11 §3).
- **DMG** — original 1989 Game Boy (Dot Matrix Game).
- **GBDK** — C toolkit used here, vendored in `tools/gbdk/` (chapters 09, 11).
- **LCDC** — LCD control register, one bit per feature (chapter 11 §5 table).
- **MBC1 (`0x03`)** — cartridge mapper: banking + battery SRAM at `0xA000–0xBFFF` (chapters 09–10).
- **Nibble** — 4 bits, one hex digit, half a solution byte (chapter 07 §3).
- **OAM** — sprite attribute memory; shadow OAM = RAM staging copy (chapter 11 §7).
- **ROM / WRAM / SRAM / VRAM** — cartridge code+data / work RAM / battery save / video memory (chapter 10).
- **SDCC** — the strict 8-bit C compiler inside GBDK's `lcc` (chapters 02, 09).
- **Sentinel** — in-band "none" value: `0xFF`, `NULL`, `"?????"` (chapter 04 §3).
- **Translation unit** — one `.c` plus its pasted headers, compiled alone (chapter 09 §1).
- **VBlank / vsync / HBlank / STAT** — between-frames pause / waiting for it / between-lines gap / status register (chapter 11 §8).
- **YAGNI** — you aren't gonna need it: one-shot build, no `malloc`, no audio (whole manual).

You can now read, modify and verify this codebase. Start small, keep warnings at zero, and let `make test-host` + `make check` guard every change. When in doubt, re-read the header comment of the module you are touching — the contract lives there.
