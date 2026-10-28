from pathlib import Path
import subprocess
import tempfile


ROOT = Path.cwd()


def extract_function(text, name, return_type):
    start = text.index(f"{return_type} {name}(")
    opening = text.index("{", start) + 1
    depth = 1
    cursor = opening
    while depth:
        depth += (text[cursor] == "{") - (text[cursor] == "}")
        cursor += 1
    return text[start:cursor]


source = (ROOT / "src/battle/bobj_a.c").read_text()
create_face = extract_function(source, "BtlObj_CreateFace", "void")
update_face = extract_function(source, "BtlObj_UpdateFace", "s32")

harness = r"""
#include <assert.h>
#include <cstdio>
#include <string.h>
#include "battle/bobj_b.h"

static int face_allocs;
BObjFace *BtlObjPool70_Alloc(void) {
    static BObjFace face;
    face_allocs++;
    return &face;
}
s32 BtlObjMdl_HasMouth(BObjMdl *mdl) { return 0; }
void BtlObjFace_Init(BObj *obj, BObjFace *face) {}
void BObjFace_InitMouth(BObj *obj, BObjFace *face) {}
void BtlObjFace_StepBlink(BObj *obj, BObjFace *face) {}
void BObjFace_UpdateMouth(BObj *obj, BObjFace *face) {}
"""

footer = r"""
int main(void) {
    BObj obj;
    memset(&obj, 0, sizeof(obj));
    obj.mdl.eyes = NULL; /* Hatchiyack-like: no eyes and no mouth */

    BtlObj_CreateFace(&obj);
    assert(obj.face == NULL);
    assert(face_allocs == 0);
    BtlObj_UpdateFace(&obj); /* stock query/update accepts the native NULL face */

    /* Repeating creation/update is also NULL-safe and does not allocate. */
    BtlObj_CreateFace(&obj);
    BtlObj_UpdateFace(&obj);
    assert(obj.face == NULL && face_allocs == 0);

    puts("PASS faceless model: native CreateFace leaves NULL, no pool allocation, UpdateFace accepts it (no status-173 guard)");
    return 0;
}
"""

with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    source_path = work / "test.cpp"
    binary = work / "test"
    source_path.write_text(harness + create_face + "\n" + update_face + footer)
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-fsanitize=address,undefined",
            # Stock BtlObj_UpdateFace has no explicit return; unrelated to NULL safety.
            "-fno-sanitize=return",
            "-fno-omit-frame-pointer",
            "-I",
            str(ROOT / "include"),
            str(source_path),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
