# 05 — Functions, scope and storage

Functions are C's only unit of behaviour (no methods, no lambdas in this codebase). This chapter explains how to declare, define and read them, what `static` really means, and where local variables live.

## 1. Declaration vs definition (the `.h` / `.c` split preview)

```c
/* board.h — DECLARATION: "this function exists, trust me" */
uint8_t board_get(uint8_t idx);

/* board.c — DEFINITION: "here is how it works" */
uint8_t board_get(uint8_t idx) {
    return cells[idx];
}
```

> **Python vs C:** Python `def` does both at once. C splits the promise (header) from the implementation (source) so files can call each other without circular imports. To know *what a module offers*, read its `.h`. To know *how*, read its `.c`.

A declaration without any definition compiles but **fails at link time** (`undefined reference`). A definition without a declaration works but cannot be called from other files cleanly (the caller would have to redeclare it). Chapter 08 covers headers fully; for now remember: every public function in this repo appears in both places with identical signatures.

## 2. Anatomy of a function

```c
/* src/board.c:63 */
uint8_t board_conflicts(uint8_t idx) {
    uint8_t row, col, r, c, value;

    value = cells[idx];
    if (value == 0) {
        return 0;   /* Empty cell: no conflict possible. */
    }
    row = (uint8_t)(idx / GRID_SIZE);
    col = (uint8_t)(idx % GRID_SIZE);
    /* ... row/column/box scans, each `return 1;` on first duplicate ... */
    return 0;
}
```

- `uint8_t` before the name = return type ("this function hands back one byte").
- `(uint8_t idx)` = parameter list with types. `void` means "nothing": `void board_add_mistake(void)` takes and returns nothing.
- Locals (`row, col, …`) are declared at the top (GBDK/SDCC style; also fine on modern `gcc`). They exist only while the function runs.
- `return value;` exits immediately with a value. A function with no `return` on some path returns garbage — `-Wall` warns (`control reaches end of non-void function`). Treat that warning as a bug.

Callers ignore or use the return:

```c
if (board_conflicts(idx)) {   /* use it as a true/false answer */
    /* ... */
}
(void)board_errors;           /* (rare) explicitly ignoring needs a cast; plain calls like */
board_add_mistake();          /* `board_add_mistake();` simply discard void */
```

## 3. Pass-by-value: C copies every argument

```c
#include <stdio.h>

void set_to_zero(uint8_t v) {  /* `v` is a COPY */
    v = 0;
}

int main(void) {
    uint8_t x = 5;
    set_to_zero(x);
    printf("%u\n", x);   /* still 5! */
    return 0;
}
```

> **Python vs C:** Python passes object references (mutating a list inside a function mutates the caller's list). C copies the bytes. To let a function modify the caller's variable you must pass its *address* (a pointer — chapter 06):
>
> ```c
> void reset(uint8_t *v) { *v = 0; }  /* write through the address */
> reset(&x);                          /* now x == 0 */
> ```
>
> This is why `scanf("%d", &guess)` (chapter 02) and `save_read(&slot)` (`main.c:593`) take `&…`: they need to write into the caller's memory.

Repo consequence: `board_set(idx, value)` works because the grid is a **global `static` array**, not a parameter — the function writes directly to shared storage. `marks_set(bm, level)` works on any bitmap because the caller passes the array (arrays decay to addresses, chapter 06).

## 4. `static`: two jobs, one keyword

`static` means "private" or "persistent" depending on where it appears. Both uses are everywhere here.

**4a. `static` on a function or global = private to this file.**

```c
/* src/main.c:152 — only main.c can call wrap_add */
static uint8_t wrap_add(uint8_t v, int8_t d, uint8_t n) { /* ... */ }
```

Non-`static` functions (`board_load`, `ui_select`, …) are the module's public API and are listed in the header. `static` helpers (`wrap_add`, `cell_index`, `preview_update`, `sram_valid`, …) are implementation details. If it is `static`, you can change it freely — no other file depends on it.

**4b. `static` on a local or global variable = lives forever in RAM.**

```c
/* src/board.c:9 — the grid, alive for the whole session */
static uint8_t cells[CELL_COUNT];
static uint8_t origin[CELL_COUNT];
static uint8_t error_count;

/* src/main.c:52 — game state, likewise persistent */
static State state;
static uint16_t level;
static uint8_t cursor_row, cursor_col;
```

Globals without `static` would be visible to the linker across files (risking name clashes). `static` keeps them file-local. Initialisation: `static` storage **starts as zero** — the codebase relies on this (`marks` starts cleared; an explicit `marks_clear` still exists for restarts).

## 5. Scope: where a name is visible

```c
static uint8_t error_count;      /* FILE scope: board.c only */

void board_add_mistake(void) {
    if (error_count < 255) {     /* visible here */
        error_count++;
    }
}

void other(void) {
    uint8_t i;                   /* BLOCK scope: this function only */
    for (i = 0; i < 10; i++) {
        uint8_t tmp = i;         /* LOOP scope: this block only */
        (void)tmp;
    }
    /* `i` visible, `tmp` gone */
}
```

Rules: inner declarations shadow outer ones (avoid it — confusing). A local with the same name as a global hides the global inside that function (also avoid it). Keep names distinct; the repo does (`cells` vs `cell`, `marks` vs `marks_get`).

## 6. The stack: where locals live

Each function call gets a *stack frame*: space for parameters, locals and the return address. Returning frees the frame. Two implications:

1. **Never return the address of a local.** It points at a dead frame:
   ```c
   uint8_t *bad(void) {
       uint8_t x = 5;
       return &x;   /* WRONG: x dies with the return. -Wall warns. */
   }
   ```
   Return the value, or write through a caller-provided pointer, or use `static`/global storage.
2. **Deep recursion and huge local arrays can overflow the tiny Game Boy stack.** The repo never recurses (the puzzle *generator* in Python recurses; the C game only loops) and keeps locals small. `SaveSlot` (~210 bytes) is `static` in `main.c`, not a local — parking it on the stack every frame would be wasteful and risky.

The main-loop state (`state, level, cursor_*, editing, marks, slot, …`) is all `static` for exactly this reason: it must survive across frames, and `main` never returns anyway.

## 7. Reading a new function in 30 seconds

Checklist (try it on `confirm_editing` in `src/main.c:271`):

1. Signature: what goes in, what comes out? (`void` = nothing.)
2. `static`? Private helper or public API?
3. First lines: which case returns early? (Guard clauses from chapter 04.)
4. What shared state does it touch? (`board_set`, `ui_cell`, `board_add_mistake` = grid + screen + counter.)
5. Who calls it? (Search the file: `confirm_editing` is called once, from `game_update` on `J_A`.)

## Exercises

1. Why can `board_get()` be non-`static` (declared in `board.h`) while `cell_index()` is `static` in `board.c`? What breaks if you make `cell_index` non-`static` without adding it to the header? (Nothing breaks — but what do you lose?)
2. `save_read(SaveSlot *slot)` takes a pointer. Explain why it cannot be `save_read(SaveSlot slot)` (pass-by-value) given what this chapter taught you.
3. Find three `static` variables in `src/main.c` that *must* persist across frames. For each, describe the visible bug if it were a local reset every frame.

Next: `06-arrays-strings-pointers.md` — the chapter that unlocks the rest of C.
