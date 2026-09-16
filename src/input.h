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

/* Read the joypad. Call once per frame, before any input_* query. */
void input_poll(void);

/* Return non-zero if any button in `mask` was JUST pressed this frame. */
uint8_t input_pressed(uint8_t mask);

/* Return non-zero if a direction in `mask` should move the cursor now:
 * true on the first press, then repeatedly while held (after a delay). */
uint8_t input_dir(uint8_t mask);

#endif /* INPUT_H */
