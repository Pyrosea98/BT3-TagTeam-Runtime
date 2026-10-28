from pathlib import Path
import subprocess
import tempfile


ROOT = Path.cwd()


def extract_function(text, name):
    start = text.index(f"s32 {name}(") if f"s32 {name}(" in text else text.index(f"void {name}(")
    opening = text.index("{", start) + 1
    depth = 1
    cursor = opening
    while depth:
        depth += (text[cursor] == "{") - (text[cursor] == "}")
        cursor += 1
    return text[start:cursor]


team_fill = extract_function(
    (ROOT / "port/src/plat_fighters.c").read_text(), "Port_TeamFillFusion"
)
team_fill = team_fill.replace(
    "extern void *BtlMember_GetActive(void*);",
    "extern BtlActJMember *BtlMember_GetActive(void*);",
)
team_fill=team_fill.replace("extern void*BtlChar_Get(int),*BtlMember_GetActive(void*);", "extern void*BtlChar_Get(int);extern BtlActJMember*BtlMember_GetActive(void*);")
can_fuse = extract_function(
    (ROOT / "src/battle/btl_act_j.c").read_text(), "BtlAct_CanFuse"
)

harness = r"""
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "battle/btl_act_j.h"
#include "port_fighters.h"

#define PORT 1

typedef struct FightExtra {
    int chara, costume, team, level;
    int member;
    int pad;
    int consumed;
} FightExtra;
static FightExtra sFight[PORT_EXTRA_MAX];
static int sFightCount;static int sFusionMirror[12][5],sFusionOwner[12];
int BtlChar_GetCount(){return 3;}int Port_Team(int i){return i==1?1:0;}
static u8 sNoModelLogged[256];
static int fusionResult = 51;
static int fusionPartners[4] = {21, 255, 255, 255};
static BtlActJChr leaderStore, allyStore;

static unsigned char *member_row(void *chr, int member) {
    return (unsigned char *)chr + 0x9A4 + member * 0xA4;
}
static int *field(void *base, int offset) { return (int *)((unsigned char *)base + offset); }
static void set_member(void *chr, int member, int chara, int health) {
    unsigned char *row = member_row(chr, member);
    memset(row, 0, 0xA4);
    *field(row, 0) = chara;
    *field(row, 8) = 1;
    *field(row, 0x40) = health;
}
static void reset_world(void) {
    memset(&leaderStore, 0, sizeof(leaderStore));
    memset(&allyStore, 0, sizeof(allyStore));
    memset(sFight, 0, sizeof(sFight));
    sFightCount = 0;
    leaderStore.player = 0;
    leaderStore.form.allowed = 1;
    *field(&leaderStore, 0x998) = 1;
    set_member(&leaderStore, 0, 3, 40000);
    set_member(&allyStore, 0, 21, 30000);
    sFightCount = 1;
    sFight[0].chara = 21;
    sFight[0].member = 1;
    sFight[0].consumed = 0;
}

void *BtlChar_Get(int index) {
    if (index == 0) return &leaderStore;
    if (index == 2) return &allyStore;
    return NULL;
}
BtlActJMember *BtlMember_GetActive(void *chr) { return (BtlActJMember *)member_row(chr, 0); }
BtlActJGauge *BtlMember_GetActiveGauge(void *chr) { return (BtlActJGauge *)(member_row(chr, 0) + 0x40); }
BtlActJMember *BtlMember_Get(void *chr, int member) { return (BtlActJMember *)member_row(chr, member); }
BtlActJGauge *BtlMember_GetGauge(void *chr, int member) { return (BtlActJGauge *)(member_row(chr, member) + 0x40); }
int BtlMember_FindPresentIndex(void *chr, int chara) {
    int count = *field(chr, 0x998);
    for (int i = 0; i < count && i < 5; i++) {
        unsigned char *row = member_row(chr, i);
        if (*field(row, 0) == chara && *field(row, 8) != 0) return i;
    }
    return -1;
}
int BtlMember_HasAbility(void *chr, int ability) { return 0; }
int BtlChar_TestFlag(void *chr, int flag) { return 0; }
int BtlChar_TestMemberUnk70(void *chr) { return 0; }
int BtlParam_GetUnkB4(void *chr, int index) { return index == 0 ? fusionResult : 255; }
int BtlParam_GetCostAE(void *chr, int index) { return 100000; }
int BtlParam_GetUnkBA(void *chr, int index, int partner) { return fusionPartners[partner]; }
int BattleSide_IsCharaUsable(int player, int chara) { return 1; }
int BtlMember_HasBlast(void *chr, int cost) { return 1; }
int Port_Setting(const char *key, int fallback) { return fallback; }
int Port_FusionClockActive(int player) { return 0; }
static int cooldowns[12],recipeSeq=1;
int Port_DanceCooldown(int who){return who>=0&&who<12?cooldowns[who]:0;}
int Port_FusionPartnerActor(int who,int member){return who==0&&member==1?2:-1;}
int BtlParam_GetUnkB1(void*,int){return recipeSeq;}

int Port_FusionCostume(int source, int partner, int result) { return 0; }
int Port_ModelUsable(int chara, int costume, int variant) { return 1; }
static void Port_FusionLog(const char *fmt, ...) { (void)fmt; }
"""

footer = r"""
int main(void) {
    int partner = -1;

    /* Direct AI/story admission (not the human R3 path) must see a living
       simultaneous ally through the common CanFuse entry point. */
    reset_world();
    assert(BtlAct_CanFuse(&leaderStore, 0, 1, 1, &partner) == 1);
    assert(partner == 1);
    assert(*field(&leaderStore, 0x998) == 2);

    /* Killing that ally must invalidate the row mirrored by the previous call;
       stale present/health data must not make a dead partner eligible. */
    *field(&allyStore, 0x9A4 + 0x40) = 0;
    partner = -1;
    assert(BtlAct_CanFuse(&leaderStore, 0, 1, 1, &partner) == 0);
    assert(*field(member_row(&leaderStore, 1), 8) == 0);
    assert(*(int *)(member_row(&leaderStore, 1) + 0x40) == 0);

    /* A consumed partner is likewise never offered again, but its stale row is
       scrubbed rather than left eligible. */
    *field(&allyStore, 0x9A4 + 0x40) = 30000;
    sFight[0].consumed = 1;
    partner = -1;
    assert(BtlAct_CanFuse(&leaderStore, 0, 1, 1, &partner) == 0);
    assert(*field(member_row(&leaderStore, 1), 8) == 0);

    /* Native team members are not scrubbed by the extra mirroring logic. */
    reset_world();
    sFightCount = 0;
    *field(&leaderStore, 0x998) = 2;
    set_member(&leaderStore, 1, 21, 30000);
    partner = -1;
    assert(BtlAct_CanFuse(&leaderStore, 0, 1, 1, &partner) == 1);
    assert(partner == 1);
    assert(*field(member_row(&leaderStore, 1), 8) == 1);
    assert(*(int *)(member_row(&leaderStore, 1) + 0x40) == 30000);

    reset_world();cooldowns[0]=300;assert(!BtlAct_CanFuse(&leaderStore,0,1,1,&partner));
    cooldowns[0]=0;cooldowns[2]=300;assert(!BtlAct_CanFuse(&leaderStore,0,1,1,&partner));
    recipeSeq=2;assert(BtlAct_CanFuse(&leaderStore,0,1,1,&partner));recipeSeq=1;cooldowns[2]=0;assert(BtlAct_CanFuse(&leaderStore,0,1,1,&partner));
    puts("PASS fusion admission: direct CanFuse mirrors live extras, dead/consumed rows scrubbed, native partners preserved");
    return 0;
}
"""

with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    source = work / "test.cpp"
    binary = work / "test"
    source.write_text(harness + team_fill + "\n" + can_fuse + footer)
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(ROOT / "include"),
            "-I",
            str(ROOT / "port/include"),
            str(source),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)

check_input = (ROOT / "src/battle/btl_act_j.c").read_text()
api = (ROOT / "src/battle/btl_capi_b.c").read_text()
assert "BtlAct_CanFuse(chr, i, 1, 1, NULL)" in api
assert "BtlAct_CanFuse(chr, slot, 0, 0, &partner)" in api
assert "Port_TeamFillFusion(chr);" in check_input
