from pathlib import Path
import ast
import subprocess
import tempfile


ROOT = Path.cwd()
tree = ast.parse((ROOT / "port/tests/native_hud/check_native_assists.py").read_text())
values = {}
for node in tree.body:
    if isinstance(node, ast.Assign) and isinstance(node.value, ast.Constant):
        for name in node.targets:
            if isinstance(name, ast.Name):
                values[name.id] = node.value.value

head = values["head"]
init = values["foot"].split("int main()")[0]

footer = r"""
static void ultimate_case(int target_action) {
    init();
    clash = 0;
    action[0] = 11;
    action[2] = target_action;
}

int main(void) {
    int victims[] = {313, 314, 315};
    for (int action : victims) {
        ultimate_case(action);
        assert(as_kind(2) == 0);
        assert(as_find(0) == -1);
        Port_AssistTick();
        assert(sAssist[0].state == 0);
        assert(members[0][0x54 / 4] == 200000);
    }

    /* The actual ultimate attacker still admits and pays exactly once. */
    ultimate_case(261);
    assert(as_kind(2) == 3);
    assert(as_find(0) == 2);
    human[0] = 0;
    Port_AssistTick();
    assert(sAssist[0].state == 1);
    action[0] = 7;
    Port_AssistTick();
    assert(sAssist[0].state == 2);
    assert(members[0][0x54 / 4] == 100000);
    Port_AssistTick();
    assert(members[0][0x54 / 4] == 100000);

    /* Pin the pending grace boundary: six unconfirmed ticks are tolerated;
       the seventh releases without charging. */
    ultimate_case(261);
    human[0] = 0;
    Port_AssistTick();
    assert(sAssist[0].state == 1);
    for (int i = 0; i < 6; i++) {
        Port_AssistTick();
        assert(sAssist[0].state == 1);
        assert(members[0][0x54 / 4] == 200000);
    }
    Port_AssistTick();
    assert(sAssist[0].state == 0);
    assert(members[0][0x54 / 4] == 200000);

    puts("PASS assist boundaries: victim 0x139-0x13B rejected, attacker accepted, one stock charge, pending grace exact");
    return 0;
}
"""

with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    source = work / "test.cpp"
    binary = work / "test"
    source.write_text(
        "#include <initializer_list>\n"
        + head
        + (ROOT / "port/src/mod_assists_native.inc").read_text()
        + init
        + footer
    )
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            str(source),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
