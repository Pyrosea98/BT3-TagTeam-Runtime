#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
task_tmp=$(mktemp -d)
trap 'rm -rf "$task_tmp"' EXIT
cc -include stdlib.h -std=gnu99 -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers -fsanitize=address,undefined -Iinclude -Iport/include -Iport/src port/tests/story/test_chapter1.c port/src/gs/tag_tournament_core.c -o "$task_tmp/chapter1"
"$task_tmp/chapter1"
cc -include stdlib.h -std=gnu99 -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers -fsanitize=address,undefined -DPORT -Iinclude -Iport/include -Iport/src port/tests/story/test_chapter1_scenes.c -lm -o "$task_tmp/chapter1_scenes"
"$task_tmp/chapter1_scenes"
python3 port/tests/story/check_clear_ranks.py
python3 port/tests/story/check_ending_fade.py
python3 port/tests/story/check_dragon_balls.py
cc -include stdlib.h -std=gnu99 -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers -fsanitize=address,undefined -DPORT -Iinclude -Iport/include -Iport/src port/tests/story/test_chapter2_scenes.c -lm -o "$task_tmp/chapter2_scenes"
"$task_tmp/chapter2_scenes"
cc -include stdlib.h -std=gnu99 -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers -fsanitize=address,undefined -DPORT -Iinclude -Iport/include -Iport/src port/tests/story/test_chapter3_scenes.c -lm -o "$task_tmp/chapter3_scenes"
"$task_tmp/chapter3_scenes"
cc -include stdlib.h -std=gnu99 -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers -fsanitize=address,undefined -DPORT -Iinclude -Iport/include -Iport/src port/tests/story/test_saga_chapters.c -lm -o "$task_tmp/saga_chapters"
"$task_tmp/saga_chapters"
python3 port/tests/story/check_scene_actors.py
python3 port/tests/story/check_result_receipts.py

cc -include stdlib.h -std=gnu99 -w -fsanitize=address,undefined -Iinclude -Iport/include -Iport/src port/tests/story/test_difficulty.c -o "$task_tmp/difficulty"
"$task_tmp/difficulty"
python3 port/tests/story/check_fixed_story_launch.py

# Real native readers: story-only combat profiles survive damaged model reloads.
cc -include stdlib.h -std=gnu99 -DPORT -w -O1 -g -ffunction-sections -fdata-sections -fsanitize=address,undefined -Iinclude -Iport/include -include stdlib.h -include math.h src/battle/btl_tech_a.c src/battle/btl_tech_b.c port/tests/story/test_combat_pin.c -Wl,--gc-sections -lm -o "$task_tmp/combat_pin"
"$task_tmp/combat_pin"

python3 port/tests/story/check_water_scenes.py

python3 port/tests/story/check_prefight_presentation.py

python3 port/tests/story/check_story_polish.py

python3 port/tests/story/check_parked_end_guard.py

python3 port/tests/story/check_effect_owners.py

python3 port/tests/story/check_capacity_abi.py

python3 port/tests/story/check_native_story_contact.py

python3 port/tests/story/check_story_pause.py

python3 port/tests/story/check_interlude_chain.py

python3 port/tests/story/check_story_handover.py

python3 port/tests/story/check_native_story_commands.py

python3 port/tests/story/check_native_placement_effect.py

python3 port/tests/story/check_native_parked_freeze.py

python3 port/tests/story/check_native_story_beam_sync.py

python3 port/tests/story/check_story_camera_direction.py

python3 port/tests/story/check_story_contact_wish.py

python3 port/tests/story/check_story_retired_load.py

python3 port/tests/story/check_story_transition_boundaries.py

python3 port/tests/story/check_story_music_chain.py

python3 port/tests/story/check_custom_menu_gate.py

python3 port/tests/story/check_native_story_pose_release.py

python3 port/tests/story/check_melee_capacity.py

python3 port/tests/story/check_costume_flash_cover.py

python3 port/tests/story/check_story_camera_owner.py

python3 port/tests/story/check_shield_bystander_hit.py

python3 port/tests/story/check_story_ultimate_entry.py

python3 port/tests/story/check_cpu_clash_pace.py

python3 port/tests/story/check_story_punch_armor.py

python3 port/tests/story/check_nappa_ending_ground_anchor.py

python3 port/tests/story/check_flat_scene_zones.py
python3 port/tests/story/check_scene_cached_ground.py

cc -include stdlib.h -std=gnu99 -w -fsanitize=address,undefined -Iinclude -Iport/include -Iport/src port/tests/story/test_campaign_completion.c port/src/gs/tag_tournament_core.c -lm -o "$task_tmp/completion"
"$task_tmp/completion"
python3 port/tests/story/check_result_sound_gate.py
