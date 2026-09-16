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
 * States: TITLE -> PASSWORD? -> GAME <-> PAUSE -> WIN / GAMEOVER.
 * Each frame: poll input once, run the current handler, wait for VBlank.
 *
 * Controls:
 *   D-Pad .... move cursor (wraps at grid edges)
 *   Up/Down .. change proposed digit (on editable cells)
 *   A ........ confirm digit / menu OK
 *   B ........ erase player digit / back
 *   START .... pause menu
 * -------------------------------------------------------------------------*/

/* Game states. */
typedef enum {
    ST_TITLE,
    ST_PASSWORD,
    ST_GAME,
    ST_PAUSE,
    ST_WIN,
    ST_GAMEOVER
} State;

/* Current state. */
static State state;

/* Current level (0-based). */
static uint8_t level;

/* Cursor position on the grid. */
static uint8_t cursor_row;
static uint8_t cursor_col;

/* Proposed digit for the cursor cell (1-9). */
static uint8_t entry_value;

/* Title / pause / gameover menu selection. */
static uint8_t menu_choice;

/* Password entry digits + edited slot + error flag. */
static uint8_t pwd_digits[PASSWORD_DIGITS];
static uint8_t pwd_pos;
static uint8_t pwd_bad;

/* Start (or restart) a level: reset board, cursor, entry. */
static void start_level(uint8_t new_level)
{
    uint8_t r, c;

    level = new_level;
    board_load(level);
    entry_value = 1;
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
    ui_game_full(level);
    ui_cursor(cursor_row, cursor_col, cursor_row, cursor_col);
    ui_message("");
    state = ST_GAME;
}

/* Move the cursor by (dr, dc), wrapping at the edges. */
static void move_cursor(int8_t dr, int8_t dc)
{
    uint8_t old_row, old_col;

    old_row = cursor_row;
    old_col = cursor_col;
    cursor_row = (uint8_t)((cursor_row + GRID_SIZE + dr) % GRID_SIZE);
    cursor_col = (uint8_t)((cursor_col + GRID_SIZE + dc) % GRID_SIZE);
    ui_cursor(old_row, old_col, cursor_row, cursor_col);
    ui_message("");
}

/* Title screen handler. */
static void title_update(void)
{
    if (input_pressed(J_UP) || input_pressed(J_DOWN)) {
        menu_choice = (uint8_t)(1 - menu_choice);
        ui_title(menu_choice);
    }
    if (input_pressed(J_A) || input_pressed(J_START)) {
        if (menu_choice == 0) {
            start_level(0);
        } else {
            pwd_digits[0] = 0;
            pwd_digits[1] = 0;
            pwd_digits[2] = 0;
            pwd_digits[3] = 0;
            pwd_pos = 0;
            pwd_bad = 0;
            ui_password(pwd_digits, pwd_pos, pwd_bad);
            state = ST_PASSWORD;
        }
    }
}

/* Password entry handler. */
static void password_update(void)
{
    uint16_t code;
    int8_t found;

    if (input_pressed(J_B)) {
        ui_title(menu_choice);
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
            start_level((uint8_t)found);
        }
    }
}
/* Confirm the proposed digit on the cursor cell. */
static void confirm_entry(void)
{
    uint8_t idx;

    idx = (uint8_t)(cursor_row * GRID_SIZE + cursor_col);
    if (board_is_given(idx)) {
        ui_message("LOCKED CELL");
        return;
    }
    board_set(idx, entry_value);
    if (board_conflicts(idx)) {
        /* Illegal move: reject it and count a mistake. */
        board_set(idx, 0);
        ui_cell(cursor_row, cursor_col);
        ui_cursor(cursor_row, cursor_col, cursor_row, cursor_col);
        if (board_register_error()) {
            menu_choice = 0;
            ui_gameover(menu_choice);
            state = ST_GAMEOVER;
        } else {
            ui_mistakes();
            ui_message("MISTAKE!");
        }
        return;
    }
    /* Legal move: show it and check for the win. */
    ui_cell(cursor_row, cursor_col);
    ui_cursor(cursor_row, cursor_col, cursor_row, cursor_col);
    ui_message("");
    if (board_is_solved()) {
        if (level + 1 < LEVEL_COUNT) {
            ui_win(level, password_for_level((uint8_t)(level + 1)), 0);
        } else {
            ui_win(level, 0, 1);
        }
        state = ST_WIN;
    }
}

/* Game screen handler. */
static void game_update(void)
{
    uint8_t idx;

    if (input_pressed(J_START)) {
        menu_choice = 0;
        ui_pause(menu_choice);
        state = ST_PAUSE;
        return;
    }
    if (input_dir(J_LEFT)) {
        move_cursor(0, -1);
        return;
    }
    if (input_dir(J_RIGHT)) {
        move_cursor(0, 1);
        return;
    }
    if (input_dir(J_UP)) {
        /* Up cycles the digit on editable cells... */
        idx = (uint8_t)(cursor_row * GRID_SIZE + cursor_col);
        if (!board_is_given(idx)) {
            entry_value = (uint8_t)(entry_value % 9 + 1);
            ui_entry(entry_value);
        } else {
            /* ...but moves the cursor on given cells. */
            move_cursor(-1, 0);
        }
        return;
    }
    if (input_dir(J_DOWN)) {
        idx = (uint8_t)(cursor_row * GRID_SIZE + cursor_col);
        if (!board_is_given(idx)) {
            entry_value = (uint8_t)((entry_value + 7) % 9 + 1);
            ui_entry(entry_value);
        } else {
            move_cursor(1, 0);
        }
        return;
    }
    if (input_pressed(J_A)) {
        confirm_entry();
        return;
    }
    if (input_pressed(J_B)) {
        idx = (uint8_t)(cursor_row * GRID_SIZE + cursor_col);
        if (!board_is_given(idx)) {
            board_set(idx, 0);
            ui_cell(cursor_row, cursor_col);
            ui_cursor(cursor_row, cursor_col, cursor_row, cursor_col);
            ui_message("");
        } else {
            ui_message("LOCKED CELL");
        }
    }
}
/* Pause menu handler. */
static void pause_update(void)
{
    if (input_pressed(J_UP)) {
        menu_choice = (uint8_t)((menu_choice + 2) % 3);
        ui_pause(menu_choice);
    }
    if (input_pressed(J_DOWN)) {
        menu_choice = (uint8_t)((menu_choice + 1) % 3);
        ui_pause(menu_choice);
    }
    if (input_pressed(J_B) || input_pressed(J_START)) {
        /* Back to the game: redraw everything (menu cleared it). */
        ui_game_full(level);
        ui_entry(entry_value);
        ui_cursor(cursor_row, cursor_col, cursor_row, cursor_col);
        state = ST_GAME;
        return;
    }
    if (input_pressed(J_A)) {
        if (menu_choice == 0) {
            ui_game_full(level);
            ui_entry(entry_value);
            ui_cursor(cursor_row, cursor_col, cursor_row, cursor_col);
            state = ST_GAME;
        } else if (menu_choice == 1) {
            start_level(level);
        } else {
            menu_choice = 0;
            ui_title(menu_choice);
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
            ui_title(menu_choice);
            state = ST_TITLE;
        }
    }
}

/* Game over screen handler. */
static void gameover_update(void)
{
    if (input_pressed(J_UP) || input_pressed(J_DOWN)) {
        menu_choice = (uint8_t)(1 - menu_choice);
        ui_gameover(menu_choice);
    }
    if (input_pressed(J_A) || input_pressed(J_START)) {
        if (menu_choice == 0) {
            start_level(level);
        } else {
            menu_choice = 0;
            ui_title(menu_choice);
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
    ui_title(menu_choice);
    state = ST_TITLE;

    while (1) {
        input_poll();
        switch (state) {
        case ST_TITLE:
            title_update();
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
        case ST_GAMEOVER:
            gameover_update();
            break;
        }
        vsync();
    }
}
