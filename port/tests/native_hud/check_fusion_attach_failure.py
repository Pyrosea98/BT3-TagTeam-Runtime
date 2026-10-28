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


load_step = extract_function(
    (ROOT / "src/battle/battle_load.c").read_text(), "BtlLoad_StepObject", "s32"
)
attach = extract_function(
    (ROOT / "src/battle/btl_char_fx_c.c").read_text(), "BtlPartner_Attach", "void"
)

harness = r"""
#include <assert.h>
#include <cstdio>
#include <string.h>
#include "types.h"
#include "battle/battle_work.h"
#include "battle/btl_char_fx.h"

#define BATTLE_FLAG_PAUSE 0x100
#define BATTLE_FLAG_LOAD_OBJECT 0x1000

static int create_result;
static int attached = -2;
static int job_freed;
static int gBtlLoadHandle = 9;
static int gBtlLoadObj;
static FxChr partnerOwner;
static BattleWork battleWork;

int BtlObj_Create(int type, void *res, int active) { return create_result; }
void *BtlObj_Get(int id) { assert(id >= 0); return NULL; }
void BtlObjAnim_PlayModel(void *obj, int anim, int flags) {}
void *BtlRes_GetSlot(int handle) { return (void *)1; }
int BtlRes_Request(int type, int id, int a, int b) { return 9; }
int File_UpdateRequests(void) { return 1; }
void BtlChange_NotifyLoaded(void) {}
s32 BtlJob_Free(BtlJob *job) { job_freed++; return 0; }
int BtlChange_IsReady(void) { return 1; }
BattleWork *Battle_GetWork(void) { return &battleWork; }
void *BtlChar_Get(int player) { return &partnerOwner; }
void BtlCtrl_AttachPartner(int player, int slot, int objId) {
    attached = objId;
    BtlPartner_Attach(&partnerOwner, slot, objId);
}
"""

footer = r"""
static void prepare_job(BtlJob *job) {
    memset(job, 0, sizeof(*job));
    memset(&partnerOwner, 0, sizeof(partnerOwner));
    memset(&battleWork, 0, sizeof(battleWork));
    job->state = 2;
    job->kind = BTL_JOB_KIND_OBJECT;
    job->side = 0;
    job->chara = 38;
    job->animChara = 0;
    job->unk1C = 0;
    attached = -2;
    job_freed = 0;
}

int main(void) {
    BtlJob job;

    create_result = 7;
    prepare_job(&job);
    assert(BtlLoad_StepObject(&job) == 1);
    assert(attached == 7);
    assert(partnerOwner.partner.active == 1);
    assert(partnerOwner.partner.res == 9 && partnerOwner.partner.objId == 7);
    assert(job_freed == 1);

    /* A full object pool must not become an active partner with objId=-1.
       Completion/cleanup can remain native; invalid attachment is forbidden. */
    create_result = -1;
    prepare_job(&job);
    assert(BtlLoad_StepObject(&job) == 1);
    assert(attached == -2);
    assert(partnerOwner.partner.active == 0);
    assert(partnerOwner.partner.objId == 0);
    assert(job_freed == 1);

    puts("PASS fusion attach: valid object attaches; Create=-1 never activates/dereferences an invalid partner");
    return 0;
}
"""

with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    source = work / "test.cpp"
    binary = work / "test"
    source.write_text(harness + load_step + "\n" + attach + footer)
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(ROOT / "include"),
            str(source),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
