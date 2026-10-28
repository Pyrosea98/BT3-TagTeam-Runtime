#!/bin/sh
set -eu
cd "$(dirname "$0")"
output=$(mktemp)
trap 'rm -f "$output"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers test.cpp ../../src/gs/tag_tournament_core.c -o "$output"
"$output"
echo "PASS native menu navigation and launch contracts"
