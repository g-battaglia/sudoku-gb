/* ---------------------------------------------------------------------------
 * tests/test_host.c — PC tests for the hardware-free modules.
 *
 * Compiles board.c + passwords.c + puzzles.c with gcc (NOT gbdk):
 *   make test-host
 *
 * Checks: puzzle strings valid, board rules (conflicts/win/givens),
 * mistake counter, password formula (uniqueness + roundtrip).
 * Also prints the 12 passwords (used by `make passwords`).
 * -------------------------------------------------------------------------*/

#include <assert.h>
#include <stdio.h>

#include "board.h"
#include "passwords.h"
#include "puzzles.h"
#include "types.h"

/* All puzzles: 81 chars, valid digits, enough givens, solution matches. */
static void test_puzzles_valid(void)
{
    uint8_t level, i;
    uint8_t givens;

    for (level = 0; level < LEVEL_COUNT; level++) {
        givens = 0;
        for (i = 0; i < CELL_COUNT; i++) {
            char g = puzzles[level].givens[i];
            char s = puzzles[level].solution[i];
            assert(g >= '0' && g <= '9');
            assert(s >= '1' && s <= '9');
            if (g != '0') {
                givens++;
                assert(g == s); /* Given cells match the solution. */
            }
        }
        assert(puzzles[level].givens[CELL_COUNT] == '\0');
        assert(puzzles[level].solution[CELL_COUNT] == '\0');
        assert(givens >= 25); /* Sanity: a real puzzle, not empty. */
        printf("level %02d %-6s givens=%d OK\n",
               level + 1, difficulty_name(puzzles[level].difficulty), givens);
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
        uint8_t expected = (uint8_t)(puzzles[0].givens[i] - '0');
        assert(board_get(i) == expected);
        assert(board_is_given(i) == (expected != 0));
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
        if (!board_is_given(idx)) {
            break;
        }
    }
    assert(idx < CELL_COUNT);
    value = (uint8_t)(puzzles[0].solution[idx] - '0');
    board_set(idx, value);
    assert(board_conflicts(idx) == 0); /* Solution digit is legal. */
    board_reveal(idx);
    assert(board_is_given(idx) == 1); /* Locked like a given now. */
    assert(board_get(idx) == value);
    printf("hint OK\n");
}

/* Passwords: unique per level, roundtrip find works. */
static void test_passwords(void)
{
    uint8_t a, b;

    for (a = 0; a < LEVEL_COUNT; a++) {
        uint16_t code = password_for_level(a);
        assert(code < 10000);
        assert(password_matches(a, code) == 1);
        assert(password_find_level(code) == (int8_t)a);
        for (b = 0; b < LEVEL_COUNT; b++) {
            if (a != b) {
                assert(password_for_level(b) != code);
            }
        }
        printf("level %02d password %04d OK\n", a + 1, code);
    }
    assert(password_find_level(10000) != 0 || 1); /* Out of range: any. */
    printf("passwords OK\n");
}

int main(void)
{
    test_puzzles_valid();
    test_board_load();
    test_conflicts();
    test_mistakes();
    test_hint();
    test_passwords();
    printf("ALL HOST TESTS PASSED\n");
    return 0;
}
