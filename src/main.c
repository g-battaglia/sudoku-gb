#include <gbdk/platform.h>

#include "types.h"
#include "board.h"
#include "input.h"
#include "passwords.h"
#include "puzzles.h"
#include "ui.h"

/* ---------------------------------------------------------------------------
 * main.c — Game flow. One state machine, one small handler per state.
 *
 * States: SELECT -> PASSWORD? -> GAME <-> PAUSE -> WIN.
 * Each frame: poll input once, run the current handler, wait for VBlank.
 * No game over: mistakes are only tallied (START menu) and play goes on.
 *
 * Select screen: all 12 levels are freely playable from boot.
 * A password never unlocks anything: it only marks the beaten levels
 * (`*`) and jumps to the next one to play.
 *
 * Game controls:
 *   D-Pad .... move cursor (wraps at grid edges)
 *   A ........ edit the cursor cell (digit-pick mode)
 *     Up/Down  pick the digit (blinks in the cell)
 *     A ...... confirm, B = back without changing
 *   B ........ erase a player digit
 *   START .... pause menu (RESUME / HINT / RESTART / TITLE)
 * -------------------------------------------------------------------------*/

/* Game states. */
typedef enum {
    ST_TITLE,
    ST_PASSWORD,
    ST_GAME,
    ST_PAUSE,
    ST_WIN
} State;

/* Current state. */
static State state;

/* Current level (0-based). */
static uint8_t level;

/* Cursor position on the grid. */
static uint8_t cursor_row;
static uint8_t cursor_col;

/* Digit picked in edit mode (1-9). */
static uint8_t entry_value;

/* 1 = digit-pick mode (Up/Down picks, A confirms, B cancels). */
static uint8_t editing;

/* Completed levels in this session (1 = beaten, shown with `*`).
 * Static storage starts at zero: nothing is complete at boot. */
static uint8_t completed[LEVEL_COUNT];

/* Title / pause menu selection. On SELECT it is the level cursor. */
static uint8_t menu_choice;

/* Password entry digits + edited slot + error flag. */
static uint8_t pwd_digits[PASSWORD_DIGITS];
static uint8_t pwd_pos;
static uint8_t pwd_bad;

/* Frame counter (blink timing) + mistake feedback (frames, cursor hidden). */
static uint8_t frame;
static uint8_t flash;

/* Blinking preview tracker. pv_row = 0xFF means "nothing tracked". */
static uint8_t pv_row;
static uint8_t pv_col;
static uint8_t pv_val;
static uint8_t pv_shown;

/* Forget the preview without redrawing (caller already drew the cell). */
static void preview_forget(void)
{
    pv_row = 0xFF;
    pv_shown = 0;
}

/* Erase a visible preview, redrawing the cell from the board. */
static void preview_erase(void)
{
    if (pv_shown && pv_row != 0xFF) {
        ui_cell(pv_row, pv_col);
    }
    preview_forget();
}

/* Blink the picked digit in the cursor cell while editing. Call every
 * frame in ST_GAME: the board is never touched, only tiles are drawn. */
static void preview_update(void)
{
    uint8_t idx, want, phase;

    idx = (uint8_t)(cursor_row * GRID_SIZE + cursor_col);
    want = (editing && !board_is_given(idx));
    if (!want || pv_row != cursor_row || pv_col != cursor_col ||
        pv_val != entry_value) {
        preview_erase();
        if (want) {
            pv_row = cursor_row;
            pv_col = cursor_col;
            pv_val = entry_value;
        }
    }
    if (want) {
        phase = (uint8_t)((frame >> 5) & 1); /* Toggle every 32 frames. */
        if (phase != pv_shown) {
            ui_preview(cursor_row, cursor_col, entry_value, phase);
            pv_shown = phase;
        }
    }
}

/* Locked-cell feedback: blink the cursor off briefly. */
static void locked_feedback(void)
{
    ui_cursor_hide();
    flash = 20;
}

/* Start (or restart) a level: reset board, cursor, mode. */
static void start_level(uint8_t new_level)
{
    uint8_t r, c;

    level = new_level;
    board_load(level);
    entry_value = 1;
    editing = 0;
    flash = 0;
    /* Put the cursor on the first editable cell. */
    cursor_row = 0;
    cursor_col = 0;
    for (r = 0; r < GRID_SIZE; r++) {
        for (c = 0; c < GRID_SIZE; c++) {
            if (!board_is_given((uint8_t)(r * GRID_SIZE + c))) {
                cursor_row = r;
                cursor_col = c;
                r = GRID_SIZE; /* Break outer loop too. */
                break;
            }
        }
    }
    preview_forget();
    ui_game_full();
    ui_cursor(cursor_row, cursor_col);
    state = ST_GAME;
}

/* Back to the game screen (menu cleared it): redraw grid + cursor. */
static void resume_game(void)
{
    editing = 0;
    flash = 0;
    preview_forget();
    ui_game_full();
    ui_cursor(cursor_row, cursor_col);
    state = ST_GAME;
}

/* Enter the win screen for the current level. */
static void win_now(void)
{
    completed[level] = 1;
    ui_cursor_hide();
    preview_forget();
    if (level + 1 < LEVEL_COUNT) {
        ui_win(level, password_for_level((uint8_t)(level + 1)), 0);
    } else {
        ui_win(level, 0, 1);
    }
    state = ST_WIN;
}

/* Move the cursor by (dr, dc), wrapping at the edges. */
static void move_cursor(int8_t dr, int8_t dc)
{
    cursor_row = (uint8_t)((cursor_row + GRID_SIZE + dr) % GRID_SIZE);
    cursor_col = (uint8_t)((cursor_col + GRID_SIZE + dc) % GRID_SIZE);
    flash = 0;
    ui_cursor(cursor_row, cursor_col);
}

/* A on a cell: enter digit-pick mode (or complain if locked). */
static void enter_editing(void)
{
    uint8_t idx, current;

    idx = (uint8_t)(cursor_row * GRID_SIZE + cursor_col);
    if (board_is_given(idx)) {
        locked_feedback();
        return;
    }
    /* Start from the digits already there (or 1 if empty). */
    current = board_get(idx);
    entry_value = current ? current : 1;
    editing = 1;
    flash = 0;
    ui_cursor(cursor_row, cursor_col);
}

/* A in digit-pick mode: try the picked digit. */
static void confirm_editing(void)
{
    uint8_t idx, old;

    idx = (uint8_t)(cursor_row * GRID_SIZE + cursor_col);
    old = board_get(idx);
    board_set(idx, entry_value);
    if (board_conflicts(idx)) {
        /* Illegal move: restore, count a mistake, keep picking. */
        board_set(idx, old);
        ui_cell(cursor_row, cursor_col);
        board_add_mistake();
        ui_cursor_hide();
        flash = 24;
        return;
    }
    /* Legal move: show it, back to navigation, check for the win. */
    editing = 0;
    preview_forget();
    ui_cell(cursor_row, cursor_col);
    if (board_is_solved()) {
        win_now();
    }
}

/* B in digit-pick mode: back to navigation, board untouched. */
static void cancel_editing(void)
{
    editing = 0;
    preview_erase();
}

/* B on a cell: erase a player digit (givens complain). */
static void erase_cell(void)
{
    uint8_t idx;

    idx = (uint8_t)(cursor_row * GRID_SIZE + cursor_col);
    if (board_is_given(idx)) {
        if (board_get(idx) != 0) {
            locked_feedback();
        }
        return;
    }
    if (board_get(idx) != 0) {
        board_set(idx, 0);
        ui_cell(cursor_row, cursor_col);
    }
}

/* Level-select handler: free choice of any level, SELECT = password. */
static void select_update(void)
{
    if (input_pressed(J_UP)) {
        menu_choice = (uint8_t)((menu_choice + LEVEL_COUNT - 1) % LEVEL_COUNT);
        ui_select(menu_choice, completed);
    }
    if (input_pressed(J_DOWN)) {
        menu_choice = (uint8_t)((menu_choice + 1) % LEVEL_COUNT);
        ui_select(menu_choice, completed);
    }
    if (input_pressed(J_SELECT)) {
        pwd_digits[0] = 0;
        pwd_digits[1] = 0;
        pwd_digits[2] = 0;
        pwd_digits[3] = 0;
        pwd_pos = 0;
        pwd_bad = 0;
        ui_password(pwd_digits, pwd_pos, pwd_bad);
        state = ST_PASSWORD;
    }
    if (input_pressed(J_A) || input_pressed(J_START)) {
        start_level(menu_choice);
    }
}

/* Password entry handler. */
static void password_update(void)
{
    uint16_t code;
    int8_t found;

    if (input_pressed(J_B)) {
        ui_select(menu_choice, completed);
        state = ST_TITLE;
        return;
    }
    if (input_dir(J_UP)) {
        pwd_digits[pwd_pos] = (uint8_t)((pwd_digits[pwd_pos] + 1) % 10);
        ui_password(pwd_digits, pwd_pos, 0);
    }
    if (input_dir(J_DOWN)) {
        pwd_digits[pwd_pos] = (uint8_t)((pwd_digits[pwd_pos] + 9) % 10);
        ui_password(pwd_digits, pwd_pos, 0);
    }
    if (input_pressed(J_LEFT)) {
        pwd_pos = (uint8_t)((pwd_pos + PASSWORD_DIGITS - 1) % PASSWORD_DIGITS);
        ui_password(pwd_digits, pwd_pos, 0);
    }
    if (input_pressed(J_RIGHT)) {
        pwd_pos = (uint8_t)((pwd_pos + 1) % PASSWORD_DIGITS);
        ui_password(pwd_digits, pwd_pos, 0);
    }
    if (input_pressed(J_A) || input_pressed(J_START)) {
        code = (uint16_t)(pwd_digits[0] * 1000 + pwd_digits[1] * 100 +
                          pwd_digits[2] * 10 + pwd_digits[3]);
        found = password_find_level(code);
        if (found < 0) {
            pwd_bad = 1;
            ui_password(pwd_digits, pwd_pos, pwd_bad);
        } else {
            /* The password proves levels 1..N-1 beaten: mark them and
             * jump to level N. Nothing is unlocked: every level is
             * always playable, the password only restores the marks. */
            uint8_t l;

            for (l = 0; l < (uint8_t)found; l++) {
                completed[l] = 1;
            }
            menu_choice = (uint8_t)found;
            ui_select(menu_choice, completed);
            state = ST_TITLE;
        }
    }
}

/* Game screen handler: navigation vs digit-pick mode. */
static void game_update(void)
{
    if (input_pressed(J_START)) {
        editing = 0;
        preview_erase();
        flash = 0;
        menu_choice = 0;
        ui_pause(menu_choice, level);
        state = ST_PAUSE;
        return;
    }
    if (editing) {
        if (input_dir(J_UP)) {
            entry_value = (uint8_t)(entry_value % 9 + 1);
        } else if (input_dir(J_DOWN)) {
            entry_value = (uint8_t)((entry_value + 7) % 9 + 1);
        }
        if (input_pressed(J_A)) {
            confirm_editing();
        } else if (input_pressed(J_B)) {
            cancel_editing();
        }
    } else {
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
    if (flash > 0) {
        flash--;
        if (flash == 0) {
            ui_cursor(cursor_row, cursor_col);
        }
    }
    preview_update();
}

/* HINT: reveal the true digit of the cursor cell (or the first empty
 * editable cell) and lock it, then back to the game. */
static void do_hint(void)
{
    uint8_t idx, i, value;

    idx = (uint8_t)(cursor_row * GRID_SIZE + cursor_col);
    if (board_is_given(idx) || board_get(idx) != 0) {
        idx = 0xFF;
        for (i = 0; i < CELL_COUNT; i++) {
            if (!board_is_given(i) && board_get(i) == 0) {
                idx = i;
                break;
            }
        }
        if (idx == 0xFF) {
            resume_game(); /* Full grid: win already triggered. */
            return;
        }
        cursor_row = (uint8_t)(idx / GRID_SIZE);
        cursor_col = (uint8_t)(idx % GRID_SIZE);
    }
    value = (uint8_t)(puzzles[level].solution[idx] - '0');
    board_set(idx, value);
    board_reveal(idx);
    resume_game();
    ui_cell(cursor_row, cursor_col);
    if (board_is_solved()) {
        win_now();
    }
}

/* Pause (START menu) handler: RESUME / HINT / RESTART / TITLE. */
static void pause_update(void)
{
    if (input_pressed(J_UP)) {
        menu_choice = (uint8_t)((menu_choice + 3) % 4);
        ui_pause(menu_choice, level);
    }
    if (input_pressed(J_DOWN)) {
        menu_choice = (uint8_t)((menu_choice + 1) % 4);
        ui_pause(menu_choice, level);
    }
    if (input_pressed(J_B) || input_pressed(J_START)) {
        resume_game();
        return;
    }
    if (input_pressed(J_A)) {
        if (menu_choice == 0) {
            resume_game();
        } else if (menu_choice == 1) {
            do_hint();
        } else if (menu_choice == 2) {
            start_level(level);
        } else {
            menu_choice = 0;
            ui_select(menu_choice, completed);
            state = ST_TITLE;
        }
    }
}

/* Win screen handler. */
static void win_update(void)
{
    if (input_pressed(J_A) || input_pressed(J_START)) {
        if (level + 1 < LEVEL_COUNT) {
            start_level((uint8_t)(level + 1));
        } else {
            menu_choice = 0;
            ui_select(menu_choice, completed);
            state = ST_TITLE;
        }
    }
}

/* Entry point. */
void main(void)
{
    /* A few frames on startup: required on SGB PAL, harmless elsewhere. */
    uint8_t i;

    for (i = 4; i != 0; i--) {
        vsync();
    }
    ui_init();
    input_poll_init();
    menu_choice = 0;
    preview_forget();
    ui_select(menu_choice, completed);
    state = ST_TITLE;

    while (1) {
        input_poll();
        switch (state) {
        case ST_TITLE:
            select_update();
            break;
        case ST_PASSWORD:
            password_update();
            break;
        case ST_GAME:
            game_update();
            break;
        case ST_PAUSE:
            pause_update();
            break;
        case ST_WIN:
            win_update();
            break;
        }
        frame++;
        vsync();
    }
}
