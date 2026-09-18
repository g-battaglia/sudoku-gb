#ifndef INPUT_H
#define INPUT_H

/* ---------------------------------------------------------------------------
 * input.h — Joypad reading with debounce.
 *
 * The Game Boy has no key events: joypad() returns the buttons held
 * RIGHT NOW. Holding a button for 60 frames would look like 60 presses,
 * so this module keeps the previous state and reports each physical
 * press only once (edge detection). D-Pad directions also auto-repeat
 * while held, so the cursor keeps moving smoothly.
 *
 * Usage (once per frame):
 *   input_poll();                     // call first, every frame
 *   if (input_pressed(J_A)) { ... }   // single actions
 *   if (input_dir(J_UP)) { ... }      // held-friendly movement
 *
 * Hardware: uses joypad() from <gbdk/platform.h>.
 * -------------------------------------------------------------------------*/

#include "types.h"

/* Clear internal state. Call once at startup. */
void input_poll_init(void);

/* Read the joypad. Call once per frame, before any input_* query.
 * Must run every frame: edge detection compares against last frame. */
void input_poll(void);

/* Return 1 if any button in `mask` (J_* bits) was JUST pressed this
 * frame, else 0. Use for single actions (A/B/START/SELECT). */
uint8_t input_pressed(uint8_t mask);

/* Return 1 if a direction in `mask` should move the cursor now, else 0:
 * 1 on the first press, then repeated while held (after REPEAT_DELAY,
 * every REPEAT_RATE frames). Pass a SINGLE direction bit per call. */
uint8_t input_dir(uint8_t mask);

/* Return 1 on the frame A+B+START+SELECT become all held (the
 * classic Game Boy soft-reset combo). */
uint8_t input_reset_combo(void);

#endif /* INPUT_H */
