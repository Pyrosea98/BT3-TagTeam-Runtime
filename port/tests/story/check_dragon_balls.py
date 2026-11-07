"""Dragon Balls in the port's story chapters: checks the wiring in the shipped sources and runs
test_dragon_balls.c against the real Save_GrantDragonBall body."""
import os
import pathlib
import re
import subprocess
import tempfile

r = pathlib.Path(__file__).resolve().parents[3]
load = (r / "src/battle/battle_load.c").read_text()
stage = (r / "src/battle/stg_a_b.c").read_text()
menu = (r / "src/menu/menu_c_c.c").read_text()
save = (r / "src/sys/save.c").read_text()

# the hidden ball: stock mode 1, or a port story chapter; the stock selection loop itself is unchanged
assert "if (Battle_GetMode() == 1 || Port_StoryDragonBallSearch()) {" in load
assert re.search(r"#else\s+if \(Battle_GetMode\(\) == 1\) \{", load), "non-PORT builds keep the stock condition"
assert "if (!(gSaveData->unlockFlags & mask)) {" in load and "rule->unk18 = i + 1;" in load
# the stock discovery roll is untouched and needs no mode check
assert "if (((rand() >> 8) & 7) == 0) {" in stage and "result->unk44 = rule->unk18;" in stage
# the found ball is recorded before the story result, in the result hook
hook = load[load.index("void Port_AchievementBattleResult(void) {"):]
assert hook.index("Port_StoryFoundBall(res->unk44)") < hook.index("Port_ScenarioResult(")
# result screen: the custom adapter shows the saved ball (kind 9) without writing unlockFlags again
adapter = menu[menu.index("if (sPortHistoryOutcome) {"):]
adapter = adapter[:adapter.index("return;")]
assert "HR_ADD(9, id);" in adapter and "unlockFlags" not in adapter
# the real save helper, compiled into the test
body = save[save.index("s32 Save_GrantDragonBall(s32 ball) {"):]
body = body[:body.index("\n}\n") + 3]
with tempfile.TemporaryDirectory() as t:
    pathlib.Path(t, "grant_ball_body.inc").write_text(body)
    exe = os.path.join(t, "balls")
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-missing-field-initializers",
                    "-fsanitize=address,undefined", "-I" + t, "-I" + str(r / "include"), "-I" + str(r / "port/include"),
                    "-I" + str(r / "port/src"), str(r / "port/tests/story/test_dragon_balls.c"), "-o", exe], check=True)
    subprocess.run([exe], check=True)
print("PASS Dragon Ball wiring: story search condition, stock roll untouched, ball recorded before the result, result screen shows without re-granting")
