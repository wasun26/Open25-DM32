#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/../.."
python3 tools/sources.py verify-vendored
task_tmp=$(mktemp -d "${TMPDIR:-/tmp}/dm32-p25-sanitize.XXXXXX")
trap 'rm -rf "$task_tmp"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -pedantic -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc/p25 \
    -Ithird_party/p25cai/include src/p25/p25.c third_party/p25cai/src/*.c \
    tests/protocol/fuzz_p25.c -o "$task_tmp/fuzz-p25"
"$task_tmp/fuzz-p25"
printf 'PASS P25 AddressSanitizer + UndefinedBehaviorSanitizer\n'
