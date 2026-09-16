# Sudoku GB — Makefile (GBDK-2020, ROM ONLY 32KB)
# Usage:
#   make              build build/sudoku.gb
#   make run          build + open in mGBA
#   make check        header/size checks on the ROM
#   make test-host    compile + run logic tests on PC (gcc)
#   make passwords    print the 12 level passwords (from the C code)
#   make regen-puzzles regenerate src/puzzles_gen.c
#   make clean        remove build output

GBDK = tools/gbdk
LCC = $(GBDK)/bin/lcc

PROJECT = sudoku
ROM = build/$(PROJECT).gb
CSOURCES = src/board.c src/input.c src/main.c src/passwords.c src/puzzles.c src/puzzles_gen.c src/tiles.c src/ui.c

# -msm83:gb = Game Boy target. -Wm-yn = ROM title. -Wl-yt0x00 = ROM ONLY.
LCCFLAGS = -msm83:gb -Wm-yn"SUDOKU"

all: $(ROM)

$(ROM): $(CSOURCES) src/*.h
	mkdir -p build
	$(LCC) $(LCCFLAGS) -o $@ $(CSOURCES)

run: $(ROM)
	open -a mGBA $(ROM) 2>/dev/null || open -a SameBoy $(ROM) 2>/dev/null || mgba $(ROM) 2>/dev/null || echo "Open $(ROM) in your emulator"

check: $(ROM)
	python3 -c "import os; s=os.path.getsize('$(ROM)'); print('ROM size:', s, 'bytes'); assert s <= 32768, 'ROM too big!'; print('Size OK (<=32KB)')"
	python3 -c "d=open('$(ROM)','rb').read(); logo=d[0x104:0x134]; exp=bytes([0xCE,0xED,0x66,0x66,0xCC,0x0D,0x00,0x0B,0x03,0x73,0x00,0x83,0x00,0x0C,0x00,0x0D,0x00,0x08,0x11,0x1F,0x88,0x89,0x00,0x0E,0xDC,0xCC,0x6E,0xE6,0xDD,0xDD,0xD9,0x99,0xBB,0xBB,0x67,0x63,0x6E,0x0E,0xEC,0xCC,0xDD,0xDC,0x99,0x9F,0xBB,0xB9,0x33,0x3E]); print('Nintendo logo:', 'OK' if logo==exp else 'BAD'); assert logo==exp; print('Cart type:', hex(d[0x147]), '(0x00 = ROM ONLY)')"

test-host: tests/test_host.c src/board.c src/passwords.c src/puzzles.c src/puzzles_gen.c
	gcc -Wall -Wextra -Isrc -o /tmp/sudoku_test tests/test_host.c src/board.c src/passwords.c src/puzzles.c src/puzzles_gen.c && /tmp/sudoku_test

passwords: test-host

regen-puzzles:
	python3 tools/gen_puzzles.py --seed=20260916

clean:
	rm -rf build/*.gb build/*.ihx build/*.cdb build/*.map build/*.noi build/*.sym /tmp/sudoku_test

.PHONY: all run check test-host passwords regen-puzzles clean
