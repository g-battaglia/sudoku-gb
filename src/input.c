#include <gbdk/platform.h>

#include "input.h"

/* ---------------------------------------------------------------------------
 * input.c — Edge detection + D-Pad auto-repeat.
 *
 * - `prev_state`: buttons held in the previous frame.
 * - `just_pressed`: buttons that went from released to pressed.
 * - `repeat_count`: frames the current direction has been held.
 * -------------------------------------------------------------------------*/

/* Frames to wait before a held direction starts repeating. */
#define REPEAT_DELAY 18

/* Frames between repeats once repeating. */
#define REPEAT_RATE 6

/* Buttons held last frame. */
static uint8_t prev_state;

/* Buttons newly pressed this frame (edge). */
static uint8_t just_pressed;

/* How long the current direction mask has been held. */
static uint8_t repeat_count;

/* Clear internal state. Call once at startup. */
void input_poll_init(void)
{
    prev_state = 0;
    just_pressed = 0;
    repeat_count = 0;
}

/* Read the joypad. Call once per frame, before any input_* query. */
void input_poll(void)
{
    uint8_t now;

    now = joypad();
    just_pressed = (uint8_t)(now & (uint8_t)~prev_state);
    if ((now & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) == 0) {
        /* No direction held: reset the repeat timer. */
        repeat_count = 0;
    } else if (just_pressed & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) {
        /* New direction press: restart the repeat timer. */
        repeat_count = 0;
    } else if (repeat_count < 255) {
        repeat_count++;
    }
    prev_state = now;
}

/* Return non-zero if any button in `mask` was JUST pressed this frame. */
uint8_t input_pressed(uint8_t mask)
{
    return just_pressed & mask;
}

/* Return non-zero if a direction in `mask` should move the cursor now. */
uint8_t input_dir(uint8_t mask)
{
    /* First press: move immediately. */
    if (just_pressed & mask) {
        return 1;
    }
    /* Held: move only if this direction is still held and the timer says so. */
    if ((prev_state & mask) == 0) {
        return 0;
    }
    if (repeat_count < REPEAT_DELAY) {
        return 0;
    }
    return ((uint8_t)(repeat_count - REPEAT_DELAY) % REPEAT_RATE) == 0;
}
