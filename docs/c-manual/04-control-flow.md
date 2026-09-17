# 04 — Control flow: decisions, loops, state machines

C has five control-flow tools. The game is built almost entirely from two of them (`switch` + `while`) plus tiny `for` loops. Master these and you can read `main.c` end to end.

## 1. `if / else`

```c
uint8_t idx = cursor_row * GRID_SIZE + cursor_col;
if (board_is_locked(idx)) {
    locked_feedback();   /* clue or hint: complain */
} else {
    enter_editing();     /* free cell: start digit-pick mode */
}
```

Like Python's `if/else`, but the condition must be in parentheses and the block in braces. Any non-zero value counts as true; `0` is false. That is why `if (!save_read(&slot))` reads as "if loading failed".

Common Python mistake: `=` vs `==`. `if (x = 5)` *assigns* 5 and is always true (with a warning). `if (x == 5)` compares. `-Wall` warns about the first inside `if` — read the warning, fix the operator.

## 2. `switch`: one value, many branches

`src/puzzles.c:33` maps a difficulty code to its name:

```c
const char *difficulty_name(uint8_t diff) {
    switch (diff) {
    case DIFF_EASY:
        return "EASY";
    case DIFF_MEDIUM:
        return "MEDIUM";
    case DIFF_HARD:
        return "HARD";
    default:
        return "?????";
    }
}
```

> **Python vs C:** this is Python's `match` (3.10+) or an `if/elif/else` chain. Each `case` is one value. `default` is `else`. `break` exits the switch (unneeded here because `return` already left the function — forgetting `break` when you *don't* return is the classic switch bug: execution "falls through" into the next case).

The main loop of the game is a `switch` on the state (`src/main.c:608`):

```c
switch (state) {
case ST_DIFF:   diff_update();   break;
case ST_SELECT: select_update(); break;
case ST_GAME:   game_update();   break;
case ST_PAUSE:  pause_update();  break;
case ST_WIN:    /* ... */        break;
case ST_SAVED:  /* ... */        break;
}
```

One `switch`, one handler per state. To understand any screen, read its single handler. This pattern — **an enum plus a switch** — is the standard C replacement for Python classes with methods when the behaviour is "what screen am I on".

## 3. Loops: `for`, `while`, `do-while`

C has three loops; the repo uses the first two constantly, the third never.

**`for`: counted loops** (grid scans, copies, checksums):

```c
/* src/board.c:29 — reset the whole grid */
for (i = 0; i < CELL_COUNT; i++) {
    g = puzzle_given(level, i);
    cells[i] = g;
    origin[i] = (g != 0) ? ORIGIN_GIVEN : ORIGIN_PLAYER;
}
```

Anatomy: `for (init; condition; step)`. `i = 0` once; repeat while `i < 81`; `i++` after each pass. Use `< COUNT`, never `<= COUNT - 1` — off-by-one errors hide in the second form, and `<` matches array bounds directly (valid indices are `0..80`).

**`while`: open-ended loops** (the main loop runs forever):

```c
/* src/main.c:603 — the game never returns from main */
while (1) {
    input_poll();
    /* ... run one handler ... */
    frame++;
    vsync();   /* wait for the next TV-like refresh, 60 fps */
}
```

`while (1)` is infinite by construction; on the Game Boy there is no OS to return to, so `main` never ends. `soft_reset()` jumps back to address `0x0100` instead of returning.

**`break` / `continue`:**

```c
/* src/main.c:472 — find first free editable cell */
idx = 0xFF;
for (i = 0; i < CELL_COUNT; i++) {
    if (!board_is_locked(i) && board_get(i) == 0) {
        idx = i;
        break;          /* found: stop searching */
    }
}
if (idx == 0xFF) {
    resume_game();      /* full grid: nothing to hint */
    return;
}
```

`break` leaves the loop now. `continue` skips to the next iteration. `0xFF` (255) is a *sentinel*: cell indices only reach 80, so 255 unambiguously means "not found" (like Python's `None` for "no result").

## 4. The wrap trick: modulo navigation

Every menu and the cursor wrap around (`Up` from the top lands at the bottom). The helper (`src/main.c:152`):

```c
static uint8_t wrap_add(uint8_t v, int8_t d, uint8_t n) {
    return (uint8_t)((v + n + d) % n);
}
```

Why `v + n + d` and not `v + d`? Because in C, **negative `%` stays negative** (`-1 % 10 == -1`, unlike Python where it is `9`). Adding `n` first keeps the value non-negative so `% n` lands in `0..n-1`. Trace `wrap_add(0, -1, 10)`: `(0 + 10 - 1) % 10 = 9`. Correct.

> **Python vs C:** Python `-1 % 10 == 9`. C `-1 % 10 == -1`. When porting wrap logic from Python, always add the modulus first.

Cursor movement uses the same idea inline (`src/main.c:246`):

```c
cursor_row = (uint8_t)((cursor_row + GRID_SIZE + dr) % GRID_SIZE);
cursor_col = (uint8_t)((cursor_col + GRID_SIZE + dc) % GRID_SIZE);
```

## 5. Early returns: guard clauses over nesting

Compare deep nesting with the repo style:

```c
/* Nested (harder to scan) */
void confirm_editing_nested(void) {
    if (!board_conflicts(idx)) {
        if (!board_is_solved()) {
            /* ... */
        } else {
            win_now();
        }
    } else {
        /* mistake path, indented twice */
    }
}

/* Repo style: handle the odd case first, return, stay flat */
static void confirm_editing(void) {   /* src/main.c:271, simplified */
    board_set(idx, entry_value);
    if (board_conflicts(idx)) {
        board_set(idx, old);      /* restore */
        board_add_mistake();      /* tally */
        /* ... feedback ... */
        return;                   /* done: the rest is the happy path */
    }
    editing = 0;
    ui_cell(cursor_row, cursor_col);
    if (board_is_solved()) {
        win_now();
    }
}
```

Rule: **reject fast, handle errors first, keep the happy path at the left margin**. With no exceptions, this is how C stays readable.

## 6. Putting it together: one frame of the game

```c
while (1) {                        /* forever, 60 times/second */
    input_poll();                  /* 1. snapshot buttons */
    if (input_reset_combo()) {     /* 2. global combo first */
        soft_reset();
    }
    switch (state) {               /* 3. exactly one handler runs */
    case ST_GAME:
        game_update();             /* movement OR digit-pick, never both */
        break;
    /* ... other states ... */
    }
    frame++;                       /* 4. bookkeeping */
    vsync();                       /* 5. wait for the screen refresh */
}
```

Logic first, drawing second (inside handlers via `ui_*`), wait last — always in that order (see `DEVELOPMENT.md` §3.2). `game_update` itself branches on `editing`: navigation keys *mean different things* in pick mode vs move mode. That single `if (editing)` is the whole "modal editing" design.

## Exercises

1. Trace `wrap_add(9, 1, 10)` and `wrap_add(0, -1, 3)` by hand. Then explain why the `+ n` term is needed given C's negative-remainder rule.
2. In `src/main.c`, find `game_update()` and list which inputs are read with `input_pressed` (single actions) vs `input_dir` (held-friendly). Why is digit-pick increment on `input_dir` but confirm on `input_pressed`?
3. Rewrite `difficulty_name()` with `if/else` instead of `switch`. Which version is clearer for 4+ fixed values? When would you still prefer `if`? (Hint: ranges vs exact values.)

Next: `05-functions-scope-storage.md` — how C organises code into functions and where variables live.
