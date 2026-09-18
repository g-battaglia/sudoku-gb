#include <gbdk/platform.h>

#include "types.h"
#include "board.h"
#include "input.h"
#include "puzzles.h"
#include "save.h"
#include "ui.h"

/* ---------------------------------------------------------------------------
 * main.c — Game flow. One state machine, one small handler per state.
 *
 * States: DIFF -> SELECT -> GAME <-> PAUSE -> WIN, plus SAVED (save
 * confirmation). Each frame: poll input once, run the current handler,
 * wait for VBlank. No game over: mistakes are only tallied (START
 * menu) and play goes on.
 *
 * Boot asks for a difficulty (EASY / MEDIUM / HARD, 100 levels each).
 * All levels are freely playable, 10 per page. Beaten levels show `*`
 * and are kept in the battery save, together with one game in
 * progress (SAVE in the START menu, LOAD on the boot menu).
 *
 * Game controls:
 *   D-Pad .... move cursor (wraps at grid edges)
 *   A ........ edit the cursor cell (digit-pick mode)
 *     Up/Right, Down/Left: pick the digit (blinks in the cell)
 *     A ...... confirm, B = back without changing
 *   B ........ erase a player digit
 *   START .... pause menu (RESUME / HINT / SAVE / PLAY AGAIN / MENU)
 *   A+B+START+SELECT ... soft reset (boot menu again, save kept)
 * -------------------------------------------------------------------------*/

/* Game states. */
typedef enum {
    ST_DIFF,
    ST_SELECT,
    ST_GAME,
    ST_PAUSE,
    ST_WIN,
    ST_SAVED
} State;

/* START menu items (same order as ui PAUSE_ITEMS, count in ui.h). */
#define MENU_RESUME 0
#define MENU_HINT 1
#define MENU_SAVE 2
#define MENU_RESTART 3
#define MENU_MENU 4

/* --- Tunables (frames at 60fps) --- */
#define BOOT_VSYNCS 4    /* startup waits: required on SGB PAL */
#define FLASH_LOCKED 20  /* locked-cell feedback: cursor hidden frames */
#define FLASH_REJECT 24  /* rejected-digit feedback: cursor hidden frames */
#define BLINK_SHIFT 5    /* preview toggles every (1 << 5) = 32 frames */
#define PV_NONE 0xFF     /* preview.row sentinel: nothing tracked */

/* --- Session state (grouped, one struct per concern) --- */

/* Current state. */
static State state;

/* Current level (0-based, 0-299) and difficulty (0-2). */
static uint16_t level;
static uint8_t sel_diff;

/* Grid cursor + digit-pick mode. */
typedef struct {
    uint8_t row;
    uint8_t col;
    uint8_t entry;   /* picked digit 1-9 while editing */
    uint8_t editing; /* 1 = digit-pick mode, 0 = navigation */
} Cursor;
static Cursor cursor;

/* Completed levels (battery-saved bitmap: one bit per level, `*`). */
static uint8_t marks[MARKS_BYTES];

/* Battery save slot: read at boot (to know if LOAD must be shown and
 * to restore the marks) and written on SAVE and on every win. */
static SaveSlot slot;
static uint8_t has_save; /* 1 = SRAM holds a valid save, else 0 */

/* Pause menu selection (< UI_PAUSE_COUNT). */
static uint8_t menu_choice;

/* Select screen position: page 0-9, row 0-9 (level = page*10+row). */
static uint8_t sel_page;
static uint8_t sel_row;

/* Pending screens: drawn on state entry (flat from main), never nested
 * inside a game update (nested draws garbled menu text once). */
typedef struct {
    uint8_t need_win;
    uint8_t win_level; /* level within difficulty, 0-99 */
    uint8_t win_last;  /* 1 = difficulty completed */
    uint8_t need_saved;
} Pending;
static Pending pend;

/* Frame counter (blink timing, 16-bit: wraps cleanly every ~18min) +
 * mistake feedback (frames left with the cursor hidden). */
static uint16_t frame;
static uint8_t flash;

/* Blinking preview tracker: the picked digit shown on tiles only
 * (board untouched). row = PV_NONE means "nothing tracked". */
typedef struct {
    uint8_t row;
    uint8_t col;
    uint8_t val;
    uint8_t shown; /* currently visible phase 0/1 */
} Preview;
static Preview pv;

/* Forget the preview without redrawing (caller already drew the cell). */
static void preview_forget(void)
{
    pv.row = PV_NONE;
    pv.shown = 0;
}

/* Erase a visible preview, redrawing the cell from the board. */
static void preview_erase(void)
{
    if (pv.shown && pv.row != PV_NONE) {
        ui_cell(pv.row, pv.col);
    }
    preview_forget();
}

/* Blink the picked digit in the cursor cell while editing. Call every
 * frame in ST_GAME: the board is never touched, only tiles are drawn. */
static void preview_update(void)
{
    uint8_t idx, want, phase;

    idx = (uint8_t)(cursor.row * GRID_SIZE + cursor.col);
    want = (cursor.editing && !board_is_locked(idx));
    if (!want || pv.row != cursor.row || pv.col != cursor.col ||
        pv.val != cursor.entry) {
        preview_erase();
        if (want) {
            pv.row = cursor.row;
            pv.col = cursor.col;
            pv.val = cursor.entry;
        }
    }
    if (want) {
        phase = (uint8_t)((frame >> BLINK_SHIFT) & 1);
        if (phase != pv.shown) {
            ui_preview(cursor.row, cursor.col, cursor.entry, phase);
            pv.shown = phase;
        }
    }
}

/* Locked-cell feedback: hide the cursor briefly (main loop restores it
 * when flash reaches zero). */
static void locked_feedback(void)
{
    ui_cursor_hide();
    flash = FLASH_LOCKED;
}

/* Wrap v + d into 0..n-1 (menu / select navigation, n <= 10).
 * Division-free inputs are tiny here; clarity beats cycles. */
static uint8_t wrap_add(uint8_t v, int8_t d, uint8_t n)
{
    return (uint8_t)((v + n + d) % n);
}

/* Level number within the difficulty (0-99). In: level in sel_diff. */
static uint8_t level_in_diff(void)
{
    return (uint8_t)(level - (uint16_t)(sel_diff * DIFF_LEVELS));
}

/* Put the cursor on the first editable cell (top-left scan). */
static void cursor_first_editable(void)
{
    uint8_t r, c;

    cursor.row = 0;
    cursor.col = 0;
    for (r = 0; r < GRID_SIZE; r++) {
        for (c = 0; c < GRID_SIZE; c++) {
            if (!board_is_locked((uint8_t)(r * GRID_SIZE + c))) {
                cursor.row = r;
                cursor.col = c;
                return;
            }
        }
    }
}

/* Shared game entry: reset mode + preview + atomic grid present.
 * Both start_level (new board) and resume_game (same board) use it. */
static void enter_game_state(void)
{
    cursor.editing = 0;
    flash = 0;
    preview_forget();
    ui_game_full(cursor.row, cursor.col);
    state = ST_GAME;
}

/* Start (or restart) a level: reset board, cursor, mode. */
static void start_level(uint16_t new_level)
{
    level = new_level;
    board_load(level);
    cursor.entry = 1;
    cursor_first_editable();
    enter_game_state();
}

/* Back to the game screen: redraw grid + cursor in one atomic swap. */
static void resume_game(void)
{
    enter_game_state();
}

/* Snapshot the current game + marks into battery SRAM.
 * `active` = 1 from SAVE (resumable game), 0 after a win (marks only,
 * so completions survive even without an explicit save).
 * Manual loops (not memcpy): keeps the GBDK memcpy helper out of ROM. */
static void save_store(uint8_t active)
{
    uint8_t i;

    slot.game_active = active;
    slot.level = level;
    for (i = 0; i < CELL_COUNT; i++) {
        slot.values[i] = board_get(i);
        slot.origins[i] = board_origin(i);
    }
    slot.mistakes = board_errors();
    for (i = 0; i < MARKS_BYTES; i++) {
        slot.marks[i] = marks[i];
    }
    save_write(&slot);
    has_save = 1;
}

/* Record the win for the current level. The screen itself is drawn on
 * WIN state entry (flat from main, never nested in the game update). */
static void win_now(void)
{
    uint8_t lid;

    marks_set(marks, level);
    save_store(0);
    ui_cursor_hide();
    preview_forget();
    lid = level_in_diff();
    pend.win_level = lid;
    pend.win_last = (uint8_t)((lid < DIFF_LEVELS - 1) ? 0 : 1);
    pend.need_win = 1;
    state = ST_WIN;
}

/* Move the cursor by (dr, dc) in -1..1, wrapping at the edges.
 * Branch-free of division: GRID_SIZE is 9, two compares beat % on SM83.
 * Side effect: cancels any mistake blink (flash = 0). */
static void move_cursor(int8_t dr, int8_t dc)
{
    int8_t r, c;

    r = (int8_t)(cursor.row + dr);
    if (r < 0) {
        r += GRID_SIZE;
    } else if (r >= GRID_SIZE) {
        r -= GRID_SIZE;
    }
    c = (int8_t)(cursor.col + dc);
    if (c < 0) {
        c += GRID_SIZE;
    } else if (c >= GRID_SIZE) {
        c -= GRID_SIZE;
    }
    cursor.row = (uint8_t)r;
    cursor.col = (uint8_t)c;
    flash = 0;
    ui_cursor(cursor.row, cursor.col);
}

/* A on a cell: enter digit-pick mode (or complain if locked). */
static void enter_editing(void)
{
    uint8_t idx, current;

    idx = (uint8_t)(cursor.row * GRID_SIZE + cursor.col);
    if (board_is_locked(idx)) {
        locked_feedback();
        return;
    }
    /* Start from the digit already there (or 1 if empty). */
    current = board_get(idx);
    cursor.entry = current ? current : 1;
    cursor.editing = 1;
    flash = 0;
    ui_cursor(cursor.row, cursor.col);
}

/* Try placing the picked digit. Return 1 = accepted, 0 = rejected.
 * Rejected: board restored, mistake tallied, stays in pick mode. */
static uint8_t try_place_digit(void)
{
    uint8_t idx, old;

    idx = (uint8_t)(cursor.row * GRID_SIZE + cursor.col);
    old = board_get(idx);
    board_set(idx, cursor.entry);
    if (board_conflicts(idx)) {
        board_set(idx, old);
        ui_cell(cursor.row, cursor.col);
        preview_forget(); /* The redraw killed any visible preview. */
        board_add_mistake();
        ui_cursor_hide();
        flash = FLASH_REJECT;
        return 0;
    }
    return 1;
}

/* A in digit-pick mode: try the picked digit. */
static void confirm_editing(void)
{
    if (!try_place_digit()) {
        return;
    }
    /* Legal move: show it, back to navigation, check for the win. */
    cursor.editing = 0;
    preview_forget();
    ui_cell(cursor.row, cursor.col);
    if (board_is_solved()) {
        win_now();
    }
}

/* B in digit-pick mode: back to navigation, board untouched. */
static void cancel_editing(void)
{
    cursor.editing = 0;
    preview_erase();
}

/* B on a cell: erase a player digit (locked cells complain if filled,
 * empty locked cells stay silent: nothing to erase). */
static void erase_cell(void)
{
    uint8_t idx;

    idx = (uint8_t)(cursor.row * GRID_SIZE + cursor.col);
    if (board_is_locked(idx)) {
        if (board_get(idx) != 0) {
            locked_feedback();
        }
        return;
    }
    if (board_get(idx) != 0) {
        board_set(idx, 0);
        ui_cell(cursor.row, cursor.col);
    }
}

/* LOAD from the boot menu: restore the marks and (if the save holds a
 * game in progress) the exact board; otherwise land on the select
 * screen of the saved difficulty. Re-reads SRAM so it sees the last
 * save, even one made earlier in this session. save_read() already
 * validated ranges, so level/sel_diff are safe here. */
static void apply_load(void)
{
    uint8_t i, lid;

    if (!save_read(&slot)) {
        return; /* Should not happen: LOAD is only shown when valid. */
    }
    for (i = 0; i < MARKS_BYTES; i++) {
        marks[i] = slot.marks[i];
    }
    has_save = 1;
    level = slot.level;
    sel_diff = (uint8_t)(level / DIFF_LEVELS);
    if (slot.game_active) {
        board_restore(slot.values, slot.origins, slot.mistakes);
        cursor.entry = 1;
        cursor_first_editable();
        enter_game_state();
    } else {
        lid = level_in_diff();
        sel_page = (uint8_t)(lid / LEVELS_PER_PAGE);
        sel_row = (uint8_t)(lid % LEVELS_PER_PAGE);
        ui_select(sel_page, sel_row, marks, sel_diff);
        state = ST_SELECT;
    }
}

/* Difficulty handler: Up/Down = mode (LOAD last, if a save exists),
 * A = choose. */
static void diff_update(void)
{
    uint8_t next, n;

    n = (uint8_t)(DIFF_COUNT + (has_save ? 1 : 0));
    if (input_pressed(J_UP)) {
        next = wrap_add(sel_diff, -1, n);
        ui_diff_cursor(sel_diff, next);
        sel_diff = next;
    }
    if (input_pressed(J_DOWN)) {
        next = wrap_add(sel_diff, 1, n);
        ui_diff_cursor(sel_diff, next);
        sel_diff = next;
    }
    if (input_pressed(J_A) || input_pressed(J_START)) {
        if (sel_diff == DIFF_COUNT) {
            apply_load();
            return;
        }
        sel_page = 0;
        sel_row = 0;
        ui_select(sel_page, sel_row, marks, sel_diff);
        state = ST_SELECT;
    }
}

/* Level-select handler: Up/Down = row, Left/Right = page, A = play,
 * B = back to difficulty. */
static void select_update(void)
{
    uint8_t next;
    uint16_t base;

    if (input_pressed(J_UP)) {
        next = wrap_add(sel_row, -1, LEVELS_PER_PAGE);
        ui_select_cursor(sel_page, sel_row, next, marks, sel_diff);
        sel_row = next;
    }
    if (input_pressed(J_DOWN)) {
        next = wrap_add(sel_row, 1, LEVELS_PER_PAGE);
        ui_select_cursor(sel_page, sel_row, next, marks, sel_diff);
        sel_row = next;
    }
    if (input_pressed(J_LEFT)) {
        sel_page = wrap_add(sel_page, -1, SELECT_PAGE_COUNT);
        ui_select_page(sel_page, sel_row, marks, sel_diff);
    }
    if (input_pressed(J_RIGHT)) {
        sel_page = wrap_add(sel_page, 1, SELECT_PAGE_COUNT);
        ui_select_page(sel_page, sel_row, marks, sel_diff);
    }
    if (input_pressed(J_B)) {
        ui_diff(sel_diff, has_save);
        state = ST_DIFF;
        return;
    }
    if (input_pressed(J_A) || input_pressed(J_START)) {
        base = (uint16_t)((uint16_t)sel_diff * DIFF_LEVELS +
                          sel_page * LEVELS_PER_PAGE + sel_row);
        start_level(base);
    }
}

/* Digit-pick mode: Up/Right = next digit, Down/Left = previous
 * (wraps 9->1), A = confirm, B = back. */
static void game_update_editing(void)
{
    if (input_dir(J_UP) || input_dir(J_RIGHT)) {
        cursor.entry = (uint8_t)(cursor.entry % 9 + 1);
    } else if (input_dir(J_DOWN) || input_dir(J_LEFT)) {
        cursor.entry = (uint8_t)((cursor.entry + 7) % 9 + 1);
    }
    if (input_pressed(J_A)) {
        confirm_editing();
    } else if (input_pressed(J_B)) {
        cancel_editing();
    }
}

/* Navigation mode: D-Pad moves (wraps), A edits, B erases. */
static void game_update_nav(void)
{
    if (input_dir(J_LEFT)) {
        move_cursor(0, -1);
    } else if (input_dir(J_RIGHT)) {
        move_cursor(0, 1);
    } else if (input_dir(J_UP)) {
        move_cursor(-1, 0);
    } else if (input_dir(J_DOWN)) {
        move_cursor(1, 0);
    } else if (input_pressed(J_A)) {
        enter_editing();
    } else if (input_pressed(J_B)) {
        erase_cell();
    }
}

/* Game screen handler: navigation vs digit-pick mode, plus mistake
 * blink countdown and preview blink. */
static void game_update(void)
{
    if (input_pressed(J_START)) {
        cursor.editing = 0;
        preview_erase();
        flash = 0;
        menu_choice = 0;
        ui_pause(menu_choice, level_in_diff(), sel_diff, board_errors());
        state = ST_PAUSE;
        return;
    }
    if (cursor.editing) {
        game_update_editing();
    } else {
        game_update_nav();
    }
    if (flash > 0) {
        flash--;
        if (flash == 0) {
            ui_cursor(cursor.row, cursor.col);
        }
    }
    preview_update();
}

/* HINT: reveal the true digit of the cursor cell (or the first empty
 * editable cell) and lock it, then back to the game.
 * Full grid is atomic here: resume_game() redraws all 81 cells in one
 * swap. One cell would be cheaper (ui_cell), but a full redraw keeps
 * this path obviously correct; HINT is rare (menu-driven). */
static void do_hint(void)
{
    uint8_t idx, i, value;

    idx = (uint8_t)(cursor.row * GRID_SIZE + cursor.col);
    if (board_is_locked(idx) || board_get(idx) != 0) {
        idx = PV_NONE;
        for (i = 0; i < CELL_COUNT; i++) {
            if (!board_is_locked(i) && board_get(i) == 0) {
                idx = i;
                break;
            }
        }
        if (idx == PV_NONE) {
            resume_game(); /* Full grid: win already triggered. */
            return;
        }
        cursor.row = (uint8_t)(idx / GRID_SIZE);
        cursor.col = (uint8_t)(idx % GRID_SIZE);
    }
    value = puzzle_solution(level, idx);
    board_set(idx, value);
    board_reveal(idx);
    if (board_is_solved()) {
        win_now();
        return;
    }
    resume_game();
}

/* Pause (START menu) handler: RESUME / HINT / SAVE / PLAY AGAIN / MENU. */
static void pause_update(void)
{
    uint8_t next;

    if (input_pressed(J_UP)) {
        next = wrap_add(menu_choice, -1, UI_PAUSE_COUNT);
        ui_pause_cursor(menu_choice, next);
        menu_choice = next;
    }
    if (input_pressed(J_DOWN)) {
        next = wrap_add(menu_choice, 1, UI_PAUSE_COUNT);
        ui_pause_cursor(menu_choice, next);
        menu_choice = next;
    }
    if (input_pressed(J_B) || input_pressed(J_START)) {
        resume_game();
        return;
    }
    if (!input_pressed(J_A)) {
        return;
    }
    switch (menu_choice) {
    case MENU_RESUME:
        resume_game();
        break;
    case MENU_HINT:
        do_hint();
        break;
    case MENU_SAVE:
        save_store(1);
        pend.need_saved = 1;
        state = ST_SAVED;
        break;
    case MENU_RESTART:
        start_level(level);
        break;
    default: /* MENU_MENU */
        ui_diff(sel_diff, has_save);
        state = ST_DIFF;
        break;
    }
}

/* Save confirmation handler: any button goes back to the game. */
static void saved_update(void)
{
    if (input_pressed(J_A) || input_pressed(J_B) ||
        input_pressed(J_START)) {
        resume_game();
    }
}

/* Win screen handler: A advances to the next level of the same
 * difficulty, or back to select after the last one. */
static void win_update(void)
{
    if (input_pressed(J_A) || input_pressed(J_START)) {
        if (!pend.win_last) {
            start_level((uint16_t)(level + 1));
        } else {
            ui_select(sel_page, sel_row, marks, sel_diff);
            state = ST_SELECT;
        }
    }
}

/* Soft reset (A+B+START+SELECT): jump back to the boot entry at
 * 0x0100. The startup code clears RAM and calls main() again, like a
 * power cycle — while battery SRAM (marks, saved game) is untouched,
 * so LOAD still works after the reset. */
static void soft_reset(void)
{
    __asm__("jp 0x0100");
    /* Not reached: keep the CPU parked if the jump ever were. */
    while (1) {
        vsync();
    }
}

/* Entry point. */
void main(void)
{
    uint8_t i;

    for (i = BOOT_VSYNCS; i != 0; i--) {
        vsync();
    }
    ui_init();
    input_poll_init();
    sel_diff = 0;
    sel_page = 0;
    sel_row = 0;
    menu_choice = 0;
    cursor.row = 0;
    cursor.col = 0;
    cursor.entry = 1;
    cursor.editing = 0;
    pend.need_win = 0;
    pend.need_saved = 0;
    pend.win_level = 0;
    pend.win_last = 0;
    frame = 0;
    flash = 0;
    preview_forget();
    marks_clear(marks); /* Explicit: no reliance on BSS zero-init. */

    /* Battery save: restore the completion marks and remember whether
     * the boot menu must offer LOAD. */
    has_save = save_read(&slot);
    if (has_save) {
        for (i = 0; i < MARKS_BYTES; i++) {
            marks[i] = slot.marks[i];
        }
    }

    ui_diff(sel_diff, has_save);
    state = ST_DIFF;

    while (1) {
        input_poll();
        if (input_reset_combo()) {
            soft_reset();
        }
        switch (state) {
        case ST_DIFF:
            diff_update();
            break;
        case ST_SELECT:
            select_update();
            break;
        case ST_GAME:
            game_update();
            break;
        case ST_PAUSE:
            pause_update();
            break;
        case ST_WIN:
            if (pend.need_win) {
                ui_win(pend.win_level, pend.win_last, board_errors());
                pend.need_win = 0;
            }
            win_update();
            break;
        case ST_SAVED:
            if (pend.need_saved) {
                ui_saved();
                pend.need_saved = 0;
            }
            saved_update();
            break;
        }
        frame++;
        vsync();
    }
}
