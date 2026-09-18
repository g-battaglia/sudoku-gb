#!/usr/bin/env python3
"""Sudoku puzzle generator for sudoku-gb.

Generates 300 valid puzzles (100 EASY + 100 MEDIUM + 100 HARD, each with
a unique solution verified with a counting solver capped at 2) and
writes them as a generated C file (src/puzzles_gen.c) ready to compile
with GBDK-2020.

Levels are packed for ROM ONLY 32KB: 41 bytes of solution nibbles
(81 digits 1-9, 4 bits each, even cell = high nibble) + 11 bytes of
givens mask (bit i = cell i is a given). 52 bytes/level.

Usage:
    python3 tools/gen_puzzles.py [--seed N]

The generated file must NOT be edited by hand: to change the puzzles,
just re-run this script.
"""

import argparse
import random
from pathlib import Path

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

# (difficulty_name, puzzle_count, givens_target)
# The first 10 EASY are introductory (48 givens): gentle onboarding.
DIFFICULTIES: list[tuple[str, int, int]] = [
    ("EASY", 10, 48),
    ("EASY", 90, 42),
    ("MEDIUM", 100, 34),
    ("HARD", 100, 29),
]

SECRET_SEED_DEFAULT = 20260916

# Repo-rooted output (works from any cwd: `make regen-puzzles` or direct).
OUT_PATH = Path(__file__).resolve().parent.parent / "src" / "puzzles_gen.c"


# ---------------------------------------------------------------------------
# Counting solver (cap at 2: enough to prove uniqueness)
# ---------------------------------------------------------------------------

def find_empty(board: list[int]) -> int:
    """Return the index of the first empty cell (0), or -1 if full."""
    for i in range(81):
        if board[i] == 0:
            return i
    return -1


def candidates(board: list[int], idx: int) -> list[int]:
    """Return the list of legal values (1-9) for cell idx (0-80)."""
    row, col = divmod(idx, 9)
    used = set()
    for c in range(9):
        used.add(board[row * 9 + c])
    for r in range(9):
        used.add(board[r * 9 + col])
    br, bc = (row // 3) * 3, (col // 3) * 3
    for r in range(br, br + 3):
        for c in range(bc, bc + 3):
            used.add(board[r * 9 + c])
    return [v for v in range(1, 10) if v not in used]


def count_solutions(board: list[int], limit: int = 2) -> int:
    """Count solutions (up to `limit`) with backtracking + MRV."""
    # MRV: pick the empty cell with fewest candidates (effective pruning).
    best, best_cands = -1, None
    for i in range(81):
        if board[i] == 0:
            cands = candidates(board, i)
            if not cands:
                return 0
            if best_cands is None or len(cands) < len(best_cands):
                best, best_cands = i, cands
                if len(best_cands) == 1:
                    break
    if best == -1:
        return 1  # full board: one solution found
    total = 0
    for v in best_cands:
        board[best] = v
        total += count_solutions(board, limit)
        board[best] = 0
        if total >= limit:
            break
    return total


def fill_full_grid(rng: random.Random) -> list[int]:
    """Generate a valid full grid with randomized backtracking."""
    board = [0] * 81

    def rec():
        idx = find_empty(board)
        if idx == -1:
            return True
        vals = candidates(board, idx)
        rng.shuffle(vals)
        for v in vals:
            board[idx] = v
            if rec():
                return True
            board[idx] = 0
        return False

    rec()
    return board


def dig_holes(solution: list[int], givens_target: int,
              rng: random.Random) -> list[int]:
    """Dig cells while keeping a unique solution. Return the puzzle."""
    puzzle = solution[:]
    order = list(range(81))
    rng.shuffle(order)
    givens = 81
    for idx in order:
        if givens <= givens_target:
            break
        backup = puzzle[idx]
        puzzle[idx] = 0
        trial = puzzle[:]
        if count_solutions(trial, 2) != 1:
            puzzle[idx] = backup  # restore: needed for uniqueness
        else:
            givens -= 1
    return puzzle


def pack_solution(solution: list[int]) -> list[int]:
    """81 digits 1-9 -> 41 bytes (even cell = high nibble)."""
    out = []
    for i in range(0, 81, 2):
        hi = solution[i]
        lo = solution[i + 1] if i + 1 < 81 else 0
        out.append((hi << 4) | lo)
    assert len(out) == 41, len(out)
    return out


def pack_mask(puzzle: list[int]) -> list[int]:
    """81 given flags -> 11 bytes (bit i = cell i is given)."""
    out = [0] * 11
    for i in range(81):
        if puzzle[i] != 0:
            out[i >> 3] |= 1 << (i & 7)
    return out


def fmt_bytes(data: list[int]) -> str:
    """Format bytes as `0x.., ...` for the generated C table."""
    return ", ".join(f"0x{b:02X}" for b in data)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description="Generate 300 puzzles.")
    parser.add_argument("--seed", type=int, default=SECRET_SEED_DEFAULT)
    seed = parser.parse_args().seed

    rng = random.Random(seed)
    levels = []  # (difficulty, solution_bytes, mask_bytes)
    for diff_name, count, givens_target in DIFFICULTIES:
        for _ in range(count):
            solution = fill_full_grid(rng)
            puzzle = dig_holes(solution, givens_target, rng)
            # Final sanity check: unique solution, consistent with it.
            assert count_solutions(puzzle[:], 2) == 1, "puzzle without unique solution!"
            for i in range(81):
                assert puzzle[i] == 0 or puzzle[i] == solution[i]
            n_givens = sum(1 for v in puzzle if v != 0)
            print(f"{diff_name}: givens={n_givens} (target {givens_target})", flush=True)
            levels.append((diff_name, pack_solution(solution), pack_mask(puzzle)))

    assert len(levels) == 300, len(levels)
    with open(OUT_PATH, "w") as f:
        f.write("/* GENERATED by tools/gen_puzzles.py -- DO NOT edit by hand. */\n")
        f.write("/* 300 levels (100 EASY + 100 MEDIUM + 100 HARD): packed\n")
        f.write(" * solution nibbles + givens mask, 52 bytes each. See puzzles.h. */\n")
        f.write('#include "puzzles.h"\n\n')
        f.write("const Puzzle puzzles[LEVEL_COUNT] = {\n")
        for diff_name, sol, mask in levels:
            f.write(f"    {{ {{ {fmt_bytes(sol)} }}, {{ {fmt_bytes(mask)} }} }},\n")
        f.write("};\n")
    print(f"Wrote {len(levels)} puzzles to {OUT_PATH}")


if __name__ == "__main__":
    main()
