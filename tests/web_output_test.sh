#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
binary="$(mktemp /tmp/web_output_test.XXXXXX)"
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=gnu89 -Wall -Wextra -Werror -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/web_output_test.c -o "$binary"
ASAN_OPTIONS=detect_leaks=1 "$binary"
