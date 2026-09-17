# 04 — Control flow: decisions, loops, state machines

C has five control-flow tools (`if`, `switch`, `for`, `while`, `do-while`) plus `break`/`continue`/`return`/`goto`. The game is built almost entirely from `if`, `switch`, `while` and tiny `for` loops; `goto` never appears. Master these and you can read `main.c` end to end.

## 1. `if / else` and `else if`

```c
uint8_t idx = (uint8_t)(cursor_row * GRID_SIZE + cursor_col);
if (board_is_locked(idx)) {
    locked_feedback();   /* clue or hint: complain */
} else if (board_get(idx) != 0) {
    enter_editing();     /* occupied: edit from current digit */
} else {
    enter_editing();     /* empty: edit from 1 */
}
```

Like Python's `if/elif/else`, but the condition must be in parentheses and the block in braces. Any non-zero value counts as true; `0` is false. That is why `if (!save_read(&slot))` reads as "if loading failed" and `if (board_conflicts(idx))` as "if illegal".

Always use braces, even for one line. The repo does without exception, because the brace-less form breeds the classic bug:

```c
if (locked)
    locked_feedback();
    resume_game();   /* looks guarded, ALWAYS runs — indentation lies, braces don't */
```

Common Python mistake: `=` vs `==`. `if (x = 5)` *assigns* 5 and is always true (with a warning). `if (x == 5)` compares. `-Wall` warns about the first inside `if` — read the warning, fix the operator. Yoda conditions (`if (5 == x)`) catch the typo at compile time but the repo does not use them; attention plus warnings suffice.

`else if` chains test ranges and priorities (pause-menu dispatch in `pause_update` is an `if/else if` ladder on `menu_choice`). For many *exact values* of one variable, prefer `switch` (§2).

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

> **Python vs C:** this is Python's `match` (3.10+) or an `if/elif/else` chain. Each `case` is one compile-time constant value (no ranges, no variables). `default` is `else`. `break` exits the switch.

Forgetting `break` when you *don't* return is the classic switch bug: execution "falls through" into the next case. Here `return` already left the function, so no `break` is needed — but in the main dispatch it is load-bearing:

```c
switch (state) {   /* src/main.c:608 */
case ST_DIFF:   diff_update();   break;
case ST_SELECT: select_update(); break;
case ST_GAME:   game_update();   break;
case ST_PAUSE:  pause_update();  break;
case ST_WIN:
    if (need_win_draw) {
        ui_win(win_level, win_last);
        need_win_draw = 0;
    }
    win_update();
    break;
case ST_SAVED:
    if (need_saved_draw) {
        ui_saved();
        need_saved_draw = 0;
    }
    saved_update();
    break;
}
```

Delete one `break` and two screens run per frame — input double-handled, drawing garbled. Intentional fall-through exists in C (stacked cases sharing a body) and must be commented when meant:

```c
switch (key) {
case 'y':
case 'Y':   /* fall through: both spell yes */
    confirm();
    break;
default:
    cancel();
    break;
}
```

Rule: **one `switch`, one handler per state**. To understand any screen, read its single handler. This pattern — **an enum plus a switch** — is the standard C replacement for Python classes with methods when the behaviour is "what screen am I on". With `-Wall`, switching over an `enum` without `default` can warn about unhandled enumerators — the compiler audits your state coverage.

When to choose: exact values of one integer → `switch`; ranges, floats, strings, multiple variables → `if/else if`. `switch` accepts only integer-like subjects (`int`, `char`, `enum`) — strings need `strcmp` chains (chapter 06).

## 3. Loops: `for`, `while`, `do-while`

C has three loops; the repo uses the first two constantly, the third never (but you should recognise it).

**`for`: counted loops** (grid scans, copies, checksums). Three clauses, all optional:

```c
/* src/board.c:29 — reset the whole grid */
for (i = 0; i < CELL_COUNT; i++) {
    g = puzzle_given(level, i);
    cells[i] = g;
    origin[i] = (g != 0) ? ORIGIN_GIVEN : ORIGIN_PLAYER;
}
```

Anatomy: `for (init; condition; step)`. `i = 0` once; repeat while `i < 81`; `i++` after each pass. Use `< COUNT`, never `<= COUNT - 1` — off-by-one errors hide in the second form, and `<` matches array bounds directly (valid indices are `0..80`). The comma form `for (r = 0, c = 0; …)` exists but the repo keeps one variable per loop and nests instead — readability over cleverness.

Nested loops scan 2-D structure (`board_conflicts` box scan, `src/board.c:87`):

```c
/* Same 3x3 box: rows box_r..box_r+2, cols box_c..box_c+2 */
for (r = (uint8_t)((row / BOX_SIZE) * BOX_SIZE);
     r < (uint8_t)((row / BOX_SIZE) * BOX_SIZE + BOX_SIZE); r++) {
    for (c = (uint8_t)((col / BOX_SIZE) * BOX_SIZE);
         c < (uint8_t)((col / BOX_SIZE) * BOX_SIZE + BOX_SIZE); c++) {
        if ((r != row || c != col) && cells[cell_index(r, c)] == value) {
            return 1;
        }
    }
}
```

`row / BOX_SIZE * BOX_SIZE` snaps to the box origin (integer truncation does the flooring: row 7 → `7/3*3` = 6). The `(r != row || c != col)` guard skips the cell itself. Trace box of cell (7,8): rows 6–8, cols 6–8. Nine comparisons, first duplicate returns.

**`while`: open-ended loops** (the main loop runs forever; scans stop on conditions):

```c
/* src/main.c:603 — the game never returns from main */
while (1) {
    input_poll();
    /* ... run one handler ... */
    frame++;
    vsync();   /* wait for the next TV-like refresh, 60 fps */
}
```

`while (1)` is infinite by construction; on the Game Boy there is no OS to return to, so `main` never ends. `soft_reset()` jumps back to address `0x0100` instead of returning. A `while` whose condition never becomes false on PC is a hang you kill with Ctrl-C; on hardware it is indistinguishable from correct operation with no output — another reason infinite loops are reserved for the one true main loop.

**`do-while`: runs the body at least once** (absent here, common in input-retry and macro-writing):

```c
/* Not in repo — recognise the shape: */
do {
    printf("digit 1-9: ");
} while (scanf("%d", &v) != 1 || v < 1 || v > 9);
```

Note the trailing `;`. If you meet `do { … } while (0);` wrapping a multi-statement macro, that is the standard trick making macros behave like single statements (chapter 08 §5 explains why it exists).

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

`break` leaves the innermost loop (or `switch`) now. `continue` skips to the next iteration (use sparingly; a guard clause usually reads better). `0xFF` (255) is a *sentinel*: cell indices only reach 80, so 255 unambiguously means "not found" (like Python's `None` for "no result"). Sentinels recur: `pv_row = 0xFF` ("no preview tracked"), `NULL` ("no address"), `default: return "?????"` ("no such difficulty").

`goto` exists in C and has one legitimate use (shared cleanup before many returns in large functions). This codebase never needs it: functions are short enough that early returns suffice. If a function grows `goto`-shaped, split it instead.

## 4. The wrap trick: modulo navigation

Every menu and the cursor wrap around (`Up` from the top lands at the bottom). The helper (`src/main.c:152`):

```c
static uint8_t wrap_add(uint8_t v, int8_t d, uint8_t n) {
    return (uint8_t)((v + n + d) % n);
}
```

Why `v + n + d` and not `v + d`? Because in C, **negative `%` stays negative** (`-1 % 10 == -1`, unlike Python where it is `9`). Adding `n` first keeps the value non-negative so `% n` lands in `0..n-1`. Trace `wrap_add(0, -1, 10)`: `(0 + 10 - 1) % 10 = 9`. Correct. Trace `wrap_add(9, 1, 10)`: `(9 + 10 + 1) % 10 = 0`. Correct. The `+ n` works because `d >= -n` always holds here (steps are ±1, sizes ≥ 3) — for arbitrary large negative steps you would loop or use a floor-mod helper instead.

> **Python vs C:** Python `-1 % 10 == 9`. C `-1 % 10 == -1`. When porting wrap logic from Python, always add the modulus first.

Cursor movement uses the same idea inline (`src/main.c:246`):

```c
cursor_row = (uint8_t)((cursor_row + GRID_SIZE + dr) % GRID_SIZE);
cursor_col = (uint8_t)((cursor_col + GRID_SIZE + dc) % GRID_SIZE);
```

Digit-pick wraps 1–9 rather than 0–8, so it shifts by one (`src/main.c:436`): `entry_value % 9 + 1` maps 9→1; `(entry_value + 7) % 9 + 1` maps 1→9 (`+7` ≡ `−2`… precisely: `(v+7)%9+1` with v=1 gives `(8)%9+1 = 9`). Same family, 1-based flavour.

## 5. Early returns: guard clauses over nesting

Compare deep nesting with the repo style:

```c
/* Nested (harder to scan): every path indents further */
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
static void confirm_editing(void) {   /* src/main.c:271 */
    uint8_t idx, old;

    idx = (uint8_t)(cursor_row * GRID_SIZE + cursor_col);
    old = board_get(idx);
    board_set(idx, entry_value);
    if (board_conflicts(idx)) {
        /* Illegal move: restore, count a mistake, keep picking. */
        board_set(idx, old);
        ui_cell(cursor_row, cursor_col);
        preview_forget(); /* The redraw killed any visible preview. */
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
```

Rule: **reject fast, handle errors first, keep the happy path at the left margin**. With no exceptions, this is how C stays readable. Notice the tentative-write-then-validate order: write first, `board_conflicts` after (the checker reads the live grid), restore on failure. The header contract "caller must call `board_conflicts()` before accepting" means exactly this sequence.

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

Logic first, drawing second (inside handlers via `ui_*`), wait last — always in that order (see `DEVELOPMENT.md` §3.2). `game_update` itself branches on `editing`: navigation keys *mean different things* in pick mode vs move mode. That single `if (editing)` is the whole "modal editing" design — compare `if/else` on mode versus two separate states, and notice the code chose the flag because preview blink, cursor display and START handling are shared between modes.

The frame budget view: 60 frames/second ÷ 4.19 MHz ≈ 70 000 CPU cycles per frame. Every handler must finish well within that (they do — a conflicts scan is ~81 reads). Python thinking ("a millisecond here or there") becomes cycle thinking; small fixed loops are not just style but schedule.

Next: `05-functions-scope-storage.md` — how C organises code into functions and where variables live.
