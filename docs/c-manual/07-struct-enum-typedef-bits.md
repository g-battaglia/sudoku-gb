# 07 — struct, enum, typedef and bits

C groups values with `struct`, names small sets with `enum`, shortens names with `typedef`, and packs flags and digits with bit operations. The level format, the save slot and the completion bitmap are all built from these tools — this chapter also covers layout/padding, the full set/clear/toggle/extract idioms, and hex fluency.

## 1. `struct`: several values under one name

> **Python vs C:** a `struct` is a frozen `dataclass`/`NamedTuple` with fixed types, fixed order, no methods, no defaults — and a *memory layout* you can draw.

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

`save_read(&slot)` / `save_write(&slot)` pass the *address* of one `SaveSlot` so ~210 bytes are not copied per call. Inside, `->` reads through the address. `(*s).level` means the same as `s->level`; everyone writes the arrow form. Nesting works the same way: `puzzles[level].solution[idx >> 1]` is struct member (array) indexed — dot-then-bracket, read left to right.

Initialisation and assignment:

```c
Puzzle p = {{0}, {0}};        /* zero everything (nested braces per member) */
SaveSlot s;
s = (SaveSlot){0};            /* compound literal: zero-assign in one go (C99+) */
s.level = 12;                 /* member assignment copies the bytes */
```

Structs assign by value (`a = b` copies all members) but do not compare (`a == b` is an error — compare member by member, or `memcmp` for byte-exact equality). Functions can return structs by value, but the repo passes struct *pointers* for anything large (chapter 05 §3 cost model).

Size and layout — draw the bytes:

```text
Puzzle (52 bytes, no gaps: all members are 1-byte arrays):
  offset 0..40   solution[41]
  offset 41..51  givens_mask[11]

SaveSlot (~210 bytes; uint16_t level wants even alignment):
  offset 0        game_active
  offset 1        (padding: 1 byte so level starts even)
  offset 2..3     level
  offset 4..84    values[81]
  offset 85..165  origins[81]
  offset 166      mistakes
  offset 167..204 marks[38]
```

**Padding and alignment** are the reason for the sketch: CPUs prefer multi-byte members at even addresses, so the compiler may insert invisible padding bytes. `sizeof(SaveSlot)` can therefore exceed the member sum — which is exactly why `src/save.c` does *not* `memcpy` the struct to SRAM but writes each field at hand-listed offsets (§5 chapter 10). Struct layout is compiler- and platform-dependent; the wire format is hand-pinned. Rule: `memcpy` structs only within one build, never across builds, files, or machines. Reordering fields would likewise shift the layout — the save-format comments forbid it.

## 2. `enum`: named small sets

```c
/* src/main.c:34 — game states */
typedef enum { ST_DIFF, ST_SELECT, ST_GAME, ST_PAUSE, ST_WIN, ST_SAVED } State;

/* src/puzzles.h:24 — difficulties (order is significant!) */
typedef enum { DIFF_EASY = 0, DIFF_MEDIUM = 1, DIFF_HARD = 2 } Difficulty;
```

Enums are named integers (`ST_DIFF == 0`, `ST_SELECT == 1`, …), auto-numbered from 0 unless assigned. Their value over bare numbers: readability (`case ST_GAME:` vs `case 2:`), debugger symbols, and compiler auditing — switching over an enum without `default` lets `-Wall` warn about unhandled enumerators, i.e. the compiler checks your state coverage (chapter 04 §2). The comment "order is significant" is load-bearing: difficulty is computed as `level / DIFF_LEVELS`, so `DIFF_EASY` must be 0 and the sequence gapless.

`typedef` gives a nickname: `typedef enum {…} State;` lets you write `State state;` instead of `enum state_t state;`. `uint8_t` itself is a typedef (for `unsigned char` on most platforms). Typedefs do not create new types — only shorter names. Do not confuse with `#define` (text substitution, no type) — `typedef uint8_t byte;` respects scope and debugging; `#define byte uint8_t` does not.

Cell origins are `#define`, not `enum`, deliberately (`src/board.h:35`):

```c
#define ORIGIN_PLAYER 0 /* gray, editable */
#define ORIGIN_GIVEN 1  /* black, locked clue */
#define ORIGIN_HINT 2   /* gray, locked reveal */
```

Why not an enum? The codes are stored in SRAM (81 bytes per save), so stability and width matter more than type nicety — and SDCC treats enums as `int` (2 bytes), doubling the storage for zero benefit. One `origin[81]` array encodes both shade and lock: `board_is_original = (origin == GIVEN)`, `board_is_locked = (origin != PLAYER)`. A single source of truth replaces two boolean arrays and saves 81 WRAM bytes (`COMPACT.md` §4.4). When the domain is "tiny codes stored in bulk", `#define` wins; when it is "states switched in logic", `enum` wins. The repo uses both, each where it fits.

## 3. Bits: the six operators, fluently

| Op | Name | Example | Result | Reads as |
|----|------|---------|--------|----------|
| `<<` | shift left | `1u << 3` | `0b1000` (8) | "bit 3" |
| `>>` | shift right | `43 >> 3` | `5` (`43/8`) | "divide by 8, drop remainder" |
| `&` | AND | `43 & 7` | `3` (`43%8`) | "low 3 bits" |
| `\|` | OR | `a \| 0x04` | bit 2 set | "add this flag" |
| `^` | XOR | `a ^ 0xFF` | all 8 bits flipped | "toggle" |
| `~` | NOT | `~prev` | every bit flipped | "everything except" |

Shifts on unsigned types multiply/divide by powers of two (`x << 3` ≡ `x*8`, exact, fast on 8-bit CPUs — the repo prefers `>> 3`/`& 7` over `/8`/`%8` partly for speed, partly for idiom). Shifts that overflow the width or shift by ≥ width are undefined — `1u << 8` into a `uint8_t` context is a classic truncation bug, hence the explicit `(uint8_t)` casts after intentional narrowing.

The four bit idioms (memorise all four — they cover nearly every flag/bitmap in C):

```c
/* TEST bit k: */   (byte >> k) & 1;
/* SET bit k:   */  byte |= (1u << k);
/* CLEAR bit k: */  byte &= (uint8_t)~(1u << k);
/* TOGGLE bit k:*/  byte ^= (1u << k);
```

Repo sightings of each: TEST in `marks_get` and `puzzle_given` (mask check); SET in `marks_set`; CLEAR in cursor-sprite parking logic (bits cleared to hide); TOGGLE in the blink phase (`phase = (frame >> 5) & 1` tests bit 5 of the frame counter — the whole preview blink is one TEST idiom on a free-running counter, `src/main.c:136`).

Three worked idioms from the codebase:

**Idiom A — bit `k` of a bitmap** (`marks_*`, `puzzle_given`):

```c
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

Digits 1–9 fit in 4 bits (0–15), so two digits share one byte: 81 digits → 41 bytes (40.5 rounded up). Saving ~40 bytes/level × 300 levels ≈ 12 KB — the difference between fitting 300 levels in 32 KB ROM and not fitting (`puzzles.h` header comment). Packing is: `packed = (hi << 4) | lo` (verified in the writing checks: 7,3 → `0x73`); unpacking is shift-and-mask per parity. Bit-packing is not cleverness here; it is the feature that makes the product possible.

**Idiom C — edge detection** (`input.c:48`):

```c
just_pressed = (uint8_t)(now & (uint8_t)~prev_state);
```

`~prev` flips last frame's buttons; AND with `now` keeps only buttons held now *but not before* = newly pressed. `combo_fire` extends it: all four of A+B+START+SELECT held now, but not all held before = the chord *became* complete this frame. One line of bits replaces any event system (chapter 11 §9 re-tells it from the hardware side).

## 4. Masks, hex and binary fluency

`0x` = hexadecimal (base 16). One hex digit = exactly 4 bits = one nibble, so hex *shows* the bits — the reason addresses, opcodes and masks are written in hex by universal convention:

```text
0x0F = 0b00001111   low nibble mask (b & 0x0F keeps the low digit)
0xF0 = 0b11110000   high nibble mask
0xFF = 0b11111111   all 8 bits / sentinel "none" (pv_row = 0xFF)
0xA000              SRAM address (addresses in hex, always)
0x104               ROM logo offset (make check reads d[0x104:0x134])
```

`1u << (level & 7)` uses `1u` (unsigned int literal) rather than bare `1` so the shift never touches the sign bit (shifting into the sign bit of a signed int is undefined); the cast `(uint8_t)` then narrows explicitly. Suffixes: `1u` unsigned, `1L` long, `0xFF` int-valued hex (narrowed on assignment with a possible warning if it does not fit — another warning to read, not silence). Pedantic, portable, warning-free — the house style, and now you know each piece earns its place.

Converting on sight: each hex digit maps to 4 bits (`0x73` → `0111 0011` → digits 7 and 3 — the nibble example from §3). Practice until `0xD2` reads as "210, bit pattern 1101 0010" without a calculator; save-slot sizes (`0xD2` end offset) and LCDC values (`0x93`) are discussed in exactly these terms in chapters 10–11.

## 5. Worked example: unpack one cell by hand

Level data: `solution = {0x12, …}`, `givens_mask = {0b00000011, …}`.

- Cell 0: byte `0 >> 1 = 0` → `0x12`, even → high nibble `0x12 >> 4 = 1`. Solution digit 1. Mask bit 0 of byte 0 → `0b00000011 & 0b00000001` = set → given. `puzzle_given = 1`.
- Cell 1: same byte `0x12`, odd → `0x12 & 0x0F = 2`. Solution digit 2. Mask bit 1 → set → given 2.
- Cell 2: mask bit 2 → clear → empty (`puzzle_given = 0`), solution still available for HINT via `puzzle_solution`.

HINT (`main.c:471`) is exactly this machinery: read the hidden solution digit from ROM nibbles, `board_set` it, `board_reveal` (lock as `ORIGIN_HINT`), redraw. The solution was in ROM all along; the mask decides what the player sees at load.

Bit-fields (`struct { unsigned a:4, b:4; }`) exist in C for the same nibble job but are avoided here: their layout is implementation-defined (compiler chooses packing order), which would silently break the ROM format under a different SDCC version. Hand shifts are verbose and exact — portability over brevity, again.

Next: `08-preprocessor-headers.md` — the text-processing stage before compilation.
