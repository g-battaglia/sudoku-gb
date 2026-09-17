# 07 — struct, enum, typedef and bits

C groups values with `struct`, names small sets with `enum`, and packs flags/digits with bit operations. The level format, the save slot and the completion bitmap are all built from these three tools.

## 1. `struct`: several values under one name

> **Python vs C:** a `struct` is a frozen `dataclass`/`NamedTuple` with fixed types and no methods.

```c
/* src/puzzles.h:40 — one level, 52 bytes */
typedef struct {
    uint8_t solution[41];        /* 81 digits 1-9, 4 bits each */
    uint8_t givens_mask[11];     /* bit i = cell i is a given */
} Puzzle;
```

```c
/* src/save.h:20 — everything worth persisting, one object */
typedef struct {
    uint8_t game_active;
    uint16_t level;
    uint8_t values[81];
    uint8_t origins[81];
    uint8_t mistakes;
    uint8_t marks[38];
} SaveSlot;
```

Access with `.` (direct) or `->` (through a pointer):

```c
SaveSlot slot;
slot.level = 5;               /* struct variable: dot */
slot.marks[0] = 0;

void save_write(const SaveSlot *s) {
    /* s is a POINTER: arrow */
    uint8_t a = s->game_active;
    uint16_t l = s->level;
}
```

`save_read(&slot)` / `save_write(&slot)` pass the *address* of one `SaveSlot` so 200+ bytes are not copied. Inside, `->` reads through the address. `(*s).level` means the same as `s->level`; everyone writes the arrow form.

Size matters: `sizeof(Puzzle)` is 52, `sizeof(SaveSlot)` is ~210 (0xD2). The SRAM map in `src/save.c:13` lists every field's offset by hand — the struct layout *is* the save format, so reordering fields would corrupt old saves. Comments in `save.h` warn that `ORIGIN_*` numbering is part of the format too.

## 2. `enum`: named small sets

```c
/* src/main.c:34 — game states */
typedef enum { ST_DIFF, ST_SELECT, ST_GAME, ST_PAUSE, ST_WIN, ST_SAVED } State;

/* src/puzzles.h:24 — difficulties (order is significant!) */
typedef enum { DIFF_EASY = 0, DIFF_MEDIUM = 1, DIFF_HARD = 2 } Difficulty;
```

Enums are just named integers (`ST_DIFF == 0`, …). Their value: readability (`case ST_GAME:` vs `case 2:`) plus compiler checking in `switch` (with `-Wall`, a missing case can warn). The comment "order is significant" is load-bearing: difficulty is computed as `level / DIFF_LEVELS`, so `DIFF_EASY` must be 0.

`typedef` gives a nickname: `typedef enum {…} State;` lets you write `State state;` instead of `enum state_t state;`. `uint8_t` itself is a typedef (for `unsigned char`). Typedefs do not create new types — only shorter names.

Cell origins are `#define`, not `enum`, deliberately (`src/board.h:35`):

```c
#define ORIGIN_PLAYER 0 /* gray, editable */
#define ORIGIN_GIVEN 1  /* black, locked clue */
#define ORIGIN_HINT 2   /* gray, locked reveal */
```

Why not an enum? The codes are stored in SRAM, so stability matters more than type nicety, and SDCC treats enums as `int` (2 bytes) — wasteful for 81 stored bytes. One `origin[81]` array encodes both shade and lock: `board_is_original = (origin == GIVEN)`, `board_is_locked = (origin != PLAYER)`. A single source of truth replaces two boolean arrays and saves 81 WRAM bytes (`COMPACT.md` §4.4).

## 3. Bits: the six operators

| Op | Name | Example | Result |
|----|------|---------|--------|
| `<<` | shift left | `1u << 3` | `0b1000` (8) |
| `>>` | shift right | `43 >> 3` | `5` (`/8`) |
| `&` | AND | `43 & 7` | `3` (`%8`) |
| `\|` | OR | `a \| 0x04` | set bit 2 |
| `^` | XOR | `a ^ 0xFF` | flip all 8 bits |
| `~` | NOT | `~prev` | flip; `now & ~prev` = newly pressed |

Two idioms cover 90% of the repo:

**Idiom A — bit `k` of a bitmap** (`marks_*`, `puzzle_given`):

```c
/* set / test bit `level` */
bm[level >> 3] |= (uint8_t)(1u << (level & 7));          /* set */
(bm[level >> 3] >> (level & 7)) & 1;                     /* get */
```

`>> 3` selects the byte (`/8`), `& 7` the position inside it (`%8`), `1u << k` builds a one-hot mask, `|=` sets the bit without touching neighbours. `tests/test_host.c:213` sets bits 7 and 8 (straddling bytes 0/1) and asserts neighbours 6 and 9 stay clear — the test that proves no leaking.

**Idiom B — nibbles (half-bytes)** (`puzzle_solution`, `src/puzzles.c:9`):

```c
b = puzzles[level].solution[idx >> 1];  /* byte holds TWO digits */
if ((idx & 1) == 0) return (uint8_t)(b >> 4);  /* even cell: high nibble */
return (uint8_t)(b & 0x0F);                    /* odd cell: low nibble */
```

Digits 1–9 fit in 4 bits (0–15), so two digits share one byte: 81 digits → 41 bytes (40.5 rounded up). Saving 40 bytes/level × 300 levels = ~12 KB saved — the difference between fitting 300 levels in 32 KB ROM and not fitting (`puzzles.h` header comment). Bit-packing is not cleverness here; it is the feature that makes the product possible.

**Idiom C — edge detection** (`input.c:48`):

```c
just_pressed = (uint8_t)(now & (uint8_t)~prev_state);
```

`~prev` flips last frame's buttons; AND with `now` keeps only buttons held now *but not before* = newly pressed. `combo_fire` extends it: all four of A+B+START+SELECT held now, but not all held before = the combo *became* complete this frame. One line of bits replaces any event system.

## 4. Masks, hex and why `0x` is everywhere

`0x` = hexadecimal (base 16). One hex digit = exactly 4 bits = one nibble, so hex *shows* the bits:

```text
0x0F = 0b00001111   low nibble mask
0xF0 = 0b11110000   high nibble mask
0xFF = 0b11111111   all 8 bits / sentinel "none" (pv_row = 0xFF)
0xA000              SRAM address (convention: addresses in hex)
```

`1u << (level & 7)` uses `1u` (unsigned int literal) rather than `1` so the shift never hits sign-bit undefined behaviour; the cast `(uint8_t)` then truncates explicitly. Pedantic, portable, warning-free — the house style.

## 5. Worked example: unpack one cell by hand

Level data: `solution = {0x12, …}`, `givens_mask = {0b00000011, …}`.

- Cell 0: byte `0 >> 1 = 0` → `0x12`, even → high nibble `0x12 >> 4 = 1`. Solution digit 1. Mask bit 0 of byte 0 → `0b00000011 & 0b00000001` = set → given. `puzzle_given = 1`.
- Cell 1: same byte `0x12`, odd → `0x12 & 0x0F = 2`. Solution digit 2. Mask bit 1 → set → given 2.
- Cell 2: mask bit 2 → clear → empty (`puzzle_given = 0`), solution still available for HINT via `puzzle_solution`.

HINT (`main.c:471`) is exactly this: read the hidden solution digit, `board_set` it, `board_reveal` (lock as `ORIGIN_HINT`), redraw. The solution was in ROM all along.

## Exercises

1. `MARKS_BYTES` is `((300 + 7) / 8)`. Compute it by hand. Why `+7`? (Hint: round up integer division.)
2. Pack two digits `7` and `3` into one byte by hand (high/low nibble). Then unpack `0x53` the same way `puzzle_solution` does.
3. `just_pressed = now & ~prev` with `now = 0b00000101`, `prev = 0b00000001`: compute `just_pressed` bit by bit. Which button was newly pressed?

Next: `08-preprocessor-headers.md` — the text-processing stage before compilation.
