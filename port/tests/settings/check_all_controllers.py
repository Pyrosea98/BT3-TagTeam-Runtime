from pathlib import Path
import subprocess
import tempfile


ROOT = Path.cwd()


def extract_function(text, name):
    start = text.index(f"int {name}(")
    opening = text.index("{", start) + 1
    depth = 1
    cursor = opening
    while depth:
        depth += (text[cursor] == "{") - (text[cursor] == "}")
        cursor += 1
    return text[start:cursor]


function = extract_function(
    (ROOT / "port/src/plat_fighters.c").read_text(), "Port_ExtraPadsOn"
)

harness = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int local_players;
static int all_controllers;
static int fight_count;
static int fight_pads[4];
static int sFightCount;

int Port_LocalPlayers(void) { return local_players; }
int Port_Setting(const char *key, int fallback) {
    if (!strcmp(key, "all_controllers_character_select")) return all_controllers;
    return fallback;
}
int Port_FightPad(int k) {
    if (k < 0 || k >= fight_count) return -1;
    return fight_pads[k];
}
"""

footer = r"""
int main(void) {
    /* Existing behavior outside the new setting stays unchanged. */
    local_players = 2;
    all_controllers = 0;
    sFightCount = fight_count = 0;
    assert(Port_ExtraPadsOn() == 0);

    /* Two local players with 3/4 controllers must still read the extra pads
       when the one-selector option merges every connected controller. */
    all_controllers = 1;
    assert(Port_ExtraPadsOn() == 1);

    /* The setting also works before a battle exists and with one local player. */
    local_players = 1;
    assert(Port_ExtraPadsOn() == 1);

    /* Existing 3+-player selection and in-fight pad routing still win when the
       option is disabled. */
    local_players = 3;
    all_controllers = 0;
    assert(Port_ExtraPadsOn() == 1);

    local_players = 2;
    sFightCount = fight_count = 1;
    fight_pads[0] = 2;
    assert(Port_ExtraPadsOn() == 1);

    fight_pads[0] = 1;
    assert(Port_ExtraPadsOn() == 0);

    puts("PASS all-controllers selection reads pads 3/4 with two local players; existing selection/fight routes preserved");
    return 0;
}
"""

with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    source = work / "test.cpp"
    binary = work / "test"
    source.write_text(harness + function + footer)
    subprocess.run(
        ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
