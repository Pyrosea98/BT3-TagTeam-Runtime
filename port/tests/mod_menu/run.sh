#!/bin/sh
set -eu
cd "$(dirname "$0")"
output=$(mktemp)
trap 'rm -f "$output"' EXIT
# Shared header-only reducers intentionally expose unused helpers and compact clauses.
# Keep assertions and other warnings strict; these diagnostics predate this menu fixture.
c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-unused-function -Wno-unused-variable -Wno-misleading-indentation test.cpp ../../src/gs/tag_tournament_core.c -o "$output"
"$output"
echo "PASS native menu navigation and launch contracts"
python3 check_localization.py
