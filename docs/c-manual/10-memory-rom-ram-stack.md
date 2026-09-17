# 10 — Memory: stack, RAM, ROM, SRAM, VRAM

Python has "memory". C has five memories, each with its own size, lifetime and rules. This chapter maps them using the game's real data.

## 1. The map (Game Boy numbers)

| Memory | Size | Lives | Holds in this game | Written? |
|--------|------|-------|--------------------|----------|
| Stack | ~hundreds of bytes (shared with WRAM) | one function call | locals, parameters, return addresses | constantly |
| WRAM (work RAM) | 8 KB | whole session (cleared on boot/reset) | `cells[81]`, `origin[81]`, `marks[38]`, `slot`, cursor, state | freely |
| ROM (cartridge) | 32 KB total | forever (factory-pressed) | code + `puzzles[300]` (15.6 KB) + `tiles_gen` art + font | never at runtime |
| SRAM (battery RAM) | 8 KB at `0xA000` | years (battery) | one `SaveSlot` (~0xD2 bytes) | via MBC latch only |
| VRAM / OAM | 8 KB video + 160 B sprites | per frame | tile patterns, two 32×32 maps, 40 sprites | via GBDK helpers / shadow OAM |

On your PC the numbers are gigabytes and only stack/heap/static matter. On the Game Boy every byte is budgeted: the ROM holds ~28 KB of 32 KB (`COMPACT.md`), WRAM holds a few hundred bytes of state out of 8192. `sizeof` is a design tool here, not trivia.

## 2. Stack: locals and call frames

```c
uint8_t board_conflicts(uint8_t idx) {
    uint8_t row, col, r, c, value;  /* live on the stack, dead at return */
    /* ... */
}
```

Calling a function pushes parameters, the return address and locals; returning pops them. Rules with teeth on small machines:

- **Never return `&local`.** The frame dies with the return; the pointer dangles (`-Wall` warns: `function returns address of local variable`).
- **Keep locals small.** `SaveSlot` (~210 bytes) is `static` in `main.c`, never a local — parking it on the stack each frame would risk overflow.
- **No recursion here.** The Python *generator* recurses (solving); the C *game* only loops. Deep call chains on a tiny stack are a crash vector, so handlers stay shallow: `main → handler → board/ui helper`, two levels.

> **Python vs C:** Python frames hold objects and a traceback; infinite recursion gives `RecursionError`. C frames hold raw bytes; stack overflow corrupts memory silently. Iterative loops over 81 cells are the house style for a reason.

## 3. WRAM: `static` state that survives frames

```c
/* src/board.c:9 — the live game, 81 + 81 + 1 bytes, zeroed at boot */
static uint8_t cells[CELL_COUNT];
static uint8_t origin[CELL_COUNT];
static uint8_t error_count;

/* src/main.c — session state: state, level, cursor, editing, marks[38], slot, ... */
static State state;
static uint16_t level;
static uint8_t marks[MARKS_BYTES];   /* 38 bytes, one bit per level */
static SaveSlot slot;                /* ~210-byte working copy of the save */
```

`static` storage is allocated once at boot (and zero-initialised — the code relies on `marks` starting cleared) and lives until power-off or soft reset (`jp 0x0100` clears RAM like a power cycle; SRAM survives — that asymmetry *is* the save feature).

Why not locals in `main` passed everywhere? Threading 10+ state variables through every handler signature would obscure the code for no benefit on a single-threaded game. File-`static` state with small handler functions is the pragmatic C equivalent of a Python object's `self.*` attributes.

## 4. ROM: `const` data baked into the cartridge

```c
extern const Puzzle puzzles[LEVEL_COUNT];   /* puzzles.h:46 — lives in ROM */
static const uint8_t MAGIC[4] = {'S','U','D','K'};  /* save.c:40 — ROM literal */
```

`const` at file scope (plus string literals like `"EASY"`) goes into ROM: readable, never writable. Writing through a `const`-stripped pointer is undefined behaviour (on hardware: a bus write to ROM that silently does nothing or crashes). The `const` in `const uint8_t *marks` / `const char *s` is the compiler enforcing "read-only" at every call site.

Budget thinking: 300 levels × 52 bytes = 15 600 bytes of ROM — over half the cartridge. The old text format (~165 B/level ≈ 49 KB) could never fit; nibble-packing (chapter 07) bought the product. `make check` asserts the total stays exactly 32 768 bytes.

## 5. SRAM: the battery save (with latch, magic and checksum)

The cartridge adds 8 KB of RAM kept alive by a battery — but the Game Boy cannot just write there. An MBC1 latch gates access (`src/save.c`):

```c
ENABLE_RAM;    /* open the latch: SRAM visible at 0xA000 */
SRAM[off] = …; /* read/write bytes */
DISABLE_RAM;   /* close it: stray writes cannot corrupt the save */
```

Every `save_*` function opens, works, and closes around the access — so a crash mid-frame cannot silently scribble on the slot. The on-wire layout is fixed offsets (`src/save.c:13`):

```text
0x00 'S''U''D''K' magic   0x59 origins[81]
0x04 0x01 version         0xAA mistakes
0x05 game_active          0xAB marks[38]
0x06 level u16 lo/hi      0xD1 checksum
0x08 values[81]           0xD2 end
```

Three guards, three failure modes handled:

1. **Magic `SUDK`** — first boot SRAM is garbage; wrong magic ⇒ no save. LOAD hides.
2. **Version `0x01`** — future formats will not be misread as current ones.
3. **Checksum** (8-bit sum of all preceding bytes) — dead battery / wrong cartridge ⇒ mismatch ⇒ ignored.

`save_store(1)` on SAVE writes `game_active = 1` (resumable); every win rewrites with `game_active = 0` (marks persist even without explicit save). Boot does `has_save = save_read(&slot)` once; LOAD re-reads SRAM to see the latest save. `board_restore()` then copies values + origins + mistakes back — the same layout in RAM and SRAM, zero conversion, PC-testable (`tests/test_host.c:224`).

> **Python vs C:** Python `pickle.dump(obj, open("save.dat","wb"))` handles format, versioning and errors opaquely. Here every byte offset, the checksum loop and the latch discipline are handwritten and commented — because on hardware there is no filesystem, no exceptions, and a dead battery must degrade to "LOAD hidden", never to a crash.

## 6. VRAM and OAM: video memory (preview of chapter 11)

Tile patterns, the two background maps and sprite attributes live in video memory with timing rules (writes must wait for safe PPU moments; GBDK helpers do `WAIT_STAT` automatically). The game draws full screens into the *hidden* map and flips with one LCDC write; sprites go through *shadow OAM* copied at VBlank. Details in chapter 11 — the memory point is: VRAM is a fifth address space with its own protocol, not ordinary RAM.

## 7. Why no `malloc` (no heap in this game)

Python allocates constantly (`append`, slicing, objects). C's `malloc`/`free` would allow it — and this codebase refuses, deliberately:

- 8 KB WRAM with no virtual memory fragments fast; a leak or fragmentation crash has no OS to catch it.
- Every allocation size here is known at compile time (81 cells, 38 mark bytes, 300 levels) — fixed arrays are simpler, provable, and `sizeof`-visible.
- SDCC heap support on the Game Boy is minimal and error-prone; static allocation sidesteps it entirely (`PLAN.md` risk table: fixed arrays, `uint8_t`, no `malloc`).

Rule: if the maximum size is known, declare it. The whole game is the proof that non-trivial software fits that discipline.

## Exercises

1. Compute the WRAM cost of `cells + origin + marks + SaveSlot` in bytes. What fraction of 8 KB is it?
2. Explain what goes wrong if `DISABLE_RAM` is forgotten after a write (hint: every later stray pointer write lands in the save). Why is open-work-close the right discipline?
3. `static uint8_t marks[MARKS_BYTES]` starts zeroed. The boot code still overwrites it from SRAM when a save exists. Why both? (Hint: first boot vs resume.)

Next: `11-gameboy-gbdk-hardware.md` — the 10 hardware ideas behind every `ui_*` call.
