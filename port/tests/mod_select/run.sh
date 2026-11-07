#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
task_tmp=$(mktemp -d)
trap 'rm -rf "$task_tmp"' EXIT
cc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -Iport/include port/tests/mod_select/test_layout.c -o "$task_tmp/layout"
"$task_tmp/layout"
cc -std=gnu99 -w -ffunction-sections -fdata-sections -Iinclude -Iport/include -Iport/src port/tests/mod_select/test_runtime.c -Wl,--gc-sections -o "$task_tmp/runtime"
"$task_tmp/runtime"

python3 port/tests/mod_select/check_draft_owners.py

python3 port/tests/mod_select/check_stage_music.py

cc -w -Iport/include -ffunction-sections -fdata-sections port/tests/mod_select/test_music_alias.c -Wl,--gc-sections -o "$task_tmp/music_alias"
"$task_tmp/music_alias"

python3 port/tests/mod_select/check_roster_layout.py

cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -Iport/include port/tests/mod_select/test_preview_frame.c -o "$task_tmp/preview_frame"
"$task_tmp/preview_frame"
