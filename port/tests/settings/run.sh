#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
python3 port/tests/settings/check_local_players.py
python3 port/tests/settings/check_l10n.py
python3 port/tests/settings/check_all_controllers.py
task_tmp=$(mktemp -d)
trap 'rm -rf "$task_tmp"' EXIT
cc -std=c11 -Wall -Wextra -Werror -pthread port/src/plat_settings.c port/tests/settings/settings_test.c -o "$task_tmp/test"
cc -std=c11 -Wall -Wextra -Werror -Iport/src port/tests/settings/clash_input_test.c -o "$task_tmp/clash_test"
"$task_tmp/clash_test"
cd "$task_tmp"
BT3_SETTINGS=settings.txt ./test write
BT3_SETTINGS=settings.txt ./test read
printf 'scale=2\nclash_square=1\n' > legacy.txt
BT3_SETTINGS=legacy.txt ./test legacy
grep -q '^scale=4$' legacy.txt
BT3_SETTINGS=missing/settings.txt ./test retry
BT3_SETTINGS= ./test fail
BT3_SETTINGS=full-test.txt ./test full
echo 'PASS: 400 long keys, restart readback, text keys, invalid-key rejection, legacy settings, failed-save retry'
echo 'PASS: four-player struggle input isolation, button choice, held-admission suppression, ordinary input preservation'
