#include <gbdk/platform.h>

#include "input.h"

/* ---------------------------------------------------------------------------
 * input.c — Edge detection + D-Pad auto-repeat.
 *
 * State:
 * - `prev_state`: buttons held in the previous frame.
 * - `just_pressed`: buttons that went from released to pressed this frame.
 * - `repeat_held`: frames the current direction has been held continuously.
 *   16-bit on purpose: an 8-bit counter capped at 255 would stall the
 *   repeat after ~4s of holding (the modulo below would freeze).
 *   Single shared timer: if two directions are held at once, both share
 *   the same phase. Callers pass one direction per input_dir() call,
 *   so this is fine in practice.
 * -------------------------------------------------------------------------*/

/* Frames to wait before a held direction starts repeating. */
#define REPEAT_DELAY 18

/* Frames between repeats once repeating. */
#define REPEAT_RATE 6

/* Buttons held last frame. */
static uint8_t prev_state;

/* Buttons newly pressed this frame (edge). Return 1 = true, 0 = false. */
static uint8_t just_pressed;

/* How long the current direction has been held (frames, never wraps). */
static uint16_t repeat_held;

/* 1 on the frame the A+B+START+SELECT combo becomes complete. */
static uint8_t combo_fire;

#define COMBO_MASK (J_A | J_B | J_START | J_SELECT)

/* Clear internal state. Call once at startup. */
void input_poll_init(void)
{
    prev_state = 0;
    just_pressed = 0;
    repeat_held = 0;
    combo_fire = 0;
}

/* Read the joypad. Call once per frame, before any input_* query.
 * In: none. Out: updates prev_state/just_pressed/repeat_held/combo_fire. */
void input_poll(void)
{
    uint8_t now;

    now = joypad();
    just_pressed = (uint8_t)(now & (uint8_t)~prev_state);
    /* Reset combo: fires once, when the last of the four buttons
     * joins (all held together for the first time this press). */
    combo_fire = (uint8_t)((now & COMBO_MASK) == COMBO_MASK &&
                           (prev_state & COMBO_MASK) != COMBO_MASK);
    if ((now & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) == 0) {
        /* No direction held: reset the repeat timer. */
        repeat_held = 0;
    } else if (just_pressed & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) {
        /* New direction press: restart the repeat timer. */
        repeat_held = 0;
    } else if (repeat_held < 60000) {
        /* Saturate far beyond any real hold: the modulo below keeps
         * firing forever, it never freezes like a capped uint8_t. */
        repeat_held++;
    }
    prev_state = now;
}

/* Return non-zero if any button in `mask` was JUST pressed this frame.
 * In: mask of J_* bits. Out: 1 = pressed this frame, 0 = not. */
uint8_t input_pressed(uint8_t mask)
{
    return just_pressed & mask;
}

/* Return 1 if a direction in `mask` should move the cursor now, else 0.
 * In: mask with a SINGLE direction bit (J_UP/J_DOWN/J_LEFT/J_RIGHT).
 *   Passing several bits works but shares one timer phase.
 * Out: 1 on the first press, then 1 every REPEAT_RATE frames after
 *   REPEAT_DELAY while still held. */
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
    if (repeat_held < REPEAT_DELAY) {
        return 0;
    }
    return (uint8_t)((repeat_held - REPEAT_DELAY) % REPEAT_RATE) == 0;
}

/* Return 1 on the frame A+B+START+SELECT become all held
 * (classic soft-reset combo, handled by main.c). */
uint8_t input_reset_combo(void)
{
    return combo_fire;
}
