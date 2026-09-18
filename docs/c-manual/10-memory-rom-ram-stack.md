# 10 — Memory: stack, RAM, ROM, SRAM, VRAM (and the heap you will not use)

Python has "memory". C has address spaces, each with its own size, lifetime, rules and failure modes — plus a heap this project deliberately refuses. This chapter maps all six using the game's real data, adds `volatile` (hardware registers), endianness (the save's level bytes), and the `malloc` literacy you need everywhere else.

## 1. The map (Game Boy numbers, PC analogues)

| Memory | Size (GB) | Lives | Holds in this game | Written? |
|--------|-----------|-------|--------------------|----------|
| Stack | ~hundreds of bytes (inside WRAM) | one function call | locals, parameters, return addresses | constantly |
| WRAM (work RAM) | 8 KB | whole session (cleared on boot/reset) | `cells[81]`, `cell_origin[81]`, `marks[38]`, `slot`, cursor, state | freely |
| ROM (cartridge) | 32 KB total | forever (factory-pressed) | code + `puzzles[300]` (15.6 KB) + tile art + font | never at runtime |
| SRAM (battery RAM) | 8 KB at `0xA000` | years (battery) | one `SaveSlot` (~0xD2 = 210 bytes) | via MBC latch only |
| VRAM / OAM | 8 KB video + 160 B sprites | per frame | tile patterns, two 32×32 maps, 40 sprites | via GBDK helpers / shadow OAM |
| Heap | — (unused) | — | nothing (no `malloc` anywhere) | n/a |

On your PC the numbers are gigabytes and only stack/heap/static matter. On the Game Boy every byte is budgeted: the ROM holds ~28 KB of 32 KB (`COMPACT.md`), WRAM holds a few hundred bytes of live state out of 8192. `sizeof` is a design tool here, not trivia — §7 works the full budget.

## 2. Stack: locals and call frames

```c
uint8_t board_conflicts(uint8_t idx) {
    uint8_t row, col, r, c, value;  /* live on the stack, dead at return */
    /* ... */
}
```

Calling a function pushes parameters, the return address and locals; returning pops them. Draw one frame of `board_conflicts(47)`:

```text
high addresses
  ... caller (confirm_editing) frame: idx, old ...
  return address -> back into confirm_editing
  idx = 47 (parameter copy)
  row, col, r, c, value (locals, garbage until assigned)
low addresses  <- stack pointer
```

Rules with teeth on small machines:

- **Never return `&local`.** The frame dies with the return; the pointer dangles (`-Wall` warns: `function returns address of local variable`). The value may *look* right until the next call reuses the space — Heisenbugs (chapter 06 §7).
- **Keep locals small.** `SaveSlot` (~210 bytes) is `static` in `main.c`, never a local — parking it on the stack each frame would risk overflow and waste cycles.
- **No recursion here.** The Python *generator* recurses (solving); the C *game* only loops (chapter 05 §6 shows the rejected shape). Deep call chains on a tiny stack are a crash vector, so handlers stay shallow: `main → handler → board/ui helper`, two levels. That sentence is the stack-safety argument; anything adding depth must re-make it.

> **Python vs C:** Python frames hold objects and a traceback; infinite recursion gives `RecursionError` with a stack dump. C frames hold raw bytes; stack overflow corrupts neighbouring memory silently. Iterative loops over 81 cells are the house style for a reason, and `while (1)` belongs only to the one true main loop.

## 3. WRAM: `static` state that survives frames

```c
/* src/board.c — the live game, 81 + 81 + 1 bytes, zeroed at boot */
static uint8_t cells[CELL_COUNT];
static uint8_t cell_origin[CELL_COUNT];
static uint8_t error_count;

/* src/main.c — session state (grouped): state, level, Cursor, Preview,
 * Pending, marks[38], slot, ... */
static State state;
static uint16_t level;
static uint8_t marks[MARKS_BYTES];   /* 38 bytes, one bit per level */
static SaveSlot slot;                /* ~210-byte working copy of the save */
```

`static` storage is allocated once at boot (and zero-initialised) and lives until power-off or soft reset (`jp 0x0100` clears RAM like a power cycle while SRAM survives — that asymmetry *is* the save feature, §5). Boot additionally calls `marks_clear(marks)` plus explicit field resets rather than relying on zero-init alone.

Why not locals in `main` passed everywhere? Threading 10+ state variables through every handler signature would obscure the logic for no benefit on a single-threaded game. File-`static` state with small handler functions is the pragmatic C equivalent of a Python object's `self.*` attributes — with the linker enforcing the privacy (`static` = this file only, chapter 05 §4).

## 4. ROM: `const` data baked into the cartridge

```c
extern const Puzzle puzzles[LEVEL_COUNT];   /* puzzles.h — lives in ROM */
static const uint8_t MAGIC[4] = {'S','U','D','K'};  /* save.c — ROM literal */
```

`const` at file scope (plus string literals like `"EASY"`, `"?????"`) goes into ROM: readable, never writable. Writing through a `const`-stripped pointer is undefined behaviour — on hardware, a bus write to ROM that silently does nothing (or worse). The `const` in `const uint8_t *marks` / `const char *s` is the compiler enforcing "read-only" at every call site, including refusing to pass ROM data to `marks_set`'s mutable `uint8_t *` (which would be a hardware fault).

Budget thinking: 300 levels × 52 bytes = 15 600 bytes of ROM — over half the cartridge. The old text format (~165 B/level ≈ 49 KB) could never fit; nibble-packing (chapter 07 §3) bought the product. `make check` asserts the total stays exactly 32 768 bytes, so every future feature pays ROM rent — a float `printf` here, a lookup table there, and the build breaks loudly instead of overflowing silently.

## 5. SRAM: the battery save (latch, layout, guards)

The cartridge adds 8 KB of RAM kept alive by a battery — but the Game Boy cannot just write there. An MBC1 latch gates access, and GBDK names the two operations (`src/save.c`):

```c
ENABLE_RAM;    /* open the latch: SRAM visible at 0xA000 */
SRAM[off] = …; /* read/write bytes */
DISABLE_RAM;   /* close it: stray writes cannot corrupt the save */
```

Every `save_*` function opens, works, and closes around the access — so a crash mid-frame cannot silently scribble on the slot through a stale mapping. Forgetting `DISABLE_RAM` leaves the save exposed to every stray pointer write thereafter; the open-work-close discipline makes the window explicit and minimal. The pointer itself is a cast integer (`#define SRAM ((uint8_t *)0xA000)`, chapter 06 §3 use 2): address `0xA000` treated as byte array.

The on-wire layout is fixed offsets (`src/save_format.h: SAVE_OFF_*`), completely independent of the compiler's struct layout (chapter 07 §1 padding discussion). The checksum (`save_checksum`) and field validation (`save_fields_valid`) are hardware-free and host-tested; `src/save.c` only moves bytes through the latch:

```text
0x00 'S''U''D''K' magic   0x59 origins[81]
0x04 0x01 version         0xAA mistakes
0x05 game_active          0xAB marks[38]
0x06 level u16 lo/hi      0xD1 checksum
0x08 values[81]           0xD2 end (210 bytes used of 8192)
```

Field I/O is byte-explicit (`sram_read`/`sram_write` loops; level split into low/high bytes §6). Four guards, four failure modes handled:

1. **Magic `SUDK`** — first boot SRAM is garbage; wrong magic ⇒ no save, boot menu hides LOAD.
2. **Version `0x01`** (`SAVE_VERSION`) — future formats will not be misread as current ones (bump on any layout change, old saves cleanly ignored).
3. **Checksum** (8-bit sum of all preceding bytes, wrapping — the *wanted* overflow from chapter 03 §8) — dead battery / wrong cartridge / half-written slot ⇒ mismatch ⇒ ignored.
4. **Field ranges** (`save_fields_valid`: level < 300, active 0/1, digits 0-9, origins 0-2) — a checksum-passing foreign slot still cannot jump outside the level table.

`sram_valid` checks magic + version + checksum with SRAM enabled; `save_read` additionally validates ranges and rejects garbage; `save_present`/`save_read` wrap everything in open/close; `save_write` recomputes the checksum *last*, over the bytes just written (re-read from SRAM, so it covers what landed). `save_store(1)` on SAVE writes `game_active = 1` (resumable); every win rewrites with `game_active = 0` (`marks_set` first) so completions persist even without explicit save. Boot does `has_save = save_read(&slot)` once; LOAD re-reads SRAM to see the latest save; `board_restore()` copies values + origins + mistakes back — same layout in RAM and SRAM, zero conversion, PC-testable (`tests/test_host.c` round-trips it without any hardware, plus a layout + validation test).

> **Python vs C:** Python `pickle.dump(obj, open("save.dat","wb"))` handles format, versioning and errors opaquely — and opaquely breaks across versions. Here every byte offset, the checksum loop and the latch discipline are handwritten and commented — because on hardware there is no filesystem, no exceptions, and a dead battery must degrade to "LOAD hidden", never to a crash. The explicitness *is* the robustness.

## 6. Endianness: why the level is two bytes, low first

```c
slot->level = (uint16_t)(SRAM[SAVE_OFF_LEVEL_LO] | (SRAM[SAVE_OFF_LEVEL_HI] << 8));
/* ... and on write: */
lo = (uint8_t)(slot->level & 0xFF);
hi = (uint8_t)(slot->level >> 8);
```

A `uint16_t` is two bytes; *endianness* is which order they are stored in. The save format is **little-endian** (low byte at the lower address — the LR35902's native order): level 300 = `0x012C` → `LO=0x2C` at `0x06`, `HI=0x01` at `0x07`. Hand-splitting (mask + shift) instead of `memcpy`-ing the `uint16_t` makes the format independent of host byte order — the same reason offsets are hand-listed. Reading a foreign save (or a network packet, or a ROM header) always means: know the order, shift the high part, OR the low part. `make check`'s Python does the mirror operation when it parses header bytes.

## 7. The full WRAM/ROM budget (worked)

WRAM (8 192 bytes total):

```text
cells      81
origin     81
marks      38
slot      ~210  (SaveSlot working copy)
cursor/state/scratch   tens of bytes (input prev/just_pressed/repeat, pv_* trackers, menu vars)
stack      hundreds worst-case (2-level calls, tiny locals)
TOTAL     ~500 bytes  ≈ 6% of WRAM — comfortable, and every entry is sizeof-visible.
```

ROM (32 768 bytes total, ~28 KB used per `COMPACT.md`):

```text
puzzles   15 600  (300 × 52)
tiles      ~3 700  (234 tiles × 16 bytes)
font       ~1 500  (96 tiles × 16 bytes)
code       ~7 000  (the rest: logic + screens + GBDK lib slice actually referenced)
TOTAL     ~28 KB   ≈ 4 KB slack — every new feature spends it; make check collects.
```

Unused SRAM (8 192 − 210) and unused WRAM (~7.5 KB) are headroom, not waste: they bound future features (more marks? second slot? undo buffer?) before anyone writes code. Budgets first, features second — embedded planning in one sentence.

## 8. The heap and `malloc`: literacy for everywhere else

Python allocates constantly (`append`, slicing, objects). C's heap would allow it — and this codebase refuses, deliberately:

```c
/* Elsewhere-style heap (NOT in this repo — recognise the shape): */
uint8_t *buf = (uint8_t *)malloc(81);
if (!buf) { /* handle out-of-memory */ }
free(buf); buf = NULL;   /* free exactly once, null the pointer */
```

The four heap rules (for codebases that use it): check `malloc`'s return (`NULL` = exhausted), `free` exactly once, never touch after `free` (use-after-free), never lose the last pointer before freeing (leak). Fragmentation (free blocks too scattered for big requests), double-free corruption, and forgotten frees are the tax. On 8 KB WRAM with no virtual memory and minimal SDCC heap support, the tax exceeds any benefit — and every size here is known at compile time (81 cells, 38 mark bytes, 300 levels), so fixed arrays are simpler, provable, and `sizeof`-visible (`PLAN.md` risk table: fixed arrays, `uint8_t`, no `malloc`).

Rule, portable beyond this project: **if the maximum size is known, declare it**. The whole game is the proof that non-trivial software fits that discipline — and when you do need the heap elsewhere, isolate ownership (who allocates, who frees, exactly once) in one module, the way this repo isolates SRAM behind `save.c`.

## 9. `volatile`: memory that changes behind your back

```c
/* Hardware registers (pattern — GBDK wraps these for you): */
#define LCDC_REG (*(volatile uint8_t *)0xFF40)
```

`volatile` tells the compiler "this address may change without any visible store (hardware, interrupts) — reload it every time, never optimise away or reorder accesses". Without it, `while (LCDC_REG & 0x80);` could be optimised into an infinite loop on a cached first read. You never write `volatile` in this codebase because GBDK's helpers (`vsync`, `set_bkg_*`, `move_sprite`, `joypad`) already contain it — but every register those helpers touch is volatile underneath, and any bare-metal C you meet later leads with it. `const volatile` (read-only hardware status) completes the pair: the program cannot write it, the hardware can.

Next: `11-gameboy-gbdk-hardware.md` — the 10 hardware ideas behind every `ui_*` call.
