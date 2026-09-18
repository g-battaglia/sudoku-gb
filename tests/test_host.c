/* ---------------------------------------------------------------------------
 * tests/test_host.c — PC tests for the hardware-free modules.
 *
 * Compiles board.c + puzzles.c + save_format.c with gcc (NOT gbdk):
 *   make test-host
 *
 * Checks: puzzle strings valid, solutions are valid Sudoku grids,
 * first 10 levels are introductory (>= 48 givens), board rules
 * (conflicts/win/origins), mistake counter, hint locking,
 * marks bitmap, board restore, save image layout + field validation.
 * -------------------------------------------------------------------------*/

#include <assert.h>
#include <stdio.h>

#include "board.h"
#include "puzzles.h"
#include "save_format.h"
#include "types.h"

/* All puzzles: valid digits, enough givens, solution matches mask. */
static void test_puzzles_valid(void)
{
    uint16_t level;
    uint8_t i, givens;
    uint8_t diff;

    for (level = 0; level < LEVEL_COUNT; level++) {
        givens = 0;
        diff = (uint8_t)(level / DIFF_LEVELS);
        for (i = 0; i < CELL_COUNT; i++) {
            uint8_t g = puzzle_given(level, i);
            uint8_t s = puzzle_solution(level, i);
            assert(g <= 9);
            assert(s >= 1 && s <= 9);
            if (g != 0) {
                givens++;
                assert(g == s); /* Given cells match the solution. */
            }
        }
        assert(givens >= 25); /* Sanity: a real puzzle, not empty. */
        if (level < 10) {
            /* Introductory levels: gentle EASY onboarding. */
            assert(diff == DIFF_EASY);
            assert(givens >= 48);
        }
        if ((level % DIFF_LEVELS) == 0) {
            printf("levels %d-%d %-6s OK\n", level + 1,
                   level + DIFF_LEVELS, difficulty_name(diff));
        }
    }
}

/* Givens load correctly and are protected. */
static void test_board_load(void)
{
    uint8_t i;

    board_load(0);
    assert(board_errors() == 0);
    assert(!board_is_solved()); /* A fresh puzzle is not solved. */
    for (i = 0; i < CELL_COUNT; i++) {
        uint8_t expected = puzzle_given(0, i);
        assert(board_get(i) == expected);
        assert(board_is_original(i) == (expected != 0));
        assert(board_is_locked(i) == (expected != 0));
        /* Fresh load, no hints yet: locked means original and only that. */
        assert(board_is_locked(i) == board_is_original(i));
    }
    printf("board_load OK\n");
}

/* Row/column/box conflicts are detected. */
static void test_conflicts(void)
{
    uint8_t i;

    board_load(0);
    /* Clear the board for a controlled test. */
    for (i = 0; i < CELL_COUNT; i++) {
        board_set(i, 0);
    }
    /* Row duplicate: cells 0 and 1 share row 0. */
    board_set(0, 5);
    board_set(1, 5);
    assert(board_conflicts(1) == 1);
    assert(board_conflicts(0) == 1);
    board_set(1, 0);
    assert(board_conflicts(0) == 0);
    /* Column duplicate: cells 0 and 9 share column 0. */
    board_set(9, 5);
    assert(board_conflicts(9) == 1);
    assert(board_conflicts(0) == 1);
    board_set(9, 0);
    assert(board_conflicts(0) == 0);
    /* Box duplicate: cells 0 and 10 share the top-left box. */
    board_set(10, 5);
    assert(board_conflicts(10) == 1);
    board_set(10, 0);
    assert(board_conflicts(0) == 0);
    /* Empty cell never conflicts. */
    board_set(0, 0);
    assert(board_conflicts(0) == 0);
    printf("conflicts OK\n");
}

/* Mistake counter: tallied forever, no game over. */
static void test_mistakes(void)
{
    uint16_t i;

    board_load(0);
    assert(board_errors() == 0);
    board_add_mistake();
    board_add_mistake();
    board_add_mistake();
    assert(board_errors() == 3); /* 3 mistakes: still playing. */
    for (i = 0; i < 300; i++) {
        board_add_mistake();
    }
    assert(board_errors() == 255); /* Saturates, never wraps. */
    printf("mistakes OK\n");
}

/* HINT: reveal locks the cell with the solution digit. */
static void test_hint(void)
{
    uint8_t idx, value;

    board_load(0);
    /* First editable cell. */
    for (idx = 0; idx < CELL_COUNT; idx++) {
        if (!board_is_locked(idx)) {
            break;
        }
    }
    assert(idx < CELL_COUNT);
    value = puzzle_solution(0, idx);
    board_set(idx, value);
    assert(board_conflicts(idx) == 0); /* Solution digit is legal. */
    assert(board_is_original(idx) == 0); /* Not a clue: renders gray. */
    board_reveal(idx);
    assert(board_is_locked(idx) == 1); /* Hint locks the cell. */
    assert(board_is_original(idx) == 0); /* ...but it is still not a clue. */
    assert(board_get(idx) == value);
    printf("hint OK\n");
}

/* One group of 9 cells holds digits 1-9 exactly once. */
static void check_unit9(const uint8_t *cells9)
{
    uint8_t seen[10] = {0};
    uint8_t k, v;

    for (k = 0; k < 9; k++) {
        v = cells9[k];
        assert(v >= 1 && v <= 9 && !seen[v]);
        seen[v] = 1;
    }
}

/* Every stored solution is a valid Sudoku grid: each row, column
 * and 3x3 box holds the digits 1-9 exactly once. */
static void test_solutions_valid(void)
{
    uint16_t level;
    uint8_t r, c, dr, dc;
    uint8_t cells9[9];

    for (level = 0; level < LEVEL_COUNT; level++) {
        for (r = 0; r < 9; r++) {
            for (c = 0; c < 9; c++) {
                cells9[c] = puzzle_solution(level, (uint8_t)(r * 9 + c));
            }
            check_unit9(cells9);
            for (c = 0; c < 9; c++) {
                cells9[c] = puzzle_solution(level, (uint8_t)(c * 9 + r));
            }
            check_unit9(cells9);
        }
        for (r = 0; r < 9; r += 3) {
            for (c = 0; c < 9; c += 3) {
                uint8_t k = 0;
                for (dr = 0; dr < 3; dr++) {
                    for (dc = 0; dc < 3; dc++) {
                        cells9[k++] = puzzle_solution(
                            level, (uint8_t)((r + dr) * 9 + c + dc));
                    }
                }
                check_unit9(cells9);
            }
        }
    }
    printf("solutions valid OK\n");
}

/* Completion marks bitmap: set/get round trip, boundaries, counting. */
static void test_marks(void)
{
    uint8_t bm[MARKS_BYTES];

    assert(MARKS_BYTES == 38); /* 300 levels -> 304 bits. */
    marks_clear(bm);
    assert(marks_count(bm, 0, LEVEL_COUNT) == 0);
    assert(marks_get(bm, 0) == 0);
    assert(marks_get(bm, LEVEL_COUNT - 1) == 0);

    marks_set(bm, 0);
    marks_set(bm, 1);
    marks_set(bm, (uint16_t)(LEVEL_COUNT - 1));
    assert(marks_get(bm, 0) == 1);
    assert(marks_get(bm, 1) == 1);
    assert(marks_get(bm, 2) == 0);
    assert(marks_get(bm, (uint16_t)(LEVEL_COUNT - 1)) == 1);
    assert(marks_count(bm, 0, LEVEL_COUNT) == 3);
    assert(marks_count(bm, 0, DIFF_LEVELS) == 2); /* First two levels. */
    assert(marks_count(bm, DIFF_LEVELS,
                       (uint16_t)(LEVEL_COUNT - DIFF_LEVELS)) == 1);

    /* Neighbouring levels must not leak into each other's bits. */
    marks_clear(bm);
    marks_set(bm, 7);
    marks_set(bm, 8);
    assert(marks_get(bm, 6) == 0);
    assert(marks_get(bm, 9) == 0);
    printf("marks OK\n");
}

/* SAVE/LOAD board snapshot: board_restore gives back the exact game
 * (values, origins -> shading/locks, mistakes). */
static void test_board_restore(void)
{
    uint8_t values[CELL_COUNT];
    uint8_t origins[CELL_COUNT];
    uint8_t i, idx;

    board_load(5);
    /* Make the board dirty: player digit, hint, mistake. */
    for (idx = 0; idx < CELL_COUNT; idx++) {
        if (!board_is_locked(idx)) {
            break;
        }
    }
    board_set(idx, puzzle_solution(5, idx));
    board_reveal(idx);
    board_add_mistake();
    board_add_mistake();
    for (i = 0; i < CELL_COUNT; i++) {
        values[i] = board_get(i);
        origins[i] = board_origin(i);
    }

    /* Trash the state, then restore the snapshot. */
    board_load(200);
    board_restore(values, origins, 2);
    assert(board_errors() == 2);
    assert(board_get(idx) == puzzle_solution(5, idx));
    assert(board_is_locked(idx) == 1); /* Hint origin restored. */
    for (i = 0; i < CELL_COUNT; i++) {
        assert(board_get(i) == values[i]);
        assert(board_origin(i) == origins[i]);
    }
    printf("board restore OK\n");
}

/* Save image layout: offsets cover the whole image with no overlap,
 * checksum is the 8-bit sum, field validation rejects garbage. */
static void test_save_format(void)
{
    uint8_t values[CELL_COUNT];
    uint8_t origins[CELL_COUNT];
    uint8_t bytes[4] = {'S', 'U', 'D', 'K'};
    uint8_t i;

    /* Layout: values + origins + fixed header fit exactly before checksum. */
    assert(SAVE_OFF_VALUES + CELL_COUNT == SAVE_OFF_ORIGINS);
    assert(SAVE_OFF_ORIGINS + CELL_COUNT == SAVE_OFF_MISTAKES);
    assert(SAVE_OFF_MARKS + MARKS_BYTES == SAVE_OFF_CHECKSUM);
    assert(SAVE_OFF_CHECKSUM + 1 == SAVE_IMAGE_SIZE);
    assert(SAVE_IMAGE_SIZE == 0xD2);
    assert(MARKS_BYTES == 38);

    /* Checksum: plain 8-bit sum. */
    assert(save_checksum(bytes, 4) == (uint8_t)('S' + 'U' + 'D' + 'K'));
    assert(save_checksum(bytes, 0) == 0);

    /* Valid fields pass. */
    for (i = 0; i < CELL_COUNT; i++) {
        values[i] = (uint8_t)(i % 10);
        origins[i] = (uint8_t)(i % 3);
    }
    assert(save_fields_valid(0, 1, values, origins) == 1);
    assert(save_fields_valid((uint16_t)(LEVEL_COUNT - 1), 0, values,
                             origins) == 1);
    /* Out-of-range fields fail: level, active flag, digit, origin. */
    assert(save_fields_valid(LEVEL_COUNT, 1, values, origins) == 0);
    assert(save_fields_valid(0, 2, values, origins) == 0);
    values[0] = 10;
    assert(save_fields_valid(0, 1, values, origins) == 0);
    values[0] = 0;
    origins[0] = 3;
    assert(save_fields_valid(0, 1, values, origins) == 0);
    printf("save format OK\n");
}

int main(void)
{
    test_puzzles_valid();
    test_solutions_valid();
    test_board_load();
    test_conflicts();
    test_mistakes();
    test_hint();
    test_marks();
    test_board_restore();
    test_save_format();
    printf("ALL HOST TESTS PASSED\n");
    return 0;
}
