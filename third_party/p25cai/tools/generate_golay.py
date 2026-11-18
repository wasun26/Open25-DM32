#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Derive a perfect Golay syndrome decoder from BAAA-B 4.7's polynomial.

No external source, stored matrix or imported implementation is used.
All weight <= 3 error patterns partition the 2**11 syndrome classes.
"""
import argparse
import itertools
from pathlib import Path


def remainder(word):
    while word.bit_length() > 11:
        word ^= 0xC75 << (word.bit_length() - 12)
    return word


def generate():
    errors = [None] * 2048
    for count in range(4):
        for positions in itertools.combinations(range(23), count):
            mask = sum(1 << position for position in positions)
            syndrome = remainder(mask)
            assert errors[syndrome] is None, "nonunique error syndrome"
            errors[syndrome] = mask
    assert None not in errors
    lines = ["/* SPDX-License-Identifier: MIT",
             " * Derived by tools/generate_golay.py; do not edit. */",
             "static const uint32_t golay_errors[2048] = {"]
    for start in range(0, 2048, 8):
        lines.append("    " + ", ".join(f"0x{x:06x}u" for x in errors[start:start+8]) + ",")
    return "\n".join(lines + ["};", ""])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    target = Path(__file__).resolve().parents[1] / "src/golay_errors.inc"
    result = generate()
    if args.check:
        if target.read_text() != result:
            raise SystemExit("Golay table differs from its mathematical derivation")
        print("Golay table: 2048 unique syndromes verified")
    else:
        target.write_text(result)
