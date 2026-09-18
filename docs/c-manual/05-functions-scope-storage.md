# 05 — Functions, scope and storage

Functions are C's only unit of behaviour (no methods, no lambdas in this codebase, no closures). This chapter explains how to declare, define and read them, what `static` really means in both its jobs, how the stack frames calls, why recursion is avoided here, and the 30-second routine for reading any new function.

## 1. Declaration vs definition (the `.h` / `.c` split preview)

```c
/* board.h — DECLARATION: "this function exists, trust me" */
uint8_t board_get(uint8_t idx);

/* board.c — DEFINITION: "here is how it works" */
uint8_t board_get(uint8_t idx) {
    return cells[idx];
}
```

> **Python vs C:** Python `def` does both at once. C splits the promise (header) from the implementation (source) so files can call each other without circular imports and so callers compile without seeing bodies. To know *what a module offers*, read its `.h`. To know *how*, read its `.c`.

A declaration without any definition compiles but **fails at link time** (`undefined reference`). A definition without a prior declaration works but forfeits the compiler's cross-check (mismatched argument counts become silent corruption instead of errors). Chapter 08 covers headers fully; for now remember: every public function in this repo appears in both places with identical signatures, and each `.c` includes its own `.h` first so the compiler verifies the match.

Parameter names in declarations are documentation (`uint8_t board_get(uint8_t idx)` vs `uint8_t board_get(uint8_t)`): the compiler ignores them, readers do not. The repo always names them.

## 2. Anatomy of a function

```c
/* src/board.c:72 */
uint8_t board_conflicts(uint8_t idx) {
    uint8_t row, col, r, c, value;
    uint8_t box_row0, box_col0;

    value = cells[idx];
    if (value == 0) {
        return 0;   /* Empty cell: no conflict possible. */
    }
    row = (uint8_t)(idx / GRID_SIZE);
    col = (uint8_t)(idx % GRID_SIZE);
    /* ... row scan, column scan, box scan (corner precomputed once),
       each `return 1;` on first duplicate ... */
    return 0;
}
```

- `uint8_t` before the name = return type ("this function hands back one byte").
- `(uint8_t idx)` = parameter list with types. `void` means "nothing": `void board_add_mistake(void)` takes and returns nothing. Empty `()` means "unspecified arguments" in C (a fossil — always write `(void)`).
- Locals (`row, col, …`) are declared at the top (GBDK/SDCC style; SDCC historically required declarations before statements, and the habit keeps diffs clean). They exist only while the function runs.
- `return value;` exits immediately with a value. A function with no `return` on some path returns garbage — `-Wall` warns (`control reaches end of non-void function`). Treat that warning as a bug: on hardware the "garbage" is whatever register was left over, i.e. nondeterministic difficulty names or phantom conflicts.
- `return;` (bare) exits a `void` function early — the guard-clause workhorse of chapter 04.

Callers use or ignore the return:

```c
if (board_conflicts(idx)) {   /* use it as a true/false answer */
    /* ... */
}
board_add_mistake();          /* void: nothing to use */
```

Ignoring a *non-void* return is legal but suspicious; `__attribute__((warn_unused_result))` can flag it, though this repo relies on review instead. Ignoring `scanf`'s return, by contrast, is a genuine bug (chapter 02 §7).

## 3. Pass-by-value: C copies every argument

```c
#include <stdio.h>
#include <stdint.h>

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

> **Python vs C:** Python passes object references (mutating a list inside a function mutates the caller's list; rebinding the name does not). C copies the bytes, always. To let a function modify the caller's variable you must pass its *address* (a pointer — chapter 06):
>
> ```c
> void reset(uint8_t *v) { *v = 0; }  /* write through the address */
> reset(&x);                          /* now x == 0 */
> ```
>
> This is why `scanf("%d", &guess)` (chapter 02) and `save_read(&slot)` (`main.c:670`) take `&…`: they need to write into the caller's memory. And it is why `board_restore(values, origins, 2)` passes arrays (which decay to addresses) but the mistake count by value.

Repo consequence: `board_set(idx, value)` works because the grid is a **global `static` array**, not a parameter — the function writes directly to shared storage. `marks_set(bm, level)` works on any bitmap because the caller passes the array (arrays decay to addresses, chapter 06). Two parameter-passing idioms, chosen by size and sharing intent: small inputs by value, shared/output data by address. `const`-qualified addresses (`const uint8_t *values`) add "read-only" to the contract.

Cost model: passing a `uint8_t` costs a byte on the stack; passing a `SaveSlot` by value would copy ~210 bytes per call. Hence `save_read(&slot)` / `save_write(&slot)` — and hence the rule of thumb: structs travel by pointer, scalars by value.

## 4. `static`: two jobs, one keyword

`static` means "private" or "persistent" depending on where it appears. Both uses are everywhere here; confusing them is a rite of passage, so fix the distinction now.

**4a. `static` on a function or file-scope variable = private to this file (internal linkage).**

```c
/* src/main.c:167 — only main.c can call wrap_add */
static uint8_t wrap_add(uint8_t v, int8_t d, uint8_t n) { /* ... */ }
```

Non-`static` functions (`board_load`, `ui_select`, …) are the module's public API, listed in the header, visible to the linker. `static` helpers (`wrap_add`, `cell_index`, `preview_update`, `preview_erase`, `sram_valid`, `sram_read`, `marker_pair_set`, `try_place_digit`, …) are implementation details. If it is `static`, you can change it freely — no other file depends on it. If it is public, changing the signature means updating the header plus every caller.

The linker enforces this: two files may each define their own `static uint8_t i;` or `static void helper(void)` with zero conflict — they are different entities that happen to share a name. Two files defining *non-static* `helper` is a `multiple definition` link error (chapter 09 §2).

**4b. `static` storage duration = lives forever in RAM (zero-initialised).**

```c
/* src/board.c — the live game, 81 + 81 + 1 bytes, zeroed at boot */
static uint8_t cells[CELL_COUNT];
static uint8_t cell_origin[CELL_COUNT];
static uint8_t error_count;

/* src/main.c — game state, likewise persistent (grouped in structs) */
static State state;
static uint16_t level;
static Cursor cursor;        /* row/col/entry/editing */
static Preview pv;           /* blink tracker */
static Pending pend;         /* deferred win/save screens */
static uint8_t marks[MARKS_BYTES];
static SaveSlot slot;
```

File-scope variables are *always* static-duration (the `static` keyword there controls visibility, §4a). Block-scope `static` is the interesting case:

```c
void counter(void) {
    static uint8_t calls = 0;  /* initialised ONCE, retains value between calls */
    calls++;
}
```

versus a plain local, reborn garbage on every entry. The codebase relies on zero-initialisation of statics (`state` starts `ST_DIFF == 0`; `error_count` starts 0) — and still calls `marks_clear(marks)` plus explicit field resets at boot, because "starts zeroed" covers boot but not restart-after-win. Belt and suspenders: trust the language for boot, reset explicitly for replay.

`extern` (preview of chapter 08) is `static`'s mirror: "this name lives in *another* file" (`extern const Puzzle puzzles[LEVEL_COUNT]`). `static` = mine alone; `extern` = someone else's, shared.

## 5. Scope: where a name is visible

Four scopes, smallest first:

```c
static uint8_t error_count;      /* FILE scope: board.c only */

void board_add_mistake(void) {
    if (error_count < 255) {     /* visible here (file scope) */
        error_count++;
    }
}

uint8_t marks_count(const uint8_t *bm, uint16_t first, uint16_t n) {
    uint16_t level, count;       /* BLOCK scope: this function */
    count = 0;
    for (level = first; level < (uint16_t)(first + n); level++) {
        count += marks_get(bm, level);
    }
    return (uint8_t)count;
}
```

- **Block scope**: locals, visible from declaration to closing brace. Loop-body variables die each iteration (a fresh `tmp` per pass — and its address must never escape).
- **Function scope**: labels (`goto` targets) only — irrelevant here since `goto` is unused.
- **File scope**: `static` globals/helpers — the file's private attic.
- **Function prototype scope**: parameter names inside declarations — visible nowhere else.

Shadowing (inner declaration hiding an outer one) compiles and confuses:

```c
static uint8_t level;   /* file scope */
void f(void) {
    uint8_t level = 0;  /* shadows the global inside f — legal, avoid it */
}
```

`-Wshadow` flags it (not enabled by `-Wall`; enable explicitly for cleanup passes). The repo avoids shadowing entirely — names stay distinct (`cells` vs `cell`, `marks` vs `marks_get`, `slot` vs `SaveSlot`).

## 6. The stack: where calls and locals live

Each function call gets a *stack frame*: parameters, return address, locals. Call pushes, return pops — last-in-first-out, hence "stack". Two implications with teeth on small machines:

1. **Never return the address of a local.** It points at a dead frame:
   ```c
   uint8_t *bad(void) {
       uint8_t x = 5;
       return &x;   /* WRONG: x dies with the return. -Wall warns. */
   }
   ```
   Return the value, or write through a caller-provided pointer, or use `static`/global storage. (`difficulty_name` returns pointers to *string literals*, which live in ROM forever — safe precisely because they are not locals.)
2. **Deep recursion and huge local arrays can overflow the tiny Game Boy stack.** The repo never recurses (the puzzle *generator* in Python recurses while solving; the C game only loops) and keeps locals small — a conflicts scan holds 5 bytes of locals. `SaveSlot` (~210 bytes) is `static` in `main.c`, never a local: parking it on the stack every frame would risk overflow *and* waste cycles copying it per call.

A recursion sketch, to show what is being refused and why it is fine to refuse:

```c
/* Hypothetical recursive cell counter — correct, but each level costs a frame. */
uint8_t count_filled_rec(uint8_t i) {
    if (i == CELL_COUNT) return 0;
    return (uint8_t)((cells[i] != 0) + count_filled_rec((uint8_t)(i + 1)));
}
/* Repo style: the same in one flat loop, one frame, obvious bounds. */
uint8_t count_filled(void) {
    uint8_t i, n = 0;
    for (i = 0; i < CELL_COUNT; i++) n += (cells[i] != 0);
    return n;
}
```

81 nested frames × (return address + locals) would work on PC and flirt with the Game Boy's stack. Iteration is not just taste here; it is budget. The main-loop state (`state, level, cursor, pv, pend, marks, slot, …`) is all `static` for the same reason it is file-scoped: it must survive across frames, `main` never returns, and threading 10+ variables through every handler would obscure the logic for no benefit on a single-threaded game. File-`static` state with small handlers is the pragmatic C equivalent of a Python object's `self.*` attributes.

Call-depth audit of the game: `main → handler → board/ui helper`, two levels, bounded locals. That sentence is the whole stack-safety argument, and any change adding depth (callbacks, recursion, big locals) must re-make it.

## 7. Reading a new function in 30 seconds

Checklist (try it on `try_place_digit`/`confirm_editing` in `src/main.c:307`, then on `sram_valid` in `src/save.c:47`):

1. Signature: what goes in, what comes out? (`void` = nothing. Pointer = shared/output data.)
2. `static`? Private helper (free to change) or public API (header contract)?
3. First lines: which case returns early? (Guard clauses from chapter 04 — the function's preconditions, stated as code.)
4. What shared state does it touch? (`board_set` + `ui_cell` + `board_add_mistake` = grid + screen + counter. `sram_valid` touches only the SRAM window — pure query.)
5. Who calls it, and how often? (Search the file: `confirm_editing` runs on `J_A` in pick mode; `preview_update` runs *every frame* — per-frame code must stay tiny, and it does: a few compares plus at most one tile write.)

Frequency awareness is the senior skill this checklist builds: init code can afford loops over 300 levels (host tests do), per-frame code must cost ~nothing, and interrupt-time code (none here — no custom ISRs) would have to cost less than nothing. When you add code, first ask "how often does this run?" — the answer sets your complexity budget.

Next: `06-arrays-strings-pointers.md` — the chapter that unlocks the rest of C.
