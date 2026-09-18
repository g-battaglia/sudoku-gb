# Sudoku GB — Makefile (GBDK-2020, ROM ONLY 32KB)
# Usage:
#   make setup-gbdk   download + extract GBDK 4.5.0 into tools/gbdk/
#                     (needed once on a fresh clone; tools/gbdk/ is gitignored)
#   make              build build/sudoku.gb
#   make run          build + open in mGBA
#   make check        header/size checks on the ROM
#   make test-host    compile + run logic tests on PC (gcc)
#   make test-emulator headless smoke test (needs: pip install pyboy pillow)
#   make regen-puzzles regenerate src/puzzles_gen.c
#   make regen-tiles   regenerate src/tiles_gen.c
#   make clean        remove build output

GBDK = tools/gbdk
GBDK_VERSION = 4.5.0
GBDK_URL = https://github.com/gbdk-2020/gbdk-2020/releases/download/$(GBDK_VERSION)/gbdk-macos-arm64.tar.gz
LCC = $(GBDK)/bin/lcc

PROJECT = sudoku
ROM = build/$(PROJECT).gb
# save_format.c is hardware-free (shared by ROM and host tests).
CSOURCES = src/board.c src/input.c src/main.c src/puzzles.c src/puzzles_gen.c src/save.c src/save_format.c src/tiles.c src/tiles_gen.c src/ui.c
HOST_SOURCES = tests/test_host.c src/board.c src/puzzles.c src/puzzles_gen.c src/save_format.c

# -msm83:gb = Game Boy target. -Wm-yn = ROM title.
# -Wl-yt0x03 = MBC1+RAM+BATTERY (battery save), -Wl-ya1 = 8KB SRAM.
LCCFLAGS = -msm83:gb -Wm-yn"SUDOKU" -Wl-yt0x03 -Wl-ya1

all: $(ROM)

$(ROM): $(CSOURCES) src/*.h
	mkdir -p build
	$(LCC) $(LCCFLAGS) -o $@ $(CSOURCES)

run: $(ROM)
	open -a mGBA $(ROM) 2>/dev/null || open -a SameBoy $(ROM) 2>/dev/null || mgba $(ROM) 2>/dev/null || echo "Open $(ROM) in your emulator"

check: $(ROM)
	python3 -c "import os; s=os.path.getsize('$(ROM)'); print('ROM size:', s, 'bytes'); assert s == 32768, 'ROM must be exactly 32KB!'; print('Size OK (32KB, 2 banks)')"
	python3 -c "d=open('$(ROM)','rb').read(); logo=d[0x104:0x134]; exp=bytes([0xCE,0xED,0x66,0x66,0xCC,0x0D,0x00,0x0B,0x03,0x73,0x00,0x83,0x00,0x0C,0x00,0x0D,0x00,0x08,0x11,0x1F,0x88,0x89,0x00,0x0E,0xDC,0xCC,0x6E,0xE6,0xDD,0xDD,0xD9,0x99,0xBB,0xBB,0x67,0x63,0x6E,0x0E,0xEC,0xCC,0xDD,0xDC,0x99,0x9F,0xBB,0xB9,0x33,0x3E]); print('Nintendo logo:', 'OK' if logo==exp else 'BAD'); assert logo==exp; print('Cart type:', hex(d[0x147]), '(0x03 = MBC1+RAM+BATTERY)'); assert d[0x147] == 0x03; print('ROM banks:', {0:2,1:4,2:8,3:16,4:32,5:64}[d[0x148]]); assert d[0x148] == 0; print('SRAM size:', {0:0,1:2,2:8,3:32,4:128,5:64}[d[0x149]], 'KB'); assert d[0x149] == 2"

test-host: $(HOST_SOURCES) src/*.h
	gcc -std=c99 -Wall -Wextra -Werror -Isrc -o /tmp/sudoku_test $(HOST_SOURCES) && /tmp/sudoku_test

test-emulator: $(ROM)
	python3 tools/smoke_pyboy.py $(ROM)

regen-puzzles:
	python3 tools/gen_puzzles.py --seed=20260916

regen-tiles:
	python3 tools/gen_tiles.py

setup-gbdk:
	mkdir -p tools
	curl -L -o /tmp/gbdk-macos-arm64.tar.gz $(GBDK_URL)
	tar -xzf /tmp/gbdk-macos-arm64.tar.gz -C tools

clean:
	rm -rf build/*.gb build/*.ihx build/*.cdb build/*.map build/*.noi build/*.sym /tmp/sudoku_test

.PHONY: all run check test-host test-emulator regen-puzzles regen-tiles setup-gbdk clean
