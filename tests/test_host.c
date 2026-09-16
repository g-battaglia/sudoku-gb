/* ---------------------------------------------------------------------------
 * tests/test_host.c — PC tests for the hardware-free modules.
 *
 * Compiles board.c + puzzles.c with gcc (NOT gbdk):
 *   make test-host
 *
 * Checks: puzzle strings valid, solutions are valid Sudoku grids,
 * first 10 levels are introductory (>= 48 givens), board rules
 * (conflicts/win/origins), mistake counter, hint locking.
 * -------------------------------------------------------------------------*/

#include <assert.h>
#include <stdio.h>

#include "board.h"
#include "puzzles.h"
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
    /* Row duplicate. */
    board_set(0, 5);
    board_set(1, 5);
    assert(board_conflicts(1) == 1);
    board_set(1, 0);
    assert(board_conflicts(0) == 0);
    /* Column duplicate. */
    board_set(9, 5);
    assert(board_conflicts(9) == 1);
    board_set(9, 0);
    /* Box duplicate: cells 0 and 10 share the top-left box. */
    board_set(10, 5);
    assert(board_conflicts(10) == 1);
    board_set(10, 0);
    /* Empty cell never conflicts. */
    assert(board_conflicts(0) == 0 || board_get(0) != 0);
    printf("conflicts OK\n");
}

/* Mistake counter: tallied forever, no game over. */
static void test_mistakes(void)
{
    uint8_t i;

    board_load(0);
    assert(board_errors() == 0);
    board_add_mistake();
    board_add_mistake();
    board_add_mistake();
    assert(board_errors() == 3); /* 3 mistakes: still playing. */
    for (i = 0; i < 250; i++) {
        board_add_mistake();
    }
    board_add_mistake();
    board_add_mistake();
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

/* Every stored solution is a valid Sudoku grid: each row, column
 * and 3x3 box holds the digits 1-9 exactly once. */
static void test_solutions_valid(void)
{
    uint16_t level;
    uint8_t r, c, v;
    uint8_t seen[10];

    for (level = 0; level < LEVEL_COUNT; level++) {
        for (r = 0; r < 9; r++) {
            for (v = 0; v < 10; v++) {
                seen[v] = 0;
            }
            for (c = 0; c < 9; c++) {
                v = puzzle_solution(level, (uint8_t)(r * 9 + c));
                assert(v >= 1 && v <= 9 && !seen[v]);
                seen[v] = 1;
            }
            for (v = 0; v < 10; v++) {
                seen[v] = 0;
            }
            for (c = 0; c < 9; c++) {
                v = puzzle_solution(level, (uint8_t)(c * 9 + r));
                assert(v >= 1 && v <= 9 && !seen[v]);
                seen[v] = 1;
            }
        }
        for (r = 0; r < 9; r += 3) {
            for (c = 0; c < 9; c += 3) {
                uint8_t dr, dc;
                for (v = 0; v < 10; v++) {
                    seen[v] = 0;
                }
                for (dr = 0; dr < 3; dr++) {
                    for (dc = 0; dc < 3; dc++) {
                        v = puzzle_solution(level, (uint8_t)((r + dr) * 9 + c + dc));
                        assert(v >= 1 && v <= 9 && !seen[v]);
                        seen[v] = 1;
                    }
                }
            }
        }
    }
    printf("solutions valid OK\n");
}

int main(void)
{
    test_puzzles_valid();
    test_solutions_valid();
    test_board_load();
    test_conflicts();
    test_mistakes();
    test_hint();
    printf("ALL HOST TESTS PASSED\n");
    return 0;
}
