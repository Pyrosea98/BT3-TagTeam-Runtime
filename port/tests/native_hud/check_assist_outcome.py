from pathlib import Path
import ast
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


tree = ast.parse((ROOT / "port/tests/native_hud/check_native_assists.py").read_text())
values = {}
for node in tree.body:
    if isinstance(node, ast.Assign) and isinstance(node.value, ast.Constant):
        for name in node.targets:
            if isinstance(name, ast.Name):
                values[name.id] = node.value.value

head = values["head"].replace(
    "void BtlChar_SetHeldFlag(void*,int){}",
    "void BtlChar_SetHeldFlag(void*p,int b){((unsigned char*)p)[0x1085+b]=1;}",
)

clash_source = (ROOT / "src/battle/btl_char_flag_clash.c").read_text()
calc_bias = extract_function(clash_source, "BtlClash_CalcBias", "f32")
update_clash = extract_function(clash_source, "BtlClash_UpdateA", "s32").replace(
    "BtlChar_Get(", "Flag_Get("
)
setup_rush = extract_function(
    (ROOT / "src/battle/btl_act_f.c").read_text(), "BtlSuper_SetupRushDamage", "void"
)
member_source = (ROOT / "src/battle/btl_char_member.c").read_text()
queued_start = member_source.index("#ifdef PORT\nstatic s32 Port_AssistQueuedDamage")
queued_end = member_source.index("/* Per frame: deals the damage", queued_start)
queued_wrapper = member_source[queued_start:queued_end]
update_queued = extract_function(member_source, "BtlMembers_UpdateQueuedDamage", "void").replace(
    "BtlChar_Get(", "Member_Get("
)

harness = r"""
#include <initializer_list>
#include <cstdint>

#define PORT 1
#define BTL_DMG_NO_DEFENSE 0x1
#define BTL_DMG_EXACT 0x2
#define BTL_DMG_NO_HIT 0x8

typedef int s32;
typedef float f32;
typedef struct Vec4 { f32 x, y, z, w; } Vec4;

typedef struct BtlClashA {
    int timer, winner, lead;
    f32 bias;
    Vec4 mid;
} BtlClashA;

typedef struct BtlFlagChr {
    int player, pad, side, objId;
    unsigned char unk10[0xE4C - 0x10];
    int clashCountA;
} BtlFlagChr;

typedef struct BtlFlagRoster {
    unsigned char unk0[0x40];
    int clashState;
    unsigned char unk44[0x50 - 0x44];
    BtlClashA clashA;
} BtlFlagRoster;

typedef struct BtlSuperChr {
    int player;
    unsigned char unk4[0xD94 - 4];
    int dmgUnk0, dmgTotal, dmgPerHit, dmgHits, dmgFlags, dmgUnk14;
    int drainHealth, drainHealthStep, dmgUnk20, drainKi, drainKiStep, drainFrom, drainTo;
    unsigned char unkDC8[0xE40 - 0xDC8];
    int cooldown;
    unsigned char unkE44[0xE5C - 0xE44];
    int unkE5C, unkE60;
    unsigned char unkE64[0xEC4 - 0xE64];
    int unkEC4;
    unsigned char unkEC8[0xEE8 - 0xEC8];
    int unkEE8;
    unsigned char unkEEC[0xEF8 - 0xEEC];
    int rushHits;
    unsigned char unkEFC[0xF10 - 0xEFC];
    int rushFrames, rushFrameA, rushFrameB;
} BtlSuperChr;

typedef struct BtlMemberCombo { int damage, hits, timer, newHit, changed, drain; } BtlMemberCombo;
typedef struct BtlMemberQueue {
    int unk0, total, perHit, hits, flags, unk14;
    int drainHealth, drainHealthStep, unk20, drainKi, drainKiStep, drainFrom;
} BtlMemberQueue;
typedef struct BtlMemberChr {
    int side, pad, index, objId;
    unsigned char unk10[0xD40 - 0x10];
    BtlMemberCombo combo;
    unsigned char unkD58[0xD94 - 0xD58];
    BtlMemberQueue queue;
    unsigned char unkDC4[0xFE4 - 0xDC4];
    int unkFE4;
} BtlMemberChr;

static BtlFlagRoster roster;
static BtlFlagRoster *gBtlChars = &roster;
static int current_class[4], tech_damage[4], throw_mode, throw_started;
static int opponent[4] = {1, 0, 3, 2};
static int anim_hit[4], anim_hits = 2;
static int damage_count[4], damage_values[4][16], damage_flags[4][16];

static BtlFlagChr *Flag_Get(int i) { return (BtlFlagChr *)actors[i].data; }
static BtlMemberChr *Member_Get(int i) { return (BtlMemberChr *)actors[i].data; }

float BtlUtil_ClampF(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
int BtlUtil_Min(int a, int b) { return a < b ? a : b; }
int BtlUtil_Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
int BtlChar_FrameMod(int n) { return 0; }
void BtlChar_PlayVoice(void *, int) {}
void BtlCharSnd_PlayStream(void *, int) {}
void BtlChar_RaiseFirstClash(void *) {}
void BtlCharApi_GetNodePos(int, int, Vec4 *out) { *out = {0, 0, 0, 1}; }
void BtlClash_SetOrbitCut(int, int) {}
void BtlClash_SetMidCut(Vec4 *, float) {}
void Vec4_Lerp(Vec4 *dst, Vec4 *, Vec4 *, float) { *dst = {0, 0, 0, 1}; }
int BtlAct_GetCurrentClass(void *p) { return current_class[idx(p)]; }
int BtlSuper_IsThrow(void *, int) { return throw_mode; }
int BtlSuper_GetFlags(void *, int) { return 0; }
int BtlSuper_GetDamage(void *p, int, int, int) { return tech_damage[idx(p)]; }
int BtlSuper_GetDrain(void *, int) { return 0; }
int BtlSuper_GetId(void *, int) { return 0; }
void BtlColl_StartThrow(void *, void *, int, int) { throw_started++; }
void EftStruggle_End(int) {}
void port_beam_audio_begin(int) {} void StreamSe_PlayDefault(int,int) {}
void Port_ClashPairLog(const char *, int, int, int, int, int) {}
int BtlCharApi_IsInputInjected(int) { return 0; }

int BtlChar_TestFlag(void *p, int b) {
    return ((unsigned char *)p)[0x1085 + b] || ((unsigned char *)p)[0x10AD + b];
}
void BtlChar_SetFlag(void *p, int b) { ((unsigned char *)p)[0x10AD + b] = 1; }
void *BtlChar_GetObj(void *p) { return p; }
unsigned BtlObjAnim_QueryEvent(void *, unsigned long long, int, int) { return (unsigned)anim_hits; }
int BtlOpp_GetParamWord0(void *) { return 0x80; }
int BtlOpp_GetPlayer(void *p) { return opponent[idx(p)]; }
void BtlOpp_GetTargetRot(void *, Vec4 *out) { *out = {0, 0, 0, 1}; }
void BtlOpp_GetObjVecFA0(void *, Vec4 *out) { *out = {0, 0, 0, 1}; }
void BtlMove_SetHeading(void *, float, float) {}
void BtlChar_SetUnk1310(void *, Vec4 *) {}

int BtlAnim_TestAttr(void *p, unsigned long long) { return anim_hit[idx(p)]; }
int BtlAnim_GetId(void *) { return 0; }
int BtlAnim_GetFlags(int) { return 0; }
float BtlAnim_GetFrame(void *) { return 0; }
int BtlCharApi_IsInTechnique(int) { return 0; }
int BtlCharApi_IsInRushSequence(int) { return 0; }
float BtlCharApi_GetChargedUnkC78(int) { return 0; }
void BtlChar_Vibrate(void *, float, float) {}
int BtlMember_Damage(void *p, int amount, int flags) {
    int who = idx(p);
    assert(damage_count[who] < 16);
    damage_values[who][damage_count[who]] = amount;
    damage_flags[who][damage_count[who]] = flags;
    damage_count[who]++;
    return amount;
}
void BtlMember_AddHealth(void *, int) {}
void BtlMember_AddKi(void *, int) {}
int BtlMember_DrainKi(void *, int amount) { return amount; }

#define CLASH_P0 gPortClash[0]
#define CLASH_P1 gPortClash[1]
static int port_clash_scale(int) { return 1; }
static int port_clash_count(BtlFlagChr *, int n, int) { return n; }
"""

footer = r"""
static void reset_case(void) {
    Port_AssistReset();
    memset(actors, 0, sizeof(actors));
    memset(members, 0, sizeof(members));
    memset(pos, 0, sizeof(pos));
    memset(&roster, 0, sizeof(roster));
    memset(damage_count, 0, sizeof(damage_count));
    memset(damage_values, 0, sizeof(damage_values));
    memset(damage_flags, 0, sizeof(damage_flags));
    memset(anim_hit, 0, sizeof(anim_hit));
    net = replay = pause = frozen = cut = disabled = 0;
    cpu = 1;
    clash = 1;
    state = 3;
    throw_mode = throw_started = 0;
    anim_hits = 2;
    gPortClash[0] = 0;
    gPortClash[1] = 1;
    for (int i = 0; i < 4; i++) {
        ((BtlFlagChr *)actors[i].data)->player = i;
        action[i] = 11;
        current_class[i] = 2;
        tech_damage[i] = 0;
        members[i][2] = 1;
        members[i][0x40 / 4] = 10000;
        members[i][0x44 / 4] = 10000;
        members[i][0x4C / 4] = 100000;
        members[i][0x50 / 4] = 100000;
        members[i][0x54 / 4] = 200000;
        for (int q : {0x94C, 0x954, 0x958, 0x95C, 0x960})
            *(int *)((char *)&actors[i] + q) = -1;
    }
    action[0] = action[1] = 304;
    team[0] = team[2] = 0;
    team[1] = team[3] = 1;
}

static void confirm_helper(int helper, int kind) {
    sAssist[helper].target = 1;
    sAssist[helper].kind = kind;
    sAssist[helper].state = 2;
    sAssist[helper].paid = 1;
    as_credit(0,kind);
    action[helper] = 7; /* all helper poses remain presentation-only */
}

static void decide(int is_throw, int cls) {
    throw_mode = is_throw;
    current_class[0] = current_class[1] = cls;
    tech_damage[0] = 1000;
    tech_damage[1] = 200;
    roster.clashA.timer = 0;
    roster.clashA.lead = 10;
    assert(BtlClash_UpdateA(5) == 0);
}

static void setup(int cls) {
    action[0] = cls + 0x12B;
    action[1] = cls + 0x137; /* victim sequence: 0x139..0x13B for class 2..4 */
    ((BtlSuperChr *)actors[0].data)->rushHits = 2;
    BtlSuper_SetupRushDamage((BtlSuperChr *)actors[0].data, cls, 0);
}

static void queued_hit(void) {
    anim_hit[0] = 1;
    BtlMembers_UpdateQueuedDamage();
    anim_hit[0] = 0;
}

int main(void) {
    BtlSuperChr *winner;

    /* Existing direct, non-throw outcome keeps its winner-side beam multiplier. */
    reset_case();
    confirm_helper(2, 1);
    decide(0, 2);
    assert(damage_count[1] == 1 && damage_values[1][0] == 3300);

    /* Stock throw setup: 1000 base, +50% clash winner bonus, two queued events.
       The beam multiplier must survive helper release and scale both per-hit and residual commits. */
    reset_case();
    confirm_helper(2, 1);
    decide(1, 2);
    assert(throw_started == 1 && damage_count[1] == 0);
    assert(sBeamPendingMult == 300 && sBeamPendingWinner == 0 && sBeamPendingLoser == 1);
    memset(&sAssist[2], 0, sizeof(sAssist[2]));
    setup(2);
    winner = (BtlSuperChr *)actors[0].data;
    assert(winner->dmgUnk0 == 1500 && winner->dmgTotal == 1500);
    assert(winner->dmgPerHit == 220 && winner->dmgHits == 2);
    assert(sBeamQueueMult[0] == 300 && sBeamQueueTarget[0] == 1);
    queued_hit();
    assert(damage_count[1] == 1 && damage_values[1][0] == 660);
    queued_hit();
    assert(damage_count[1] == 2 && damage_values[1][1] == 3840);

    /* The real closing handler refills the queue from animation events. It is the same cinematic,
       so both of those commits retain the snapshot too. */
    action[0] = 2 + 0x128;
    BtlSuper_SetupRushDamage((BtlSuperChr *)actors[0].data, 2, 1);
    assert(sBeamQueueMult[0] == 300 && sBeamQueueTarget[0] == 1);
    queued_hit();
    queued_hit();
    assert(damage_values[1][2] == 660 && damage_values[1][3] == 3840);

    /* No assists is the exact stock queued outcome. */
    reset_case();
    decide(1, 2);
    setup(2);
    queued_hit();
    queued_hit();
    assert(damage_values[1][0] == 220 && damage_values[1][1] == 1280);

    /* A class-4 beam clash can have both modifiers: snapshot beam x3, live ultimate x1.5.
       They multiply each actual queue commit once; the setup total itself is never pre-boosted. */
    reset_case();
    confirm_helper(2, 1);
    confirm_helper(3, 3);
    team[3] = 0;
    decide(1, 4);
    memset(&sAssist[2], 0, sizeof(sAssist[2]));
    setup(4);
    winner = (BtlSuperChr *)actors[0].data;
    assert(winner->dmgUnk0 == 1500 && winner->dmgTotal == 1500);
    queued_hit();
    assert(damage_values[1][0] == 990);
    queued_hit();
    assert(damage_values[1][1] == 5760);

    /* The forced queue flush uses the same snapshot once, then cannot leak into a later ordinary queue. */
    reset_case();
    confirm_helper(2, 1);
    decide(1, 2);
    memset(&sAssist[2], 0, sizeof(sAssist[2]));
    setup(2);
    BtlChar_SetFlag(actors[0].data, 0x8C);
    BtlMembers_UpdateQueuedDamage();
    assert(damage_values[1][0] == 4500);
    assert(damage_count[1] == 1);
    for (int i = 0x1085; i < 0x1085 + 0x140; i++) actors[0].data[i] = 0;
    for (int i = 0x10AD; i < 0x10AD + 0x140; i++) actors[0].data[i] = 0;
    action[0] = 11;
    setup(2);
    queued_hit();
    queued_hit();
    assert(damage_values[1][1] == 150 && damage_values[1][2] == 850);

    puts("PASS assist outcome: direct clash, throw winner bonus, beam snapshot, ultimate stack, residual/flush isolation");
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
        + harness
        + (ROOT / "port/src/mod_assists_native.inc").read_text()
        + "\n"
        + calc_bias
        + "\n"
        + update_clash
        + "\n"
        + setup_rush
        + "\n"
        + queued_wrapper
        + "\n"
        + update_queued
        + "\n"
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
